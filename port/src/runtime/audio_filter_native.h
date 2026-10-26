#pragma once
#include "guest_memory.h"
#include "ppc_recomp_shared.h"
#include <bit>

// Comb-style delay filter at 0x82D0DB08. Preserve generated PPC arithmetic,
// register results and scratch writes while avoiding redundant reloads.
namespace ttt2_audio {
inline uint32_t Read(uint8_t *base, uint32_t address) {
  return __builtin_bswap32(
      *reinterpret_cast<const volatile uint32_t *>(base + address));
}
inline double ReadFloat(uint8_t *base, uint32_t address) {
  return double(std::bit_cast<float>(Read(base, address)));
}
inline void Write(uint8_t *base, uint32_t address, uint32_t value) {
  guest_memory::StoreScalar(base, address, __builtin_bswap32(value));
}
inline void WriteFloat(uint8_t *base, uint32_t address, double value) {
  Write(base, address, std::bit_cast<uint32_t>(float(value)));
}
inline bool RunCombFilter(PPCContext &ctx, uint8_t *base) {
  // Forward only stores that cannot be swallowed by guest low-memory
  // protection.
  const uint32_t state = ctx.r3.u32;
  const uint32_t scratch = ctx.r1.u32 - 16;
  if (state < 0x10000 || state > 0xFFFFFFE0 || scratch < 0x10000 ||
      scratch > 0xFFFFFFFC)
    return false;
  const uint32_t offset = Read(base, state + 24) << 2;
  const uint32_t buffer = Read(base, state + 16);
  ctx.fpscr.disableFlushMode();
  const double zero = ReadFloat(base, 0x820F19F4);
  double sample = ReadFloat(base, buffer + offset);
  const uint32_t sampleBits = std::bit_cast<uint32_t>(float(sample));
  Write(base, scratch, sampleBits);
  ctx.cr0.compare<int32_t>(sampleBits & 0x7F800000u, 0, ctx.xer);
  if (ctx.cr0.eq)
    sample = zero;

  // Preserve the original double intermediates and single-precision rounding
  // points. Build this function with floating-point contraction disabled.
  double filtered = double(float(ReadFloat(base, state + 12) * sample));
  const double previous = ReadFloat(base, state + 4);
  const double feedback = ReadFloat(base, state + 8);
  filtered = double(float(feedback * previous + filtered));
  const uint32_t filteredBits = std::bit_cast<uint32_t>(float(filtered));
  Write(base, state + 4, filteredBits);
  const uint32_t exponent = filteredBits & 0x7F800000u;
  ctx.cr0.compare<int32_t>(exponent, 0, ctx.xer);
  if (ctx.cr0.eq) {
    filtered = zero;
    WriteFloat(base, state + 4, zero);
  }
  const double gain = ReadFloat(base, state);
  const double output = double(float(filtered * gain + ctx.f1.f64));
  WriteFloat(base, buffer + offset, output);
  const uint32_t length = Read(base, state + 20);
  uint64_t next = uint64_t(Read(base, state + 24)) + 1;
  Write(base, state + 24, uint32_t(next));
  ctx.cr6.compare<int32_t>(int32_t(uint32_t(next)), int32_t(length), ctx.xer);
  if (!ctx.cr6.lt) {
    next = 0;
    Write(base, state + 24, 0);
  }
  ctx.r9.u64 = exponent;
  ctx.r10.u64 = length;
  ctx.r11.u64 = next;
  ctx.f0.f64 = sample;
  ctx.f1.f64 = sample;
  ctx.f10.f64 = feedback;
  ctx.f11.f64 = previous;
  ctx.f12.f64 = gain;
  ctx.f13.f64 = output;
  return true;
}
} // namespace ttt2_audio
