#pragma once

// Direct form of four-bone vertex jobs at 0x82B10F40 and 0x82B10A70.
// The original PPC functions remain the reference implementations. This uses
// SIMDe vectors, which compile to the host's SIMD instructions (NEON on ARM64).
#include "kernel/kernel_shared.h"

#include <cstdint>

namespace ttt2_skin {
using Vec = simde__m128;

inline simde__m128i VertexByteOrderMask()
{
#if defined(SIMDE_ARM_NEON_A32V7_NATIVE)
    // Keep the fixed permutation visible to the compiler. Loading the mutable
    // PPC mask table otherwise prevents hoisting it across output writes.
    return simde_mm_setr_epi8(15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);
#else
    return simde_mm_load_si128(reinterpret_cast<const simde__m128i*>(VectorMaskL));
#endif
}

inline Vec ReadVector(const uint8_t* base, uint32_t address)
{
    const auto raw = simde_mm_load_si128(
        reinterpret_cast<const simde__m128i*>(base + (address & ~15u)));
    return simde_mm_castsi128_ps(simde_mm_shuffle_epi8(
        raw, VertexByteOrderMask()));
}

inline void WriteVector(uint8_t* destination, Vec value)
{
    const auto raw = simde_mm_shuffle_epi8(
        simde_mm_castps_si128(value),
        VertexByteOrderMask());
    simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(destination), raw);
}

template <int Word> inline Vec Splat(Vec value)
{
    static_assert(Word >= 0 && Word < 4);
    constexpr int lane = 3 - Word; // the PPC vector is stored in guest byte order
    return simde_mm_castsi128_ps(simde_mm_shuffle_epi32(
        simde_mm_castps_si128(value), lane * 0x55));
}

inline Vec Mul(Vec a, Vec b) { return simde_mm_mul_ps(a, b); }
inline Vec Add(Vec a, Vec b) { return simde_mm_add_ps(a, b); }

struct Rows {
    Vec x, y, z, translate;
};

inline Vec TransformDirection(const Rows& m, Vec x, Vec y, Vec z)
{
    // Keep the PPC function's multiply/add order. The Android target builds
    // with -ffp-contract=off so this does not silently become fused FMA.
    const Vec xy = Add(Mul(m.y, y), Mul(m.x, x));
    return Add(Mul(m.z, z), xy);
}

inline Vec TransformPosition(const Rows& m, Vec x, Vec y, Vec z)
{
    const Vec tx = Add(Mul(m.x, x), m.translate);
    const Vec txy = Add(Mul(m.y, y), tx);
    return Add(Mul(m.z, z), txy);
}

// `destination` can point into guest memory or to a separate comparison buffer.
// The caller supplies a live PPCContext only for its FP flush setting; the
// original function is still used when exact guest-register effects matter.
// Fields includes position: 2 selects position/normal, 4 adds two directions.
template <unsigned Fields = 4>
inline void RunFourBone(PPCContext& ctx, uint8_t* base, uint32_t job,
                        uint8_t* destination)
{
    static_assert(Fields == 2 || Fields == 4);
    constexpr uint32_t InputStride = (Fields + 2) * 16;
    constexpr uint32_t OutputStride = Fields * 16;
    const uint32_t count = GRead32(base, job + 24);
    if (!count) return;
    const uint32_t paletteA = GRead32(base, job + 4);
    const uint32_t paletteB = GRead32(base, job + 8);
    const uint32_t boneOffset = uint32_t(GRead16(base, job + 12)) << 6;
    const uint32_t input = GRead32(base, job + 20);
    ctx.fpscr.enableFlushMode();

    for (uint32_t vertex = 0; vertex < count; ++vertex)
    {
        const uint32_t record = input + vertex * InputStride;
        const Vec position = ReadVector(base, record);
        const Vec direction0 = ReadVector(base, record + 16);
        const Vec direction1 = Fields == 4 ? ReadVector(base, record + 32) : Vec{};
        const Vec direction2 = Fields == 4 ? ReadVector(base, record + 48) : Vec{};
        const Vec weights = ReadVector(base, record + (Fields + 1) * 16);
        const Vec px = Splat<0>(position), py = Splat<1>(position), pz = Splat<2>(position);
        const Vec d0x = Splat<0>(direction0), d0y = Splat<1>(direction0), d0z = Splat<2>(direction0);
        const Vec d1x = Splat<0>(direction1), d1y = Splat<1>(direction1), d1z = Splat<2>(direction1);
        const Vec d2x = Splat<0>(direction2), d2y = Splat<1>(direction2), d2z = Splat<2>(direction2);
        const Vec weight[4] = { Splat<0>(weights), Splat<1>(weights),
                                Splat<2>(weights), Splat<3>(weights) };

        Vec output[Fields]{};
#if defined(__clang__) && defined(SIMDE_ARM_NEON_A32V7_NATIVE)
        #pragma clang loop unroll(full)
#endif
        for (int bone = 0; bone < 4; ++bone)
        {
            const uint32_t index = GRead32(base, record + Fields * 16 + bone * 4);
            const uint32_t offset = index << 6;
            const uint32_t a = paletteA - boneOffset + offset;
            const uint32_t b = paletteB - boneOffset + offset;
            const Rows ma{ ReadVector(base, a), ReadVector(base, a + 16),
                           ReadVector(base, a + 32), ReadVector(base, a + 48) };
            const Rows mb{ ReadVector(base, b), ReadVector(base, b + 16),
                           ReadVector(base, b + 32), {} };
            Vec transformed[Fields];
            transformed[0] = TransformPosition(ma, px, py, pz);
            transformed[1] = TransformDirection(mb, d0x, d0y, d0z);
            if constexpr (Fields == 4) {
                transformed[2] = TransformDirection(mb, d1x, d1y, d1z);
                transformed[3] = TransformDirection(mb, d2x, d2y, d2z);
            }
            for (unsigned field = 0; field < Fields; ++field)
            {
                const Vec weighted = Mul(transformed[field], weight[bone]);
                output[field] = bone == 0 ? weighted : Add(weighted, output[field]);
            }
        }
        uint8_t* out = destination + vertex * OutputStride;
        for (unsigned field = 0; field < Fields; ++field)
            WriteVector(out + field * 16, output[field]);
    }
}
} // namespace ttt2_skin
