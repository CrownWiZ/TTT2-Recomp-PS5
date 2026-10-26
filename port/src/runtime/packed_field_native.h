#pragma once
// Exact shortcut for inactive mode-4 jobs in sub_82AE9C28. Unsupported inputs
// keep using the generated implementation; no floating-point work is reordered.
#include "ppc_recomp_shared.h"
#include <cstring>
namespace ttt2_packed_field
{
static uint32_t Read32(uint8_t *b, uint32_t a)
{
    uint32_t v;
    std::memcpy(&v, b + a, 4);
    return __builtin_bswap32(v);
}
static uint64_t Read64(uint8_t *b, uint32_t a)
{
    uint64_t v;
    std::memcpy(&v, b + a, 8);
    return __builtin_bswap64(v);
}
static void Write32(uint8_t *b, uint32_t a, uint32_t v)
{
    v = __builtin_bswap32(v);
    std::memcpy(b + a, &v, 4);
}
static void Write64(uint8_t *b, uint32_t a, uint64_t v)
{
    v = __builtin_bswap64(v);
    std::memcpy(b + a, &v, 8);
}
static bool Intersects(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    return n && m && a < b + m && b < a + n;
}
static bool Valid(uint64_t a, uint64_t n)
{
    return a >= 0x10000 && a + n <= 0x100000000ull;
}

inline uint32_t InactivePrefix(const PPCContext &ctx, uint8_t *base)
{
    // Reject wrapped addresses and values outside the supported job shape.
    // Comparing nonnegative float encodings also rejects negative values and NaNs.
    const uint32_t sp = ctx.r1.u32, n = ctx.r5.u32;
    if (sp < 0x10000 + 8192 || sp > 0xffff0000 || n < 4 || n > 16384 || ctx.r9.u32 < 12 ||
        ctx.r9.u32 > 256 || ctx.r9.u32 % 4 || ctx.f1.u64 > 0x4030000000000000ull)
    {
        return 0;
    }
    if (Read32(base, sp + 84) != 4 || Read32(base, sp + 124) != 1 || Read32(base, sp + 92) != 0 ||
        Read64(base, sp + 176) != 0)
    {
        return 0;
    }
    const uint32_t params = Read32(base, sp + 116), vol = uint32_t(Read64(base, sp + 128)),
                   pos = uint32_t(Read64(base, sp + 136)), prev = uint32_t(Read64(base, sp + 152)),
                   stats = uint32_t(Read64(base, sp + 168));
    const uint32_t dst = ctx.r6.u32, aux = ctx.r7.u32, stride = ctx.r9.u32;
    const uint64_t length = uint64_t(n) * 8, auxBytes = aux ? uint64_t(n) * 4 : 0,
                   posBytes = uint64_t(n - 1) * stride + 16;
    if (!Valid(params, 64) || !Valid(vol, 96) || !Valid(pos, posBytes) || !Valid(prev, length) ||
        !Valid(dst, length) || !Valid(stats, 16) || (aux && !Valid(aux, auxBytes)))
    {
        return 0;
    }
    // Only the old/output eight-byte records may alias, and only exactly.
    // In particular, do not move writes across parameter, image, or stack reads.
    struct Range
    {
        uint64_t a, n;
    };
    const Range inputs[] = {
        {params, 64},          {vol, 96}, {pos, posBytes}, {prev, length}, {sp - 8192, 8192 + 256},
        {0x82000000, 0x200000}};
    const Range outputs[] = {{dst, length}, {aux, auxBytes}, {stats, 16}};
    bool overlap = false;
    for (unsigned oi = 0; oi < 3; ++oi)
        for (unsigned ii = 0; ii < 6; ++ii)
            overlap |= Intersects(outputs[oi].a, outputs[oi].n, inputs[ii].a, inputs[ii].n) &&
                       !(oi == 0 && ii == 3 && dst == prev);
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < i; ++j)
            overlap |= Intersects(outputs[i].a, outputs[i].n, outputs[j].a, outputs[j].n);
    for (unsigned ii = 0; ii < 4; ++ii)
        overlap |= Intersects(inputs[ii].a, inputs[ii].n, sp - 8192, 8192 + 256);
    if (overlap || (Read32(base, vol) != 0 && Read32(base, vol + 4) != 0) ||
        Read32(base, params) > 0x3f800000u)
    {
        return 0;
    }
    // Fixed image constants used by the unchanged original routine.
    if (Read32(base, 0x820f19f4) != 0 || Read32(base, 0x8201e1d8) != 0x43000000u ||
        Read32(base, 0x820099fc) > 0x3f800000u)
    {
        return 0;
    }
    // With one inactive volume the cadence is three. A zero field cannot decay
    // further, and none of the byte channels changes. Keep at least one complete
    // cadence in the reference implementation to reproduce its final registers
    // and stack scratch. A multiple-of-three prefix preserves the phase.
    const uint32_t prefix = ((n - 3) / 3) * 3;
    if (!prefix)
    {
        return 0;
    }
    for (uint32_t i = 0; i < prefix; ++i)
        if (base[prev + i * 8] || base[prev + i * 8 + 1])
        {
            return 0;
        }
    return prefix;
}
inline bool TryRunInactivePrefix(PPCContext &ctx, uint8_t *base,
                                 void (*original)(PPCContext &, uint8_t *))
{
    const uint32_t prefix = InactivePrefix(ctx, base);
    if (!prefix)
        return false;
    const uint32_t sp = ctx.r1.u32, n = ctx.r5.u32, stride = ctx.r9.u32;
    const uint32_t prev = uint32_t(Read64(base, sp + 152)), pos = uint32_t(Read64(base, sp + 136)),
                   stats = uint32_t(Read64(base, sp + 168));
    const uint32_t dst = ctx.r6.u32, aux = ctx.r7.u32;
    if (dst != prev)
        std::memcpy(base + dst, base + prev, size_t(prefix) * 8);
    if (aux)
        for (uint32_t i = 0; i < prefix; ++i)
        {
            auto s = base + prev + i * 8;
            auto d = base + aux + i * 4;
            uint16_t v =
                uint16_t((uint32_t(s[4] & 0xf8) << 8) | (uint32_t(s[5] & 0xfc) << 3) | (s[6] >> 3));
            d[0] = d[1] = 0;
            d[2] = uint8_t(v >> 8);
            d[3] = uint8_t(v);
        }
    // The remaining records use the same addresses and cadence as the full job.
    // Restore the caller's pointer arguments and full count after the reference
    // epilogue; its nonzero-field total already excludes this all-zero prefix.
    const uint64_t positionArg = Read64(base, sp + 136), previousArg = Read64(base, sp + 152);
    Write64(base, sp + 136, uint32_t(pos + prefix * stride));
    Write64(base, sp + 152, uint32_t(prev + prefix * 8));
    ctx.r5.u64 = n - prefix;
    ctx.r6.u64 = uint32_t(dst + prefix * 8);
    if (aux)
        ctx.r7.u64 = uint32_t(aux + prefix * 4);
    original(ctx, base);
    Write64(base, sp + 136, positionArg);
    Write64(base, sp + 152, previousArg);
    Write32(base, sp + 36, n);
    Write32(base, stats + 4, n);
    ctx.r8.u64 = n;
    return true;
}
} // namespace ttt2_packed_field
