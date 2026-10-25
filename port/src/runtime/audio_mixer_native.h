#pragma once
#include "guest_memory.h"
#include "ppc_recomp_shared.h"
#include <cstring>

// Scheduled form of the PCM mixer at 0x82D0FCB0. The entry has paths for
// direct and interpolated samples, mono/stereo and arbitrary channel counts.
// Preserve guest registers, scratch writes, load/store order and
// single-precision rounding. Independent vector arithmetic is grouped before
// scalar work to avoid repeatedly changing the host FP flush mode. Compile with
// FP contraction off.
namespace ttt2_audio_mixer {
// Preserve the exact subset of a byte-reversed vector written by stvlx/stvrx.
inline void CopyPart(uint8_t *to, const uint8_t *from, unsigned count) {
  if (count == 16) {
    std::memcpy(to, from, 16);
    return;
  }
  if (count & 8) {
    std::memcpy(to, from, 8);
    to += 8;
    from += 8;
  }
  if (count & 4) {
    std::memcpy(to, from, 4);
    to += 4;
    from += 4;
  }
  if (count & 2) {
    std::memcpy(to, from, 2);
    to += 2;
    from += 2;
  }
  if (count & 1)
    *to = *from;
}
inline void StoreLeft(uint8_t *base, uint32_t ea, const uint8_t *source) {
  if (ea < guest_memory::NullGuardBytes)
    return;
  alignas(16) uint8_t reversed[16];
  for (unsigned i = 0; i < 16; ++i)
    reversed[i] = source[15 - i];
  CopyPart(base + ea, reversed, 16 - (ea & 15));
}
inline void StoreRight(uint8_t *base, uint32_t ea, const uint8_t *source) {
  const unsigned count = ea & 15;
  if (ea < guest_memory::NullGuardBytes || !count)
    return;
  alignas(16) uint8_t reversed[16];
  for (unsigned i = 0; i < 16; ++i)
    reversed[i] = source[15 - i];
  CopyPart(base + (ea & ~15u), reversed + 16 - count, count);
}

inline void Store32(uint8_t *base, uint32_t address, uint32_t value) {
  guest_memory::StoreScalar(base, address, __builtin_bswap32(value));
}
inline void Store64(uint8_t *base, uint32_t address, uint64_t value) {
  guest_memory::StoreScalar(base, address, __builtin_bswap64(value));
}
inline void StoreVector(uint8_t *base, uint32_t address, simde__m128i value) {
  alignas(16) uint8_t bytes[16];
  simde_mm_store_si128(reinterpret_cast<simde__m128i *>(bytes), value);
  guest_memory::CopyToGuest(base, base + address, bytes, sizeof(bytes));
}

inline void Run(PPCContext &__restrict ctx, uint8_t *base) {
  PPC_FUNC_PROLOGUE();
  PPCRegister temp{};
  uint32_t ea{};
  ctx.r12.u64 = ctx.lr;
  ctx.lr = 0x82D0FCB8;
  __savegprlr_17(ctx, base);
  ctx.r19.u64 = PPC_LOAD_U32(ctx.r1.u32 + 156);
  ctx.r7.s64 = 16;
  ctx.r11.s64 = 32;
  ctx.fpscr.disableFlushMode();
  temp.f32 = float(ctx.f5.f64);
  Store32(base, ctx.r1.u32 + 116, temp.u32);
  ctx.r30.s64 = ctx.r1.s64 + 116;
  temp.f32 = float(ctx.f1.f64);
  Store32(base, ctx.r1.u32 + 84, temp.u32);
  ctx.r28.s64 = ctx.r1.s64 + 84;
  temp.f32 = float(ctx.f2.f64);
  Store32(base, ctx.r1.u32 + 92, temp.u32);
  ctx.r27.s64 = ctx.r1.s64 + 92;
  temp.f32 = float(ctx.f3.f64);
  Store32(base, ctx.r1.u32 + 100, temp.u32);
  temp.u32 = ctx.r7.u32 + ctx.r19.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v63.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  ctx.r26.s64 = ctx.r1.s64 + 100;
  temp.u32 = ctx.r11.u32 + ctx.r19.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v62.u8,
      temp.u32 & 0xF
          ? simde_mm_shuffle_epi8(
                simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
                simde_mm_load_si128(
                    (simde__m128i *)&VectorMaskR[(temp.u32 & 0xF) * 16]))
          : simde_mm_setzero_si128());
  ctx.r24.s64 = ctx.r1.s64 + 108;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v63.u8,
      simde_mm_or_si128(simde_mm_load_si128((simde__m128i *)ctx.v63.u8),
                        simde_mm_load_si128((simde__m128i *)ctx.v62.u8)));
  temp.u32 = ctx.r30.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v62.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.f32 = float(ctx.f4.f64);
  Store32(base, ctx.r1.u32 + 108, temp.u32);
  ctx.r18.u64 = PPC_LOAD_U32(ctx.r1.u32 + 164);
  temp.u32 = ctx.r28.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v58.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  ctx.r31.s64 = -2111897600;
  temp.u32 = ctx.r27.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v57.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  ctx.r29.s64 = ctx.r1.s64 + 124;
  temp.u32 = ctx.r26.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v56.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  ctx.r31.s64 = ctx.r31.s64 + 7184;
  temp.u32 = ctx.r24.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v55.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128((simde__m128i *)ctx.v54.u32,
                       simde_mm_set1_epi32(int(0x0)));
  temp.u32 = ctx.r11.u32 + ctx.r18.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v61.u8,
      temp.u32 & 0xF
          ? simde_mm_shuffle_epi8(
                simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
                simde_mm_load_si128(
                    (simde__m128i *)&VectorMaskR[(temp.u32 & 0xF) * 16]))
          : simde_mm_setzero_si128());
  ctx.r25.s64 = 0;
  temp.u32 = ctx.r7.u32 + ctx.r18.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v60.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  ctx.cr6.compare<uint32_t>(ctx.r9.u32, ctx.r8.u32, ctx.xer);
  simde_mm_store_si128(
      (simde__m128i *)ctx.v59.u8,
      simde_mm_or_si128(simde_mm_load_si128((simde__m128i *)ctx.v60.u8),
                        simde_mm_load_si128((simde__m128i *)ctx.v61.u8)));
  temp.f32 = float(ctx.f6.f64);
  Store32(base, ctx.r1.u32 + 124, temp.u32);
  temp.u32 = ctx.r29.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v60.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v48.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v60.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v60.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + ((ctx.r31.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v61.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v62.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v62.u8,
      simde_mm_and_si128(simde_mm_load_si128((simde__m128i *)ctx.v63.u8),
                         simde_mm_load_si128((simde__m128i *)ctx.v60.u8)));
  temp.u32 = ctx.r19.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v63.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v59.u8,
      simde_mm_and_si128(simde_mm_load_si128((simde__m128i *)ctx.v59.u8),
                         simde_mm_load_si128((simde__m128i *)ctx.v60.u8)));
  temp.u32 = ctx.r18.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v60.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r19.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      temp.u32 & 0xF
          ? simde_mm_shuffle_epi8(
                simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
                simde_mm_load_si128(
                    (simde__m128i *)&VectorMaskR[(temp.u32 & 0xF) * 16]))
          : simde_mm_setzero_si128());
  simde_mm_store_si128(
      (simde__m128i *)ctx.v58.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v58.u32),
                             0xFF));
  temp.u32 = ctx.r7.u32 + ctx.r18.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v52.u8,
      temp.u32 & 0xF
          ? simde_mm_shuffle_epi8(
                simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
                simde_mm_load_si128(
                    (simde__m128i *)&VectorMaskR[(temp.u32 & 0xF) * 16]))
          : simde_mm_setzero_si128());
  simde_mm_store_si128(
      (simde__m128i *)ctx.v57.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v57.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v56.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v56.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v63.u8,
      simde_mm_or_si128(simde_mm_load_si128((simde__m128i *)ctx.v63.u8),
                        simde_mm_load_si128((simde__m128i *)ctx.v53.u8)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v60.u8,
      simde_mm_or_si128(simde_mm_load_si128((simde__m128i *)ctx.v60.u8),
                        simde_mm_load_si128((simde__m128i *)ctx.v52.u8)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v55.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v55.u32),
                             0xFF));
  if (!ctx.cr6.eq)
    goto loc_82D100EC;
  ctx.cr6.compare<uint32_t>(ctx.r6.u32, 1, ctx.xer);
  if (!ctx.cr6.eq)
    goto loc_82D0FE68;
  ctx.r11.u64 = PPC_LOAD_U32(ctx.r1.u32 + 140);
  ctx.cr6.compare<uint32_t>(ctx.r5.u32, 0, ctx.xer);
  ctx.r10.u64 = PPC_LOAD_U32(ctx.r1.u32 + 148);
  temp.u32 = ctx.r11.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v54.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r10.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v52.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r11.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r10.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v51.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  if (ctx.cr6.eq)
    goto loc_82D100E4;
  ctx.r8.u64 = PPC_LOAD_U32(ctx.r1.u32 + 132);
  ctx.r10.s64 = 0;
  ctx.ctr.u64 = ctx.r5.u64;
