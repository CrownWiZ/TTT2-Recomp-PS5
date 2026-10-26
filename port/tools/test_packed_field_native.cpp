// CPU-only differential test of the inactive packed-field shortcut.
// All linked math and ABI helpers are extracted, unmodified generated functions.
// The flat image supplies real constants. This checks the native shortcut, not
// equivalence to physical PPC hardware and not in-game performance.
#include "ppc_recomp_shared.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <random>
#include <sys/mman.h>
#include <vector>

#include "packed_field_native.h"
extern "C" PPC_FUNC(__imp__sub_82AE9C28);
using Function = void (*)(PPCContext &, uint8_t *);
static uint64_t fastHits = 0;
static void Native(PPCContext &ctx, uint8_t *base)
{
    if (ttt2_packed_field::TryRunInactivePrefix(ctx, base, __imp__sub_82AE9C28))
        ++fastHits;
    else
        __imp__sub_82AE9C28(ctx, base);
}
static constexpr std::array<Function, 2> functions{__imp__sub_82AE9C28, Native};
static constexpr size_t guestSize = size_t{1} << 32, scratchSize = 4u << 20;
static constexpr uint32_t sp = 0x300000, params = 0x101000, volumes = 0x102000,
                          positions = 0x110000, auxPositions = 0x120000, previous = 0x130000,
                          packedInput = 0x140000, output8 = 0x150000, output4 = 0x160000,
                          candidates = 0x170000, stats = 0x180000, transform = 0x181000;
static void put16(uint8_t *b, uint32_t a, uint16_t v)
{
    v = __builtin_bswap16(v);
    std::memcpy(b + a, &v, 2);
}
static void put32(uint8_t *b, uint32_t a, uint32_t v)
{
    v = __builtin_bswap32(v);
    std::memcpy(b + a, &v, 4);
}
static void put64(uint8_t *b, uint32_t a, uint64_t v)
{
    v = __builtin_bswap64(v);
    std::memcpy(b + a, &v, 8);
}
static void putFloat(uint8_t *b, uint32_t a, float v)
{
    put32(b, a, std::bit_cast<uint32_t>(v));
}
static uint32_t get32(uint8_t *b, uint32_t a)
{
    uint32_t v;
    std::memcpy(&v, b + a, 4);
    return __builtin_bswap32(v);
}

