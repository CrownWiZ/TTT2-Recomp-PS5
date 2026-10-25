#include "audio_filter_native.h"
#include <array>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>
#include <sys/mman.h>
extern "C" PPC_FUNC(filter_reference);
PPC_FUNC_IMPL(filter_candidate) {
  if (!ttt2_audio::RunCombFilter(ctx, base))
    filter_reference(ctx, base);
}
constexpr size_t bytes = 1 << 20;
void Put(uint8_t *b, uint32_t a, uint32_t v) {
  v = __builtin_bswap32(v);
  std::memcpy(b + a, &v, 4);
}
int main() {
  std::array<uint8_t *, 2> b;
  for (auto &p : b) {
    p = (uint8_t *)mmap(nullptr, size_t{1} << 32, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED)
      return 2;
  }
  std::mt19937 random(9981);
  constexpr uint32_t edge[] = {0,          0x80000000, 1,          0x80000001,
                               0x007FFFFF, 0x7f800000, 0xff800000, 0x7fc00001,
                               0x7fa12345, 0x7f7fffff, 0xff7fffff};
  for (unsigned trial = 0; trial < 4096; ++trial) {
    std::memset(b[0], 0, bytes);
    uint32_t state = trial % 17 == 0 ? 0xfff0 : 0x20000;
    const uint32_t ring = trial % 5 == 0   ? state
                          : trial % 5 == 1 ? state + 4
                          : trial % 5 == 2 ? 0xfff0
                                           : 0x30000;
    uint32_t stack = trial % 7 == 0   ? state + 40
                     : trial % 7 == 1 ? state + 20
                     : trial % 7 == 2 ? 0x10008
                                      : 0x80000;
    auto word = [&]() {
      return trial >= 2048 && random() % 3 == 0
                 ? edge[random() % std::size(edge)]
                 : std::bit_cast<uint32_t>(float(int(random() % 4001) - 2000) /
                                           1000);
    };
    for (unsigned i = 0; i < 128; ++i)
      Put(b[0], ring + i * 4, word());
    for (unsigned i = 0; i < 4; ++i)
      Put(b[0], state + i * 4, word());
    Put(b[0], state + 16, ring);
    Put(b[0], state + 20, 1 + random() % 64);
    Put(b[0], state + 24, random() % 64);
    Put(b[0], 0x820F19F4, 0);
    std::memcpy(b[1], b[0], bytes);
    Put(b[1], 0x820F19F4, 0);
    PPCContext original, a, c;
    std::memset(&original, 0, sizeof(original));
    original.r3.u64 = state;
    original.r1.u64 = stack;
    original.r31.u64 = 0x12345678;
    original.f7.f64 = 0.123;
    original.f1.f64 =
        double(std::bit_cast<float>(word())) + (trial % 11 ? 0 : 0.000000001);
    original.fpscr.loadFromHost();
    original.fpscr.storeFromGuest(trial % 4);
    if (trial % 2)
      original.fpscr.enableFlushMode();
    std::memcpy(&a, &original, sizeof(a));
    std::memcpy(&c, &original, sizeof(c));
    a.fpscr.setcsr(a.fpscr.csr);
    filter_reference(a, b[0]);
    auto acsr = a.fpscr.getcsr();
    c.fpscr.setcsr(c.fpscr.csr);
    filter_candidate(c, b[1]);
    auto ccsr = c.fpscr.getcsr();
    if (std::memcmp(&a, &c, sizeof(a)) || std::memcmp(b[0], b[1], bytes) ||
        acsr != ccsr) {
      auto *ap = (uint8_t *)&a;
      auto *cp = (uint8_t *)&c;
      size_t at = 0;
      while (at < sizeof(a) && ap[at] == cp[at])
        ++at;
      size_t mem = 0;
      while (mem < bytes && b[0][mem] == b[1][mem])
        ++mem;
      std::printf("FAIL trial=%u ctx=%zu/%zu mem=%zx csr=%x/%x state=%x "
                  "ring=%x stack=%x\n",
                  trial, at, sizeof(a), mem, acsr, ccsr, state, ring, stack);
      return 1;
    }
  }
  std::puts("PASS 4096 filters: full context, memory and FP control; aliases, "
            "low-memory guards, IEEE edge values, four rounding modes");
  for (unsigned mode = 0; mode < 4; ++mode) {
    auto *base = b[0];
    std::memset(base, 0, bytes);
    Put(base, 0x820F19F4, 0);
    constexpr uint32_t state = 0x20000, ring = 0x30000;
    Put(base, state, std::bit_cast<uint32_t>(.7f));
    Put(base, state + 4, std::bit_cast<uint32_t>(.2f));
    Put(base, state + 8, std::bit_cast<uint32_t>(.3f));
    Put(base, state + 12, std::bit_cast<uint32_t>(.4f));
    Put(base, state + 16, ring);
    Put(base, state + 20, 64);
    Put(base, state + 24, 0);
    PPCContext c{};
    c.r1.u64 = 0x80000;
    c.r3.u64 = state;
    c.f1.f64 = .1;
    c.fpscr.loadFromHost();
    c.fpscr.storeFromGuest(0);
    auto fn = (mode == 1 || mode == 2) ? filter_candidate : filter_reference;
    auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; i < 1000000; ++i) {
      c.f1.f64 = .1;
      fn(c, base);
    }
    std::printf("candidate=%u ns/call=%.3f\n", mode == 1 || mode == 2,
                std::chrono::duration<double, std::nano>(
                    std::chrono::steady_clock::now() - start)
                        .count() /
                    1000000);
  }
}