loc_82D0FDB0:
  ctx.r6.u64 = PPC_LOAD_U32(ctx.r4.u32 + 0);
  ctx.r5.s64 = ctx.r1.s64 + -160;
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v61.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v48.f32)));
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v53.f32),
                                    simde_mm_load_ps(ctx.v51.f32)));
  simde_mm_store_ps(ctx.v54.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v54.f32),
                                    simde_mm_load_ps(ctx.v52.f32)));
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  ctx.fpscr.disableFlushModeUnconditional();
  temp.u32 = PPC_LOAD_U32(ctx.r8.u32 + 0);
  ctx.f0.f64 = double(temp.f32);
  ctx.r11.s64 = ctx.r3.s64 + 16;
  ctx.r31.s64 = 4;
  ctx.r6.s64 = int16_t(PPC_LOAD_U16(ctx.r10.u32 + ctx.r6.u32));
  ctx.r6.s64 = int64_t(ctx.r6.s32) * int64_t(ctx.r9.s32);
  ctx.r6.s64 = ctx.r6.s32;
  ctx.r10.s64 = ctx.r10.s64 + 2;
  Store64(base, ctx.r1.u32 + -152, ctx.r6.u64);
  ctx.f13.u64 = PPC_LOAD_U64(ctx.r1.u32 + -152);
  ctx.f13.f64 = double(ctx.f13.s64);
  ctx.f13.f64 = double(float(ctx.f13.f64));
  ctx.f0.f64 = double(float(ctx.f13.f64 * ctx.f0.f64));
  temp.f32 = float(ctx.f0.f64);
  Store32(base, ctx.r1.u32 + -160, temp.u32);
  temp.u32 = ctx.r5.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v50.u32),
                             0xFF));
  ctx.fpscr.enableFlushModeUnconditional();
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v50.f32)));
  simde_mm_store_ps(ctx.v49.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v54.f32)));
  simde_mm_store_ps(ctx.v49.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v49.f32)));
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v50.f32)));
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v49.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v49.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v49.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v62.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v49.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v63.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v47.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v50.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v50.u32),
                             0xAA));
  ea = ctx.r3.u32;
  StoreLeft(base, ea, ctx.v49.u8);
  ea = ctx.r3.u32 + ctx.r7.u32;
  StoreRight(base, ea, ctx.v49.u8);
  ctx.r3.s64 = ctx.r11.s64 + 8;
  ea = (ctx.r11.u32) & ~0x3;
  Store32(base, ea, ctx.v47.u32[3 - ((ea & 0xF) >> 2)]);
  ea = (ctx.r11.u32 + ctx.r31.u32) & ~0x3;
  Store32(base, ea, ctx.v50.u32[3 - ((ea & 0xF) >> 2)]);
  --ctx.ctr.u64;
  if (ctx.ctr.u32 != 0)
    goto loc_82D0FDB0;
  goto loc_82D100E4;
