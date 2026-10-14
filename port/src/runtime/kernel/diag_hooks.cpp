// Opt-in diagnostic wrappers around individual recompiled game functions. The generated
// code defines every sub_X as a weak alias of __imp__sub_X, so a strong definition here
// takes over all direct and table calls while the original stays reachable as __imp__.
// Each wrapper only observes; it never changes arguments or results.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "kernel_shared.h"
#include "guest_heap.h"
#include "timeline.h"
#include <cmath>
#include <xmmintrin.h>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <string>
#include <chrono>

extern "C" PPC_FUNC(__imp__sub_82AD8A00);
extern "C" PPC_FUNC(__imp__sub_82BD3DF0);
extern "C" PPC_FUNC(__imp__sub_82BD43C0);
extern "C" PPC_FUNC(__imp__sub_82BD39B0);
extern "C" PPC_FUNC(__imp__sub_82BD4278);

// TTT2_TRACE_COPY=<text> (or hex:<bytes> for binary needles such as a XUID): log every call of
// the game's copy routines whose source starts with the needle - which routine, destination,
// size and the guest call chain - to find where a value (e.g. the default name "Player") is put
// into a structure. Routines: memcpy 82BD3DF0, strncpy 82BD43C0, memmove 82BD39B0, memcpy (2)
// 82BD4278; all take r3 dst, r4 src, r5 size.
static void TraceCopy(const char* routine, PPCContext& ctx, uint8_t* base)
{
    static std::string needleBytes = [] {
        const char* e = std::getenv("TTT2_TRACE_COPY");
        if (!e) return std::string();
        if (std::strncmp(e, "hex:", 4) != 0) return std::string(e);
        std::string b;
        for (const char* p = e + 4; p[0] && p[1]; p += 2) b.push_back(char(std::strtoul(std::string(p, 2).c_str(), nullptr, 16)));
        return b;
    }();
    const size_t needleLen = needleBytes.size();
    if (!needleLen || ctx.r5.u32 < needleLen || ctx.r4.u32 < 0x1000 ||
        std::memcmp(base + ctx.r4.u32, needleBytes.data(), needleLen) != 0)
        return;
    static std::atomic<int> logged{ 0 };
    if (logged++ >= 20000) return;
    char line[400];
    int n = std::snprintf(line, sizeof(line), "[copy] %s '%s' %08X -> %08X (%u bytes) lr=%08X chain:", routine,
                          std::getenv("TTT2_TRACE_COPY"), ctx.r4.u32, ctx.r3.u32, ctx.r5.u32, uint32_t(ctx.lr));
    uint32_t frame = ctx.r1.u32;
    for (int i = 0; i < 10 && frame && frame < 0xFFFFFFF0u && n < 360; ++i)
    {
        const uint32_t prev = GRead32(base, frame);
        if (!prev || prev <= frame || prev - frame > 0x10000) break;
        n += std::snprintf(line + n, sizeof(line) - n, " %08X", GRead32(base, prev - 8));
        frame = prev;
    }
    std::fprintf(stderr, "%s\n", line);
}

PPC_FUNC(sub_82BD3DF0) { TraceCopy("memcpy", ctx, base); __imp__sub_82BD3DF0(ctx, base); }
PPC_FUNC(sub_82BD43C0) { TraceCopy("strncpy", ctx, base); __imp__sub_82BD43C0(ctx, base); }
PPC_FUNC(sub_82BD39B0) { TraceCopy("memmove", ctx, base); __imp__sub_82BD39B0(ctx, base); }
PPC_FUNC(sub_82BD4278) { TraceCopy("memcpy2", ctx, base); __imp__sub_82BD4278(ctx, base); }

