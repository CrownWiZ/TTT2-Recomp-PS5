// PS5 guest VM (interface: host_vm.h) — same memory model as the Linux build
// with the syscall surface substituted. Write-watch is mprotect+SIGSEGV
// bookkeeping exactly as on Linux; the only shadows are in how the host
// primitives express those moves:
// - the fault pipe uses pipe/read/write libc calls (async-signal-safe here),
//   and the worker wake/wait spins the completion flag with sched_yield,
//   because Prospero exposes no futex syscall through its libc;
// - the physical mirror maps one physical allocation twice (guest alias and
//   worker stage), the standard console-memory pattern, instead of a memfd.
// The null-page helper set stays available: InstallGuestGuards simply does
// not call it on this platform.
#include "host_vm.h"
#include "guest_memory.h"
#if defined(TTT2_GEOMETRY_AB_PROBE)
#include "kernel/gpu_geometry_probe.h"
#endif

#include <algorithm>

#if !defined(__PROSPERO__) || !defined(__x86_64__)
#error "The PS5 guest VM backend currently requires Prospero on x86-64"
#endif

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <new>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <ucontext.h>
#include <unistd.h>

#include "kernel/sce_ps5.h"

namespace host_vm {
namespace {
constexpr size_t PageSize = 4096; // The guest GPU and write-watch call sites use 4 KiB pages.
constexpr uint8_t Armed = 1, Dirty = 2;
constexpr uint8_t NoAccess = 0, Read = 1, Write = 2;

uint8_t* g_base = nullptr;
size_t g_bytes = 0;
std::unique_ptr<uint8_t[]> g_state;
std::unique_ptr<uint8_t[]> g_permission;
std::unique_ptr<std::atomic<uint8_t>[]> g_rollbackPages;
uint8_t* g_mirrorStage = nullptr;
uintptr_t g_mirrorStart = 0;
size_t g_mirrorBytes = 0;
int64_t g_mirrorDevice = -1;
std::atomic_flag g_lock = ATOMIC_FLAG_INIT;
struct sigaction g_previous{};
struct sigaction g_previousTrap{};
bool g_handlerInstalled = false;
bool g_trapHandlerInstalled = false;
std::atomic<bool> g_nullGuard{false};
thread_local bool t_nullStep = false;
uintptr_t g_deferredFirst = 0, g_deferredLast = 0;
DeferredFaultHandler g_deferredHandler = nullptr;
std::atomic<UnhandledFaultHandler> g_unhandledHandler{nullptr};
CaptureRollbackWriter g_captureRollback = nullptr;
RollbackWriteHandler g_rollbackWrite = nullptr;
std::atomic<bool> g_rollbackRegistered{false};
int g_faultPipe[2] = {-1, -1};
// A guest can return from main while this worker is still waiting for faults.
// ReleaseGuest joins it explicitly before tearing anything down.
pthread_t g_faultWorker{};
bool g_faultWorkerStarted = false;
// Run-stable worker identity for the recursion guard.
unsigned long g_workerKey = 0;

struct FaultRequest
{
    uintptr_t address;
    bool rollback = false;
    uint64_t writer = 0;
    alignas(4) int done = 0;
    int handled = 0;
};

struct Lock {
    Lock() { while (g_lock.test_and_set(std::memory_order_acquire)) {} }
    ~Lock() { g_lock.clear(std::memory_order_release); }
};

bool WorkerThreadNow()
{
    const pthread_t self = pthread_self();
    return *reinterpret_cast<const unsigned long*>(&self) == g_workerKey;
}

bool Range(void* address, size_t bytes, size_t& first, size_t& last)
{
    if (!g_base || !bytes) { errno = EINVAL; return false; }
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_base);
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (start < base || start - base >= g_bytes || bytes > g_bytes - (start - base))
    {
        errno = EINVAL;
        return false;
    }
    first = (start - base) / PageSize;
    last = (start - base + bytes + PageSize - 1) / PageSize;
    return true;
}

bool ProtectRun(size_t first, size_t last, int native)
{
    return mprotect(g_base + first * PageSize, (last - first) * PageSize, native) == 0;
}

// Caller holds g_lock. Protect before clearing dirty bits so a subsequent write
// cannot pass through a writable PTE without being observed by the fault handler.
bool ResetLocked(size_t first, size_t last)
{
    for (size_t page = first; page < last;)
    {
        if (g_permission[page] != Write) { g_state[page++] = Armed; continue; }
        const size_t begin = page;
        while (page < last && g_permission[page] == Write) ++page;
        if (!ProtectRun(begin, page, PROT_READ)) return false;
        for (size_t i = begin; i < page; ++i) g_state[i] = Armed;
    }
    return true;
}

void* FaultWorker(void*)
{
    const pthread_t self = pthread_self();
    g_workerKey = *reinterpret_cast<const unsigned long*>(&self);
    for (;;)
    {
        FaultRequest* request = nullptr;
        ssize_t got;
        do { got = read(g_faultPipe[0], &request, sizeof(request)); }
        while (got < 0 && errno == EINTR);
        if (got != sizeof(request) || !request) break;
        request->handled = request->rollback
            ? g_rollbackRegistered.load(std::memory_order_acquire) && g_rollbackWrite(request->address, request->writer)
            : g_deferredHandler && g_deferredHandler(request->address);
        __atomic_store_n(&request->done, 1, __ATOMIC_RELEASE);
    }
    g_workerKey = 0;
    return nullptr;
}

bool DeferFault(uintptr_t address, bool rollback = false)
{
    // The worker must not wait for itself. A recursive fault means that the
    // callback touched the protected range and needs a different implementation.
    if (WorkerThreadNow())
        return false;
    FaultRequest request{address};
    request.rollback = rollback;
    if (rollback) request.writer = g_captureRollback(address);
    FaultRequest* pointer = &request;
    ssize_t sent;
    do { sent = write(g_faultPipe[1], &pointer, sizeof(pointer)); }
    while (sent < 0 && errno == EINTR);
    if (sent != sizeof(pointer)) return false;
    while (__atomic_load_n(&request.done, __ATOMIC_ACQUIRE) == 0)
        sched_yield();
    return request.handled != 0;
}

void Chain(int number, siginfo_t* info, void* context)
{
    const auto& previous = number == SIGTRAP ? g_previousTrap : g_previous;
    (void)info; (void)context;
    if (previous.sa_handler == SIG_IGN) return;
    // No restartable default disposition here: a chained fault at this point
    // means real corruption, and the ELF exits the way the M3 cycle does.
    _exit(128 + number);
}

void FinishNullPageStep(void* context)
{
    if (!t_nullStep) return;
    t_nullStep = false;
    if (context)
        static_cast<ucontext_t*>(context)->uc_mcontext.__mc_saved_registers.? = 0;
    // One instruction may write more than 32 bytes or cross a page. Clear
    // the entire null guard before restoring its read-only protection.
    auto* bytes = reinterpret_cast<volatile uint8_t*>(g_base);
    for (size_t i = 0; i < guest_memory::ZeroTailBytes; ++i) bytes[i] = 0;
    if (mprotect(g_base, guest_memory::ZeroTailBytes, PROT_READ) != 0)
        _exit(128 + SIGSEGV);
}

void Fault(int number, siginfo_t* info, void* context)
{
    if (number == SIGTRAP && info && info->si_code == TRAP_TRACE &&
        t_nullStep && context && g_base)
    {
        FinishNullPageStep(context);
        return;
    }
    // A source fault, breakpoint, or another signal may interrupt the stepped
    // instruction. Do not leave the shared null range writable on recovery.
    FinishNullPageStep(context);
    if (number == SIGSEGV && info && info->si_code == SEGV_ACCERR && context && g_base)
    {
        auto* cpu = static_cast<ucontext_t*>(context);
        const bool write = (cpu->uc_mcontext.__mc_saved_registers_.? & 2) != 0;
        const uintptr_t base = reinterpret_cast<uintptr_t>(g_base);
        const uint32_t address = reinterpret_cast<uint32_t>(info->si_addr);
        if (write && g_nullGuard.load(std::memory_order_acquire) &&
            address >= 0 && address < guest_memory::ZeroTailSize)
        {
            if (mprotect(g_base, guest_memory::ZeroTailSize,
                        PROT_READ | PROT_WRITE) == 0)
            {
                t_nullStep = true;
                // Single-step: set the trap flag in the interrupted context.
                cpu->uc_mcontext.__mc_saved_registers_.? |= 0x100;
                return;
            }
        }
        if (address >= 0 && address < g_bytes)
        {
            const size_t page = address / PageSize;
            if (g_rollbackPages[page].load(std::memory_order_acquire) &&
                g_rollbackRegistered.load(std::memory_order_acquire) && DeferFault(g_base + address, true)) return;
            while (g_lock.test_and_set(std::memory_order_acquire)) {}
            // A second thread may have opened the page while this thread entered
            // the handler. Retrying its instruction is then sufficient.
            if (g_permission[page] == Write && g_state[page] == Dirty)
            {
                g_lock.clear(std::memory_order_release);
                return;
            }
            if (g_permission[page] == Write && g_state[page] == Armed &&
                mprotect(g_base + page * PageSize, PageSize,
                        PROT_READ | PROT_WRITE) == 0)
            {
                g_state[page] = Dirty;
                g_lock.clear(std::memory_order_release);
                return;
            }
            g_lock.clear(std::memory_order_release);
        }
        if (base + address >= g_deferredFirst && base + address < g_deferredLast &&
            g_deferredHandler && DeferFault(g_base + address))
            return;
    }
    if (auto handler = g_unhandledHandler.load(std::memory_order_acquire))
        handler(number, info, context);
    Chain(number, info, context);
}
} // namespace