loc_82D0FE68:
  ctx.cr6.compare<uint32_t>(ctx.r6.u32, 2, ctx.xer);
  if (!ctx.cr6.eq)
    goto loc_82D0FFBC;
  ctx.r10.u64 = PPC_LOAD_U32(ctx.r1.u32 + 140);
  ctx.r30.s64 = 48;
  ctx.r8.u64 = PPC_LOAD_U32(ctx.r1.u32 + 148);
  ctx.cr6.compare<uint32_t>(ctx.r5.u32, 0, ctx.xer);
  temp.u32 = ctx.r10.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v54.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r8.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v52.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r10.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r11.u32 + ctx.r10.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v0.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r30.u32 + ctx.r10.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v13.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r8.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v51.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r11.u32 + ctx.r8.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r30.u32 + ctx.r8.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v49.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  if (ctx.cr6.eq)
    goto loc_82D100E4;
  ctx.r6.u64 = PPC_LOAD_U32(ctx.r1.u32 + 132);
  ctx.ctr.u64 = ctx.r5.u64;
  ctx.r8.s64 = ctx.r4.s64 + 4;
  ctx.r5.s64 = ctx.r6.s64 + 4;
  ctx.r11.s64 = 0;
loc_82D0FEB8:
  ctx.r31.u64 = PPC_LOAD_U32(ctx.r4.u32 + 0);
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v61.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v48.f32)));
  simde_mm_store_ps(ctx.v54.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v54.f32),
                                    simde_mm_load_ps(ctx.v52.f32)));
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v53.f32),
                                    simde_mm_load_ps(ctx.v51.f32)));
  simde_mm_store_ps(ctx.v0.f32, simde_mm_add_ps(simde_mm_load_ps(ctx.v0.f32),
                                                simde_mm_load_ps(ctx.v50.f32)));
  simde_mm_store_ps(ctx.v13.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v13.f32),
                                    simde_mm_load_ps(ctx.v49.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  ctx.fpscr.disableFlushModeUnconditional();
  temp.u32 = PPC_LOAD_U32(ctx.r5.u32 + 0);
  ctx.f0.f64 = double(temp.f32);
  ctx.r29.u64 = PPC_LOAD_U32(ctx.r8.u32 + 0);
  ctx.r30.s64 = ctx.r1.s64 + -160;
  ctx.r28.s64 = ctx.r1.s64 + -160;
  temp.u32 = PPC_LOAD_U32(ctx.r6.u32 + 0);
  ctx.f13.f64 = double(temp.f32);
  ctx.r10.s64 = ctx.r3.s64 + 16;
  ctx.r31.s64 = int16_t(PPC_LOAD_U16(ctx.r11.u32 + ctx.r31.u32));
  ctx.r31.s64 = int64_t(ctx.r31.s32) * int64_t(ctx.r9.s32);
  ctx.r29.s64 = int16_t(PPC_LOAD_U16(ctx.r11.u32 + ctx.r29.u32));
  ctx.r31.s64 = ctx.r31.s32;
  ctx.r29.s64 = int64_t(ctx.r29.s32) * int64_t(ctx.r9.s32);
  Store64(base, ctx.r1.u32 + -152, ctx.r31.u64);
  ctx.f12.u64 = PPC_LOAD_U64(ctx.r1.u32 + -152);
  ctx.f12.f64 = double(ctx.f12.s64);
  ctx.fpscr.enableFlushModeUnconditional();
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  ctx.r31.s64 = ctx.r29.s32;
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  ctx.r29.s64 = 4;
  Store64(base, ctx.r1.u32 + -144, ctx.r31.u64);
  ctx.fpscr.disableFlushModeUnconditional();
  ctx.f11.u64 = PPC_LOAD_U64(ctx.r1.u32 + -144);
  ctx.f11.f64 = double(ctx.f11.s64);
  ctx.r11.s64 = ctx.r11.s64 + 2;
  ctx.f11.f64 = double(float(ctx.f11.f64));
  ctx.f0.f64 = double(float(ctx.f11.f64 * ctx.f0.f64));
  temp.f32 = float(ctx.f0.f64);
  Store32(base, ctx.r1.u32 + -160, temp.u32);
  ctx.f0.f64 = double(float(ctx.f12.f64));
  temp.u32 = ctx.r30.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v47.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v47.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v47.u32),
                             0xFF));
  ctx.f0.f64 = double(float(ctx.f0.f64 * ctx.f13.f64));
  temp.f32 = float(ctx.f0.f64);
  Store32(base, ctx.r1.u32 + -160, temp.u32);
  temp.u32 = ctx.r28.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v46.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v46.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v46.u32),
                             0xFF));
  ctx.fpscr.enableFlushModeUnconditional();
  simde_mm_store_ps(ctx.v46.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v46.f32)));
  simde_mm_store_ps(ctx.v12.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v47.f32)));
  simde_mm_store_ps(ctx.v11.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v46.f32),
                                    simde_mm_load_ps(ctx.v54.f32)));
  simde_mm_store_ps(ctx.v10.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v46.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  simde_mm_store_ps(
      ctx.v11.f32,
      simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v12.f32),
                                      simde_mm_load_ps(ctx.v0.f32)),
                      simde_mm_load_ps(ctx.v11.f32)));
  simde_mm_store_ps(
      ctx.v12.f32,
      simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v12.f32),
                                      simde_mm_load_ps(ctx.v13.f32)),
                      simde_mm_load_ps(ctx.v10.f32)));
  simde_mm_store_ps(ctx.v47.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v11.f32)));
  simde_mm_store_ps(ctx.v46.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v12.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v47.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v46.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v47.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v46.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v47.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v46.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v47.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v63.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v46.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v62.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  ea = ctx.r3.u32;
  StoreLeft(base, ea, ctx.v47.u8);
  ea = ctx.r3.u32 + ctx.r7.u32;
  StoreRight(base, ea, ctx.v47.u8);
  simde_mm_store_si128(
      (simde__m128i *)ctx.v45.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v46.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v47.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v46.u32),
                             0xAA));
  ctx.r3.s64 = ctx.r10.s64 + 8;
  ea = (ctx.r10.u32) & ~0x3;
  Store32(base, ea, ctx.v45.u32[3 - ((ea & 0xF) >> 2)]);
  ea = (ctx.r10.u32 + ctx.r29.u32) & ~0x3;
  Store32(base, ea, ctx.v47.u32[3 - ((ea & 0xF) >> 2)]);
  --ctx.ctr.u64;
  if (ctx.ctr.u32 != 0)
    goto loc_82D0FEB8;
  goto loc_82D100E4;
