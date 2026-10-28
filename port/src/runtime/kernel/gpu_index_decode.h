#include "cxx20_compat.h"
#pragma once

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace ttt2_hw {
struct IndexDecodeStorage
{
    std::vector<uint32_t> indices;
    std::vector<uint8_t> cuts;
};
struct DecodedIndices
{
    ttt2_c20::span<const uint32_t> indices;
    ttt2_c20::span<const uint8_t> cuts;
    uint32_t highest = 0;
    bool anyCut = false;
};

// Hoist the format/restart/clamp decisions out of the inner loop. The maximum
// is accumulated while decoding; the caller no longer needs a second scan.
template<unsigned Width, bool Restart, bool Clamp>
inline DecodedIndices DecodeIndexLoop(const uint8_t* guest, uint32_t address, uint32_t count,
    uint32_t restartIndex, uint32_t offset, uint32_t minimum, uint32_t maximum,
    IndexDecodeStorage& storage)
{
    auto* indices = storage.indices.data();
    auto* cuts = storage.cuts.data();
    uint32_t highest = 0;
    bool anyCut = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t raw;
        if constexpr (Width == 0) raw = i;
        else if constexpr (Width == 2)
        {
            uint16_t value;
            std::memcpy(&value, guest + uint32_t(address + i * 2), 2);
            if constexpr (ttt2_c20::endian::native == ttt2_c20::endian::little)
                value = uint16_t((value >> 8) | (value << 8));
            raw = value;
        }
        else
        {
            std::memcpy(&raw, guest + uint32_t(address + i * 4), 4);
            if constexpr (ttt2_c20::endian::native == ttt2_c20::endian::little)
                raw = (raw >> 24) | ((raw >> 8) & 0xFF00) | ((raw << 8) & 0xFF0000) | (raw << 24);
        }
        const bool cut = Restart && raw == restartIndex;
        uint32_t value = (raw + offset) & 0xFFFFFF;
        if constexpr (Clamp) value = (std::min)((std::max)(value, minimum), maximum);
        if (cut) value = 0;
        indices[i] = value;
        cuts[i] = uint8_t(cut);
        highest = (std::max)(highest, value);
        anyCut |= cut;
    }
    return {{indices, count}, {cuts, count}, highest, anyCut};
}

template<unsigned Width>
inline DecodedIndices DecodeIndexFormat(const uint8_t* guest, uint32_t address, uint32_t count,
    bool restart, uint32_t restartIndex, uint32_t offset, uint32_t minimum, uint32_t maximum,
    IndexDecodeStorage& storage)
{
    const bool clamp = maximum != 0 && maximum >= minimum;
    if (restart)
    {
        if (clamp) return DecodeIndexLoop<Width, true, true>(guest, address, count, restartIndex, offset, minimum, maximum, storage);
        return DecodeIndexLoop<Width, true, false>(guest, address, count, restartIndex, offset, minimum, maximum, storage);
    }
    if (clamp) return DecodeIndexLoop<Width, false, true>(guest, address, count, restartIndex, offset, minimum, maximum, storage);
    return DecodeIndexLoop<Width, false, false>(guest, address, count, restartIndex, offset, minimum, maximum, storage);
}

inline DecodedIndices DecodeIndices(const uint8_t* guest, uint32_t address, uint32_t count,
    bool automatic, bool index32, bool restart, uint32_t restartIndex, uint32_t offset,
    uint32_t minimum, uint32_t maximum, IndexDecodeStorage& storage)
{
    if (storage.indices.size() < count) storage.indices.resize(count);
    if (storage.cuts.size() < count) storage.cuts.resize(count);
    if (automatic) return DecodeIndexFormat<0>(guest, address, count, restart, restartIndex, offset, minimum, maximum, storage);
    if (index32) return DecodeIndexFormat<4>(guest, address, count, restart, restartIndex, offset, minimum, maximum, storage);
    return DecodeIndexFormat<2>(guest, address, count, restart, restartIndex, offset, minimum, maximum, storage);
}
} // namespace ttt2_hw
