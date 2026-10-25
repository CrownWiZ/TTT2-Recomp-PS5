#pragma once

#include <cstdint>
#include <cstring>

namespace ttt2_hw {

// Only plain ALU constants: scratch, gamma, fetch and context registers retain
// the general register-write path. Source and aperture words are guest endian.
// Keep reads/writes interleaved so even overlapping input preserves word order.
inline bool UploadAluConstants(uint32_t* registers, uint8_t* ever,
                               uint32_t& sequence, uint32_t& writes,
                               uint32_t first, const uint32_t* source,
                               uint32_t* aperture, uint32_t count)
{
    if (!count || first < 0x4000 || first >= 0x4800 || count > 0x4800 - first)
        return false;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint32_t raw = source[i];
        registers[first + i] = __builtin_bswap32(raw);
        aperture[first + i] = raw;
    }
    const uint32_t firstRow = (first - 0x4000) / 4;
    const uint32_t lastRow = (first + count - 1 - 0x4000) / 4;
    std::memset(ever + firstRow, 1, lastRow - firstRow + 1);
    sequence += count;
    writes += count;
    return true;
}

} // namespace ttt2_hw
