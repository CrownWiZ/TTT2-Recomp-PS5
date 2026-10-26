#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace frame_benchmark {
// Opt-in, CP-thread-only measurement. Write "60 run-name" to capture.request in
// TTT2_BENCHMARK_DIR. A completed run produces run-name.json atomically. No file
// I/O occurs inside the timed window; samples are retained in memory.
inline void Present()
{
    static const char* directory = std::getenv("TTT2_BENCHMARK_DIR");
    if (!directory || !*directory) return;
    using Clock = std::chrono::steady_clock;
    static auto lastPoll = Clock::time_point{}, started = Clock::time_point{}, previous = Clock::time_point{};
    static std::vector<double> intervals;
    static double seconds = 0;
    static std::string name;
    const auto now = Clock::now();
    if (!seconds)
    {
        if (now - lastPoll < std::chrono::seconds(1)) return;
        lastPoll = now;
        const auto request = std::filesystem::path(directory) / "capture.request";
        FILE* input = std::fopen(request.string().c_str(), "r");
        if (!input) return;
        unsigned duration = 0;
        char label[96]{};
        const int read = std::fscanf(input, "%u %95s", &duration, label);
        std::fclose(input);
        std::error_code error;
        std::filesystem::remove(request, error);
        const std::string candidate = label;
        if (read != 2 || duration < 1 || duration > 600 || candidate.empty() ||
            candidate.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos)
        {
            std::fprintf(stderr, "[benchmark] invalid request\n");
            return;
        }
        name = candidate;
        intervals.clear();
        intervals.reserve(size_t(duration) * 1100);
        seconds = duration;
        std::fprintf(stderr, "[benchmark] starting %s for %u seconds\n", name.c_str(), duration);
        started = previous = Clock::now();
        return;
    }
    intervals.push_back(std::chrono::duration<double, std::milli>(now - previous).count());
    previous = now;
    const double elapsed = std::chrono::duration<double>(now - started).count();
    if (elapsed < seconds) return;
    seconds = 0;
    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    auto percentile = [&](double p) { return sorted[size_t(std::ceil(p * sorted.size())) - 1]; };
    const auto result = std::filesystem::path(directory) / (name + ".json");
    const auto temporary = result.string() + ".tmp";
    FILE* output = std::fopen(temporary.c_str(), "w");
    if (!output) { std::fprintf(stderr, "[benchmark] cannot write result\n"); return; }
    std::fprintf(output,
        "{\"frames\":%zu,\"seconds\":%.9f,\"fps\":%.6f,\"p50_ms\":%.6f,\"p95_ms\":%.6f,\"p99_ms\":%.6f,\"max_ms\":%.6f,\"intervals_ms\":[",
        intervals.size(), elapsed, intervals.size() / elapsed,
        percentile(.5), percentile(.95), percentile(.99), sorted.back());
    for (size_t i = 0; i < intervals.size(); ++i)
        std::fprintf(output, "%s%.6f", i ? "," : "", intervals[i]);
    std::fprintf(output, "]}\n");
    bool ok = !std::ferror(output);
    ok = std::fclose(output) == 0 && ok;
    std::error_code error;
    if (ok) std::filesystem::rename(temporary, result, error);
    std::fprintf(stderr, "[benchmark] %s %s: %.3f FPS, p95 %.3f ms, p99 %.3f ms\n",
                 name.c_str(), ok && !error ? "saved" : "write failed",
                 intervals.size() / elapsed, percentile(.95), percentile(.99));
}
} // namespace frame_benchmark