namespace {
bool DiagBake()
{
    static const bool on = [] { const char* e = std::getenv("TTT2_DIAG_BAKE"); return e && *e == '1'; }();
    return on;
}

uint32_t NonZeroWords(uint8_t* base, uint32_t addr, uint32_t pitch, uint32_t rows, uint32_t rowBytes)
{
    uint32_t n = 0;
    for (uint32_t y = 0; y < rows; ++y)
        for (uint32_t x = 0; x < rowBytes; x += 4)
            n += *reinterpret_cast<uint32_t*>(base + addr + y * pitch + x) != 0;
    return n;
}
}

// Texture recompression dispatcher (CharacterTextureBake output -> DXTn). On entry:
// r4 destination bits, r5 source bits, r6 width, r7 height, r8 source pitch, r9 format.
PPC_FUNC(sub_82AD8A00)
{
    static uint32_t calls = 0;                     // guest calls are serialised per caller
    const uint32_t n = calls++;
    if (DiagBake() && (n < 64 || (n & 63) == 0))    // all early calls, then a sample
    {
        const uint32_t src = ctx.r5.u32, w = ctx.r6.u32, h = ctx.r7.u32, pitch = ctx.r8.u32;
        const uint32_t rows = h < 4096 ? h : 0, rowBytes = pitch < 0x4000 ? (w * 4 < pitch ? w * 4 : pitch) : 0;
        std::fprintf(stderr, "[diag-bake] recompress #%u dst=%08X src=%08X %ux%u pitch=%u fmt=%u r10=%08X "
            "src_nonzero=%u/%u first=%08X %08X\n", n, ctx.r4.u32, src, w, h, pitch, ctx.r9.u32, ctx.r10.u32,
            NonZeroWords(base, src, pitch, rows, rowBytes), rows * (rowBytes / 4),
            GRead32(base, src), GRead32(base, src + 4));
    }
    __imp__sub_82AD8A00(ctx, base);
}

// CPU skinning kernels (vtable slot 3 of the skinning job classes, table at 0x82232EA0): job
// r3 = {+4 palette, +8 palette, +12 bone base, +16 output, +20 input, +24 vertex count}; output
// stride per kernel (16..64 bytes, from each kernel's output increment). TTT2_DIAG_SKIN=1 logs jobs whose output range overlaps another recent job
// on a different input: the thin streak lines are vertices one job writes into another's mesh.
namespace {
struct SkinJob { uint32_t job, out, end, in, lr, tid; uint64_t seq; uint32_t count, stride, palA, palB, boneBase, boneCount, kernel; };
std::mutex g_skinMutex;
SkinJob g_skinRecent[512];
uint64_t g_skinSeq = 0;
int g_skinLogged = 0;

bool DiagSkin()
{
    static const bool on = [] { const char* e = std::getenv("TTT2_DIAG_SKIN"); return e && *e == '1'; }();
    return on;
}

void NoteSkinJob(PPCContext& ctx, uint8_t* base, const char* kernel, uint32_t stride, uint32_t kernelAddr)
{
    const uint32_t job = ctx.r3.u32;
    const uint32_t count = GRead32(base, job + 24);
    const SkinJob cur{ job, GRead32(base, job + 16), GRead32(base, job + 16) + count * stride,
                       GRead32(base, job + 20), uint32_t(ctx.lr), GetCurrentThreadId(), 0,
                       count, stride, GRead32(base, job + 4), GRead32(base, job + 8),
                       GRead16(base, job + 12), GRead16(base, job + 14), kernelAddr };
    std::lock_guard lock(g_skinMutex);
    const uint64_t seq = ++g_skinSeq;
    for (const SkinJob& o : g_skinRecent)
    {
        if (!o.seq || seq - o.seq > 512 || g_skinLogged >= 80) continue;
        if (o.in != cur.in && o.out < cur.end && cur.out < o.end)
        {
            ++g_skinLogged;
            std::fprintf(stderr, "[diag-skin] OVERLAP %s job=%08X out=%08X..%08X in=%08X lr=%08X tid=%u"
                                 " | earlier(-%llu) job=%08X out=%08X..%08X in=%08X lr=%08X tid=%u\n",
                         kernel, cur.job, cur.out, cur.end, cur.in, cur.lr, cur.tid,
                         (unsigned long long)(seq - o.seq), o.job, o.out, o.end, o.in, o.lr, o.tid);
        }
    }
    SkinJob& slot = g_skinRecent[seq % 512];
    slot = cur;
    slot.seq = seq;
}
}

