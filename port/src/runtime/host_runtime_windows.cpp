// Windows process services and optional diagnostics for the guest runtime.
#include "host_runtime.h"
#include "host_vm.h"
#include "guest_memory.h"
#include "kernel/kernel_shared.h"
#include "ppc_recomp_shared.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <timeapi.h>
#include <fcntl.h>
#include <io.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <thread>

// Main guest thread handle for renderer CPU cost reports; retained for process lifetime.
HANDLE g_mainThreadHandle = nullptr;

// --- Null-page guard: guest reads through (near-)null pointers must return zero (the
// game relies on it), but WRITES through null must not stick - polluted low memory flips
// dormant TU3 guards that read e.g. *(0x59) and call through garbage. Keep the first
// 64 KiB write-protected; when guest code writes there, let the instruction execute once
// (single-step) then erase the written bytes and re-protect. Each write is an upstream
// divergence worth studying (on hardware the object pointer would be non-null).
static thread_local bool t_npStep = false;

static bool NullPageProtect(bool on)
{
    return host_vm::Protect(g_base, guest_memory::NullGuardBytes,
        on ? host_vm::Protection::ReadOnly : host_vm::Protection::ReadWrite);
}

static LONG CALLBACK NullPageVeh(EXCEPTION_POINTERS* ep)
{
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2
        && r->ExceptionInformation[0] == 1)
    {
        uint64_t fa = r->ExceptionInformation[1];
        uint64_t lo = reinterpret_cast<uint64_t>(g_base);
        if (fa >= lo && fa < lo + guest_memory::NullGuardBytes)
        {
            static int logged = 0;
            if (logged < 20)
            {
                ++logged;
                uint64_t modBase = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
                std::fprintf(stderr, "[nullpage] write@0x%llX pc RVA=0x%llX (ignored)\n",
                    (unsigned long long)(fa - lo),
                    (unsigned long long)(reinterpret_cast<uint64_t>(r->ExceptionAddress) - modBase));
            }
            if (!NullPageProtect(false)) return EXCEPTION_CONTINUE_SEARCH;
            ep->ContextRecord->EFlags |= 0x100;
            t_npStep = true;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    else if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && t_npStep)
    {
        t_npStep = false;
        // A guest instruction can write more than 32 bytes or cross a page.
        // Keep every byte of the null guard zero, as on the Linux host.
        std::memset(g_base, 0, guest_memory::NullGuardBytes);
        if (!NullPageProtect(true)) std::abort();
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

// TTT2_STACKDUMP=<seconds>: after that delay, suspend every other thread and print each
// one's RIP plus in-module stack return addresses (resolve offline via the linker map).
void DumpAllStacks(const char* why)
{
    std::fprintf(stderr, "[stacks] %s\n", why ? why : "thread dump");
    uint64_t modBase = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    THREADENTRY32 te{ sizeof(te) };
    // Capture with threads suspended, but PRINT only after resuming them: a suspended
    // thread may hold the CRT stderr lock and printing would deadlock the dump.
    struct Cap { DWORD tid; uint64_t rip; uint64_t rets[12]; int nret; };
    Cap caps[64]; int ncap = 0;
    for (BOOL ok = Thread32First(snap, &te); ok && ncap < 64; ok = Thread32Next(snap, &te))
    {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE h = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, te.th32ThreadID);
        if (!h) continue;
        if (SuspendThread(h) != (DWORD)-1)
        {
            CONTEXT c{}; c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            if (GetThreadContext(h, &c))
            {
                Cap& cap = caps[ncap++];
                cap.tid = te.th32ThreadID;
                cap.rip = c.Rip - modBase;
                cap.nret = 0;
                for (int i = 0; i < 2048 && cap.nret < 12; ++i)
                {
                    uint64_t v = 0;
                    if (!ReadProcessMemory(GetCurrentProcess(),
                        reinterpret_cast<void*>(c.Rsp + i * 8), &v, 8, nullptr)) break;
                    if (v >= modBase && v < modBase + 0x20000000ull)
                        cap.rets[cap.nret++] = v - modBase;
                }
            }
            ResumeThread(h);
        }
        CloseHandle(h);
    }
    CloseHandle(snap);
    extern void KernelDumpGuestThreads();
    KernelDumpGuestThreads();
    std::fprintf(stderr, "[stacks] ==== thread dump ====\n");
    for (int i = 0; i < ncap; ++i)
    {
        std::fprintf(stderr, "[stacks] tid=%lu rip=0x%llX", caps[i].tid,
                     (unsigned long long)caps[i].rip);
        for (int j = 0; j < caps[i].nret; ++j)
            std::fprintf(stderr, " 0x%llX", (unsigned long long)caps[i].rets[j]);
        std::fprintf(stderr, "\n");
    }
    std::fprintf(stderr, "[stacks] ==== end dump ====\n");
}

static void StackDumpAfter(int seconds)
{
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
    DumpAllStacks("scheduled thread dump");
    LogDrain(); TerminateProcess(GetCurrentProcess(), 3);
}

// --- optional write-watch (env TTT2_WATCH=<hex guest addr>): protects the page and logs the
// PC of every write overlapping the watched word via a vectored handler + single-step. ---
// The address can also be armed while the game runs: write "<hex addr>" to
// C:/ttt2/port/watch.txt (consumed) - for buffers whose address is only known in that run.
static uint32_t g_watchAddr = 0;
static int g_watchLogged = 0;
static thread_local bool t_watchStep = false;    // this thread is stepping past the page
static thread_local uint32_t t_watchPre = 0;     // watched-word value before the write
static thread_local uint64_t t_watchPc  = 0;     // PC of the writing instruction
static thread_local uint64_t t_watchStack[12];   // its callers (return-address RVAs)
static thread_local int t_watchFrames = 0;

// Callers of the faulting instruction, unwound from its context (x64 unwind data).
static void WatchCaptureStack(const CONTEXT& at, uint64_t modBase)
{
    CONTEXT c = at;
    t_watchFrames = 0;
    for (int i = 0; i < 12; ++i)
    {
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION f = RtlLookupFunctionEntry(c.Rip, &imageBase, nullptr);
        if (f)
        {
            void* handlerData = nullptr;
            DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, c.Rip, f, &c, &handlerData, &establisher, nullptr);
        }
        else
        {
            c.Rip = *reinterpret_cast<DWORD64*>(c.Rsp);   // leaf: return address on top
            c.Rsp += 8;
        }
        if (!c.Rip) break;
        t_watchStack[t_watchFrames++] = c.Rip - modBase;
    }
}

