// Core kernel imports: process type, critical sections, spin locks, TLS.
// Critical sections and spin locks are REAL cross-thread locks: the game runs on real host
// threads (ExCreateThread), and its GC/allocator relies on Rtl* critical sections for
// exclusion; no-op locking let the GC zero live objects out from under other threads.
#include "kernel_shared.h"
#include "kernel_trace.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

PPC_FUNC(__imp__KeGetCurrentProcessType)
{
    KTrace("KeGetCurrentProcessType"); ctx.r3.u32 = 1; }              // title process

// --- critical sections (guest CRITICAL_SECTION lives in guest memory; ~0x1C bytes) ---
// The guest struct is treated as an opaque key: each guest address gets a host recursive
// mutex, so recursion and cross-thread blocking behave like the real Rtl API. Entries are
// never destroyed (a game may leak/reinit sections; a locked mutex must stay valid).
static std::shared_mutex g_csMapLock;
static std::unordered_map<uint32_t, std::unique_ptr<std::recursive_mutex>> g_csMap;

static std::recursive_mutex& CsFor(uint32_t ga)
{
    {
        std::shared_lock rl(g_csMapLock);
        auto it = g_csMap.find(ga);
        if (it != g_csMap.end()) return *it->second;
    }
    std::unique_lock wl(g_csMapLock);
    auto& slot = g_csMap[ga];
    if (!slot) slot = std::make_unique<std::recursive_mutex>();
    return *slot;
}

PPC_FUNC(__imp__RtlInitializeCriticalSection)
{
    KTrace("RtlInitializeCriticalSection");
    std::memset(base + ctx.r3.u32, 0, 0x1C);
}
PPC_FUNC(__imp__RtlInitializeCriticalSectionAndSpinCount)
{
    KTrace("RtlInitializeCriticalSectionAndSpinCount");
    std::memset(base + ctx.r3.u32, 0, 0x1C);
    ctx.r3.u32 = STATUS_SUCCESS;
}
// Slow-acquisition diagnostics: report any CS wait >50ms with the waiter's guest lr and
// the holder's last-known lock site, to find lock-held-across-slow-work stalls.
namespace {
std::mutex g_csOwnerMutex;
struct CsOwnerInfo { uint32_t lr; unsigned long tid; };
std::unordered_map<uint32_t, CsOwnerInfo> g_csOwners;
}
PPC_FUNC(__imp__RtlEnterCriticalSection)
{
    KTrace("RtlEnterCriticalSection");
    auto& m = CsFor(ctx.r3.u32);
    if (!m.try_lock())
    {
        auto t0 = std::chrono::steady_clock::now();
        m.lock();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - t0).count();
        if (ms > 50)
        {
            CsOwnerInfo prev{};
            {
                std::lock_guard lk(g_csOwnerMutex);
                auto it = g_csOwners.find(ctx.r3.u32);
                if (it != g_csOwners.end()) prev = it->second;
            }
            static std::atomic<int> slowLog{0};
            if (slowLog++ < 60)
                std::fprintf(stderr,
                    "[cs] slow acquire cs=%08X waited=%lldms waiter_lr=%08X prev_owner_lr=%08X tid=%lu\n",
                    ctx.r3.u32, (long long)ms, (uint32_t)ctx.lr, prev.lr, prev.tid);
        }
    }
    {
        std::lock_guard lk(g_csOwnerMutex);
        g_csOwners[ctx.r3.u32] = { (uint32_t)ctx.lr, GetCurrentThreadId() };
    }
}
PPC_FUNC(__imp__RtlLeaveCriticalSection)
{
    KTrace("RtlLeaveCriticalSection");
    CsFor(ctx.r3.u32).unlock();
}
PPC_FUNC(__imp__RtlTryEnterCriticalSection)
{
    KTrace("RtlTryEnterCriticalSection");
    ctx.r3.u32 = CsFor(ctx.r3.u32).try_lock() ? 1 : 0;
}
PPC_FUNC(__imp__KeEnterCriticalRegion)
{
    KTrace("KeEnterCriticalRegion"); }
PPC_FUNC(__imp__KeLeaveCriticalRegion)
{
    KTrace("KeLeaveCriticalRegion"); }

// --- spin locks: real atomic CAS on the guest lock word (0 = free, nonzero = held).
// Inlined game code uses lwarx/stwcx on the same word; both sides agree on 0/nonzero. ---
static void SpinAcquire(uint8_t* base, uint32_t ga)
{
    volatile long* p = reinterpret_cast<volatile long*>(base + ga);
    while (_InterlockedCompareExchange(p, 1, 0) != 0) _mm_pause();
}
static void SpinRelease(uint8_t* base, uint32_t ga)
{
    _InterlockedExchange(reinterpret_cast<volatile long*>(base + ga), 0);
}

PPC_FUNC(__imp__KfAcquireSpinLock)
{
    KTrace("KfAcquireSpinLock"); SpinAcquire(base, ctx.r3.u32); ctx.r3.u32 = 0; }   // old IRQL
