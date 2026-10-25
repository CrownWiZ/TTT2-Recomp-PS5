#pragma once

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace ttt2_hw::geometry_probe {
// Benchmark flags: 1 old indices, 2 old packing, 4 expand strips, 8 old decoder,
// 32 old bounds, 64 rehash shaders, 128 no CPU index cache. Bit 16 forces full shader constant banks.
// 256: original per-draw constant-buffer DISCARD uploads.
// 512: rearm write-watch mappings even when no page was written.
// 1024: original repeated vertex-shader and render-target bindings.
// 2048: count every page instead of bulk-searching clean ranges.
// 4096: byte-at-a-time in-memory texture-key hash.
// 8192: original generated four-bone skinning.
// 16384: verify native skinning against original output (diagnostics only).
// 32768: original baseline-target vertex byte-swap loop.
// 65536: original per-word ALU writes when TTT2_ALU_BATCH_EXPERIMENT is compiled.
// 131072: disable open-chunk vertex-upload reuse experiment.
// 262144: original per-page bounds checks in the texture sequence reduction.
inline std::atomic<unsigned> referenceMask{0};
inline unsigned ReferenceMask() { return referenceMask.load(std::memory_order_relaxed); }

inline void Poll()
{
    static const char* path = std::getenv("TTT2_GEOMETRY_MODE_FILE");
    if (!path) return;
    using Clock = std::chrono::steady_clock;
    static auto last = Clock::time_point{};
    const auto now = Clock::now();
    if (now - last < std::chrono::seconds(1)) return;
    last = now;
    unsigned mask = ReferenceMask();
    if (FILE* file = std::fopen(path, "r"))
    {
        if (std::fscanf(file, "%u", &mask) != 1) mask = ReferenceMask();
        std::fclose(file);
    }
    if (mask > 524287 || mask == ReferenceMask()) return;
    referenceMask.store(mask, std::memory_order_relaxed);
    std::fprintf(stderr, "[geometry-probe] mode=%u indices=%s packing=%s strips=%s decoder=%s bounds=%s shader_keys=%s index_cache=%s\n", mask,
                 mask & 1 ? "original" : "optimized", mask & 2 ? "original" : "optimized", mask & 5 ? "expanded" : "native", mask & 8 ? "original" : "fused", mask & 32 ? "pairwise" : "reduction", mask & 64 ? "rehash" : "memoized", mask & 173 ? "off" : "on");
}
} // namespace ttt2_hw::geometry_probe
