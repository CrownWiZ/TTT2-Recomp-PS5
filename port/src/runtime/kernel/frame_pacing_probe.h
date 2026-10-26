#pragma once

// Opt-in pacing diagnostics. Vblank and CP own separate counters; only marker
// timestamps and the last dispatch timestamp cross threads, through atomics.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace frame_pacing_probe {
using Clock = std::chrono::steady_clock;
inline bool On() {
    static const bool on = [] { const char* s = std::getenv("TTT2_PACING_PROBE"); return s && *s == '1'; }();
    return on;
}
inline uint64_t Us(Clock::time_point t = Clock::now()) {
    return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count());
}
inline double Ms(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
struct Distribution {
    uint64_t n = 0, overBudget = 0;
    double sum = 0, maximum = 0;
    std::array<uint64_t, 129> bins{}; // 0.25 ms bins; last bin overflows 32 ms.
    void Add(double value) {
        value = (std::max)(0.0, value);
        ++n; sum += value; maximum = (std::max)(maximum, value);
        overBudget += value > 1000.0 / 60;
        ++bins[(std::min)(size_t(value / .25), bins.size() - 1)];
    }
    double Mean() const { return n ? sum / n : 0; }
    double P95() const {
        uint64_t total = 0, goal = (n * 95 + 99) / 100;
        if (!n) return 0;
        for (size_t i = 0; i < bins.size(); ++i) {
            total += bins[i];
            if (total >= goal) return i == 128 ? maximum : (i + 1) * .25;
        }
        return maximum;
    }
};
// Bounded, CP-owned trace. No file writes while collecting; request N frames
// through TTT2_PACING_TRACE_FILE, then read <request>.csv after capture completes.
// Timestamps are monotonic microseconds; tick is the most recent dispatch.
struct TraceEvent { const char* kind; uint64_t begin, end, tick, value; };
inline std::array<TraceEvent, 16384> traceEvents;
inline size_t traceSize = 0;
inline unsigned traceFrames = 0, traceDropped = 0;
inline void Trace(const char* kind, uint64_t begin, uint64_t end, uint64_t tick, uint64_t value) {
    if (!traceFrames) return;
    if (traceSize == traceEvents.size()) { ++traceDropped; return; }
    traceEvents[traceSize++] = {kind, begin, end, tick, value};
}
inline void TraceFrame(uint32_t id, uint64_t tick) {
    if (!On()) return;
    static const char* path = std::getenv("TTT2_PACING_TRACE_FILE");
    if (!path || !*path) return;
    const auto now = Clock::now();
    static auto poll = now;
    if (traceFrames) {
        Trace("marker", Us(now), Us(now), tick, id);
        if (--traceFrames == 0) {
            const std::string output = std::string(path) + ".csv";
            if (FILE* f = std::fopen(output.c_str(), "w")) {
                std::fprintf(f, "kind,begin_us,end_us,last_tick_us,value\n");
                for (size_t i = 0; i < traceSize; ++i) {
                    const auto& e = traceEvents[i];
                    std::fprintf(f, "%s,%llu,%llu,%llu,%llu\n", e.kind,
                        (unsigned long long)e.begin, (unsigned long long)e.end,
                        (unsigned long long)e.tick, (unsigned long long)e.value);
                }
                std::fclose(f);
                std::fprintf(stderr, "[pace-trace] saved events=%zu dropped=%u\n", traceSize, traceDropped);
            } else std::fprintf(stderr, "[pace-trace] cannot write trace output\n");
        }
    } else if (now - poll >= std::chrono::seconds(1)) {
        poll = now;
        if (FILE* f = std::fopen(path, "r")) {
            unsigned frames = 0;
            const bool valid = std::fscanf(f, "%u", &frames) == 1 && frames;
            std::fclose(f); std::remove(path);
            if (valid) {
                traceFrames = (std::min)(frames, 600u); traceSize = 0; traceDropped = 0;
                std::fprintf(stderr, "[pace-trace] capturing %u frames\n", traceFrames);
            }
        }
    }
}
struct Marker { std::atomic<uint64_t> time{0}; std::atomic<uint32_t> id{0}; };
inline std::array<Marker, 64> markers;
inline std::atomic<uint64_t> lastTickUs{0}, readyCount{0};
inline void Published(uint32_t id) {
    if (!On()) return;
    auto& m = markers[id % markers.size()];
    m.id.store(0, std::memory_order_release);
    m.time.store(Us(), std::memory_order_relaxed);
    m.id.store(id, std::memory_order_release);
}
struct CpCounters {
    Clock::time_point since{}, lastReady{}, lastReadback{};
    Distribution readyGap, publishAge, tickPhase, present, mapAsync, mapBlocking;
    Distribution mapNotReady, mapSuccess, readbackAge, readbackGap;
    double waitMs[3]{};
    uint64_t frames = 0, ready = 0, notReady = 0;
};
inline CpCounters cp; // CP thread only.
inline thread_local bool isCpThread = false;
inline uint32_t lastReadyId = 0;
inline void Ready(uint32_t id) {
    if (!On() || id == lastReadyId) return;
    isCpThread = true;
    lastReadyId = id;
    const auto now = Clock::now();
    const uint64_t us = Us(now);
    if (cp.lastReady != Clock::time_point{}) cp.readyGap.Add(Ms(now - cp.lastReady));
    cp.lastReady = now; ++cp.ready;
    const auto& m = markers[id % markers.size()];
    uint64_t publishedUs = 0;
    if (m.id.load(std::memory_order_acquire) == id) {
        const auto published = m.time.load(std::memory_order_relaxed);
        if (m.id.load(std::memory_order_acquire) == id && published <= us) {
            publishedUs = published;
            cp.publishAge.Add(double(us - published) / 1000);
        }
    }
    const uint64_t tick = lastTickUs.load(std::memory_order_relaxed);
    if (tick && tick <= us) cp.tickPhase.Add(double(us - tick) / 1000);
    Trace("ready", us, us, tick, id);
    if (publishedUs) Trace("published", publishedUs, publishedUs, tick, id);
    readyCount.fetch_add(1, std::memory_order_relaxed);
}
inline void Wait(unsigned kind, double ms, uint32_t address = 0) {
    if (!On() || kind >= 3) return;
    isCpThread = true;
    cp.waitMs[kind] += ms;
    const auto end = Us();
    Trace(kind == 0 ? "wait_scratch" : kind == 1 ? "wait_memory" : "wait_register",
          end - uint64_t(ms * 1000), end, lastTickUs.load(), address);
}
// Cached-view readbacks can call Map on guest CPU threads too. Exclude those
// from CP-owned counters and trace storage; no cross-thread writes are allowed.
inline void Map(double ms, bool noWait, bool notReady) {
    if (!On() || !isCpThread) return;
    (noWait ? cp.mapAsync : cp.mapBlocking).Add(ms);
    cp.notReady += notReady;
    if (noWait) (notReady ? cp.mapNotReady : cp.mapSuccess).Add(ms);
    const auto end = Us();
    Trace(noWait ? "map_async" : "map_block", end - uint64_t(ms * 1000), end, lastTickUs.load(), notReady);
}
inline void SmallWriteback(double ageMs) {
    if (!On() || !isCpThread) return;
    const auto now = Clock::now();
    cp.readbackAge.Add(ageMs);
    if (cp.lastReadback != Clock::time_point{}) cp.readbackGap.Add(Ms(now - cp.lastReadback));
    cp.lastReadback = now;
}
inline void Present(double ms) {
    if (!On()) return;
    isCpThread = true;
    const auto now = Clock::now();
    if (cp.since == Clock::time_point{}) cp.since = now;
    ++cp.frames; cp.present.Add(ms);
    Trace("present", Us(now) - uint64_t(ms * 1000), Us(now), lastTickUs.load(), lastReadyId);
    const double seconds = Ms(now - cp.since) / 1000;
    if (seconds < 5) return;
    const double f = double(cp.frames);
    std::fprintf(stderr,
        "[pace-cp] seconds=%.3f frames=%llu ready=%llu ready_gap_mean=%.3f ready_gap_p95=%.3f ready_gap_max=%.3f ready_gap_over16=%llu "
        "publish_ready_mean=%.3f publish_ready_p95=%.3f tick_phase_mean=%.3f "
        "present_mean=%.3f present_max=%.3f map_async_calls=%llu map_async_ms_frame=%.3f map_async_max=%.3f map_not_ready=%llu "
        "map_block_calls=%llu map_block_ms_frame=%.3f wait_scratch_ms_frame=%.3f wait_memory_ms_frame=%.3f wait_register_ms_frame=%.3f\n",
        seconds, (unsigned long long)cp.frames, (unsigned long long)cp.ready,
        cp.readyGap.Mean(), cp.readyGap.P95(), cp.readyGap.maximum, (unsigned long long)cp.readyGap.overBudget,
        cp.publishAge.Mean(), cp.publishAge.P95(), cp.tickPhase.Mean(),
        cp.present.Mean(), cp.present.maximum, (unsigned long long)cp.mapAsync.n,
        cp.mapAsync.sum / f, cp.mapAsync.maximum, (unsigned long long)cp.notReady,
        (unsigned long long)cp.mapBlocking.n, cp.mapBlocking.sum / f,
        cp.waitMs[0] / f, cp.waitMs[1] / f, cp.waitMs[2] / f);
    std::fprintf(stderr,
        "[pace-readback] seconds=%.3f frames=%llu not_ready_ms_frame=%.3f success_ms_frame=%.3f "
        "completed=%llu age_mean=%.3f age_max=%.3f gap_max=%.3f since_last_ms=%.3f\n",
        seconds, (unsigned long long)cp.frames, cp.mapNotReady.sum / f, cp.mapSuccess.sum / f,
        (unsigned long long)cp.readbackAge.n, cp.readbackAge.Mean(), cp.readbackAge.maximum,
        cp.readbackGap.maximum, cp.lastReadback == Clock::time_point{} ? -1.0 : Ms(now - cp.lastReadback));
    const auto ready = cp.lastReady;
    const auto readback = cp.lastReadback;
    cp = {}; cp.since = now; cp.lastReady = ready; cp.lastReadback = readback;
}
inline void Dispatch(Clock::time_point t) {
    if (On()) lastTickUs.store(Us(t), std::memory_order_relaxed);
}
// The vblank thread owns these counters. Log outside the measured callback.
inline void Tick(Clock::time_point deadline, Clock::time_point woke,
                 Clock::time_point dispatch, Clock::time_point done,
                 int hz, int stalls, bool reanchored) {
    if (!On()) return;
    struct Counters {
        Clock::time_point since{}, previous{};
        Distribution wakeLate, dispatchLate, gap, callback;
        uint64_t ticks=0, stalls=0, reanchors=0, empty=0, multiple=0, previousReady=0;
        int hz=0;
    };
    static Counters c;
    if (c.hz != hz) { c = {}; c.hz = hz; c.since = dispatch; c.previousReady = readyCount.load(); }
    ++c.ticks; c.stalls += stalls; c.reanchors += reanchored;
    c.wakeLate.Add(Ms(woke - deadline)); c.dispatchLate.Add(Ms(dispatch - deadline));
    c.callback.Add(Ms(done - dispatch));
    if (c.previous != Clock::time_point{}) c.gap.Add(Ms(dispatch - c.previous));
    c.previous = dispatch;
    const auto ready = readyCount.load(std::memory_order_relaxed);
    const auto count = ready - c.previousReady;
    c.empty += !count; c.multiple += count > 1; c.previousReady = ready;
    const double seconds = Ms(dispatch - c.since) / 1000;
    if (seconds < 5) return;
    std::fprintf(stderr,
        "[pace-vblank] hz=%d seconds=%.3f ticks=%llu dispatch_hz=%.3f "
        "wake_late_mean=%.3f wake_late_p95=%.3f wake_late_max=%.3f "
        "dispatch_late_mean=%.3f dispatch_late_max=%.3f gap_mean=%.3f gap_p95=%.3f gap_max=%.3f "
        "callback_mean=%.3f callback_p95=%.3f callback_max=%.3f stalls=%llu reanchors=%llu ticks_no_ready=%llu ticks_multi_ready=%llu\n",
        hz, seconds, (unsigned long long)c.ticks, c.gap.n / seconds,
        c.wakeLate.Mean(), c.wakeLate.P95(), c.wakeLate.maximum,
        c.dispatchLate.Mean(), c.dispatchLate.maximum, c.gap.Mean(), c.gap.P95(), c.gap.maximum,
        c.callback.Mean(), c.callback.P95(), c.callback.maximum,
        (unsigned long long)c.stalls, (unsigned long long)c.reanchors,
        (unsigned long long)c.empty, (unsigned long long)c.multiple);
    const auto prev = c.previous; const auto prevReady = c.previousReady;
    c = {}; c.hz = hz; c.since = dispatch; c.previous = prev; c.previousReady = prevReady;
}
} // namespace frame_pacing_probe
