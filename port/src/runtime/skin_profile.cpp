// Shared overrides of generated weak PPC worker-job symbols. The original
// implementations remain callable through their __imp__ aliases.
#include "kernel/kernel_shared.h"
#include "skin_four_bone_native.h"
#include "packed_field_native.h"
#include "kernel/timeline.h"

void DiagSkinJob(PPCContext& ctx, uint8_t* base, uint32_t kernel, uint32_t stride);
#if defined(TTT2_GEOMETRY_AB_PROBE)
#include "kernel/gpu_geometry_probe.h"
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct SkinCounter {
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> vertices{0};
    std::atomic<uint64_t> nanoseconds{0};
};

std::array<SkinCounter, 6> counters;
std::atomic<int64_t> nextReportNs{0};
constexpr std::array<const char*, 6> names = {
    "64a", "16", "32", "48", "64b", "20"
};

bool SkinProfileOn()
{
    static const bool on = [] {
        const char* value = std::getenv("TTT2_SKIN_PROFILE");
        return value && *value == '1';
    }();
    return on;
}

bool SkipPackedFieldJob()
{
    // Diagnostic ablation only: skipping leaves output buffers and auxiliary
    // state untouched. It is not an equivalent implementation of this job.
    static std::atomic<bool> skip{[] {
        const char* value = std::getenv("TTT2_PACKED_FIELD_MODE");
        const bool enabled = value && std::strcmp(value, "skip") == 0;
        if (enabled) std::fprintf(stderr, "[packed-field] mode=skip (diagnostic)\n");
        return enabled;
    }()};
    static const char* path = std::getenv("TTT2_PACKED_FIELD_MODE_FILE");
    if (path && *path)
    {
        static std::atomic<uint32_t> calls{0};
        static std::atomic_flag reading = ATOMIC_FLAG_INIT;
        if ((calls.fetch_add(1, std::memory_order_relaxed) & 63u) == 0 &&
            !reading.test_and_set(std::memory_order_acquire))
        {
            std::ifstream file(path);
            std::string value;
            if ((file >> value) && (value == "original" || value == "skip"))
            {
                const bool next = value == "skip";
                if (skip.exchange(next, std::memory_order_relaxed) != next)
                    std::fprintf(stderr, "[packed-field] mode=%s (diagnostic)\n", value.c_str());
            }
            reading.clear(std::memory_order_release);
        }
    }
    return skip.load(std::memory_order_relaxed);
}

enum class NativeMode { Off, Verify, On };
NativeMode ParseNativeMode(const char* value)
{
    if (!value || !*value) return NativeMode::On;
    if (std::strcmp(value, "verify") == 0) return NativeMode::Verify;
    if (std::strcmp(value, "on") == 0 || std::strcmp(value, "1") == 0)
        return NativeMode::On;
    return NativeMode::Off;
}

template <unsigned Fields = 4>
NativeMode FourBoneMode()
{
    static std::atomic<NativeMode> mode{[] {
        const char* value = std::getenv(Fields == 4 ? "TTT2_SKIN_NATIVE" : "TTT2_SKIN_32_NATIVE");
        return ParseNativeMode(value);
    }()};
    static std::atomic<uint32_t> calls{0};
    // Recheck the device config about once per thousand jobs. This permits a
    // same-fight comparison without restarting DXVK or its shader cache.
    if ((calls.fetch_add(1, std::memory_order_relaxed) & 1023u) == 0)
    {
        if (const char* path = std::getenv(Fields == 4 ? "TTT2_SKIN_NATIVE_FILE" : "TTT2_SKIN_32_NATIVE_FILE"))
        {
            std::ifstream file(path);
            std::string value;
            if (file >> value)
            {
                const NativeMode next = ParseNativeMode(value.c_str());
                const NativeMode previous = mode.exchange(next, std::memory_order_relaxed);
                if (next != previous)
                    std::fprintf(stderr, "[skin-native] fields=%u mode=%s\n", Fields, value.c_str());
            }
        }
    }
    return mode.load(std::memory_order_relaxed);
}