static void WatchProtect(bool on)
{
    host_vm::Protect(g_base + (g_watchAddr & ~0xFFFu), 0x1000,
        on ? host_vm::Protection::ReadOnly : host_vm::Protection::ReadWrite);
}

static LONG CALLBACK WatchVeh(EXCEPTION_POINTERS* ep)
{
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if (!g_watchAddr) return EXCEPTION_CONTINUE_SEARCH;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2
        && r->ExceptionInformation[0] == 1)      // write fault
    {
        uint64_t fa = r->ExceptionInformation[1];
        uint64_t page = reinterpret_cast<uint64_t>(g_base) + (g_watchAddr & ~0xFFFu);
        if (fa >= page && fa < page + 0x1000)
        {
            t_watchPre = __builtin_bswap32(*reinterpret_cast<uint32_t*>(g_base + g_watchAddr));
            t_watchPc  = reinterpret_cast<uint64_t>(r->ExceptionAddress);
            WatchCaptureStack(*ep->ContextRecord, reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)));
            WatchProtect(false);
            ep->ContextRecord->EFlags |= 0x100;  // TF: re-protect after this one instruction
            t_watchStep = true;
            return EXCEPTION_CONTINUE_EXECUTION;
        }
    }
    else if (r->ExceptionCode == EXCEPTION_SINGLE_STEP && t_watchStep)
    {
        t_watchStep = false;
        uint32_t now = __builtin_bswap32(*reinterpret_cast<uint32_t*>(g_base + g_watchAddr));
        if (now != t_watchPre && g_watchLogged < 300)
        {
            ++g_watchLogged;
            uint64_t modBase = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
            char stack[12 * 12 + 1] = {};
            int at = 0;
            for (int i = 0; i < t_watchFrames; ++i)
                at += std::snprintf(stack + at, sizeof(stack) - at, " %llX", (unsigned long long)t_watchStack[i]);
            std::fprintf(stderr, "[watch] 0x%08X: %08X -> %08X pc RVA=0x%llX callers:%s\n",
                g_watchAddr, t_watchPre, now, (unsigned long long)(t_watchPc - modBase), stack);
        }
        WatchProtect(true);
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static int FaultFilter(EXCEPTION_POINTERS* ep);

// Shared with kernel threads (GuestCall wrappers) so their faults get reported too.
int GuestFaultReport(EXCEPTION_POINTERS* ep) { return FaultFilter(ep); }

// Turn a crash into a diagnostic: report the faulting guest address (Phase-2 work-list).
static int FaultFilter(EXCEPTION_POINTERS* ep)
{
    const EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if (r->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r->NumberParameters >= 2)
    {
        uint64_t fault = r->ExceptionInformation[1];
        long long guest = (long long)(fault - (uint64_t)g_base);
        std::fprintf(stderr, "[fault] access violation %s host=0x%llX guest=0x%llX pc=%p\n",
            r->ExceptionInformation[0] ? "write" : "read",
            (unsigned long long)fault, (unsigned long long)guest, r->ExceptionAddress);
    }
    else
    {
        std::fprintf(stderr, "[fault] exception 0x%lX pc=%p\n",
            (unsigned long)r->ExceptionCode, r->ExceptionAddress);
    }
    uint64_t modBase = reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr));
    std::fprintf(stderr, "[fault] module base=0x%llX  pc RVA=0x%llX\n",
        (unsigned long long)modBase, (unsigned long long)(reinterpret_cast<uint64_t>(r->ExceptionAddress) - modBase));
    // Optional fault-time guest dump: TTT2_DUMP=<hexaddr>,<hexlen> prints that guest region.
    if (const char* d = std::getenv("TTT2_DUMP"))
    {
        uint32_t da = 0, dl = 0x40;
        if (std::sscanf(d, "%x,%x", &da, &dl) >= 1 && da)
        {
            std::fprintf(stderr, "[dump] guest 0x%08X (%u bytes):\n", da, dl);
            for (uint32_t i = 0; i < dl; i += 16)
            {
                std::fprintf(stderr, "  %08X:", da + i);
                for (uint32_t j = 0; j < 16 && i + j < dl; ++j)
                    std::fprintf(stderr, " %02X", g_base[da + i + j]);
                std::fprintf(stderr, "  |");
                for (uint32_t j = 0; j < 16 && i + j < dl; ++j)
                {
                    unsigned char c = g_base[da + i + j];
                    std::fputc((c >= 0x20 && c < 0x7F) ? c : '.', stderr);
                }
                std::fprintf(stderr, "|\n");
            }
        }
    }
    // Host registers + code bytes at RIP: enough to identify the exact faulting expression.
    if (CONTEXT* c = ep->ContextRecord)
    {
        std::fprintf(stderr, "[fault] rax=%016llX rbx=%016llX rcx=%016llX rdx=%016llX\n", c->Rax, c->Rbx, c->Rcx, c->Rdx);
        std::fprintf(stderr, "[fault] rsi=%016llX rdi=%016llX rbp=%016llX rsp=%016llX\n", c->Rsi, c->Rdi, c->Rbp, c->Rsp);
        std::fprintf(stderr, "[fault] r8 =%016llX r9 =%016llX r10=%016llX r11=%016llX\n", c->R8, c->R9, c->R10, c->R11);
        std::fprintf(stderr, "[fault] r12=%016llX r13=%016llX r14=%016llX r15=%016llX\n", c->R12, c->R13, c->R14, c->R15);
        // A wild jump makes ExceptionAddress itself unreadable; probe before dumping code bytes.
        const unsigned char* ip = static_cast<const unsigned char*>(r->ExceptionAddress);
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(ip, &mbi, sizeof(mbi)) && mbi.State == MEM_COMMIT &&
            !(mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        {
            std::fprintf(stderr, "[fault] code:");
            for (int i = 0; i < 16; ++i) std::fprintf(stderr, " %02X", ip[i]);
            std::fprintf(stderr, "\n");
        }
        else
        {
            std::fprintf(stderr, "[fault] code: <pc unreadable - wild jump>\n");
        }
    }
    // If we jumped to a null/wild address, the caller's return address is on top of the stack.
    if (ep->ContextRecord)
    {
        uint64_t rsp = ep->ContextRecord->Rsp;
        for (int i = 0; i < 8; ++i)   // scan a few slots for the first in-module return address
        {
            uint64_t v = *reinterpret_cast<uint64_t*>(rsp + i * 8);
            if (v >= modBase && v < modBase + 0x20000000ull)
            {
                std::fprintf(stderr, "[fault] caller ret[+%d]=0x%llX  RVA=0x%llX\n",
                    i * 8, (unsigned long long)v, (unsigned long long)(v - modBase));
                break;
            }
        }
    }
    LogDrain();
    return EXCEPTION_EXECUTE_HANDLER;
}