loc_82D0FFBC:
  ctx.r28.s64 = 0;
  ctx.cr6.compare<uint32_t>(ctx.r5.u32, 0, ctx.xer);
  if (ctx.cr6.eq)
    goto loc_82D100E4;
  ctx.r27.u64 = PPC_LOAD_U32(ctx.r1.u32 + 148);
  ctx.r21.s64 = -16;
  ctx.r26.u64 = PPC_LOAD_U32(ctx.r1.u32 + 140);
  ctx.r25.u64 = PPC_LOAD_U32(ctx.r1.u32 + 132);
loc_82D0FFD8:
  simde_mm_store_si128((simde__m128i *)ctx.v0.u8,
                       simde_mm_load_si128((simde__m128i *)ctx.v54.u8));
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v61.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v48.f32)));
  simde_mm_store_si128((simde__m128i *)ctx.v13.u8,
                       simde_mm_load_si128((simde__m128i *)ctx.v54.u8));
  ctx.cr6.compare<uint32_t>(ctx.r6.u32, 0, ctx.xer);
  if (ctx.cr6.eq)
    goto loc_82D1007C;
  ctx.r31.u64 =
      __builtin_rotateleft64(ctx.r28.u32 | (ctx.r28.u64 << 32), 1) & 0xFFFFFFFE;
  ctx.ctr.u64 = ctx.r6.u64;
  ctx.r10.u64 = ctx.r25.u64;
  ctx.r8.u64 = ctx.r27.u64;
  ctx.r11.s64 = ctx.r26.s64 + 16;
  ctx.r30.s64 = ctx.r27.s64 - ctx.r26.s64;
  ctx.r29.s64 = ctx.r4.s64 - ctx.r25.s64;
loc_82D10008:
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + ((ctx.r8.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  ctx.r24.s64 = ctx.r1.s64 + -160;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128(
              (simde__m128i *)(base + ((ctx.r11.u32 + ctx.r21.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  ctx.r8.s64 = ctx.r8.s64 + 32;
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v11.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v52.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128(
              (simde__m128i *)(base + ((ctx.r30.u32 + ctx.r11.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v51.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + ((ctx.r11.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  simde_mm_store_ps(ctx.v12.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v51.f32),
                                    simde_mm_load_ps(ctx.v52.f32)));
  StoreVector(
      base, ((ctx.r11.u32 + ctx.r21.u32) & ~0xFu),
      simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i *)ctx.v11.u8),
                            simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  StoreVector(
      base, ((ctx.r11.u32) & ~0xFu),
      simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i *)ctx.v12.u8),
                            simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  ctx.r11.s64 = ctx.r11.s64 + 32;
  ctx.fpscr.disableFlushModeUnconditional();
  temp.u32 = PPC_LOAD_U32(ctx.r10.u32 + 0);
  ctx.f0.f64 = double(temp.f32);
  ctx.r23.u64 = PPC_LOAD_U32(ctx.r29.u32 + ctx.r10.u32);
  ctx.r10.s64 = ctx.r10.s64 + 4;
  ctx.r23.s64 = int16_t(PPC_LOAD_U16(ctx.r23.u32 + ctx.r31.u32));
  ctx.r23.s64 = int64_t(ctx.r23.s32) * int64_t(ctx.r9.s32);
  ctx.r23.s64 = ctx.r23.s32;
  Store64(base, ctx.r1.u32 + -144, ctx.r23.u64);
  ctx.f13.u64 = PPC_LOAD_U64(ctx.r1.u32 + -144);
  ctx.f13.f64 = double(ctx.f13.s64);
  ctx.f13.f64 = double(float(ctx.f13.f64));
  ctx.f0.f64 = double(float(ctx.f13.f64 * ctx.f0.f64));
  temp.f32 = float(ctx.f0.f64);
  Store32(base, ctx.r1.u32 + -160, temp.u32);
  temp.u32 = ctx.r24.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v53.u32),
                             0xFF));
  ctx.fpscr.enableFlushModeUnconditional();
  simde_mm_store_ps(ctx.v10.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  simde_mm_store_ps(
      ctx.v0.f32,
      simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v10.f32),
                                      simde_mm_load_ps(ctx.v11.f32)),
                      simde_mm_load_ps(ctx.v0.f32)));
  simde_mm_store_ps(
      ctx.v13.f32,
      simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v10.f32),
                                      simde_mm_load_ps(ctx.v12.f32)),
                      simde_mm_load_ps(ctx.v13.f32)));
  --ctx.ctr.u64;
  if (ctx.ctr.u32 != 0)
    goto loc_82D10008;
loc_82D1007C:
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v13.f32)));
  ctx.r11.s64 = ctx.r3.s64 + 16;
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  ctx.r10.s64 = 4;
  simde_mm_store_ps(ctx.v52.f32, simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                                 simde_mm_load_ps(ctx.v0.f32)));
  ctx.r28.s64 = ctx.r28.s64 + 1;
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  ctx.cr6.compare<uint32_t>(ctx.r28.u32, ctx.r5.u32, ctx.xer);
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v53.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v52.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v52.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v53.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v52.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v62.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v52.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v63.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v51.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v53.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v53.u32),
                             0xAA));
  ea = ctx.r3.u32;
  StoreLeft(base, ea, ctx.v52.u8);
  ea = ctx.r3.u32 + ctx.r7.u32;
  StoreRight(base, ea, ctx.v52.u8);
  ctx.r3.s64 = ctx.r11.s64 + 8;
  ea = (ctx.r11.u32) & ~0x3;
  Store32(base, ea, ctx.v51.u32[3 - ((ea & 0xF) >> 2)]);
  ea = (ctx.r11.u32 + ctx.r10.u32) & ~0x3;
  Store32(base, ea, ctx.v53.u32[3 - ((ea & 0xF) >> 2)]);
  if (ctx.cr6.lt)
    goto loc_82D0FFD8;
