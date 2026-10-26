// TTT2 native port -- minimal runtime skeleton (Phase 1: reach a link; Phase 2: make it run).
// Reserves the 32-bit guest address space, loads the decrypted TU3-patched image, populates
// the indirect-call function table, and jumps to the guest entry point.
#include "ppc_recomp_shared.h"
#include "audio_output.h"
#include "kernel/guest_heap.h"
#include "kernel/kernel_shared.h"
#include "kernel/phys_mirror.h"
#include "kernel/xma.h"
#include "kernel/steam_net.h"
#include "host_vm.h"
#include "host_fault.h"
#include "host_runtime.h"
#include "host_process.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

// 4 GiB reserved so (base + any 32-bit guest address) is always valid.
// 4 GiB of guest space plus a 16 MiB zero tail: a garbage guest pointer near the top of the
// address space (measured: 0xFFFFFFFD, an error code used as a pointer once the menus began
// building) makes 4-byte reads that CROSS the 4 GiB edge. Real hardware wraps or reads kernel
// space and survives; with an exact-size reservation the host faults instead. The committed
// zero tail turns those into benign zero reads, the same philosophy as the committed null page.
static constexpr uint64_t GUEST_RESERVE = 0x100000000ull + 0x1000000ull;

// g_base is the shared guest memory base (declared in kernel_shared.h, defined in kernel_thread.cpp).

// Called through the lookup table when guest code indirect-calls an address with no
// compiled function. Logs the target and acts as a no-op callee. g_unmappedReporter lets
// kernel code (XexGetProcedureAddress) recognize these slots as still-free.
static void UnmappedCallReporter(PPCContext& ctx, uint8_t* base)
{
    static const bool strict = std::getenv("TTT2_STRICT_INDIRECT") != nullptr;
    static std::atomic<unsigned> logged{0};
    if (strict || logged.fetch_add(1, std::memory_order_relaxed) < 100)
    {
        // The guest's call chain (back chain at r1, saved lr at frame - 8), for the first reports.
        char chain[160] = "";
        if (logged.load(std::memory_order_relaxed) <= 20)
        {
            int k = 0;
            uint32_t sp = ctx.r1.u32;
            for (int i = 0; i < 10 && sp && sp < 0xFFFFFFF0u && k < int(sizeof(chain)) - 10; ++i)
            {
                const uint32_t prev = __builtin_bswap32(*reinterpret_cast<uint32_t*>(base + sp));
                if (!prev || prev <= sp || prev - sp > 0x10000) break;
                k += std::snprintf(chain + k, sizeof(chain) - k, " %08X", __builtin_bswap32(*reinterpret_cast<uint32_t*>(base + prev - 8)));
                sp = prev;
            }
        }
        std::fprintf(stderr, "[runtime] UNMAPPED indirect call: ctr=0x%08X lr=0x%08X r3=0x%08X r1=0x%08X%s%s\n",
            static_cast<uint32_t>(ctx.ctr.u32), static_cast<uint32_t>(ctx.lr), ctx.r3.u32, ctx.r1.u32, *chain ? ", guest callers" : "", chain);
    }
    if (strict)
    {
        // Returning from a missing switch target can leave the guest's frame
        // allocated and corrupt every later call. Validation runs must stop at
        // this first divergence, rather than reporting a later corrupt pointer.
        std::fprintf(stderr, "[runtime] strict indirect-call gate failed; stopping before further guest execution\n");
        std::fflush(stderr);
        LogDrain(); host_process::ExitNow(5);
    }
}
PPCFunc* const g_unmappedReporter = UnmappedCallReporter;

// Guest entry call isolated in its own frame for the host fault boundary.
static void RunGuest(PPCContext& ctx, uint8_t* base)
{
    struct Call { PPCContext* ctx; uint8_t* base; } call{&ctx, base};
    if (host_fault::Run([](void* arg) {
            auto* c = static_cast<Call*>(arg);
            _xstart(*c->ctx, c->base);
        }, &call))
        std::fprintf(stderr, "[runtime] _xstart returned\n");
    else
        std::fprintf(stderr, "[runtime] guest faulted\n");
}

