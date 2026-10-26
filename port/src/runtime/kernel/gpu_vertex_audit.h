#pragma once

// Diagnostic byte accounting only; never used to decide whether a draw can
// reuse GPU data. Exact snapshots add CPU/memory work and invalidate FPS tests.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ttt2_hw::vertex_audit {
struct Totals {
    uint64_t bytes = 0, unique = 0, sameFrame = 0, previousFrame = 0;
    uint64_t olderFrame = 0, changed = 0, first = 0, resets = 0, segments = 0;
};

class Census {
    struct Snapshot { std::vector<uint8_t> data; uint64_t frame = 0; };
    std::unordered_map<uint64_t, Snapshot> snapshots_;
    std::vector<std::pair<uint64_t, uint64_t>> ranges_;
    uint64_t frame_ = 1, stored_ = 0;
public:
    Totals totals;
    uint64_t StoredBytes() const { return stored_; }
    void Observe(uint32_t address, const uint8_t* data, size_t size)
    {
        if (!size) return;
        totals.bytes += size;
        ++totals.segments;
        ranges_.emplace_back(address, uint64_t(address) + size);
        // Address-aligned chunks let interior pages match despite different
        // draw bounds. Partial chunks require an exact address/length match:
        // reuse counts are conservative, not an estimate based on hashes.
        while (size) {
            const size_t n = std::min(size, size_t(4096 - (address & 4095)));
            const uint64_t key = (uint64_t(address) << 13) | n;
            auto it = snapshots_.find(key);
            if (it == snapshots_.end()) {
                if (stored_ + n > (64u << 20) || snapshots_.size() >= 65536) {
                    snapshots_.clear(); stored_ = 0; ++totals.resets;
                }
                it = snapshots_.emplace(key, Snapshot{}).first;
                stored_ += n;
                totals.first += n;
            } else if (std::memcmp(it->second.data.data(), data, n) != 0) {
                totals.changed += n;
            } else if (it->second.frame == frame_) {
                totals.sameFrame += n;
            } else if (it->second.frame + 1 == frame_) {
                totals.previousFrame += n;
            } else {
                totals.olderFrame += n;
            }
            auto& snapshot = it->second;
            snapshot.data.assign(data, data + n);
            snapshot.frame = frame_;
            address += uint32_t(n); data += n; size -= n;
        }
    }
    void EndFrame()
    {
        std::sort(ranges_.begin(), ranges_.end());
        uint64_t end = 0;
        for (auto range : ranges_) {
            if (range.second > end) totals.unique += range.second - std::max(end, range.first);
            end = std::max(end, range.second);
        }
        ranges_.clear(); ++frame_;
    }
};

#if defined(TTT2_GEOMETRY_AB_PROBE)
inline Census census;
inline bool enabled = false;
inline uint64_t frames = 0;
inline void Observe(uint32_t address, const uint8_t* data, size_t size)
{
    if (enabled) census.Observe(address, data, size);
}
inline void Present()
{
    if (enabled) {
        census.EndFrame();
        if (++frames % 120 == 0) {
            const auto& t = census.totals;
            std::fprintf(stderr, "[vertex-audit] frames=120 bytes=%llu unique=%llu same_frame=%llu previous_frame=%llu older_frame=%llu changed=%llu first=%llu segments=%llu snapshot_bytes=%llu resets=%llu\n",
                (unsigned long long)t.bytes, (unsigned long long)t.unique,
                (unsigned long long)t.sameFrame, (unsigned long long)t.previousFrame,
                (unsigned long long)t.olderFrame, (unsigned long long)t.changed,
                (unsigned long long)t.first, (unsigned long long)t.segments,
                (unsigned long long)census.StoredBytes(), (unsigned long long)t.resets);
            census.totals = {};
        }
    }
    static const char* path = std::getenv("TTT2_VERTEX_AUDIT_FILE");
    if (!path) return;
    using Clock = std::chrono::steady_clock;
    static auto last = Clock::time_point{};
    const auto now = Clock::now();
    if (now - last < std::chrono::seconds(1)) return;
    last = now;
    int next = enabled;
    if (FILE* file = std::fopen(path, "r")) {
        if (std::fscanf(file, "%d", &next) != 1) next = enabled;
        std::fclose(file);
    }
    if ((next != 0) != enabled) {
        enabled = next != 0; census = Census{}; frames = 0;
        std::fprintf(stderr, "[vertex-audit] enabled=%d (FPS results while enabled are not benchmarks)\n", int(enabled));
    }
}
#endif
} // namespace ttt2_hw::vertex_audit