// INSTALLED COPY (ttt2_setup.exe, port/src/setup): the exe sits next to game\ (the files the
// installer extracted from the player's own disc and title update) and is started with no
// arguments. Everything the development launcher passes through the environment gets its
// player default here, unless already set: the game data, the hardware renderer, no debug frame
// dumps, and - with no console attached (GUI subsystem, double-clicked) - the log in
// %LOCALAPPDATA%\TTT2Recomp\log.txt. Returns the image path, or empty when not installed.
std::filesystem::path host_runtime::InstalledLayout(int argc)
{
    if (argc > 1) return {};
    std::wstring self(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, self.data(), DWORD(self.size()));
    if (!length || length >= self.size()) return {};
    self.resize(length);
    const std::filesystem::path game = std::filesystem::path(self).parent_path() / L"game";
    const std::filesystem::path image = game / L"tekken.img";
    std::error_code error;
    if (!std::filesystem::is_regular_file(image, error) || error) return {};
    auto setDefaultWide = [](const wchar_t* name, const std::wstring& value) {
        const wchar_t* configured = _wgetenv(name);
        if (!configured || !*configured) _wputenv_s(name, value.c_str());
    };
    auto setDefault = [](const char* name, const char* value) {
        const char* configured = std::getenv(name);
        if (!configured || !*configured) _putenv_s(name, value);
    };
    setDefaultWide(L"TTT2_VFS_ROOT", game.wstring());
    setDefault("TTT2_GPU_HW", "1");
    setDefault("TTT2_FBDUMP", "0");
    const HANDLE err = GetStdHandle(STD_ERROR_HANDLE);
    if (!err || err == INVALID_HANDLE_VALUE || GetFileType(err) == FILE_TYPE_UNKNOWN)
    {
        wchar_t local[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH))
        {
            const std::wstring folder = std::wstring(local) + L"\\TTT2Recomp";
            CreateDirectoryW(folder.c_str(), nullptr);
            // _wfreopen, not _wfreopen_s: the _s variant opens without sharing, so the log could
            // not be opened (or sent) while the game runs.
            _wfreopen((folder + L"\\log.txt").c_str(), L"w", stderr);
        }
    }
    return image;
}