#if defined(_WIN32)
// Request the dedicated adapter on switchable-graphics systems.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char** argv)
{
    // The Steam helper process (kernel/steam_bridge.cpp): Steam lives there so Steam Input does
    // not hide the controllers from the game. Nothing of the game starts in it.
    if (argc >= 2 && std::strcmp(argv[1], "--steam-bridge") == 0) return steamnet::RunBridge(argc, argv);
    if (argc >= 4 && std::strcmp(argv[1], "--translate") == 0)
    {
        extern int GpuTranslateFile(const char* kind, const char* path);
        return GpuTranslateFile(argv[2], argv[3]);
    }
    if (argc >= 2 && std::strcmp(argv[1], "--build-shaders") == 0)
    {
        const auto installed = host_runtime::InstalledLayout(1);
        const auto game = argc > 2 && *argv[2] ? std::filesystem::path(argv[2]) : host_runtime::VfsRoot();
        const auto image = argc > 3 && *argv[3] ? std::filesystem::path(argv[3]) :
            !installed.empty() ? installed : std::filesystem::path(host_runtime::DefaultImagePath());
        const unsigned threads = argc > 4 ? unsigned(std::atoi(argv[4])) : 0u;
        extern int GpuPrebuildShaders(const std::vector<std::filesystem::path>& files, unsigned threads);
        return GpuPrebuildShaders({ game / "data002.bin", game / "data003.bin", image }, threads);
    }
    const std::filesystem::path installedImage = host_runtime::InstalledLayout(argc);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    host_runtime::StartLogWriter();
    InitializeHostAudio();
    host_runtime::ConfigureTimer();
    extern bool GpuShaderPrebuildCurrent();
    if (!installedImage.empty() && !GpuShaderPrebuildCurrent())
        host_runtime::StartShaderPrebuild();

    // 1) Reserve AND commit the whole guest address space up front (as Unleashed does).
    //    The game's init code reads through would-be-null pointers and relies on those reads
    //    returning zero (then takes a fallback path) rather than faulting. Committing everything
    //    to a zero-backed region makes those defensive paths work.
    // Last-resort logger: anything that slips past every SEH/VEH net still says WHERE it
    // died before the process goes (run_user1 vanished with an empty log).


    host_runtime::InstallFatalHandler();

    // The host VM tracks which guest pages the CPU wrote since the hardware
    // backend last uploaded a texture. The Windows implementation uses
    // MEM_WRITE_WATCH; Linux arms pages with read-only protection.
    uint8_t* base = host_vm::ReserveGuest(GUEST_RESERVE);
    if (!base) { std::fprintf(stderr, "guest alloc failed (%lu)\n", host_vm::LastError()); return 1; }
    g_base = base;   // let kernel code (threads, waits) reach guest memory without a ctx arg
    // The C and E physical aliases are filled on demand from the A view.
    if (!PhysMirrorInit(base)) return 1;

    // 3) Load the decrypted, TU3-patched PPC image at its base address.
    const std::filesystem::path imgPath = (argc > 1) ? std::filesystem::path(argv[1])
        : !installedImage.empty() ? installedImage
        : std::filesystem::path(host_runtime::DefaultImagePath());
    const auto imgUtf8 = imgPath.u8string();
    const std::string imgLabel(reinterpret_cast<const char*>(imgUtf8.data()), imgUtf8.size());
    std::ifstream img(imgPath, std::ios::binary);
    if (!img) { std::fprintf(stderr, "cannot open image: %s\n", imgLabel.c_str()); return 1; }
    if (!host_vm::PrepareHostWrite(base + PPC_IMAGE_BASE, PPC_IMAGE_SIZE))
    {
        std::fprintf(stderr, "cannot prepare guest image memory (%lu)\n", host_vm::LastError());
        return 1;
    }
    img.read(reinterpret_cast<char*>(base + PPC_IMAGE_BASE), PPC_IMAGE_SIZE);
    const std::streamsize loaded = img.gcount();
    if (loaded != static_cast<std::streamsize>(PPC_IMAGE_SIZE) || img.bad())
    {
        std::fprintf(stderr, "incomplete game image: %s (%llu of %llu bytes)\n",
                     imgLabel.c_str(), (unsigned long long)loaded,
                     (unsigned long long)PPC_IMAGE_SIZE);
        return 1;
    }
    std::fprintf(stderr, "[runtime] loaded %s (%llu bytes) at guest 0x%llX\n",
                 imgLabel.c_str(), (unsigned long long)loaded, (unsigned long long)PPC_IMAGE_BASE);

    // 4) Keep host function pointers outside the guest address space. Guest writes
    // used to corrupt the table at IMAGE_BASE + IMAGE_SIZE during the title transition.
    static_assert((PPC_CODE_SIZE & 3) == 0);
    g_ppcFunctionTable = static_cast<PPCFunc**>(
        std::calloc(PPC_CODE_SIZE / 4, sizeof(PPCFunc*)));
    if (!g_ppcFunctionTable) {
        std::fprintf(stderr, "host function table allocation failed\n");
        return 1;
    }
    size_t mapped = 0;
    for (size_t i = 0; PPCFuncMappings[i].guest != 0; ++i) {
        const uint32_t target = static_cast<uint32_t>(PPCFuncMappings[i].guest);
        if (PPCFuncMappings[i].guest != target ||
            uint32_t(target - uint32_t(PPC_CODE_BASE)) >= PPC_CODE_SIZE || (target & 3)) {
            std::fprintf(stderr, "invalid guest function mapping: 0x%llX\n",
                         (unsigned long long)PPCFuncMappings[i].guest);
            return 1;
        }
        PPC_LOOKUP_FUNC(base, target) = PPCFuncMappings[i].host;
        ++mapped;
    }
    std::fprintf(stderr, "[runtime] mapped %zu guest functions\n", mapped);

    // 4b) Fill every EMPTY slot with a reporter: an indirect call to an unmapped guest
    // address then logs the target (ctr) + caller (lr) and returns as a no-op, instead of
    // jumping to null. Reported addresses go into tekken.toml's functions list (regen).
    size_t empty = 0;
    for (uint32_t a = PPC_CODE_BASE; a < PPC_CODE_BASE + (uint32_t)PPC_CODE_SIZE; a += 4)
    {
        PPCFunc*& slot = PPC_LOOKUP_FUNC(base, a);
        if (!slot) { slot = UnmappedCallReporter; ++empty; }
    }
    std::fprintf(stderr, "[runtime] %zu unmapped slots -> reporter\n", empty);

    // Init guest heaps (regions clear of the thread block, image, and function table).
    g_virtualHeap.Init(base, VHEAP_BEGIN, VHEAP_END);
    g_physicalHeap.Init(base, PHEAP_BEGIN, PHEAP_END);
    XmaInit(base);   // XMA context array + its register, read by the title's sound init

    // 5) Guest thread block: PCR + TLS + TEB + stack (layout ported from Unleashed GuestThreadContext).
    //    The entry code dereferences the thread pointer (r13 -> PCR); without this it faults at guest 0x193.
    constexpr uint32_t PCR_SIZE = 0xAB0, TLS_SIZE = 0x100, TEB_SIZE = 0x2E0, STACK_SIZE = 0x40000;
    constexpr uint32_t THREAD_TOTAL = PCR_SIZE + TLS_SIZE + TEB_SIZE + STACK_SIZE;
    const uint32_t threadGuest = 0x40000000;   // already committed (whole space is committed)
    uint8_t* thr = base + threadGuest;
    auto put32be = [](uint8_t* p, uint32_t v) { *reinterpret_cast<uint32_t*>(p) = __builtin_bswap32(v); };
    put32be(thr + 0x00,  threadGuest + PCR_SIZE);                     // PCR: TLS pointer
    put32be(thr + 0x100, threadGuest + PCR_SIZE + TLS_SIZE);         // PCR: TEB pointer
    thr[0x10C] = 0;                                                   // PCR: current CPU number
    put32be(thr + PCR_SIZE + 0x10, 0xFFFFFFFF);                      // TLS slot (quirk from Unleashed)
    put32be(thr + PCR_SIZE + TLS_SIZE + 0x14C, 1);                   // TEB: thread id

    PPCContext ctx{};
    ctx.r1.u64  = threadGuest + THREAD_TOTAL;   // stack pointer (grows down into STACK region)
    ctx.r13.u64 = threadGuest;                  // thread pointer -> PCR
    ctx.r3.u64  = 0;                            // entry argument (params.value)
    ctx.fpscr.loadFromHost();

    // The packet transport starts in the background now (Steam: a helper process, seconds to start):
    // a profile's sign-in asks for this PC's peer id (xlive.cpp LocalPeerId), which starts it if it
    // has not started yet, on the game's main thread. MEASURED: picking a profile froze the menus for
    // ~5 s (one 7.8 s stats window at 20 fps, no shader builds) while the helper started Steam.
    extern bool XSocketStartTransport();
    std::thread([] { XSocketStartTransport(); }).detach();

    std::fprintf(stderr, "[runtime] entering _xstart (base=%p, r13=0x%X, r1=0x%X)\n",
                 static_cast<void*>(base), (unsigned)ctx.r13.u32, (unsigned)ctx.r1.u32);
    g_base = base;
    extern void KernelRegisterGuestThread(uint32_t, uint32_t);
    extern void KernelPublishMainGuestContext(PPCContext*);
    KernelRegisterGuestThread(0xFFFFFFFF, 0);   // entry FFFFFFFF = the main (_xstart) thread

    host_runtime::InstallGuestGuards();
    // Diagnostic gate: stop a corrupting write at its source, before it destroys
    // code or constants used by other guest threads. The first .rdata page holds
    // loader-patched imports, so leave that page writable. These ranges come from
    // the TU3 image's PE section table; the writable .data section starts at
    // 0x82DC0000. The normal fault logger identifies the writing host/guest code.
    if (std::getenv("TTT2_GUARD_IMAGE"))
    {
        if (!host_vm::Protect(base + 0x82001000, 0x202000, host_vm::Protection::ReadOnly) ||
            !host_vm::Protect(base + PPC_CODE_BASE, PPC_CODE_SIZE, host_vm::Protection::ReadOnly))
        {
            std::fprintf(stderr, "[guard] image protection failed (%lu)\n", host_vm::LastError());
            return 1;
        }
        std::fprintf(stderr, "[guard] TU3 read-only data and code protected\n");
    }
    extern void KernelInstallTimeStampBundle(uint8_t*);
    KernelInstallTimeStampBundle(base);     // the KeTimeStampBundle data import (kernel_sys.cpp)
    KernelPublishMainGuestContext(&ctx);   // lets profile_threads.py walk the guest stack
    RunGuest(ctx, base);
    // Guest callbacks can leave detached host threads alive. End the process
    // through the host boundary instead of destructing shared state under them.
    LogDrain();
    host_process::ExitNow(0);
}
