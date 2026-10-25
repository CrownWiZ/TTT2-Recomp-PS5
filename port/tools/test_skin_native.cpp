#include "skin_four_bone_native.h"
#undef memset
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
extern "C" PPC_FUNC(__imp__sub_82B10A70);
extern "C" PPC_FUNC(__imp__sub_82B10F40);
// ABI save/restore helpers do not affect vertex output in this isolated test.
PPC_FUNC(__savegprlr_16) {}
PPC_FUNC(__restgprlr_16) {}
PPC_FUNC(__savegprlr_22) {}
PPC_FUNC(__restgprlr_22) {}
static constexpr uint32_t Size = 4 << 20, Job = 0x10000, A = 0x20000, B = 0x30000, Input = 0x40000,
                          Output = 0x60000;
static std::mt19937 rng(124701);
static void Put(uint8_t *b, uint32_t a, float f)
{
    GWrite32(b, a, std::bit_cast<uint32_t>(f));
}
static float Sample(bool edges)
{
    constexpr uint32_t exceptional[] = {0,          0x80000000, 1,          0x80000001,
                                        0x7f800000, 0xff800000, 0x7fc00001, 0x7fa12345};
    if (edges && rng() % 5 == 0)
        return std::bit_cast<float>(exceptional[rng() % 8]);
    return float(int(rng() % 4001) - 2000) / 1000;
}
template <unsigned Fields>
void Prepare(uint8_t *b, unsigned count, unsigned boneBase, unsigned skew, unsigned destination,
             bool edges)
{
    std::memset(b, 0, Size);
    GWrite32(b, Job + 4, A);
    GWrite32(b, Job + 8, B);
    GWrite16(b, Job + 12, boneBase);
    GWrite32(b, Job + 16, destination);
    GWrite32(b, Job + 20, Input + skew);
    GWrite32(b, Job + 24, count);
    for (unsigned i = 0; i < 128 * 16; ++i)
    {
        Put(b, A + i * 4, Sample(edges));
        Put(b, B + i * 4, Sample(edges));
    }
    for (unsigned v = 0; v < count; ++v)
    {
        const auto rec = Input + skew + v * (Fields + 2) * 16;
        for (unsigned k = 0; k < Fields * 4; ++k)
            Put(b, rec + k * 4, Sample(edges));
        for (unsigned k = 0; k < 4; ++k)
        {
            GWrite32(b, rec + Fields * 16 + k * 4, boneBase + rng() % 128);
            Put(b, rec + (Fields + 1) * 16 + k * 4, Sample(edges));
        }
    }
}
template <unsigned Fields>
__attribute__((noinline)) void Candidate(PPCContext &c, uint8_t *b, uint8_t *d)
{
    ttt2_skin::RunFourBone<Fields>(c, b, Job, d);
}
template <unsigned Fields> void Reference(PPCContext &c, uint8_t *b)
{
    if constexpr (Fields == 2)
        __imp__sub_82B10A70(c, b);
    else
        __imp__sub_82B10F40(c, b);
}
// Compare all guest memory, including sentinels outside the output range.
// NaN payload/sign can differ between generated and native SIMD arithmetic.
// Every finite output word must remain bit-identical.
template <unsigned Fields> void Test(uint8_t *a, uint8_t *b)
{
    unsigned checks = 0;
    for (unsigned trial = 0; trial < 240; ++trial)
    {
        unsigned count = std::array<unsigned, 8>{0, 1, 2, 7, 31, 127, 512, 1024}[trial % 8];
        unsigned skew = trial % 4 * 4;
        unsigned destination = trial % 3 == 0 ? A : (trial % 3 == 1 ? Input : Output);
        // Overlapping palettes may overwrite input indices for very long jobs.
        if (destination != Output)
            count = std::min(count, 31u);
        Prepare<Fields>(a, count, trial % 2 ? 4 : 0, skew, destination, trial >= 160);
        std::memcpy(b, a, Size);
        PPCContext ca{}, cb{};
        ca.r3.u32 = cb.r3.u32 = Job;
        ca.r1.u32 = cb.r1.u32 = 0x90000;
        ca.fpscr.loadFromHost();
        cb.fpscr.csr = ca.fpscr.csr;
        Reference<Fields>(ca, a);
        Candidate<Fields>(cb, b, b + destination);
        // The generated stores align down to a 16-byte vector boundary.
        // Native entry points accept aligned output; keep alias cases aligned too.
        for (unsigned byte = 0; byte < Size; byte += 4)
        {
            uint32_t x = GRead32(a, byte), y = GRead32(b, byte);
            if (x == y)
                continue;
            if (byte >= destination && byte < destination + count * Fields * 16 &&
                std::isnan(std::bit_cast<float>(x)) && std::isnan(std::bit_cast<float>(y)))
                continue;
            std::fprintf(stderr, "FAIL fields=%u trial=%u byte=%x bits=%08x/%08x\n", Fields, trial,
                         byte, x, y);
            std::exit(1);
        }
        ++checks;
    }
    std::printf("PASS fields=%u randomized and overlapping jobs=%u\n", Fields, checks);
    for (unsigned count : {8u, 128u, 512u})
    {
        Prepare<Fields>(a, count, 0, 0, Output, false);
        std::memcpy(b, a, Size);
        PPCContext c{};
        c.r3.u32 = Job;
        c.r1.u32 = 0x90000;
        c.fpscr.loadFromHost();
        for (unsigned mode : {0u, 1u, 1u, 0u})
        {
            unsigned rounds = 262144 / count;
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < rounds; ++i)
            {
                if (mode)
                    Candidate<Fields>(c, b, b + Output);
                else
                    Reference<Fields>(c, b);
                std::atomic_signal_fence(std::memory_order_seq_cst);
            }
            std::printf(
                "fields=%u vertices=%u native=%u ns_per_vertex=%.3f\n", Fields, count, mode,
                std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start)
                        .count() /
                    (rounds * count));
        }
    }
}
int main()
{
    auto *a = (uint8_t *)std::aligned_alloc(32, Size);
    auto *b = (uint8_t *)std::aligned_alloc(32, Size);
    if (!a || !b)
        return 2;
    Test<2>(a, b);
    Test<4>(a, b);
    std::free(a);
    std::free(b);
}
