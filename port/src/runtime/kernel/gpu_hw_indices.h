#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>
#if defined(TTT2_GEOMETRY_AB_PROBE)
#include "gpu_geometry_probe.h"
#endif

namespace ttt2_hw {

// Xenos VGT_DRAW_INITIATOR primitive values accepted by the GPU vertex path.
enum class IndexPrimitive : uint32_t { Triangles = 4, Fan = 5, Strip = 6, Quads = 13 };

struct TriangleIndices
{
    std::span<const uint32_t> values;
    uint32_t lowest = 0, highest = 0;
};

struct StripIndices : TriangleIndices
{
    uint32_t triangles = 0;
};

inline std::pair<uint32_t, uint32_t> IndexBounds(std::span<const uint32_t> indices)
{
#if defined(TTT2_GEOMETRY_AB_PROBE)
    if (geometry_probe::ReferenceMask() & 32)
    {
        const auto [lo, hi] = std::minmax_element(indices.begin(), indices.end());
        return {*lo, *hi};
    }
#endif
    // Independent reductions let the compiler vectorize this scan. The
    // pairwise comparisons in minmax_element have data-dependent branches.
    uint32_t lo = UINT32_MAX, hi = 0;
    for (const uint32_t value : indices)
    {
        lo = (std::min)(lo, value);
        hi = (std::max)(hi, value);
    }
    return {lo, hi};
}

// Decoded guest indices are 24-bit. UINT32_MAX is therefore available as the
// D3D11 R32 strip restart sentinel. Preserve segment parity and omit segments
// too short to produce a triangle, matching the list path's fetch bounds.
inline StripIndices BuildNativeStripIndices(std::span<const uint32_t> idx,
    std::span<const uint8_t> cuts, std::vector<uint32_t>& scratch)
{
    StripIndices result;
    if (idx.size() < 3) return result;
    if (cuts.empty())
    {
        result.values = idx;
        const auto [lo, hi] = IndexBounds(idx);
        result.lowest = lo; result.highest = hi;
        result.triangles = uint32_t(idx.size() - 2);
        return result;
    }
    if (scratch.size() < idx.size()) scratch.resize(idx.size());
    size_t count = 0;
    uint32_t lowest = UINT32_MAX, highest = 0;
    for (size_t start = 0; start < idx.size();)
    {
        if (cuts[start]) { ++start; continue; }
        size_t end = start;
        while (end < idx.size() && !cuts[end]) ++end;
        if (end - start >= 3)
        {
            if (count) scratch[count++] = UINT32_MAX;
            for (size_t i = start; i < end; ++i)
            {
                const uint32_t v = idx[i];
                scratch[count++] = v;
                lowest = std::min(lowest, v); highest = std::max(highest, v);
            }
            result.triangles += uint32_t(end - start - 2);
        }
        start = end;
    }
    result.values = std::span<const uint32_t>(scratch.data(), count);
    if (count) { result.lowest = lowest; result.highest = highest; }
    return result;
}

// cuts is empty when there are no restart markers, otherwise it has idx.size()
// elements. Keep scratch at its high-water size: subsequent draws overwrite its
// live prefix without zero-filling it or checking capacity for every index.
inline TriangleIndices BuildTriangleIndices(IndexPrimitive primitive,
    std::span<const uint32_t> idx, std::span<const uint8_t> cuts,
    std::vector<uint32_t>& scratch)
{
    TriangleIndices result;
    const size_t n = idx.size();
    if (n < 3) return result;

    if (primitive == IndexPrimitive::Triangles && cuts.empty())
    {
        // A triangle list already has the exact order needed by DrawIndexed.
        result.values = idx.first(n - n % 3);
        const auto [lo, hi] = IndexBounds(result.values);
        result.lowest = lo; result.highest = hi;
        return result;
    }

    const size_t limit = primitive == IndexPrimitive::Triangles ? n - n % 3 :
                         primitive == IndexPrimitive::Quads ? (n / 4) * 6 : (n - 2) * 3;
    if (scratch.size() < limit) scratch.resize(limit);
    uint32_t* out = scratch.data();
    size_t count = 0;
    uint32_t lowest = UINT32_MAX, highest = 0;
    auto emit = [&](uint32_t a, uint32_t b, uint32_t c) {
        out[count] = a; out[count + 1] = b; out[count + 2] = c;
        count += 3;
        lowest = std::min(lowest, std::min(a, std::min(b, c)));
        highest = std::max(highest, std::max(a, std::max(b, c)));
    };
    if (primitive == IndexPrimitive::Triangles)
    {
        for (size_t i = 0; i + 2 < n; i += 3)
            if (!(cuts[i] | cuts[i + 1] | cuts[i + 2])) emit(idx[i], idx[i + 1], idx[i + 2]);
    }
    else if (primitive == IndexPrimitive::Quads)
    {
        for (size_t i = 0; i + 3 < n; i += 4)
        {
            if (!cuts.empty() && (cuts[i] | cuts[i + 1] | cuts[i + 2] | cuts[i + 3])) continue;
            emit(idx[i], idx[i + 1], idx[i + 2]);
            emit(idx[i], idx[i + 2], idx[i + 3]);
        }
    }
    else
    {
        for (size_t s = 0; s < n;)
        {
            if (!cuts.empty() && cuts[s]) { ++s; continue; }
            size_t e = s;
            if (cuts.empty()) e = n;
            else while (e < n && !cuts[e]) ++e;
            if (primitive == IndexPrimitive::Strip)
            {
                for (size_t i = s; i + 2 < e; ++i)
                    if ((i - s) & 1) emit(idx[i + 1], idx[i], idx[i + 2]);
                    else emit(idx[i], idx[i + 1], idx[i + 2]);
            }
            else
                for (size_t i = s + 1; i + 1 < e; ++i) emit(idx[s], idx[i], idx[i + 1]);
            s = e;
        }
    }
    result.values = std::span<const uint32_t>(scratch.data(), count);
    if (count) { result.lowest = lowest; result.highest = highest; }
    return result;
}

} // namespace ttt2_hw