std::filesystem::path host_runtime::VfsRoot()
{
    const wchar_t* configured = _wgetenv(L"TTT2_VFS_ROOT");
    return configured && *configured ? std::filesystem::path(configured) :
                                      std::filesystem::path(DefaultVfsRoot());
}

// The log goes through a pipe to a writer thread when stderr is a file (log.txt, or a
// launcher's redirect): a guest or GPU thread that logs only copies into the pipe, and a slow
// WriteFile (antivirus scanning or a busy disk) stalls only the writer.
// TTT2_LOG_ASYNC=0 writes directly.
static HANDLE g_logPipeRead = nullptr;
static std::atomic<bool> g_logWriting{ false };

void host_runtime::StartLogWriter()
{
    if (const char* e = std::getenv("TTT2_LOG_ASYNC"); e && *e == '0') return;
    const int fd = _fileno(stderr);
    const HANDLE file = fd >= 0 ? reinterpret_cast<HANDLE>(_get_osfhandle(fd)) : INVALID_HANDLE_VALUE;
    if (file == INVALID_HANDLE_VALUE || !file || GetFileType(file) != FILE_TYPE_DISK) return;
    HANDLE target = nullptr, read = nullptr, write = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), file, GetCurrentProcess(), &target, 0, FALSE, DUPLICATE_SAME_ACCESS)) return;
    if (!CreatePipe(&read, &write, nullptr, 1 << 20)) { CloseHandle(target); return; }
    const int pipeFd = _open_osfhandle(reinterpret_cast<intptr_t>(write), _O_WRONLY | _O_TEXT);
    if (pipeFd < 0 || _dup2(pipeFd, fd) != 0)
    {
        if (pipeFd >= 0) _close(pipeFd); else CloseHandle(write);
        CloseHandle(read);
        CloseHandle(target);
        return;
    }
    _close(pipeFd);                     // fd 2 keeps its own duplicate of the write end
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(fd)));
    g_logPipeRead = read;
    std::thread([read, target] {
        static char buffer[1 << 16];
        for (;;)
        {
            DWORD n = 0;
            if (!ReadFile(read, buffer, sizeof(buffer), &n, nullptr) || !n) return;
            g_logWriting = true;
            DWORD written = 0;
            WriteFile(target, buffer, n, &written, nullptr);
            g_logWriting = false;
        }
    }).detach();
}

