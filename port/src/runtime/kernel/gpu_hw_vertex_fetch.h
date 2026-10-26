#pragma once

#include "gpu_hw_api.h"
#include "gpu_vertex_upload_cache.h"
#if defined(TTT2_GEOMETRY_AB_PROBE)
#include "gpu_geometry_probe.h"
#include "gpu_vertex_audit.h"
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#if defined(TTT2_VERTEX_SLOT_SCAN_EXPERIMENT) && TTT2_VERTEX_SLOT_SCAN_EXPERIMENT
#include <cstring>
#endif

namespace ttt2_hw {

// Guest vertex fetches use big-endian dwords. Both GPU backends need the same
// packed host-order ranges and vfRegion offsets for the translated VS shader.
struct VsFetchPlan
{
    int64_t lo[96]{}, hi[96]{};
    bool used[96]{};
    uint64_t totalDwords = 0;

    // Fill lo/hi/used with VsFetchRanges or another guest shader decoder.
    bool Validate(uint64_t maxRegionDwords)
    {
        totalDwords = 0;
        for (uint32_t slot = 0; slot < 96; ++slot)
        {
            if (!used[slot]) continue;
            if (hi[slot] < lo[slot] ||
                uint64_t(hi[slot] - lo[slot]) > maxRegionDwords)
                return false;
            totalDwords += uint64_t(hi[slot] - lo[slot]);
        }
        return true;
    }
};

struct VsGuestRange
{
    uint32_t start = 0, size = 0;
};

inline VsGuestRange VertexFetchGuestRange(const VsFetchPlan& plan,
                                          const uint32_t* regs, uint32_t slot)
{
    if (slot >= 96 || !plan.used[slot]) return {};
    const uint32_t fc0 = regs[0x4800 + slot * 2];
    if ((fc0 & 3) != 3) return {};
    const uint32_t phys = fc0 & ~3u;
    const uint64_t guest = (phys && phys < 0x20000000u)
        ? (phys | 0xA0000000u) : phys;
    const int64_t first = int64_t(guest) + plan.lo[slot] * 4;
    const int64_t count = plan.hi[slot] - plan.lo[slot];
    if (first < 0x10000 || count <= 0 ||
        uint64_t(first) + uint64_t(count) * 4 > 0x100000000ull)
        return {};
    return {uint32_t(first), uint32_t(count * 4)};
}

// Keep one endian-conversion implementation. The x86 game already requires
// x86-64-v2 for its PPC archive; allow this hot copy to use SSSE3 byte shuffles
// even when the surrounding renderer is compiled for baseline x86-64. ARM64
// compilers lower the same loop to their native byte-reversal instructions.
#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(_M_X64))
__attribute__((target("ssse3")))
#endif
inline void CopyVertexWords(uint32_t* output, const uint32_t* source, size_t count)
{
    for (size_t i = 0; i < count; ++i) output[i] = __builtin_bswap32(source[i]);
}

// Output starts at output[0], but baseDword is its absolute offset within the
// backend's vertex-data buffer. Invalid guest ranges read as zero in the shader.
// The caller reserves plan.totalDwords; invalid or overlapping ranges can use
// less output. readStarts must preserve the original dword alignment.
inline uint64_t PackVertexFetches(const VsFetchPlan& plan,
                                  const uint32_t* regs, const uint8_t* guestBase,
                                  uint32_t* output, uint64_t baseDword,
                                  VsStateCB& state,
                                  const uint32_t* readStarts = nullptr,
                                  VertexUploadCache* cache = nullptr)
{
    struct Region { uint32_t slot, start, size; };
    std::array<Region, 96> regions;
    size_t regionCount = 0;
#if defined(TTT2_VERTEX_SLOT_SCAN_EXPERIMENT) && TTT2_VERTEX_SLOT_SCAN_EXPERIMENT
    // Linux x86-64 and ARM64 store each bool in one byte. Inspect its object
    // representation through memcpy so unaligned loads and aliasing are safe;
    // only the all-zero test is used, so host byte order does not matter.
    static_assert(sizeof(bool) == 1);
    for (uint32_t group = 0; group < 96; group += 8)
    {
        uint64_t usedBytes;
        std::memcpy(&usedBytes, plan.used + group, sizeof(usedBytes));
        if (!usedBytes) continue;
        for (uint32_t slot = group; slot < group + 8; ++slot)
#else
    for (uint32_t slot = 0; slot < 96; ++slot)
#endif
    {
        if (!plan.used[slot] || (regs[0x4800 + slot * 2] & 3) != 3) continue;
        state.vfRegion[slot][3] = 1;
        const VsGuestRange range = VertexFetchGuestRange(plan, regs, slot);
        if (!range.size)
        {
            state.vfRegion[slot][1] = state.vfRegion[slot][2] = 0;
            continue;
        }
        regions[regionCount++] = {slot, readStarts ? readStarts[slot] : range.start, range.size};
    }
#if defined(TTT2_VERTEX_SLOT_SCAN_EXPERIMENT) && TTT2_VERTEX_SLOT_SCAN_EXPERIMENT
    }
#endif
    // Slots can fetch different attributes from overlapping parts of the same
    // interleaved vertex buffer. Copy the union once within this draw, and give
    // each slot its original bounds and its own offset into that union. Nothing
    // is retained across draws, so guest writes need no new invalidation scheme.
    std::sort(regions.begin(), regions.begin() + regionCount,
        [](const Region& a, const Region& b) { return a.start < b.start; });
    if (cache)
    {
        uint64_t cursor = baseDword;
        for (size_t r = 0; r < regionCount;)
        {
            const uint64_t start = regions[r].start;
            uint64_t end = start + regions[r].size;
            size_t next = r + 1;
            while (next < regionCount && regions[next].start < end) {
                end = std::max(end, uint64_t(regions[next].start) + regions[next].size);
                ++next;
            }
            const size_t count = size_t(end - start) / 4;
            const auto* source = reinterpret_cast<const uint32_t*>(guestBase + start);
            uint64_t location = cursor;
            const auto* copy = cache->Prepare(uint32_t(start), source, count, cursor, location);
            if (copy) {
                CopyVertexWords(output, copy, count);
                output += count; cursor += count;
            }
            for (; r < next; ++r) {
                const auto& region = regions[r];
                const auto slot = region.slot;
                const uint64_t slotBase = location + (region.start - start) / 4;
                state.vfRegion[slot][0] = int32_t(int64_t(slotBase) - plan.lo[slot]);
                state.vfRegion[slot][1] = int32_t(plan.lo[slot]);
                state.vfRegion[slot][2] = int32_t(plan.hi[slot]);
            }
        }
        return cursor;
    }
    uint64_t cursor = baseDword, copiedEnd = 0;
    for (size_t r = 0; r < regionCount; ++r)
    {
        const auto& region = regions[r];
        const uint64_t start = region.start, end = start + region.size;
        const bool overlap = r && start < copiedEnd;
        const uint64_t copyStart = overlap ? copiedEnd : start;
        // cursor is the end of the previous output. A contained region may
        // start before it; its offset points back to the already copied data.
        const int64_t slotBase = int64_t(cursor) - (overlap ? int64_t(copiedEnd - start) / 4 : 0);
        if (end > copyStart)
        {
            const auto* source = reinterpret_cast<const uint32_t*>(guestBase + copyStart);
            const size_t count = size_t(end - copyStart) / 4;
#if defined(TTT2_GEOMETRY_AB_PROBE)
            if (geometry_probe::ReferenceMask() & 32768)
                for (size_t i = 0; i < count; ++i) output[i] = __builtin_bswap32(source[i]);
            else
#endif
                CopyVertexWords(output, source, count);
#if defined(TTT2_GEOMETRY_AB_PROBE)
            vertex_audit::Observe(uint32_t(copyStart), reinterpret_cast<const uint8_t*>(source), count * 4);
#endif
            output += count;
            cursor += count;
        }
        const uint32_t slot = region.slot;
        state.vfRegion[slot][0] = int32_t(slotBase - plan.lo[slot]);
        state.vfRegion[slot][1] = int32_t(plan.lo[slot]);
        state.vfRegion[slot][2] = int32_t(plan.hi[slot]);
        copiedEnd = std::max(copiedEnd, end);
    }
    return cursor;
}

} // namespace ttt2_hw
