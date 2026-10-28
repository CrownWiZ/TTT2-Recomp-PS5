#include "cxx20_compat.h"
#pragma once
// Frozen pre-optimization implementations for the opt-in Linux A/B probe and
// standalone correctness checks. Normal game builds do not include this file.
#include "gpu_hw_indices.h"
#include "gpu_hw_vertex_fetch.h"
#include "gpu_index_decode.h"

namespace ttt2_hw::reference {
using ttt2_hw::IndexPrimitive;
inline DecodedIndices DecodeIndices(const uint8_t* guest, uint32_t address, uint32_t count,
    bool automatic, bool index32, bool restart, uint32_t restartIndex, uint32_t offset,
    uint32_t minimum, uint32_t maximum, IndexDecodeStorage& storage)
{
    storage.indices.assign(count, 0);
    storage.cuts.assign(count, 0);
    bool anyCut = false;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t raw = i;
        if (!automatic)
        {
            const uint32_t a = address + i * (index32 ? 4 : 2);
            raw = uint32_t(guest[a]) * 256 + guest[a + 1];
            if (index32) raw = (raw << 16) | (uint32_t(guest[a + 2]) << 8) | guest[a + 3];
        }
        if (restart && raw == restartIndex) { storage.cuts[i] = 1; anyCut = true; continue; }
        uint32_t value = (raw + offset) & 0xFFFFFF;
        if (maximum != 0 && maximum >= minimum) value = (std::min)((std::max)(value, minimum), maximum);
        storage.indices[i] = value;
    }
    uint32_t highest = 0;
    for (uint32_t i = 0; i < count; ++i)
        if (!storage.cuts[i]) highest = (std::max)(highest, storage.indices[i]);
    return {storage.indices, storage.cuts, highest, anyCut};
}
// Frozen pre-optimization assembly, kept as an independent output oracle.
inline ttt2_hw::TriangleIndices BuildTriangleIndices(IndexPrimitive primitive,
    ttt2_c20::span<const uint32_t> idx, ttt2_c20::span<const uint8_t> cut,
    std::vector<uint32_t>& tri)
{
    const uint32_t n = uint32_t(idx.size());
    tri.clear();
    auto emit = [&](uint32_t a, uint32_t b, uint32_t c) {
        tri.push_back(idx[a]); tri.push_back(idx[b]); tri.push_back(idx[c]);
    };
    if (primitive == IndexPrimitive::Triangles)
    {
        for (uint32_t i = 0; i + 2 < n; i += 3)
            if (!(cut[i] | cut[i + 1] | cut[i + 2])) emit(i, i + 1, i + 2);
    }
    else if (primitive == IndexPrimitive::Strip || primitive == IndexPrimitive::Fan)
    {
        for (uint32_t s = 0; s < n;)
        {
            if (cut[s]) { ++s; continue; }
            uint32_t e = s;
            while (e < n && !cut[e]) ++e;
            if (primitive == IndexPrimitive::Strip)
                for (uint32_t i = s; i + 2 < e; ++i)
                {
                    if ((i - s) & 1) emit(i + 1, i, i + 2);
                    else emit(i, i + 1, i + 2);
                }
            else
                for (uint32_t i = s + 1; i + 1 < e; ++i) emit(s, i, i + 1);
            s = e;
        }
    }
    else if (primitive == IndexPrimitive::Quads)
    {
        for (uint32_t i = 0; i + 3 < n; i += 4)
        {
            if (cut[i] | cut[i + 1] | cut[i + 2] | cut[i + 3]) continue;
            emit(i, i + 1, i + 2); emit(i, i + 2, i + 3);
        }
    }
    uint32_t lowest = tri.empty() ? 0 : UINT32_MAX, highest = 0;
    for (uint32_t v : tri) { lowest = std::min(lowest, v); highest = std::max(highest, v); }
    return {tri, lowest, highest};
}


// Frozen pre-optimization copy. Output layouts can differ after coalescing;
// compare every slot's shader-visible dwords through vfRegion instead.
inline uint64_t PackVertexFetches(const ttt2_hw::VsFetchPlan& plan, const uint32_t* regs,
    const uint8_t* guest, uint32_t* output, uint64_t base, ttt2_hw::VsStateCB& state,
    const uint32_t* readStarts)
{
    uint64_t cursor = base;
    for (uint32_t slot = 0; slot < 96; ++slot)
    {
        if (!plan.used[slot] || (regs[0x4800 + slot * 2] & 3) != 3) continue;
        state.vfRegion[slot][3] = 1;
        const auto range = ttt2_hw::VertexFetchGuestRange(plan, regs, slot);
        if (!range.size) { state.vfRegion[slot][1] = state.vfRegion[slot][2] = 0; continue; }
        const uint32_t read = readStarts ? readStarts[slot] : range.start;
        const auto* src = reinterpret_cast<const uint32_t*>(guest + read);
        const int64_t count = plan.hi[slot] - plan.lo[slot];
        for (int64_t i = 0; i < count; ++i) output[i] = __builtin_bswap32(src[i]);
        state.vfRegion[slot][0] = int32_t(int64_t(cursor) - plan.lo[slot]);
        state.vfRegion[slot][1] = int32_t(plan.lo[slot]);
        state.vfRegion[slot][2] = int32_t(plan.hi[slot]);
        output += count; cursor += uint64_t(count);
    }
    return cursor;
}

} // namespace ttt2_hw::reference