void LogDrain(unsigned timeoutMs)
{
    if (!g_logPipeRead) return;
    const ULONGLONG until = GetTickCount64() + timeoutMs;
    int quiet = 0;                      // two empty checks in a row: the writer has caught up
    while (quiet < 2 && GetTickCount64() < until)
    {
        DWORD queued = 0;
        PeekNamedPipe(g_logPipeRead, nullptr, 0, nullptr, &queued, nullptr);
        quiet = queued == 0 && !g_logWriting ? quiet + 1 : 0;
        Sleep(quiet ? 5 : 1);
    }
}

const char* host_runtime::DefaultImagePath()
{
    return "C:/ttt2/re/decrypted/Default_patched_tu3.bin";
}

const char* host_runtime::DefaultVfsRoot()
{
    return "C:/ttt2/re/game_files";
}

void host_runtime::ConfigureTimer()
{
    if (!g_mainThreadHandle)
        DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThreadHandle,
            THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
    // Timer resolution. Since Windows 10 2004 a process that never calls timeBeginPeriod
    // gets the default ~15.6 ms timer even if other processes asked for more, so every
    // short runtime sleep (CP idle poll, WAIT_REG_MEM back-off, vblank sleep_until, guest
    // KeDelayExecutionThread) was quantised to 15.6 ms. Measured before this change
    // (PID71952): the main thread spun 73% of its time in the D3D GPU wait sub_8273D4A0
    // while the CP slept, and the game advanced ~16.7 ticks/s. Also opt out of Windows 11
    // power throttling, which may ignore the request for occluded windows.
    // TTT2_TIMER_RES=0 keeps the old default resolution for A/B measurement.
    if (const char* tr = std::getenv("TTT2_TIMER_RES"); !tr || std::strcmp(tr, "0") != 0)
    {
        PROCESS_POWER_THROTTLING_STATE throttling{};
        throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED |
                                 PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
        throttling.StateMask = 0;   // controlled + not set = never throttle
        const BOOL throttleOk = SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling,
                                                      &throttling, sizeof(throttling));
        const MMRESULT period = timeBeginPeriod(1);
        std::fprintf(stderr, "[runtime] timer resolution: timeBeginPeriod(1)=%s power-throttling-optout=%s\n",
                     period == TIMERR_NOERROR ? "ok" : "FAILED", throttleOk ? "ok" : "FAILED");
    }
    else
        std::fputs("[runtime] timer resolution: default (TTT2_TIMER_RES=0)\n", stderr);

}

