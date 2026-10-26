#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace worker_barrier {
inline bool Ready(uint8_t* base, uint32_t pool) {
    // Match the generated PPC's volatile, big-endian loads and signed cmpw.
    const auto active = __builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(base + uint32_t(pool + 8)));
    const auto barriers = __builtin_bswap32(*reinterpret_cast<volatile uint32_t*>(base + uint32_t(pool + 212)));
    return int32_t(active) <= int32_t(barriers);
}

inline uint64_t Wait(uint8_t* base, uint32_t pool, int delayUs) {
    // Avoid a scheduler round trip for conditions that resolve immediately.
    for (unsigned spin = 0; spin < 64; ++spin) if (Ready(base, pool)) return 0;
    uint64_t waits = 0;
    while (!Ready(base, pool)) {
        if (delayUs < 0) std::this_thread::yield();
        else std::this_thread::sleep_for(std::chrono::microseconds(delayUs));
        ++waits;
    }
    return waits;
}
} // namespace worker_barrier