loc_82D100E4:
  ctx.r10.s64 = 0;
  goto loc_82D104E4;
loc_82D100EC:
  ctx.cr6.compare<uint32_t>(ctx.r6.u32, 1, ctx.xer);
  if (!ctx.cr6.eq)
    goto loc_82D10200;
  ctx.r11.u64 = PPC_LOAD_U32(ctx.r1.u32 + 140);
  ctx.cr6.compare<uint32_t>(ctx.r5.u32, 0, ctx.xer);
  ctx.r6.u64 = PPC_LOAD_U32(ctx.r1.u32 + 148);
  temp.u32 = ctx.r11.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v54.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r6.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v52.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r11.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r6.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v51.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  if (ctx.cr6.eq)
    goto loc_82D104E4;
  ctx.ctr.u64 = ctx.r5.u64;
  ctx.r5.u64 = PPC_LOAD_U32(ctx.r1.u32 + 132);
loc_82D1011C:
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v61.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v48.f32)));
  simde_mm_store_ps(ctx.v54.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v54.f32),
                                    simde_mm_load_ps(ctx.v52.f32)));
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v53.f32),
                                    simde_mm_load_ps(ctx.v51.f32)));
  goto loc_82D10134;
loc_82D1012C:
  ctx.r10.s64 = ctx.r10.s64 - ctx.r9.s64;
  ctx.r25.s64 = ctx.r25.s64 + 1;
loc_82D10134:
  ctx.cr6.compare<int32_t>(ctx.r10.s32, ctx.r9.s32, ctx.xer);
  if (!ctx.cr6.lt)
    goto loc_82D1012C;
  ctx.r11.u64 = PPC_LOAD_U32(ctx.r4.u32 + 0);
  ctx.r6.u64 =
      __builtin_rotateleft64(ctx.r25.u32 | (ctx.r25.u64 << 32), 1) & 0xFFFFFFFE;
  ctx.r31.s64 = ctx.r9.s64 - ctx.r10.s64;
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  ctx.fpscr.disableFlushModeUnconditional();
  temp.u32 = PPC_LOAD_U32(ctx.r5.u32 + 0);
  ctx.f0.f64 = double(temp.f32);
  ctx.r11.u64 = ctx.r6.u64 + ctx.r11.u64;
  ctx.r30.s64 = ctx.r1.s64 + -160;
  ctx.r29.s64 = 4;
  ctx.r6.s64 = int16_t(PPC_LOAD_U16(ctx.r11.u32 + 0));
  ctx.r11.s64 = int16_t(PPC_LOAD_U16(ctx.r11.u32 + 2));
  ctx.r6.s64 = int64_t(ctx.r31.s32) * int64_t(ctx.r6.s32);
  ctx.r11.s64 = int64_t(ctx.r11.s32) * int64_t(ctx.r10.s32);
  ctx.r6.u64 = ctx.r6.u64 + ctx.r11.u64;
  ctx.r11.s64 = ctx.r3.s64 + 16;
  ctx.r6.s64 = ctx.r6.s32;
  ctx.r10.u64 = ctx.r8.u64 + ctx.r10.u64;
  Store64(base, ctx.r1.u32 + -144, ctx.r6.u64);
  ctx.f13.u64 = PPC_LOAD_U64(ctx.r1.u32 + -144);
  ctx.f13.f64 = double(ctx.f13.s64);
  ctx.f13.f64 = double(float(ctx.f13.f64));
  ctx.f0.f64 = double(float(ctx.f13.f64 * ctx.f0.f64));
  temp.f32 = float(ctx.f0.f64);
  Store32(base, ctx.r1.u32 + -160, temp.u32);
  temp.u32 = ctx.r30.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v50.u32),
                             0xFF));
  ctx.fpscr.enableFlushModeUnconditional();
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v50.f32)));
  simde_mm_store_ps(ctx.v49.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v54.f32)));
  simde_mm_store_ps(ctx.v49.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v49.f32)));
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v50.f32)));
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v49.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v49.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v50.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v49.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v50.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v62.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v49.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v63.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v47.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v50.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v50.u32),
                             0xAA));
  ea = ctx.r3.u32;
  StoreLeft(base, ea, ctx.v49.u8);
  ea = ctx.r3.u32 + ctx.r7.u32;
  StoreRight(base, ea, ctx.v49.u8);
  ctx.r3.s64 = ctx.r11.s64 + 8;
  ea = (ctx.r11.u32) & ~0x3;
  Store32(base, ea, ctx.v47.u32[3 - ((ea & 0xF) >> 2)]);
  ea = (ctx.r11.u32 + ctx.r29.u32) & ~0x3;
  Store32(base, ea, ctx.v50.u32[3 - ((ea & 0xF) >> 2)]);
  --ctx.ctr.u64;
  if (ctx.ctr.u32 != 0)
    goto loc_82D1011C;
  goto loc_82D104E4;
