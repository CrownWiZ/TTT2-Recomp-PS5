#pragma once

#include <chrono>
#include <cstdint>

namespace host_clock {
// Xbox/Windows wall time: 100 ns ticks since 1601-01-01 UTC.
inline uint64_t SystemTime100ns() {
    constexpr uint64_t unixEpochOffset = 116444736000000000ull;
    const auto sinceUnix = std::chrono::system_clock::now().time_since_epoch();
    return unixEpochOffset + static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(sinceUnix).count() / 100);
}
} // namespace host_clock