#define TTT2_SKIN_HOOK(addr, stride)                                                 \
    extern "C" PPC_FUNC(__imp__sub_##addr);                                    \
    PPC_FUNC(sub_##addr)                                                       \
    {                                                                          \
        const timeline::Scope skinMark{ timeline::SkinJob, 0x##addr & 0xFFFFu };          \
        if (DiagSkin()) NoteSkinJob(ctx, base, #addr, stride, 0x##addr);                 \
        __imp__sub_##addr(ctx, base);                                          \
    }
TTT2_SKIN_HOOK(82B10800, 64)
TTT2_SKIN_HOOK(82B10908, 16)
TTT2_SKIN_HOOK(82B10A70, 32)
TTT2_SKIN_HOOK(82B10C88, 48)
TTT2_SKIN_HOOK(82B10F40, 64)
TTT2_SKIN_HOOK(82B11288, 20)
TTT2_SKIN_HOOK(82B11500, 36)
TTT2_SKIN_HOOK(82B118C0, 16)

// Dynamic vertex allocator (sub_82AD46F0 -> sub_82AD5B40: a plain cursor bump at this+24, no
// lock). TTT2_DIAG_SKIN=1 also reports two threads inside it at once (thread, guest CPU byte
// KPCR+0x10C, caller), which hands both callers the same output range.
extern "C" PPC_FUNC(__imp__sub_82AD46F0);
namespace {
std::atomic<int> g_allocInside{0};
std::atomic<uint32_t> g_allocOwner{0}, g_allocOwnerLr{0}, g_allocOwnerCpu{0};
std::atomic<int> g_allocLogged{0};
}
PPC_FUNC(sub_82AD46F0)
{
    if (!DiagSkin()) { __imp__sub_82AD46F0(ctx, base); return; }
    const uint32_t tid = GetCurrentThreadId(), lr = uint32_t(ctx.lr);
    const uint32_t cpu = ctx.r13.u32 ? base[ctx.r13.u32 + 0x10C] : 0xFF;
    if (g_allocInside.fetch_add(1) > 0 && g_allocLogged.fetch_add(1) < 40)
        std::fprintf(stderr, "[diag-skin] CONCURRENT alloc this=%08X size=%u tid=%u cpu=%u lr=%08X while tid=%u cpu=%u lr=%08X\n",
                     ctx.r3.u32, ctx.r4.u32, tid, cpu, lr, g_allocOwner.load(), g_allocOwnerCpu.load(), g_allocOwnerLr.load());
    g_allocOwner = tid; g_allocOwnerLr = lr; g_allocOwnerCpu = cpu;
    __imp__sub_82AD46F0(ctx, base);
    g_allocInside.fetch_sub(1);
}

// Skinned-vertex ring advance (sub_82AD5BA0: this+20 index mod this+68 regions, cursor = base).
// A region written in frame n is reused in frame n + regions; the GPU must have drawn it by
// then. TTT2_DIAG_SKIN=1 compares, at each advance, the game's swaps issued two advances
// earlier with the frames the command processor has presented: a positive deficit means the
// CPU is about to overwrite vertices the GPU emulation has not drawn yet.
extern "C" PPC_FUNC(__imp__sub_82AD5BA0);
extern std::atomic<uint64_t> g_cpPresents, g_gameSwaps;
namespace {
std::mutex g_ringMutex;
uint64_t g_ringSwaps[4] = {};
uint64_t g_ringAdvances = 0, g_ringDeficitHist[6] = {};
}
PPC_FUNC(sub_82AD5BA0)
{
    if (DiagSkin())
    {
        std::lock_guard lock(g_ringMutex);
        const uint64_t n = g_ringAdvances++;
        const uint64_t swapsNow = g_gameSwaps.load(), presented = g_cpPresents.load();
        if (n >= 2)
        {
            const uint64_t needed = g_ringSwaps[(n - 2) & 3];
            const int64_t deficit = int64_t(needed) - int64_t(presented);
            ++g_ringDeficitHist[deficit <= 0 ? 0 : deficit >= 5 ? 5 : deficit];
        }
        g_ringSwaps[n & 3] = swapsNow;
        if (n % 600 == 599)
            std::fprintf(stderr, "[diag-skin] ring advances=%llu swaps=%llu presented=%llu deficit hist 0:%llu 1:%llu 2:%llu 3:%llu 4:%llu 5+:%llu\n",
                         (unsigned long long)(n + 1), (unsigned long long)swapsNow, (unsigned long long)presented,
                         (unsigned long long)g_ringDeficitHist[0], (unsigned long long)g_ringDeficitHist[1],
                         (unsigned long long)g_ringDeficitHist[2], (unsigned long long)g_ringDeficitHist[3],
                         (unsigned long long)g_ringDeficitHist[4], (unsigned long long)g_ringDeficitHist[5]);
    }
    __imp__sub_82AD5BA0(ctx, base);
}

// Frame-log helper: describe the skinning job (most recent covering `addr`) and, for the
// 4-bone kernel (96-byte input: +64 four bone indices, +80 four weights), the source record
// of the vertex written at `addr`. Returns false when no recorded job covers it.
bool DiagSkinDescribe(uint32_t addr, char* out, size_t size)
{
    std::lock_guard lock(g_skinMutex);
    const SkinJob* best = nullptr;
    for (const SkinJob& j : g_skinRecent)
        if (j.seq && j.out <= addr && addr < j.end && (!best || j.seq > best->seq)) best = &j;
    if (!best) return false;
    uint8_t* base = g_base;
    const uint32_t stride = best->stride;
    const uint32_t vertex = stride ? (addr - best->out) / stride : 0;
    int n = std::snprintf(out, size, "job=%08X stride=%u vertex %u/%u bones [%u,+%u) palA=%08X palB=%08X in=%08X tid=%u",
                          best->job, stride, vertex, best->count, best->boneBase, best->boneCount,
                          best->palA, best->palB, best->in, best->tid);
    if (stride == 64 && n > 0 && size_t(n) < size)
    {
        const uint32_t rec = best->in + vertex * 96;
        float w[4];
        for (int k = 0; k < 4; ++k) { const uint32_t raw = GRead32(base, rec + 80 + k * 4); std::memcpy(&w[k], &raw, 4); }
        n += std::snprintf(out + n, size - n, " idx=%u,%u,%u,%u w=%.3g,%.3g,%.3g,%.3g",
                           GRead32(base, rec + 64), GRead32(base, rec + 68), GRead32(base, rec + 72), GRead32(base, rec + 76),
                           w[0], w[1], w[2], w[3]);
        // Re-run the game's 4-bone kernel now on this one vertex (same input record and
        // palettes, scratch output): same result = the inputs give this position; different =
        // the palettes changed after the job ran (it read them mid-update).
        if (best->kernel == 0x82B10F40u && n > 0 && size_t(n) < size)
        {
            static uint32_t scratch = 0;
            if (!scratch)
                if (void* p = g_virtualHeap.Alloc(0x11000, 0x100)) scratch = GuestAddr(base, p);
            if (scratch)
            {
                GWrite32(base, scratch + 4, best->palA);
                GWrite32(base, scratch + 8, best->palB);
                GWrite16(base, scratch + 12, uint16_t(best->boneBase));
                GWrite16(base, scratch + 14, uint16_t(best->boneCount));
                GWrite32(base, scratch + 16, scratch + 0x40);
                GWrite32(base, scratch + 20, rec);
                GWrite32(base, scratch + 24, 1);
                // Guest FP/VMX code loads MXCSR from the context; keep the host thread's own
                // (a zeroed context unmasks every FP exception: C000008F on the next op).
                const unsigned int hostCsr = _mm_getcsr();
                PPCContext c{};
                c.fpscr.csr = hostCsr;
                c.r3.u32 = scratch;
                c.r1.u32 = scratch + 0x11000 - 0x100;
                __imp__sub_82B10F40(c, base);
                _mm_setcsr(hostCsr);
                float a[3], b[3];
                for (int k = 0; k < 3; ++k)
                {
                    const uint32_t ra = GRead32(base, scratch + 0x40 + k * 4), rb = GRead32(base, addr + k * 4);
                    std::memcpy(&a[k], &ra, 4); std::memcpy(&b[k], &rb, 4);
                }
                const float d = std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1]) + std::fabs(a[2] - b[2]);
                std::snprintf(out + n, size - n, " rerun=(%.0f,%.0f,%.0f) %s", a[0], a[1], a[2], d < 1.0f ? "same" : "DIFF");
            }
        }
    }
    return true;
}

// Frame pacing (TTT2_FRAMETIME=1): how long the main thread waits for TTT2's job pool.
// sub_824F58B8(job) kicks a job of the pool threads (sub_824F5458, which poll for work with
// Sleep(1) while idle) and waits for it by entering the job's lock (+24). Every 10 s: waits,
// total and longest wait (ms).
extern "C" PPC_FUNC(__imp__sub_824F58B8);
PPC_FUNC(sub_824F58B8)
{
    static const bool on = [] { const char* e = std::getenv("TTT2_FRAMETIME"); return e && *e == '1'; }();
    if (!on)
    {
        __imp__sub_824F58B8(ctx, base);
        return;
    }
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    __imp__sub_824F58B8(ctx, base);
    const double ms = std::chrono::duration<double, std::milli>(clock::now() - start).count();
    static std::mutex m;
    static uint64_t waits = 0;
    static double total = 0, longest = 0;
    static clock::time_point report = clock::now();
    std::lock_guard lock(m);
    ++waits;
    total += ms;
    longest = (std::max)(longest, ms);
    if (clock::now() - report >= std::chrono::seconds(10))
    {
        std::fprintf(stderr, "[frame] job-pool waits 10s: %llu waits, %.0f ms total, mean %.3f ms, longest %.1f ms\n",
                     (unsigned long long)waits, total, waits ? total / double(waits) : 0.0, longest);
        waits = 0;
        total = longest = 0;
        report = clock::now();
    }
}

// Frame timeline (TTT2_TIMELINE=1, timeline.h): the game-side waits of a frame.
// sub_82ACFF90(obj): main thread, polls obj+8 (busy) with 1 ms waits on the event at obj+196.
// sub_82AE4438(monitor, target): job worker, waits until the monitor's 64-bit count = target.
// sub_82738DA0: D3D's wait for the GPU (loops on sub_8273D4A0).
extern "C" PPC_FUNC(__imp__sub_82ACFF90);
PPC_FUNC(sub_82ACFF90)
{
    const timeline::Scope mark{ timeline::MainResourceWait, ctx.r3.u32 };
    __imp__sub_82ACFF90(ctx, base);
}
extern "C" PPC_FUNC(__imp__sub_82AE4438);
PPC_FUNC(sub_82AE4438)
{
    const timeline::Scope mark{ timeline::WorkerCounterWait, ctx.r4.u32 };
    __imp__sub_82AE4438(ctx, base);
}
extern "C" PPC_FUNC(__imp__sub_82738DA0);
PPC_FUNC(sub_82738DA0)
{
    const timeline::Scope mark{ timeline::MainGpuWait, ctx.r4.u32 };
    __imp__sub_82738DA0(ctx, base);
}
