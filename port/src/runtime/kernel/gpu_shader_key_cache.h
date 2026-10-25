#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ttt2_hw {

// Avoid recomputing the serial, byte-at-a-time disk-cache hash for every draw.
// The cheap bucket selector is only a hint: every hit compares all microcode
// words, and misses still use the original hash, preserving disk-cache keys.
// Each instance belongs to one fixed hash function (vertex or pixel stage).
class ShaderKeyCache
{
    struct Entry
    {
        std::vector<uint32_t> code;
        uint64_t key = 0;
        bool valid = false;
    };
    std::array<Entry, 256> entries_;
public:
    template<class Hash>
    uint64_t Get(const std::vector<uint32_t>& code, Hash hash)
    {
        uint32_t hint = uint32_t(code.size());
        if (!code.empty())
            for (unsigned i = 0; i < 4; ++i)
                hint = (hint ^ code[(code.size() - 1) * i / 3]) * 0x9E3779B1u;
        hint ^= hint >> 16;
        auto& entry = entries_[hint & 255];
        if (entry.valid && entry.code == code) return entry.key;
        entry.key = hash(code);
        entry.code = code;
        entry.valid = true;
        return entry.key;
    }
};
} // namespace ttt2_hw