loc_82D10200:
  ctx.cr6.compare<uint32_t>(ctx.r6.u32, 2, ctx.xer);
  if (!ctx.cr6.eq)
    goto loc_82D10394;
  ctx.r6.u64 = PPC_LOAD_U32(ctx.r1.u32 + 140);
  ctx.r30.s64 = 48;
  ctx.r31.u64 = PPC_LOAD_U32(ctx.r1.u32 + 148);
  ctx.cr6.compare<uint32_t>(ctx.r5.u32, 0, ctx.xer);
  temp.u32 = ctx.r6.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v54.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r31.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v52.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r6.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r11.u32 + ctx.r6.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v13.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r30.u32 + ctx.r6.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v12.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r7.u32 + ctx.r31.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v51.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r11.u32 + ctx.r31.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v50.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  temp.u32 = ctx.r30.u32 + ctx.r31.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v49.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  if (ctx.cr6.eq)
    goto loc_82D104E4;
  ctx.r30.u64 = PPC_LOAD_U32(ctx.r1.u32 + 132);
  ctx.r31.s64 = ctx.r4.s64 + 4;
  ctx.ctr.u64 = ctx.r5.u64;
  ctx.r29.s64 = ctx.r30.s64 + 4;
loc_82D1024C:
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v61.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v48.f32)));
  simde_mm_store_ps(ctx.v54.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v54.f32),
                                    simde_mm_load_ps(ctx.v52.f32)));
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v53.f32),
                                    simde_mm_load_ps(ctx.v51.f32)));
  simde_mm_store_ps(ctx.v13.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v13.f32),
                                    simde_mm_load_ps(ctx.v50.f32)));
  simde_mm_store_ps(ctx.v12.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v12.f32),
                                    simde_mm_load_ps(ctx.v49.f32)));
  goto loc_82D1026C;
loc_82D10264:
  ctx.r10.s64 = ctx.r10.s64 - ctx.r9.s64;
  ctx.r25.s64 = ctx.r25.s64 + 1;
loc_82D1026C:
  ctx.cr6.compare<int32_t>(ctx.r10.s32, ctx.r9.s32, ctx.xer);
  if (!ctx.cr6.lt)
    goto loc_82D10264;
  ctx.r11.u64 = PPC_LOAD_U32(ctx.r4.u32 + 0);
  ctx.r6.u64 =
      __builtin_rotateleft64(ctx.r25.u32 | (ctx.r25.u64 << 32), 1) & 0xFFFFFFFE;
  ctx.r5.u64 = PPC_LOAD_U32(ctx.r31.u32 + 0);
  ctx.r28.s64 = ctx.r9.s64 - ctx.r10.s64;
  ctx.r11.u64 = ctx.r6.u64 + ctx.r11.u64;
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  ctx.fpscr.disableFlushModeUnconditional();
  temp.u32 = PPC_LOAD_U32(ctx.r30.u32 + 0);
  ctx.f0.f64 = double(temp.f32);
  ctx.r6.u64 = ctx.r6.u64 + ctx.r5.u64;
  temp.u32 = PPC_LOAD_U32(ctx.r29.u32 + 0);
  ctx.f13.f64 = double(temp.f32);
  ctx.r5.s64 = ctx.r1.s64 + -160;
  ctx.r27.s64 = ctx.r1.s64 + -160;
  ctx.r26.s64 = 4;
  ctx.r24.s64 = int16_t(PPC_LOAD_U16(ctx.r11.u32 + 2));
  ctx.r11.s64 = int16_t(PPC_LOAD_U16(ctx.r11.u32 + 0));
  ctx.r22.u64 = PPC_LOAD_U16(ctx.r6.u32 + 0);
  ctx.r11.s64 = int64_t(ctx.r11.s32) * int64_t(ctx.r28.s32);
  ctx.r23.u64 = PPC_LOAD_U16(ctx.r6.u32 + 2);
  ctx.r6.s64 = int64_t(ctx.r24.s32) * int64_t(ctx.r10.s32);
  ctx.r11.u64 = ctx.r6.u64 + ctx.r11.u64;
  ctx.r6.s64 = ctx.r23.s16;
  ctx.r11.s64 = ctx.r11.s32;
  ctx.r6.s64 = int64_t(ctx.r6.s32) * int64_t(ctx.r10.s32);
  Store64(base, ctx.r1.u32 + -144, ctx.r11.u64);
  ctx.r11.s64 = ctx.r22.s16;
  ctx.r10.u64 = ctx.r8.u64 + ctx.r10.u64;
  ctx.r11.s64 = int64_t(ctx.r11.s32) * int64_t(ctx.r28.s32);
  ctx.f12.u64 = PPC_LOAD_U64(ctx.r1.u32 + -144);
  ctx.r6.u64 = ctx.r6.u64 + ctx.r11.u64;
  ctx.f12.f64 = double(ctx.f12.s64);
  ctx.r11.s64 = ctx.r3.s64 + 16;
  ctx.r6.s64 = ctx.r6.s32;
  Store64(base, ctx.r1.u32 + -152, ctx.r6.u64);
  ctx.f11.u64 = PPC_LOAD_U64(ctx.r1.u32 + -152);
  ctx.f12.f64 = double(float(ctx.f12.f64));
  ctx.f0.f64 = double(float(ctx.f12.f64 * ctx.f0.f64));
  temp.f32 = float(ctx.f0.f64);
  Store32(base, ctx.r1.u32 + -160, temp.u32);
  temp.u32 = ctx.r5.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v47.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v47.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v47.u32),
                             0xFF));
  ctx.f11.f64 = double(ctx.f11.s64);
  ctx.f11.f64 = double(float(ctx.f11.f64));
  ctx.f0.f64 = double(float(ctx.f11.f64 * ctx.f13.f64));
  temp.f32 = float(ctx.f0.f64);
  Store32(base, ctx.r1.u32 + -160, temp.u32);
  temp.u32 = ctx.r27.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v46.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v46.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v46.u32),
                             0xFF));
  ctx.fpscr.enableFlushModeUnconditional();
  simde_mm_store_ps(ctx.v47.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v47.f32)));
  simde_mm_store_ps(ctx.v10.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v47.f32),
                                    simde_mm_load_ps(ctx.v54.f32)));
  simde_mm_store_ps(ctx.v11.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v47.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  simde_mm_store_ps(ctx.v0.f32, simde_mm_mul_ps(simde_mm_load_ps(ctx.v61.f32),
                                                simde_mm_load_ps(ctx.v46.f32)));
  simde_mm_store_ps(
      ctx.v10.f32,
      simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v0.f32),
                                      simde_mm_load_ps(ctx.v13.f32)),
                      simde_mm_load_ps(ctx.v10.f32)));
  simde_mm_store_ps(
      ctx.v0.f32,
      simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v0.f32),
                                      simde_mm_load_ps(ctx.v12.f32)),
                      simde_mm_load_ps(ctx.v11.f32)));
  simde_mm_store_ps(ctx.v47.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v10.f32)));
  simde_mm_store_ps(ctx.v46.f32, simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                                 simde_mm_load_ps(ctx.v0.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v47.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v46.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v47.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v46.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v47.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v46.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v47.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v63.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v46.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v62.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  ea = ctx.r3.u32;
  StoreLeft(base, ea, ctx.v47.u8);
  ea = ctx.r3.u32 + ctx.r7.u32;
  StoreRight(base, ea, ctx.v47.u8);
  simde_mm_store_si128(
      (simde__m128i *)ctx.v45.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v46.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v47.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v46.u32),
                             0xAA));
  ctx.r3.s64 = ctx.r11.s64 + 8;
  ea = (ctx.r11.u32) & ~0x3;
  Store32(base, ea, ctx.v45.u32[3 - ((ea & 0xF) >> 2)]);
  ea = (ctx.r11.u32 + ctx.r26.u32) & ~0x3;
  Store32(base, ea, ctx.v47.u32[3 - ((ea & 0xF) >> 2)]);
  --ctx.ctr.u64;
  if (ctx.ctr.u32 != 0)
    goto loc_82D1024C;
  goto loc_82D104E4;
