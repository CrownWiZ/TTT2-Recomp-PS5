#include "ppc_store_guard_linux.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#undef memset

static void Write(PPCContext &ctx, uint8_t *base, uint32_t address,
                  const std::array<uint8_t, 16> &pattern, uint8_t *scratch) {
  const auto value = simde_mm_loadu_si128(
      reinterpret_cast<const simde__m128i *>(pattern.data()));
  simde_mm_store_si128(reinterpret_cast<simde__m128i *>(ctx.v0.u8), value);
  simde_mm_store_si128(reinterpret_cast<simde__m128i *>(ctx.v127.u8), value);
  simde_mm_store_si128(reinterpret_cast<simde__m128i *>(scratch), value);
  simde_mm_store_si128(reinterpret_cast<simde__m128i *>(base + address), value);
}

int main() {
  constexpr size_t size = 0x20000;
  auto memory = std::unique_ptr<uint8_t, decltype(&std::free)>(
      static_cast<uint8_t *>(std::aligned_alloc(32, size)), &std::free);
  if (!memory)
    return 2;
  std::array<uint8_t, size> expected;
  std::array<uint8_t, 16> pattern;
  unsigned cases = 0;
  for (unsigned patternIndex = 0; patternIndex < 4; ++patternIndex) {
    for (unsigned i = 0; i < pattern.size(); ++i)
      pattern[i] = static_cast<uint8_t>((i * 37 + patternIndex * 71) & 255);
    // Aligned vector writes throughout protected memory and ordinary memory,
    // plus every partial-store boundary where only the suffix is writable.
    for (unsigned address = 0; address <= size - 16; ++address) {
      if (address % 16 != 0 && !(address > 0xFFF0 && address < 0x10000))
        continue;
      std::memset(memory.get(), 0xA5, size);
      expected.fill(0xA5);
      PPCContext ctx, expectedContext;
      std::memset(&ctx, 0x5A, sizeof(ctx));
      std::memcpy(&expectedContext, &ctx, sizeof(ctx));
      std::memcpy(expectedContext.v0.u8, pattern.data(), 16);
      std::memcpy(expectedContext.v127.u8, pattern.data(), 16);
      alignas(16) std::array<uint8_t, 16> scratch{};
      for (unsigned i = 0; i < 16; ++i)
        if (address + i >= 0x10000)
          expected[address + i] = pattern[i];
      Write(ctx, memory.get(), address, pattern, scratch.data());
      if (std::memcmp(memory.get(), expected.data(), size) ||
          std::memcmp(&ctx, &expectedContext, sizeof(ctx)) ||
          scratch != pattern) {
        std::fprintf(stderr, "FAIL address=%x pattern=%u\n", address,
                     patternIndex);
        return 1;
      }
      ++cases;
    }
  }
  std::printf("PASS %u vector-store cases: protected guest memory, partial "
              "boundary, ordinary guest memory, host registers and scratch\n",
              cases);
}
