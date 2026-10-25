#pragma once

#include "gpu_render.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <thread>
#include <vector>

namespace ttt2_hw {

struct ResolveWritebackInfo
{
    uint32_t destBase = 0, pitch = 0, height = 0;
    bool depth = false, depthFloat = false;
    bool redGreenFloat = false, swapRB = false;
};

inline uint32_t ResolveDepthWord(uint32_t floatBits, bool depthFloat)
{
    float depth;
    std::memcpy(&depth, &floatBits, sizeof(depth));
    if (!depthFloat)
        return uint32_t(std::clamp(depth, 0.0f, 1.0f) * 16777215.0f +
                        0.5f) << 8;
    if (int32_t(floatBits) <= 0) return 0;
    if (floatBits >= 0x3ffffff8u) return 0xffffffu << 8;
    if (floatBits < 0x38800000u)
    {
        const uint32_t shift = std::min<uint32_t>(113u -
                                                  (floatBits >> 23), 24u);
        floatBits = (0x800000u | (floatBits & 0x7fffffu)) >> shift;
    }
    else floatBits += 0xc8000000u;
    floatBits += 3u + ((floatBits >> 3) & 1u);
    return ((floatBits >> 3) & 0xffffffu) << 8;
}

inline uint32_t ResolvedGuestWord(const ResolveWritebackInfo& info, uint32_t word)
{
    if (info.depth) return ResolveDepthWord(word, info.depthFloat);
    if (!info.redGreenFloat && !info.swapRB)
        return (word & 0xff00ff00u) | ((word >> 16) & 0xffu) |
               ((word & 0xffu) << 16);
    return word;
}

// The writer receives a guest address and a host-order guest word. It must
// store through the guest-memory abstraction so fault/write-watch behavior is
// preserved. Each tiled texel has a distinct address, permitting row bands
// to run independently for large resolves.
template <class WriteWord>
bool ScatterResolvedWords(const ResolveWritebackInfo& info,
                          std::span<const uint32_t> linear,
                          WriteWord&& writeWord)
{
    if (!info.pitch || !info.height ||
        uint64_t(info.pitch) * info.height > linear.size() ||
        info.pitch > UINT32_MAX - 31)
        return false;
    const uint32_t alignedPitch = (info.pitch + 31) & ~31u;
    const uint64_t alignedHeight = (uint64_t(info.height) + 31) & ~31ull;
    if (uint64_t(info.destBase) + alignedPitch * alignedHeight * 4 >
        (1ull << 32))
        return false;
    auto rows = [&](uint32_t first, uint32_t last) {
        for (uint32_t y = first; y < last; ++y)
            for (uint32_t x = 0; x < info.pitch; ++x)
            {
                const uint32_t word = ResolvedGuestWord(info, linear[size_t(y) * info.pitch + x]);
                const uint32_t address = info.destBase +
                    GpuTiledOffset2D(x, y, alignedPitch, 2) * 4;
                writeWord(address, word);
            }
    };
    if (uint64_t(info.pitch) * info.height >= 256ull * 1024 &&
        info.height >= 64)
    {
        const uint32_t band = ((info.height + 3) / 4 + 31) & ~31u;
        std::thread helpers[3];
        for (uint32_t i = 0; i < 3; ++i)
            helpers[i] = std::thread(rows,
                std::min(info.height, band * (i + 1)),
                std::min(info.height, band * (i + 2)));
        rows(0, std::min(info.height, band));
        for (auto& helper : helpers) helper.join();
    }
    else rows(0, info.height);
    return true;
}

// A cached physical-view fault asks for one 64 KiB chunk. Its tiled addresses
// can cover only a small part of a large resolve, so convert just that chunk.
// The inverse lookup is built once per resolved surface and reused each frame.
inline bool BuildResolvedTileLookup(const ResolveWritebackInfo& info,
                                    std::vector<uint32_t>& tiledToLinear)
{
    if (!info.pitch || !info.height || info.pitch > UINT32_MAX - 31)
        return false;
    const uint32_t alignedPitch = (info.pitch + 31) & ~31u;
    const uint64_t alignedHeight = (uint64_t(info.height) + 31) & ~31ull;
    const uint64_t count = uint64_t(alignedPitch) * alignedHeight;
    if (!count || count > UINT32_MAX ||
        uint64_t(info.destBase) + count * 4 > (1ull << 32))
        return false;
    tiledToLinear.assign(size_t(count), UINT32_MAX);
    for (uint32_t y = 0; y < info.height; ++y)
        for (uint32_t x = 0; x < info.pitch; ++x)
        {
            const uint32_t tiled = GpuTiledOffset2D(x, y, alignedPitch, 2);
            if (tiled >= count || tiledToLinear[tiled] != UINT32_MAX)
                return false;
            tiledToLinear[tiled] = y * info.pitch + x;
        }
    return true;
}

template <class WriteWord>
bool ScatterResolvedWordsRange(const ResolveWritebackInfo& info,
                               std::span<const uint32_t> linear,
                               std::span<const uint32_t> tiledToLinear,
                               uint32_t start, uint32_t size, WriteWord&& writeWord)
{
    if (uint64_t(info.pitch) * info.height > linear.size() ||
        tiledToLinear.empty())
        return false;
    const uint64_t begin = std::max<uint64_t>(info.destBase, start);
    const uint64_t end = std::min<uint64_t>(uint64_t(info.destBase) +
                                           tiledToLinear.size() * 4ull,
                                           uint64_t(start) + size);
    for (uint64_t address = (begin + 3) & ~3ull; address < end; address += 4)
    {
        const uint32_t linearIndex = tiledToLinear[(address - info.destBase) / 4];
        if (linearIndex != UINT32_MAX)
            writeWord(uint32_t(address), ResolvedGuestWord(info, linear[linearIndex]));
    }
    return true;
}

} // namespace ttt2_hw
