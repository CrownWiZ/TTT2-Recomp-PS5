#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <thread>

namespace host_deadline {

class Waiter {
public:
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;

    void Wait(Clock::time_point deadline, Duration maximumMargin) {
        Wait(deadline, maximumMargin, [] { return Clock::now(); },
            [](Clock::time_point wake) { std::this_thread::sleep_until(wake); },
            [] { std::this_thread::yield(); });
    }

    // The clock and waiting operations can be supplied for deterministic tests.
    template <class Now, class SleepUntil, class Yield>
    void Wait(Clock::time_point deadline, Duration maximumMargin,
              Now now, SleepUntil sleepUntil, Yield yield) {
        const auto guard = std::chrono::duration_cast<Duration>(std::chrono::microseconds(500));
        const auto floor = std::chrono::duration_cast<Duration>(std::chrono::microseconds(750));
        // Retain recent worst-case sleep overshoot plus a guard, bounded by the
        // caller's original margin. A new waiter starts with that full margin.
        const auto margin = sampled_ ? (std::min)(maximumMargin,
            (std::max)(floor, *std::max_element(overshoot_.begin(), overshoot_.end()) + guard)) :
            maximumMargin;
        const auto wake = deadline - margin;
        if (now() < wake) {
            sleepUntil(wake);
            overshoot_[cursor_] = std::clamp(now() - wake, Duration::zero(), maximumMargin);
            cursor_ = (cursor_ + 1) % overshoot_.size();
            sampled_ = true;
        }
        // Keep the caller's deadline, including when it has already passed.
        while (now() < deadline) yield();
    }

private:
    std::array<Duration, 128> overshoot_{};
    std::size_t cursor_ = 0;
    bool sampled_ = false;
};

} // namespace host_deadline