void host_runtime::InstallFatalHandler()
{
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* ep) -> LONG {
        auto* r = ep->ExceptionRecord;
        std::fprintf(stderr, "[fatal] unhandled exception %08lX at %p (tid %lu)\n",
                     r->ExceptionCode, r->ExceptionAddress, GetCurrentThreadId());
        std::fprintf(stderr, "[fatal] module base %p, RVA %llX\n", GetModuleHandleW(nullptr),
                     (unsigned long long)(reinterpret_cast<uint64_t>(r->ExceptionAddress) -
                                          reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr))));
        std::fflush(stderr);
        LogDrain();
        return EXCEPTION_EXECUTE_HANDLER;
    });
}

void host_runtime::InstallGuestGuards()
{
    if (!AddVectoredExceptionHandler(1, NullPageVeh) || !NullPageProtect(true))
        std::abort();

    if (const char* sd = std::getenv("TTT2_STACKDUMP"))
    {
        int secs = std::atoi(sd);
        if (secs > 0) std::thread(StackDumpAfter, secs).detach();
    }

    if (const char* w = std::getenv("TTT2_WATCH"))
    {
        g_watchAddr = static_cast<uint32_t>(std::strtoul(w, nullptr, 16));
        AddVectoredExceptionHandler(1, WatchVeh);
        WatchProtect(true);
        std::fprintf(stderr, "[watch] watching guest 0x%08X\n", g_watchAddr);
    }
    std::thread([] {
        bool vehInstalled = g_watchAddr != 0;
        // TTT2_INSTANCE=<tag>: watch<tag>.txt, so two instances can be armed separately.
        const char* tag = std::getenv("TTT2_INSTANCE");
        const std::string watchFile = std::string("C:/ttt2/port/watch") + (tag ? tag : "") + ".txt";
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            FILE* f = std::fopen(watchFile.c_str(), "rb");
            if (!f) continue;
            char text[32] = {};
            std::fread(text, 1, sizeof(text) - 1, f);
            std::fclose(f);
            DeleteFileA(watchFile.c_str());
            const uint32_t addr = static_cast<uint32_t>(std::strtoul(text, nullptr, 16));
            if (!addr) continue;
            if (g_watchAddr) WatchProtect(false);
            g_watchAddr = addr;
            g_watchLogged = 0;
            if (!vehInstalled) { AddVectoredExceptionHandler(1, WatchVeh); vehInstalled = true; }
            WatchProtect(true);
            std::fprintf(stderr, "[watch] watching guest 0x%08X (armed at run time)\n", addr);
        }
    }).detach();
}

void host_runtime::StartShaderPrebuild()
{
    wchar_t self[32768];
    if (!GetModuleFileNameW(nullptr, self, DWORD(std::size(self)))) return;
    SYSTEM_INFO system{};
    GetNativeSystemInfo(&system);
    const unsigned workers = system.dwNumberOfProcessors > 3 ? system.dwNumberOfProcessors / 2 : 1;
    std::wstring line = L"\"" + std::wstring(self) + L"\" --build-shaders \"\" \"\" " + std::to_wstring(workers);
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (CreateProcessW(self, line.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS, nullptr, nullptr, &startup, &process))
    {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
}
