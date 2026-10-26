#pragma once

// Android opt-in timing of the CPU vertex stage, including its worker join.
// Records on the command processor only; no worker synchronization is added.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace cpu_vertex_profile {
using Clock = std::chrono::steady_clock;
inline bool On()
{
    static const bool on = [] {
        const char* value = std::getenv("TTT2_CPU_VERTEX_PROFILE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return on;
}

inline void Record(const std::vector<uint32_t>& code, uint32_t primitive,
                   const char* reason, size_t vertices, Clock::time_point begin)
{
    const auto end = Clock::now();
    const double ms = std::chrono::duration<double, std::milli>(end - begin).count();
    uint64_t key = 0x5EEDull + code.size(); // same hash as the shader-bytecode cache
    const auto* bytes = reinterpret_cast<const uint8_t*>(code.data());
    for (size_t i = 0; i < code.size() * 4; ++i) { key ^= bytes[i]; key *= 1099511628211ull; }
    using Key = std::tuple<uint64_t, uint32_t, std::string>;
    struct Count { uint64_t calls = 0, vertices = 0; double ms = 0, maximum = 0; };
    static std::map<Key, Count> counts;
    static auto window = begin;
    auto& count = counts[{key, primitive, reason}];
    ++count.calls;
    count.vertices += vertices;
    count.ms += ms;
    count.maximum = std::max(count.maximum, ms);
    if (end - window < std::chrono::seconds(5)) return;
    std::vector<std::pair<Key, Count>> sorted(counts.begin(), counts.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    for (size_t i = 0; i < std::min<size_t>(sorted.size(), 8); ++i)
    {
        const auto& [id, c] = sorted[i];
        std::fprintf(stderr, "[cpu-vs] %.1fs shader=%016llX prim=%u reason=%s calls=%llu vertices=%llu wall=%.2f ms max=%.2f ms\n",
                     std::chrono::duration<double>(end - window).count(),
                     static_cast<unsigned long long>(std::get<0>(id)), std::get<1>(id), std::get<2>(id).c_str(),
                     static_cast<unsigned long long>(c.calls), static_cast<unsigned long long>(c.vertices), c.ms, c.maximum);
    }
    counts.clear();
    window = end;
}
} // namespace cpu_vertex_profile