loc_82D10394:
  ctx.cr6.compare<uint32_t>(ctx.r5.u32, 0, ctx.xer);
  if (ctx.cr6.eq)
    goto loc_82D104E4;
  ctx.r23.u64 = PPC_LOAD_U32(ctx.r1.u32 + 148);
  ctx.r20.u64 = ctx.r5.u64;
  ctx.r22.u64 = PPC_LOAD_U32(ctx.r1.u32 + 140);
  ctx.r21.s64 = -16;
loc_82D103AC:
  simde_mm_store_si128((simde__m128i *)ctx.v0.u8,
                       simde_mm_load_si128((simde__m128i *)ctx.v54.u8));
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v61.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v48.f32)));
  simde_mm_store_si128((simde__m128i *)ctx.v13.u8,
                       simde_mm_load_si128((simde__m128i *)ctx.v54.u8));
  goto loc_82D103C4;
loc_82D103BC:
  ctx.r10.s64 = ctx.r10.s64 - ctx.r9.s64;
  ctx.r25.s64 = ctx.r25.s64 + 1;
loc_82D103C4:
  ctx.cr6.compare<int32_t>(ctx.r10.s32, ctx.r9.s32, ctx.xer);
  if (!ctx.cr6.lt)
    goto loc_82D103BC;
  ctx.cr6.compare<uint32_t>(ctx.r6.u32, 0, ctx.xer);
  if (ctx.cr6.eq)
    goto loc_82D1047C;
  ctx.r30.u64 = PPC_LOAD_U32(ctx.r1.u32 + 132);
  ctx.r29.s64 = ctx.r9.s64 - ctx.r10.s64;
  ctx.ctr.u64 = ctx.r6.u64;
  ctx.r28.u64 =
      __builtin_rotateleft64(ctx.r25.u32 | (ctx.r25.u64 << 32), 1) & 0xFFFFFFFE;
  ctx.r5.u64 = ctx.r4.u64;
  ctx.r31.u64 = ctx.r23.u64;
  ctx.r11.s64 = ctx.r22.s64 + 16;
  ctx.r27.s64 = ctx.r23.s64 - ctx.r22.s64;
  ctx.r26.s64 = ctx.r30.s64 - ctx.r4.s64;
