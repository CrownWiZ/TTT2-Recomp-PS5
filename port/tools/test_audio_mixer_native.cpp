#include "audio_mixer_native.h"
using ttt2_audio_mixer::StoreLeft;
using ttt2_audio_mixer::StoreRight;
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <sys/mman.h>
extern "C" PPC_FUNC(mix_reference);
PPC_FUNC_IMPL(mix_candidate) { ttt2_audio_mixer::Run(ctx, base); }
constexpr size_t bytes = 1 << 20;
constexpr uint32_t mask = uint32_t(-2111897600 + 7184);
void Put(uint8_t *b, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(b + a, &v, 4);
}
void Prepare(uint8_t *b, PPCContext &c, unsigned channels, unsigned count,
             unsigned skew, unsigned rate, unsigned round, bool edge = false) {
  std::memset(b, 0, bytes);
  std::memset(&c, 0, sizeof(c));
  c.fpscr.loadFromHost();
  c.fpscr.storeFromGuest(0);
  std::mt19937 rng(774);
  auto f = [&]() {
    static constexpr uint32_t e[] = {0,          0x80000000, 1,
                                     0x80000001, 0x7fc00001, 0x7f800000,
                                     0xff800000, 0x7f7fffff};
    return edge && rng() % 5 == 0
               ? e[rng() % 8]
               : std::bit_cast<uint32_t>(float(int(rng() % 1001) - 500) / 1000);
  };
  for (unsigned i = 0; i < 0x5000; i += 4)
    Put(b, 0x22000 + i, f());
  for (unsigned ch = 0; ch < 8; ++ch) {
    Put(b, 0x20000 + ch * 4, 0x30000 + ch * 0x1000);
    for (unsigned i = 0; i < 1024; ++i) {
      uint16_t v = __builtin_bswap16(uint16_t(rng()));
      std::memcpy(b + 0x30000 + ch * 0x1000 + i * 2, &v, 2);
    }
  }
  c.r1.u64 = 0x80000;
  c.r3.u64 = 0x40000 + skew;
  c.r4.u64 = 0x20000;
  c.r5.u64 = count;
  c.r6.u64 = channels;
  c.r8.u64 = rate;
  c.r9.u64 = 128;
  c.r10.u64 = 0;
  Put(b, 0x80000 + 132, 0x22000);
  Put(b, 0x80000 + 140, 0x23000);
  Put(b, 0x80000 + 148, 0x24000);
  Put(b, 0x80000 + 156, 0x25000 + skew);
  Put(b, 0x80000 + 164, 0x26000 + skew);
  c.f1.f64 = .2;
  c.f2.f64 = .3;
  c.f3.f64 = .4;
  c.f4.f64 = .5;
  c.f5.f64 = .6;
  c.f6.f64 = .001;
  if (edge) {
    c.f1.f64 = double(std::bit_cast<float>(f()));
    c.f2.f64 = double(std::bit_cast<float>(f()));
    c.f3.f64 = double(std::bit_cast<float>(f()));
    c.f4.f64 = double(std::bit_cast<float>(f()));
    c.f5.f64 = double(std::bit_cast<float>(f()));
    c.f6.f64 = double(std::bit_cast<float>(f()));
  }
  for (unsigned i = 0; i < 16; ++i)
    b[mask + i] = i < 8 ? 0xff : 0;
  c.fpscr.loadFromHost();
  c.fpscr.storeFromGuest(round);
}
int main() {
  std::array<uint8_t *, 2> b;
  for (auto &p : b) {
    p = (uint8_t *)mmap(nullptr, size_t{1} << 32, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
      return 2;
  }
  unsigned cases = 0;
  alignas(16) uint8_t src[16];
  for (unsigned i = 0; i < 16; ++i)
    src[i] = i * 11;
  for (uint32_t addr : {0u, 0xffe0u, 0xfff0u, 0x10000u, 0x10010u, 0x2fff0u})
    for (unsigned skew = 0; skew < 16; ++skew)
      for (unsigned right = 0; right < 2; ++right) {
        std::memset(b[0], 0x55, bytes);
        std::memset(b[1], 0x55, bytes);
        uint32_t ea = addr + skew;
        if (right) {
          for (unsigned i = 0; i < (ea & 15); ++i)
            guest_memory::StoreScalar(b[0], uint32_t(ea - i - 1), src[i]);
          StoreRight(b[1], ea, src);
        } else {
          for (unsigned i = 0; i < 16 - (ea & 15); ++i)
            guest_memory::StoreScalar(b[0], uint32_t(ea + i), src[15 - i]);
          StoreLeft(b[1], ea, src);
        }
        if (std::memcmp(b[0], b[1], bytes)) {
          std::printf("helper FAIL %x %u\n", ea, right);
          return 3;
        }
        ++cases;
      }
  std::printf("PASS %u partial vector stores\n", cases);
  cases = 0;
  for (unsigned channels : {0u, 1u, 2u, 3u, 6u})
    for (unsigned count : {0u, 1u, 2u, 32u, 256u})
      for (unsigned skew = 0; skew < 16; ++skew)
        for (unsigned rate : {96u, 128u, 160u})
          for (unsigned round = 0; round < 4; ++round)
            for (bool edges : {false, true}) {
              PPCContext a, c;
              Prepare(b[0], a, channels, count, skew, rate, round, edges);
              Prepare(b[1], c, channels, count, skew, rate, round, edges);
              a.fpscr.setcsr(a.fpscr.csr);
              mix_reference(a, b[0]);
              auto acsr = a.fpscr.getcsr();
              c.fpscr.setcsr(c.fpscr.csr);
              mix_candidate(c, b[1]);
              auto ccsr = c.fpscr.getcsr();
              if (std::memcmp(&a, &c, sizeof(a)) ||
                  std::memcmp(b[0], b[1], bytes) || acsr != ccsr) {
                size_t i = 0;
                while (i < sizeof(a) && ((char *)&a)[i] == ((char *)&c)[i])
                  ++i;
                size_t j = 0;
                while (j < bytes && b[0][j] == b[1][j])
                  ++j;
                std::printf("FAIL ch=%u count=%u skew=%u rate=%u round=%u "
                            "edge=%u ctx=%zu mem=%zx csr=%x/%x\n",
                            channels, count, skew, rate, round, edges, i, j,
                            acsr, ccsr);
                return 4;
              }
              ++cases;
            }
  std::printf("PASS %u full mixer cases: memory, context, FP control\n", cases);
  // Repeated calls exercise persistent filter state, not just isolated inputs.
  for (unsigned channels : {1u, 2u, 6u})
    for (unsigned rate : {96u, 128u, 160u})
      for (unsigned round = 0; round < 4; ++round) {
        PPCContext initial, a, c;
        Prepare(b[0], initial, channels, 128, 8, rate, round);
        std::memcpy(b[1], b[0], bytes);
        std::memcpy(b[1] + mask, b[0] + mask, 16);
        for (unsigned block = 0; block < 64; ++block) {
          std::memcpy(&a, &initial, sizeof(a));
          std::memcpy(&c, &initial, sizeof(c));
          a.fpscr.setcsr(a.fpscr.csr);
          mix_reference(a, b[0]);
          auto acsr = a.fpscr.getcsr();
          c.fpscr.setcsr(c.fpscr.csr);
          mix_candidate(c, b[1]);
          auto ccsr = c.fpscr.getcsr();
          if (std::memcmp(&a, &c, sizeof(a)) ||
              std::memcmp(b[0], b[1], bytes) || acsr != ccsr) {
            std::printf("REPEAT FAIL ch=%u rate=%u round=%u block=%u\n",
                        channels, rate, round, block);
            return 5;
          }
        }
      }
  std::puts("PASS 2304 persistent-state mixer calls");
  for (unsigned channels : {1u, 2u, 6u})
    for (unsigned rate : {96u, 128u})
      for (unsigned round = 0; round < 6; ++round)
        for (unsigned i = 0; i < 2; ++i) {
          bool candidate = (round % 2) ? !i : i;
          PPCContext initial, c;
          Prepare(b[0], initial, channels, 128, 0, rate, 0);
          auto fn = candidate ? mix_candidate : mix_reference;
          auto start = std::chrono::steady_clock::now();
          for (unsigned k = 0; k < 10000; ++k) {
            std::memcpy(&c, &initial, sizeof(c));
            c.fpscr.setcsr(c.fpscr.csr);
            fn(c, b[0]);
          }
          std::printf("BENCH ch=%u rate=%u candidate=%u round=%u ns=%.3f\n",
                      channels, rate, candidate, round,
                      std::chrono::duration<double, std::nano>(
                          std::chrono::steady_clock::now() - start)
                              .count() /
                          10000);
        }
}