int64_t NowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void RecordSkin(size_t kind, uint32_t vertices, int64_t elapsedNs, int64_t nowNs)
{
    counters[kind].calls.fetch_add(1, std::memory_order_relaxed);
    counters[kind].vertices.fetch_add(vertices, std::memory_order_relaxed);
    counters[kind].nanoseconds.fetch_add(uint64_t(elapsedNs), std::memory_order_relaxed);

    int64_t due = nextReportNs.load(std::memory_order_relaxed);
    if (!due) {
        nextReportNs.compare_exchange_strong(due, nowNs + 5'000'000'000LL,
                                             std::memory_order_relaxed);
        return;
    }
    if (nowNs < due || !nextReportNs.compare_exchange_strong(
            due, nowNs + 5'000'000'000LL, std::memory_order_relaxed)) return;

    std::fprintf(stderr, "[skin] 5s:");
    for (size_t i = 0; i < counters.size(); ++i) {
        const auto calls = counters[i].calls.exchange(0, std::memory_order_relaxed);
        const auto verts = counters[i].vertices.exchange(0, std::memory_order_relaxed);
        const auto nanos = counters[i].nanoseconds.exchange(0, std::memory_order_relaxed);
        std::fprintf(stderr, " %s=%llu jobs/%llu vertices/%.1f cpu-ms", names[i],
                     static_cast<unsigned long long>(calls),
                     static_cast<unsigned long long>(verts), double(nanos) / 1e6);
    }
    std::fputc('\n', stderr);
}
} // namespace

extern "C" PPC_FUNC(__imp__sub_82AE9C28);
PPC_FUNC(sub_82AE9C28)
{
    // Switch only at a job boundary, never halfway through a running job.
    if (SkipPackedFieldJob()) return;
    if (!ttt2_packed_field::TryRunInactivePrefix(ctx, base, __imp__sub_82AE9C28))
        __imp__sub_82AE9C28(ctx, base);
}

#define TTT2_PROFILE_SKIN(addr, kind, stride)                                    \
    extern "C" PPC_FUNC(__imp__sub_##addr);                               \
    PPC_FUNC(sub_##addr)                                                  \
    {                                                                     \
        const timeline::Scope skinMark{ timeline::SkinJob, 0x##addr & 0xFFFFu }; \
        DiagSkinJob(ctx, base, 0x##addr, stride);                           \
        if (!SkinProfileOn()) { __imp__sub_##addr(ctx, base); return; }   \
        const uint32_t vertices = GRead32(base, ctx.r3.u32 + 24);        \
        const int64_t begin = NowNs();                                    \
        __imp__sub_##addr(ctx, base);                                     \
        const int64_t end = NowNs();                                      \
        RecordSkin(kind, vertices, end - begin, end);                     \
    }

TTT2_PROFILE_SKIN(82B10800, 0, 64)
TTT2_PROFILE_SKIN(82B10908, 1, 16)
TTT2_PROFILE_SKIN(82B10C88, 3, 48)
TTT2_PROFILE_SKIN(82B11288, 5, 20)

template <unsigned Fields, size_t Kind, uint32_t Address>
void RunSkinJob(PPCContext& ctx, uint8_t* base, PPCFunc* original)
{
    const timeline::Scope skinMark{ timeline::SkinJob, Address & 0xFFFFu };
    DiagSkinJob(ctx, base, Address, Fields * 16);
    NativeMode mode = FourBoneMode<Fields>();
#if defined(TTT2_GEOMETRY_AB_PROBE)
    const auto flags = ttt2_hw::geometry_probe::ReferenceMask();
    if (flags & 16384u) mode = NativeMode::Verify;
    else if (flags & 8192u) mode = NativeMode::Off;
#endif
    const bool profile = SkinProfileOn();
    if (mode == NativeMode::Off && !profile)
    {
        original(ctx, base);
        return;
    }
    const uint32_t job = ctx.r3.u32;
    const uint32_t vertices = GRead32(base, job + 24);
    const uint32_t output = GRead32(base, job + 16);
    // The direct writer requires an aligned, non-wrapping writable range.
    // Keep guarded low-memory writes and wrapped guest addresses on the original path.
    const bool supported = vertices && vertices <= 100000 && output >= 0x10000u &&
        !(output & 15u) && uint64_t(output) + uint64_t(vertices) * Fields * 16u <= 0x100000000ull;
    const int64_t begin = profile ? NowNs() : 0;

    if (mode == NativeMode::On && supported)
        ttt2_skin::RunFourBone<Fields>(ctx, base, job, base + output);
    else if (mode == NativeMode::Verify && supported)
    {
        // Shadow-run on real game data. Only the original writes the game's
        // output and context, so a mismatch cannot corrupt this test run.
        PPCContext shadow = ctx;
        std::vector<uint8_t> candidate(size_t(vertices) * Fields * 16u);
        ttt2_skin::RunFourBone<Fields>(shadow, base, job, candidate.data());
        original(ctx, base);
        const uint8_t* expected = base + output;
        const size_t bytes = candidate.size();
        static std::atomic<uint32_t> checked{0}, mismatches{0};
        const uint32_t call = checked.fetch_add(1, std::memory_order_relaxed) + 1;
        if (std::memcmp(candidate.data(), expected, bytes) != 0)
        {
            const uint32_t mismatch = mismatches.fetch_add(1, std::memory_order_relaxed) + 1;
            if (mismatch <= 10)
            {
                size_t byte = 0;
                while (byte < bytes && candidate[byte] == expected[byte]) ++byte;
                std::fprintf(stderr, "[skin-native] mismatch fields=%u job=%08X vertices=%u byte=%zu got=%02X expected=%02X\n",
                             Fields, job, vertices, byte, candidate[byte], expected[byte]);
            }
        }
        if (call % 1000 == 0)
            std::fprintf(stderr, "[skin-native] fields=%u verified %u real jobs; mismatches=%u\n",
                         Fields, call, mismatches.load(std::memory_order_relaxed));
    }
    else
        original(ctx, base);

    if (profile) RecordSkin(Kind, vertices, NowNs() - begin, NowNs());
}

extern "C" PPC_FUNC(__imp__sub_82B10F40);
PPC_FUNC(sub_82B10F40) {
    RunSkinJob<4, 4, 0x82B10F40u>(ctx, base, __imp__sub_82B10F40);
}

extern "C" PPC_FUNC(__imp__sub_82B10A70);
PPC_FUNC(sub_82B10A70) {
    RunSkinJob<2, 2, 0x82B10A70u>(ctx, base, __imp__sub_82B10A70);
}