loc_82D103F8:
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + ((ctx.r31.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  ctx.r17.s64 = ctx.r1.s64 + -160;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v52.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128(
              (simde__m128i *)(base + ((ctx.r11.u32 + ctx.r21.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  ctx.r31.s64 = ctx.r31.s64 + 32;
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v11.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v52.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128(
              (simde__m128i *)(base + ((ctx.r11.u32 + ctx.r27.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v52.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + ((ctx.r11.u32) & ~0xF))),
          simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  simde_mm_store_ps(ctx.v12.f32,
                    simde_mm_add_ps(simde_mm_load_ps(ctx.v52.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  StoreVector(
      base, ((ctx.r11.u32 + ctx.r21.u32) & ~0xFu),
      simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i *)ctx.v11.u8),
                            simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  StoreVector(
      base, ((ctx.r11.u32) & ~0xFu),
      simde_mm_shuffle_epi8(simde_mm_load_si128((simde__m128i *)ctx.v12.u8),
                            simde_mm_load_si128((simde__m128i *)VectorMaskL)));
  ctx.r11.s64 = ctx.r11.s64 + 32;
  temp.u32 = PPC_LOAD_U32(ctx.r26.u32 + ctx.r5.u32);
  ctx.f0.f64 = double(temp.f32);
  ctx.r30.u64 = PPC_LOAD_U32(ctx.r5.u32 + 0);
  ctx.r5.s64 = ctx.r5.s64 + 4;
  ctx.r30.u64 = ctx.r28.u64 + ctx.r30.u64;
  ctx.r24.s64 = int16_t(PPC_LOAD_U16(ctx.r30.u32 + 2));
  ctx.r30.s64 = int16_t(PPC_LOAD_U16(ctx.r30.u32 + 0));
  ctx.r30.s64 = int64_t(ctx.r30.s32) * int64_t(ctx.r29.s32);
  ctx.r24.s64 = int64_t(ctx.r24.s32) * int64_t(ctx.r10.s32);
  ctx.r30.u64 = ctx.r24.u64 + ctx.r30.u64;
  ctx.r30.s64 = ctx.r30.s32;
  Store64(base, ctx.r1.u32 + -144, ctx.r30.u64);
  ctx.fpscr.disableFlushModeUnconditional();
  ctx.f13.u64 = PPC_LOAD_U64(ctx.r1.u32 + -144);
  ctx.f13.f64 = double(ctx.f13.s64);
  ctx.f13.f64 = double(float(ctx.f13.f64));
  ctx.f0.f64 = double(float(ctx.f13.f64 * ctx.f0.f64));
  temp.f32 = float(ctx.f0.f64);
  Store32(base, ctx.r1.u32 + -160, temp.u32);
  temp.u32 = ctx.r17.u32;
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u8,
      simde_mm_shuffle_epi8(
          simde_mm_load_si128((simde__m128i *)(base + (temp.u32 & ~0xF))),
          simde_mm_load_si128(
              (simde__m128i *)&VectorMaskL[(temp.u32 & 0xF) * 16])));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v53.u32),
                             0xFF));
  ctx.fpscr.enableFlushModeUnconditional();
  simde_mm_store_ps(ctx.v10.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v61.f32),
                                    simde_mm_load_ps(ctx.v53.f32)));
  simde_mm_store_ps(
      ctx.v0.f32,
      simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v10.f32),
                                      simde_mm_load_ps(ctx.v11.f32)),
                      simde_mm_load_ps(ctx.v0.f32)));
  simde_mm_store_ps(
      ctx.v13.f32,
      simde_mm_add_ps(simde_mm_mul_ps(simde_mm_load_ps(ctx.v10.f32),
                                      simde_mm_load_ps(ctx.v12.f32)),
                      simde_mm_load_ps(ctx.v13.f32)));
  --ctx.ctr.u64;
  if (ctx.ctr.u32 != 0)
    goto loc_82D103F8;
loc_82D1047C:
  ctx.fpscr.enableFlushMode();
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                    simde_mm_load_ps(ctx.v13.f32)));
  ctx.r11.s64 = ctx.r3.s64 + 16;
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  ctx.r5.s64 = 4;
  simde_mm_store_ps(ctx.v52.f32, simde_mm_mul_ps(simde_mm_load_ps(ctx.v58.f32),
                                                 simde_mm_load_ps(ctx.v0.f32)));
  ctx.xer.ca = ctx.r20.u32 > 0;
  ctx.r20.s64 = ctx.r20.s64 + -1;
  ctx.cr0.compare<int32_t>(ctx.r20.s32, 0, ctx.xer);
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v57.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  ctx.r10.u64 = ctx.r8.u64 + ctx.r10.u64;
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v55.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v62.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v53.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v63.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v52.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v62.f32)));
  simde_mm_store_ps(ctx.v52.f32,
                    simde_mm_mul_ps(simde_mm_load_ps(ctx.v56.f32),
                                    simde_mm_load_ps(ctx.v63.f32)));
  simde_mm_store_ps(ctx.v59.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v53.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v60.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v52.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_ps(ctx.v53.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v62.f32),
                                    simde_mm_load_ps(ctx.v59.f32)));
  simde_mm_store_ps(ctx.v52.f32,
                    simde_mm_sub_ps(simde_mm_load_ps(ctx.v63.f32),
                                    simde_mm_load_ps(ctx.v60.f32)));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v51.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v53.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v53.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v53.u32),
                             0xAA));
  ea = ctx.r3.u32;
  StoreLeft(base, ea, ctx.v52.u8);
  ea = ctx.r3.u32 + ctx.r7.u32;
  StoreRight(base, ea, ctx.v52.u8);
  ctx.r3.s64 = ctx.r11.s64 + 8;
  ea = (ctx.r11.u32) & ~0x3;
  Store32(base, ea, ctx.v51.u32[3 - ((ea & 0xF) >> 2)]);
  ea = (ctx.r11.u32 + ctx.r5.u32) & ~0x3;
  Store32(base, ea, ctx.v53.u32[3 - ((ea & 0xF) >> 2)]);
  if (!ctx.cr0.eq)
    goto loc_82D103AC;
loc_82D104E4:
  simde_mm_store_si128(
      (simde__m128i *)ctx.v61.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v62.u32),
                             0xFF));
  ea = ctx.r19.u32;
  StoreLeft(base, ea, ctx.v63.u8);
  simde_mm_store_si128(
      (simde__m128i *)ctx.v62.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v62.u32),
                             0xAA));
  ctx.r11.s64 = 20;
  ea = ctx.r19.u32 + ctx.r7.u32;
  StoreRight(base, ea, ctx.v63.u8);
  simde_mm_store_si128(
      (simde__m128i *)ctx.v58.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v59.u32),
                             0xFF));
  simde_mm_store_si128(
      (simde__m128i *)ctx.v63.u32,
      simde_mm_shuffle_epi32(simde_mm_load_si128((simde__m128i *)ctx.v59.u32),
                             0xAA));
  ctx.r3.u64 = ctx.r10.u64;
  ea = (ctx.r19.u32 + ctx.r7.u32) & ~0x3;
  Store32(base, ea, ctx.v61.u32[3 - ((ea & 0xF) >> 2)]);
  ea = (ctx.r19.u32 + ctx.r11.u32) & ~0x3;
  Store32(base, ea, ctx.v62.u32[3 - ((ea & 0xF) >> 2)]);
  ea = ctx.r18.u32;
  StoreLeft(base, ea, ctx.v60.u8);
  ea = ctx.r18.u32 + ctx.r7.u32;
  StoreRight(base, ea, ctx.v60.u8);
  ea = (ctx.r18.u32 + ctx.r7.u32) & ~0x3;
  Store32(base, ea, ctx.v58.u32[3 - ((ea & 0xF) >> 2)]);
  ea = (ctx.r18.u32 + ctx.r11.u32) & ~0x3;
  Store32(base, ea, ctx.v63.u32[3 - ((ea & 0xF) >> 2)]);
  __restgprlr_17(ctx, base);
  return;
}
} // namespace ttt2_audio_mixer
