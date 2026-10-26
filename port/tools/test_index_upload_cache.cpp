#include "../src/runtime/kernel/gpu_index_geometry_cache.h"
#include "../src/runtime/kernel/gpu_index_upload_cache.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_set>
#include <vector>
using namespace ttt2_hw;
static void Require(bool x)
{
    if (!x)
        std::abort();
}
static void IdentityTests()
{
    std::vector<uint8_t> guest(4096);
    for (unsigned i = 0; i < guest.size(); ++i)
        guest[i] = uint8_t(i);
    IndexGeometryCache cache(8192);
    IndexGeometryCache::Key key{64, 24, 0, 0, 0xffffff, 0xffff, 0, 0, 4};
    auto *a = cache.Get(guest.data(), key);
    Require(a && a->identity);
    auto id = a->identity;
    Require(cache.Get(guest.data(), key)->identity == id);
    guest[65] ^= 1;
    auto next = cache.Get(guest.data(), key)->identity;
    Require(next != id);
    id = next;
    cache.Clear();
    Require(cache.Get(guest.data(), key)->identity != id);
    std::mutex mutex;
    std::unordered_set<uint64_t> ids;
    std::array<std::thread, 4> threads;
    for (auto &t : threads)
        t = std::thread([&] {
            IndexGeometryCache local;
            for (unsigned i = 0; i < 1000; ++i)
            {
                local.Clear();
                auto id = local.Get(guest.data(), key)->identity;
                std::lock_guard lock(mutex);
                Require(id && ids.insert(id).second);
            }
        });
    for (auto &t : threads)
        t.join();
    Require(ids.size() == 4000);
}
static void ExplicitTests()
{
    IndexUploadCache cache(64);
    uint32_t first = 123;
    Require(!cache.Find(1, 3, first));
    Require(first == 123);
    cache.BeginWrite(0, 3, false);
    cache.Store(1, 0, 3);
    Require(cache.Find(1, 3, first) && first == 0);
    Require(!cache.Find(1, 4, first));
    Require(!cache.Find(0, 3, first));
    cache.BeginWrite(3, 58, false);
    cache.Store(2, 3, 58);
    Require(cache.Find(1, 3, first));
    cache.BeginWrite(61, 3, false);
    Require(!cache.Find(1, 3, first));
    Require(!cache.Find(2, 58, first));
    cache.Store(3, 61, 3);
    Require(!cache.Find(3, 3, first));
    cache.BeginWrite(64, 3, false);
    cache.Store(4, 64, 3);
    Require(cache.Find(4, 3, first) && first == 64);
    cache.BeginWrite(0, 3, true);
    Require(!cache.Find(4, 3, first));
    cache.Store(5, 0, 3);
    Require(cache.Find(5, 3, first));
    cache.BeginWrite(3, 140, false);
    Require(!cache.Find(5, 3, first));
    cache.Store(6, 3, 140);
    Require(!cache.Find(6, 140, first));
    cache.BeginWrite(192, 4, false);
    cache.Store(7, 192, 4);
    Require(cache.Find(7, 4, first));
    cache.Invalidate();
    Require(!cache.Find(7, 4, first));
    cache.BeginWrite(SIZE_MAX, 3, false);
    cache.Store(8, SIZE_MAX, 3);
    Require(!cache.Find(8, 3, first));
    IndexUploadCache disabled(0);
    disabled.BeginWrite(0, 3, false);
    disabled.Store(1, 0, 3);
    Require(!disabled.Find(1, 3, first));
}
static void Simulate(unsigned seed)
{
    constexpr size_t Chunk = 512, Ring = Chunk * 8, Blocks = 64, Stride = 512;
    std::mt19937 random(seed);
    std::vector<uint8_t> guest(Blocks * Stride);
    std::array<uint32_t, Ring> ring{};
    size_t cursor = 0;
    for (auto &v : guest)
        v = uint8_t(random());
    IndexGeometryCache geometry(1u << 18);
    IndexUploadCache uploads(Chunk);
    uint64_t hits = 0, misses = 0, cpu = 0, wraps = 0;
    for (unsigned step = 0; step < 50000; ++step)
    {
        if (step % 67 == 0)
            geometry.Clear(); // Recycled CPU entries must not resurrect old GPU uploads.
        if (step % 19 == 0)
        { // Uncached CPU-vertex draw shares the same ring.
            unsigned count = 1 + random() % 700;
            bool wrap = cursor + count > Ring;
            if (wrap)
            {
                cursor = 0;
                ++wraps;
            }
            uploads.BeginWrite(cursor, count, wrap);
            for (unsigned i = 0; i < count; ++i)
                ring[cursor + i] = random();
            cursor += count;
            ++cpu;
            continue;
        }
        unsigned block = random() % Blocks,
                 primitive = std::array<unsigned, 4>{4, 5, 6, 13}[block % 4];
        const unsigned count = 3 + block % 67;
        bool wide = block % 2;
        if (step % 29 == 0)
            guest[block * Stride + random() % (count * (wide ? 4 : 2))] ^=
                uint8_t(1 + random() % 255);
        IndexGeometryCache::Key key{uint32_t(block * Stride),
                                    count,
                                    block % 3,
                                    0,
                                    65535,
                                    wide ? UINT32_MAX : 65535,
                                    uint32_t(wide),
                                    uint32_t(block % 5 == 0),
                                    primitive};
        const auto *result = geometry.Get(guest.data(), key);
        Require(result);
        const auto data = result->assembled.values;
        Require(data.size() < Ring);
        uint32_t first = 0;
        bool enabled = step % 123 < 100;
        if (enabled && uploads.Find(result->identity, data.size(), first))
        {
            Require(size_t(first) / Chunk == cursor / Chunk);
            ++hits;
        }
        else
        {
            bool wrap = cursor + data.size() > Ring;
            if (wrap)
            {
                cursor = 0;
                ++wraps;
            }
            uploads.BeginWrite(cursor, data.size(), wrap);
            first = uint32_t(cursor);
            std::copy(data.begin(), data.end(), ring.begin() + cursor);
            cursor += data.size();
            if (enabled)
                uploads.Store(result->identity, first, data.size());
            ++misses;
        }
        Require(std::equal(data.begin(), data.end(), ring.begin() + first));
    }
    Require(hits > 100 && misses > 100 && wraps > 10 && cpu > 100);
    std::printf("seed=%u hits=%llu misses=%llu cpu_draws=%llu wraps=%llu\n", seed,
                (unsigned long long)hits, (unsigned long long)misses, (unsigned long long)cpu,
                (unsigned long long)wraps);
}
int main()
{
    IdentityTests();
    ExplicitTests();
    for (unsigned seed : {1, 7, 123, 98765})
        Simulate(seed);
    std::puts("PASS identity lifetime/concurrency and 200000 mixed ring operations");
}