struct Job
{
    unsigned mode, count, active, optional, aux, volumeCount, alias = 0, shape = 2, flags = 3;
    uint64_t phase = 0;
};
static PPCContext prepare(uint8_t *b, const Job &job, unsigned seed)
{
    std::memset(b, 0, scratchSize);
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> scalar(0.01f, 2.0f), coordinate(-2.0f, 2.0f);
    for (unsigned i = 0; i < 6; ++i)
        putFloat(b, params + i * 4, i == 1 ? 0.0f : scalar(random));
    putFloat(b, params + 8, 2.0f);
    putFloat(b, params + 16, 0.1f);
    putFloat(b, params + 20, 0.9f);
    for (unsigned i = 0; i < job.volumeCount; ++i)
    {
        const auto v = volumes + i * 96;
        put32(b, v, job.active ? job.shape : 0u);
        put32(b, v + 4, job.active ? job.flags : 0u);
        // Type 2: finite vertical bounds. Exponent 1.5 takes the real pow path.
        putFloat(b, v + 32, -4.0f);
        putFloat(b, v + 36, 4.0f);
        putFloat(b, v + 72, 0.1f);
        putFloat(b, v + 76, 0.25f);
        putFloat(b, v + 80, 1.5f);
        // Valid finite geometry for each of the four shapes.
        if (job.shape == 1)
        {
            putFloat(b, v + 32, -4);
            putFloat(b, v + 40, -4);
            putFloat(b, v + 44, 4);
            putFloat(b, v + 52, -4);
            putFloat(b, v + 56, 0);
            putFloat(b, v + 64, 4);
            putFloat(b, v + 36, -4);
            putFloat(b, v + 68, 8);
        }
        if (job.shape == 3)
        {
            putFloat(b, v + 32, 0);
            putFloat(b, v + 36, 0);
            putFloat(b, v + 40, 0);
            putFloat(b, v + 44, 4);
        }
        if (job.shape == 4)
        {
            putFloat(b, v + 32, 0);
            putFloat(b, v + 36, -4);
            putFloat(b, v + 40, 0);
            putFloat(b, v + 44, 0);
            putFloat(b, v + 48, 4);
            putFloat(b, v + 52, 0);
            putFloat(b, v + 56, 4);
        }
        for (unsigned j = 3; j < 8; ++j)
            putFloat(b, v + 4 * j, coordinate(random));
    }
    for (unsigned i = 0; i < job.count; ++i)
    {
        for (unsigned j = 0; j < 4; ++j)
        {
            putFloat(b, positions + i * 16 + j * 4, j == 3 ? 1.0f : coordinate(random));
            putFloat(b, auxPositions + i * 16 + j * 4, j == 3 ? 1.0f : coordinate(random));
        }
        // Positive finite custom half fields, and centered byte vector channels.
        put16(b, previous + i * 8, 0x3000 + (random() % 0x1000));
        put16(b, previous + i * 8 + 2, 0x3000 + (random() % 0x1000));
        for (unsigned j = 4; j < 8; ++j)
            b[previous + i * 8 + j] = uint8_t(100 + random() % 57);
        put16(b, packedInput + i * 4, 0x3000 + (random() % 0x1000));
        put16(b, packedInput + i * 4 + 2, uint16_t(random()));
    }
    for (unsigned i = 0; i < 16; ++i)
        putFloat(b, transform + i * 4, i % 5 == 0 ? 1.0f : 0.0f);
    put32(b, sp + 84, job.mode);
    put32(b, sp + 92, job.aux);
    put32(b, sp + 116, params);
    put32(b, sp + 124, job.volumeCount);
    put64(b, sp + 128, volumes);
    put64(b, sp + 136, positions);
    put64(b, sp + 144, auxPositions);
    put64(b, sp + 152, previous);
    put64(b, sp + 160, job.optional ? candidates : 0);
    put64(b, sp + 168, stats);
    put64(b, sp + 176, job.optional ? transform : 0);
    PPCContext ctx{};
    ctx.r1.u64 = sp;
    ctx.r4.u64 = job.phase;
    ctx.r5.u64 = job.count;
    ctx.r6.u64 = job.alias ? previous : output8;
    ctx.r7.u64 = job.optional ? output4 : 0;
    ctx.r8.u64 = packedInput;
    ctx.r9.u64 = 16;
    ctx.r10.u64 = 16;
    ctx.f1.f64 = 1.0 / 60.0;
    ctx.lr = 0x12345678;
    ctx.fpscr.loadFromHost();
    return ctx;
}
static bool compare(uint8_t *expected, uint8_t *actual, unsigned trial, unsigned opt)
{
    if (!std::memcmp(expected, actual, scratchSize))
        return true;
    size_t n = 0;
    while (expected[n] == actual[n])
        ++n;
    std::fprintf(stderr, "trial %u variant %u memory differs at 0x%zx: %02x/%02x\n", trial, opt, n,
                 expected[n], actual[n]);
    return false;
}
int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    std::array<uint8_t *, 2> memory{};
    for (auto &b : memory)
    {
        b = static_cast<uint8_t *>(mmap(nullptr, guestSize, PROT_READ | PROT_WRITE,
                                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
        if (b == MAP_FAILED)
            return 2;
        std::ifstream image(argv[1], std::ios::binary | std::ios::ate);
        if (!image)
            return 2;
        auto bytes = image.tellg();
        image.seekg(0);
        image.read(reinterpret_cast<char *>(b + 0x82000000), bytes);
        if (!image)
            return 2;
    }
    unsigned trial = 0, activeHits = 0;
    struct Case
    {
        Job job;
        unsigned options;
        bool zero;
    };
    std::vector<Case> cases;
    for (unsigned mode = 0; mode <= 5; ++mode)
        for (unsigned n : {0u, 1u, 2u, 3u, 17u, 255u, 256u, 257u, 512u})
            for (unsigned options = 0; options < 64; ++options)
                cases.push_back({{mode, n, options & 1u, (options >> 1) & 1u, (options >> 2) & 1u,
                                  (options & 1u) ? 4u : 1u, (options >> 3) & 1u,
                                  1u + (options >> 4), 1u + (options % 3), uint64_t(options % 9)},
                                 options,
                                 false});
    for (unsigned n : {4u, 5u, 6u, 7u, 17u, 128u, 4096u})
        for (unsigned options = 0; options < 256; ++options)
            for (uint64_t phase : {0ull, 1ull, 0x100000001ull, 0xffffffffffffffffull})
                cases.push_back(
                    {{4, n, 0, 0, 0, 1, (options >> 1) & 1u, 2, 0, phase}, options, true});
    PPCFPSCRRegister control{};
    const uint32_t entryCSR = control.getcsr();
    for (const auto &c : cases)
    {
        const auto &job = c.job;
        const unsigned n = job.count, options = c.options;
        std::array<PPCContext, 2> contexts{};
        for (unsigned opt = 0; opt < 2; ++opt)
        {
            control.setcsr(entryCSR);
            contexts[opt] = prepare(memory[opt], job, 0x82ae9c28u + trial);
            if (c.zero)
            {
                auto b = memory[opt];
                putFloat(b, params, 0.002f);
                for (unsigned i = 0; i < n; ++i)
                {
                    put16(b, previous + i * 8, 0);
                    for (unsigned j = 4; j < 8; ++j)
                        b[previous + i * 8 + j] = uint8_t((trial * 13 + i * 37 + j * 73) & 255);
                }
                contexts[opt].r7.u64 = (options & 1) ? output4 : 0;
                const double deltas[] = {0.0, 0.01, 2.5, 16.0};
                contexts[opt].f1.f64 = deltas[(options >> 2) & 3];
                if (options & 16)
                {
                    put32(b, volumes, 2);
                    put32(b, volumes + 4, 0);
                }
                else
                {
                    put32(b, volumes, 0);
                    put32(b, volumes + 4, 3);
                }
                if (options & 32)
                {
                    if (options & 16)
                        for (unsigned i = n - 3; i < n; ++i)
                            put16(b, previous + i * 8, 0x3c00);
                    else
                        put16(b, previous + (trial % n) * 8, 0x3c00);
                }
                switch (options >> 6)
                {
                case 1:
                    contexts[opt].r6.u64 = previous + 8;
                    break;
                case 2:
                    if (n <= 128)
                        contexts[opt].r6.u64 = sp - 2048;
                    break;
                case 3:
                    put32(b, volumes, 2);
                    put32(b, volumes + 4, 1);
                    break;
                }
            }
            // Reset live rounding/flush state for each independent execution.
            contexts[opt].fpscr.setcsr(contexts[opt].fpscr.csr);
            functions[opt](contexts[opt], memory[opt]);
        }
        for (unsigned opt = 1; opt < 2; ++opt)
        {
            if (!compare(memory[0], memory[opt], trial, opt + 1))
                return 1;
            if (std::memcmp(&contexts[0], &contexts[opt], sizeof(PPCContext)))
            {
                const auto *a = reinterpret_cast<const uint8_t *>(&contexts[0]);
                const auto *b = reinterpret_cast<const uint8_t *>(&contexts[opt]);
                size_t n = 0;
                while (a[n] == b[n])
                    ++n;
                std::fprintf(stderr, "trial %u variant %u context differs at byte %zu: %02x/%02x\n",
                             trial, opt, n, a[n], b[n]);
                return 1;
            }
        }
        // Nonvolatile ABI state must also agree and remain restored.
        for (unsigned opt = 0; opt < 2; ++opt)
            if (contexts[opt].r1.u32 != sp || contexts[opt].lr != 0x12345678 ||
                contexts[opt].r14.u64 || contexts[opt].r31.u64)
                return 1;
        if (job.mode == 4 && get32(memory[0], volumes + 8))
            ++activeHits;
        ++trial;
    }
    std::printf("%u synthetic jobs, modes 0..5, counts 0..4096: native and generated context and "
                "scratch memory match byte for byte; real ABI helpers restore SP/LR/nonvolatile "
                "GPRs. Active mode4 volume hits: %u.\n",
                trial, activeHits);
    std::printf("Native prefix hits: %llu\n", (unsigned long long)fastHits);
    return fastHits ? 0 : 1;
}