PPC_FUNC(__imp__KfReleaseSpinLock)
{
    KTrace("KfReleaseSpinLock"); SpinRelease(base, ctx.r3.u32); }
PPC_FUNC(__imp__KeAcquireSpinLockAtRaisedIrql)
{
    KTrace("KeAcquireSpinLockAtRaisedIrql"); SpinAcquire(base, ctx.r3.u32); }
PPC_FUNC(__imp__KeReleaseSpinLockFromRaisedIrql)
{
    KTrace("KeReleaseSpinLockFromRaisedIrql"); SpinRelease(base, ctx.r3.u32); }

// --- thread-local storage: index allocation is global, values are per (host) thread ---
static std::atomic<uint32_t> g_tlsNext{0};
static thread_local uint32_t g_tlsValues[256];

PPC_FUNC(__imp__KeTlsAlloc)
{
    KTrace("KeTlsAlloc"); uint32_t i = g_tlsNext++; ctx.r3.u32 = (i < 256) ? i : 0xFFFFFFFF; }
PPC_FUNC(__imp__KeTlsFree)
{
    KTrace("KeTlsFree"); ctx.r3.u32 = 1; }
PPC_FUNC(__imp__KeTlsGetValue)
{
    KTrace("KeTlsGetValue"); uint32_t i = ctx.r3.u32; ctx.r3.u32 = (i < 256) ? g_tlsValues[i] : 0; }
PPC_FUNC(__imp__KeTlsSetValue)
{
    KTrace("KeTlsSetValue"); uint32_t i = ctx.r3.u32; if (i < 256) g_tlsValues[i] = ctx.r4.u32; ctx.r3.u32 = 1; }

// --- The game's own dying words -------------------------------------------------------
// Run s12n ended with the guest calling KeBugCheck after routing a message through
// _vsnprintf and DbgPrint - all three were silent auto-stubs, so the game's fatal
// diagnostics went straight to the void. These give them a voice. DbgPrint gets the format
// string and the first few integer args verbatim (unformatted args beat no message);
// _vsnprintf copies the format string through so guest-side log sinks stay legible.

PPC_FUNC(__imp__DbgPrint)
{
    KTrace("DbgPrint");
    char buf[512] = {};
    uint32_t src = ctx.r3.u32;
    for (int i = 0; i < 511 && src; ++i)
    {
        char c = (char)GRead8(base, src + i);
        if (!c) break;
        buf[i] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    std::fprintf(stderr, "[dbgprint] \"%s\" args r4=%08X r5=%08X r6=%08X r7=%08X\n",
                 buf, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
    ctx.r3.u32 = 0;
}

PPC_FUNC(__imp__KeBugCheck)
{
    KTrace("KeBugCheck");
    std::fprintf(stderr, "[bugcheck] KeBugCheck code=%08X lr=%08X r1=%08X\n",
                 ctx.r3.u32, (uint32_t)ctx.lr, ctx.r1.u32);
    // Print the guest backchain so the assert site is identifiable.
    uint32_t frame = ctx.r1.u32;
    char line[240]; int n = 0;
    n += std::snprintf(line + n, sizeof(line) - n, "[bugcheck] bt:");
    for (int i = 0; i < 10 && frame && frame < 0xFFFFFFF0u; ++i)
    {
        uint32_t prev = GRead32(base, frame);
        if (!prev || prev <= frame || prev - frame > 0x10000) break;
        n += std::snprintf(line + n, sizeof(line) - n, " %08X", GRead32(base, prev - 8));
        frame = prev;
    }
    std::snprintf(line + n, sizeof(line) - n, "\n");
    std::fputs(line, stderr);
    // Do NOT halt: report and continue, so runs survive guest asserts the way they survive
    // nullpage writes. If the game spirals afterwards the log shows why.
    ctx.r3.u32 = 0;
}

// _vsnprintf(dst, count, fmt, va): pass the format string through unformatted and log it -
// enough to identify every assert message without emulating the PPC varargs ABI.
PPC_FUNC(__imp___vsnprintf)
{
    KTrace("_vsnprintf");
    uint32_t dst = ctx.r3.u32, cnt = ctx.r4.u32, fmt = ctx.r5.u32;
    uint32_t i = 0;
    if (dst && cnt && fmt)
    {
        for (; i + 1 < cnt; ++i)
        {
            char c = (char)GRead8(base, fmt + i);
            GWrite8(base, dst + i, c);
            if (!c) break;
        }
        GWrite8(base, dst + (i + 1 < cnt ? i : cnt - 1), 0);
    }
    static int vlog = 0;
    if (vlog < 20 && fmt)
    {
        ++vlog;
        char buf[300] = {};
        for (int k = 0; k < 299; ++k)
        {
            char c = (char)GRead8(base, fmt + k);
            if (!c) break;
            buf[k] = (c == '\n' || c == '\r') ? ' ' : c;
        }
        std::fprintf(stderr, "[dbg] _vsnprintf fmt=\"%s\"\n", buf);
    }
    ctx.r3.u32 = i;
}
