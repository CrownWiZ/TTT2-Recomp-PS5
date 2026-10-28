// PS5 stand-in for diag_hooks.cpp: the registry lookup pieces the kernel and
// the rollback layer read, without the linux sampling backend (futex/ptrace
// primitives the closed kernel does not expose) and without the opt-in
// sub_X first-call wraps. The generated code weak-aliases every sub_X to its
// __imp__ so leaving the wraps out changes nothing at run time.
#include "host_thread.h"
#include "host_vm.h"
#include "kernel_shared.h"
#include "guest_heap.h"
#include "timeline.h"

#include <atomic>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace hitch {

struct Sample { int64_t at; uint32_t chain[24]; uint32_t depth; uint64_t host[32]; uint32_t hostDepth; };
struct Target
{
    std::string name;
    uint32_t tid = 0;
    pthread_t thread{};
    std::atomic<int> pending{0};
    Sample captured{};
    uintptr_t stackLow = 0, stackHigh = 0;
    std::mutex samplingMutex;
    PPCContext* ctx = nullptr;
};

std::atomic<bool> g_active{ false }, g_busy{ false };
std::vector<Target*> g_targets;
std::atomic<Target*> g_main{ nullptr };

int Mode()
{
    static const int mode = [] {
        const char* e = std::getenv("TTT2_HITCH_PROFILE");
        return e && *e >= '1' && *e <= '5' ? *e - '0' : 0;
    }();
    return mode;
}
// The sampling modes need the linux backend; the console build reports 0
// regardless: the closed kernel can park arbitrary threads the way the
// signal-sampler needs.
bool On() { return false; }

// kernel_thread.cpp's registry: host thread -> the PPCContext of its current
// guest call.
struct GuestContextSlot { uint32_t tid; uint32_t entry; PPCContext* ctx; };
extern "C" struct GuestContextRegistry
{
    uint32_t magic, version, r1Offset, lrOffset, capacity;
    std::atomic<uint32_t> count;
    GuestContextSlot slots[512];
} g_guestContextRegistry;

Target* GuestTarget(uint32_t tid)
{
    const uint32_t n = (std::min)(g_guestContextRegistry.count.load(), 512u);
    for (uint32_t i = 0; i < n; ++i)
    {
        const GuestContextSlot& slot = g_guestContextRegistry.slots[i];
        if (slot.tid != tid) continue;
        return g_main.load(); // the registry holds slotted data; the diag table is unused here
    }
    return nullptr;
}

PPCContext* GuestContextOf(uint32_t tid)
{
    if (!tid) return nullptr;
    const uint32_t n = (std::min)(g_guestContextRegistry.count.load(), 512u);
    for (uint32_t i = 0; i < n; ++i)
        if (g_guestContextRegistry.slots[i].tid == tid)
            return g_guestContextRegistry.slots[i].ctx;
    return nullptr;
}

uint32_t GuestEntryOfThread(uint32_t tid)
{
    const uint32_t n = (std::min)(g_guestContextRegistry.count.load(), 512u);
    for (uint32_t i = 0; i < n; ++i)
        if (g_guestContextRegistry.slots[i].tid == tid)
            return g_guestContextRegistry.slots[i].entry;
    return 0;
}

void DumpGuestThreads() {}

// rollback.cpp's watchdog: the live contexts (the racy reads stay reads).
int GuestContexts(uint32_t* tids, uint32_t* entries, PPCContext** ctxs, int max)
{
    const uint32_t n = (std::min)(g_guestContextRegistry.count.load(), 512u);
    int k = 0;
    for (uint32_t i = 0; i < n && k < max; ++i)
    {
        const GuestContextSlot& s = g_guestContextRegistry.slots[i];
        if (!s.ctx) continue;
        tids[k] = s.tid; entries[k] = s.entry; ctxs[k] = s.ctx; ++k;
    }
    return k;
}

} // namespace hitch

// The game's heap counters (rollback.cpp re-simulation diagnostics; the
// heapstat symbols live in guest_heap's statistics).
void GameHeapOps(uint64_t& thread, uint64_t& all)
{
    thread = 0;
    all = 0;
}

// The rollback layer's context watch (the diag table stays empty).
PPCContext* WatchGuestContextOf(uint32_t tid) { return hitch::GuestContextOf(tid); }
PPCContext* WatchGuestContextOf(unsigned long tid) { return hitch::GuestContextOf(uint32_t(tid)); }

// skin_profile.cpp's skinning diagnostic (diag_hooks.cpp's opt-in job tracker):
// the console build carries no diag table, so the hooks are no-ops.
void DiagSkinJob(PPCContext& ctx, uint8_t* base, uint32_t kernel, uint32_t stride)
{
    (void)ctx; (void)base; (void)kernel; (void)stride;
}

bool DiagSkinDescribe(uint32_t addr, char* out, size_t size)
{
    (void)addr; (void)out; (void)size;
    return false;
}

// host_runtime_posix_diagnostics.h's DumpAllStacks is a header-inline that walks
// /proc; the console has no such thing, so the dump is a single line and the
// native sampler has nothing to add.
void DumpAllStacks(const char* why)
{
    std::fprintf(stderr, "[stacks] %s\n", why ? why : "thread dump");
    hitch::DumpGuestThreads();
}

// kernel_video.cpp's mode-3 hitch hooks: hitch::Mode() is 0 on the console (the
// signal sampler needs a kernel the closed SDK does not expose), so both are
// inert — but they must exist for the command-processor call sites to link.
void HitchRegisterThread(const char* name) { (void)name; }
void HitchFrameGap(double ms) { (void)ms; }

// The posix diagnostics header contract: the scheduled dumps stay null hooks
// on the console (the fault boundary owns recovery by itself).
namespace hitch {
void DumpNativeSamples() {}
} // namespace hitch
