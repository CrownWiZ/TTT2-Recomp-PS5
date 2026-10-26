// Cached physical view mirror.
//
// The console maps its 512 MB of physical memory three times: 0xA0000000 (64 KB pages),
// 0xC0000000 (16 MB pages) and 0xE0000000 (4 KB pages), all over the same bytes - Xenia
// memory.cc builds its vA0000000 / vC0000000 / vE0000000 heaps on one physical heap. The
// port allocates every physical block in the A view (MmAllocatePhysicalMemoryEx) and the GPU
// addresses it there (PhysToGuest). TTT2's D3D runtime, however, gives READ-ONLY texture
// locks the cached alias 0xC0000000 + (address & 0x1FFFFFFF) (+0x1000 for E-view input; the
// tail of sub_827382F0). The character bake's CPU recompression (sub_82AD8A00) locks its
// 1024x1024 resolve at A7515000 that way and reads C7515000 - all zeros here, so every baked
// costume texture became all-black DXT blocks.
//
// The guest block is private and uses host VM written-page tracking for texture
// invalidation, so these views cannot share pages. C and E are copies filled on demand:
// the first access to a 64 KB chunk writes back any GPU resolve covering it, copies A into
// that alias, and opens the chunk. Switching between C and E writes the previous alias's
// dirty pages into A first. A resolve or frame swap closes open chunks in both aliases.
#include "phys_mirror.h"
#include "gpu_render.h"
#include "../host_vm.h"
#include "host_settings.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <vector>

namespace {
constexpr uint32_t VIEW_A = 0xA0000000u, VIEW_C = 0xC0000000u;
constexpr uint32_t VIEW_SIZE = 0x20000000u;
constexpr uint32_t CHUNK = 0x10000u, PAGE = 0x1000u, CHUNKS = VIEW_SIZE / CHUNK;
constexpr std::array<uint32_t, 2> ALIASES{VIEW_C, 0xE0000000u};

uint8_t* g_mirrorBase = nullptr;
std::mutex g_mirrorMutex;
std::array<std::vector<uint8_t>, 2> g_open;   // per alias/chunk: 1 = filled and accessible
std::array<std::vector<uint8_t>, 2> g_listed; // avoid duplicate open-list entries on alias switches
std::vector<uint32_t> g_openList;            // alias * CHUNKS + chunk
std::atomic<uint32_t> g_generation{ 0 };     // bumped by every invalidation
std::atomic<uint64_t> g_fills{ 0 }, g_writeBacks{ 0 };

// TTT2_MIRRORDUMP=<physStart>,<physSize> (hex, e.g. 07510000,00400000 for the 1024x1024
// character-bake resolve): the first Fill of every chunk INSIDE THE RANGE writes the A view
// bytes (the physical truth the alias would copy) to port/tex/mirrorA_<addr>.raw and logs
// it. Diagnostic for CPU-bake diagnosis: shows what the CPU recompressors read from their
// resolved render targets. The write runs under g_mirrorMutex - accepted for a gated,
// once-per-chunk debug tool (finding from the 1a46b06 review).
struct MirrorDump
{
    bool enabled = false, parsed = false;
    uint32_t firstChunk = 0, lastChunk = 0;
    std::vector<uint8_t> dumped;   // per chunk, in-range chunks only
    std::filesystem::path directory;
    void Parse()
    {
        parsed = true;
        const char* e = std::getenv("TTT2_MIRRORDUMP");
        if (!e) return;
        unsigned long long s = 0, n = 0;
        if (std::sscanf(e, "%llx,%llx", &s, &n) != 2 || n == 0) return;
        const uint32_t start = uint32_t(s) & 0x1FFFFFFFu;
        firstChunk = start / CHUNK;
        lastChunk = (start + uint32_t(n) - 1) / CHUNK;
        if (firstChunk >= CHUNKS) return;
        lastChunk = (std::min)(lastChunk, CHUNKS - 1u);
        dumped.assign(lastChunk - firstChunk + 1, 0);
        directory =
#if defined(_WIN32)
            "C:/ttt2/port/tex";
#else
            host_settings::DiagnosticPath("tex");
#endif
        std::error_code directoryError;
        std::filesystem::create_directories(directory, directoryError);
        if (directoryError) return;
        enabled = true;
        std::fprintf(stderr, "[phys-mirror] dump enabled: chunks %05X..%05X\n", firstChunk, lastChunk);
    }
    void OnFill(uint32_t chunk, const uint8_t* a)
    {
        if (!parsed) Parse();
        if (!enabled || chunk < firstChunk || chunk > lastChunk) return;
        const uint32_t index = chunk - firstChunk;
        if (dumped[index]) return;
        dumped[index] = 1;                    // one shot: never retry a failed open either
        const uint32_t addr = VIEW_A + chunk * CHUNK;
        char path[128];
        std::snprintf(path, sizeof(path), "mirrorA_%08X.raw", addr);
        if (FILE* f = std::fopen((directory / path).string().c_str(), "wb"))
        {
            std::fwrite(a, 1, CHUNK, f);
            std::fclose(f);
            std::fprintf(stderr, "[phys-mirror] dumped A %08X head=%02X %02X %02X %02X %02X %02X %02X %02X\n",
                         addr, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
        }
        else
        {
            std::fprintf(stderr, "[phys-mirror] dump FAILED %08X (%s)\n", addr, path);
        }
    }
} g_mirrorDump;

uint8_t* AliasView(unsigned alias, uint32_t chunk)
{
    return g_mirrorBase + ALIASES[alias] + size_t(chunk) * CHUNK;
}
uint8_t* AView(uint32_t chunk) { return g_mirrorBase + VIEW_A + size_t(chunk) * CHUNK; }

void RequireVm(bool succeeded, const char* operation, unsigned alias, uint32_t chunk)
{
    if (succeeded) return;
    std::fprintf(stderr, "[phys-mirror] %s failed at %08X (%lu)\n", operation,
                 ALIASES[alias] + chunk * CHUNK, host_vm::LastError());
    std::abort(); // Continuing would expose a partly copied or stale physical view.
}

// Copy A -> alias and arm write tracking. The Linux VM backend fills through a
// separate mapping while the guest view remains inaccessible. Caller holds
// g_mirrorMutex.
void Fill(unsigned alias, uint32_t chunk)
{
    uint8_t* c = AliasView(alias, chunk);
    const uint8_t* a = AView(chunk);
    g_mirrorDump.OnFill(chunk, a);
    RequireVm(host_vm::FillMirrorView(c, a, CHUNK), "fill cached view", alias, chunk);
    g_open[alias][chunk] = 1;
    if (!g_listed[alias][chunk])
    {
        g_listed[alias][chunk] = 1;
        g_openList.push_back(alias * CHUNKS + chunk);
    }
    g_fills.fetch_add(1, std::memory_order_relaxed);
}

// Caller holds g_mirrorMutex. Pages written through either alias go back to A first.
void Close(unsigned alias, uint32_t chunk)
{
    uint8_t* c = AliasView(alias, chunk);
    RequireVm(host_vm::Protect(c, CHUNK, host_vm::Protection::ReadOnly),
              "stop writers", alias, chunk);
    void* written[CHUNK / PAGE];
    size_t count = 0;
    RequireVm(host_vm::TakeWriteWatch(c, CHUNK, written, CHUNK / PAGE, count, true),
              "collect dirty pages", alias, chunk);
    for (size_t i = 0; i < count; ++i)
    {
        const size_t offset = static_cast<uint8_t*>(written[i]) - c;
        std::memcpy(AView(chunk) + offset, c + offset, PAGE);
        g_writeBacks.fetch_add(1, std::memory_order_relaxed);
    }
    RequireVm(host_vm::Protect(c, CHUNK, host_vm::Protection::None),
              "close chunk", alias, chunk);
    g_open[alias][chunk] = 0;
}

void CloseRange(uint32_t start, uint32_t size, bool announceResolve)
{
    if (!g_mirrorBase || !size || start < VIEW_A) return;
    const uint32_t phys = start & 0x1FFFFFFFu;
    const uint64_t end = (std::min<uint64_t>)(uint64_t(phys) + size, VIEW_SIZE);
    std::lock_guard<std::mutex> lock(g_mirrorMutex);
    // A new GPU resolve must make a concurrent fault worker repeat its
    // writeback before filling a cached chunk. A pre-read flush need not.
    if (announceResolve) g_generation.fetch_add(1, std::memory_order_acq_rel);
    bool closed = false;
    for (uint32_t chunk = phys / CHUNK; uint64_t(chunk) * CHUNK < end; ++chunk)
        for (unsigned alias = 0; alias < ALIASES.size(); ++alias)
            if (g_open[alias][chunk]) { Close(alias, chunk); closed = true; }
    if (closed)
    {
        g_openList.erase(std::remove_if(g_openList.begin(), g_openList.end(), [](uint32_t id) {
            const unsigned alias = id / CHUNKS;
            const uint32_t chunk = id % CHUNKS;
            if (g_open[alias][chunk]) return false;
            g_listed[alias][chunk] = 0;
            return true;
        }), g_openList.end());
    }
}

}

PhysMirrorFaultResult PhysMirrorHandleFault(uintptr_t fault)
{
    if (!g_mirrorBase) return PhysMirrorFaultResult::Unhandled;
    const uint64_t lo = reinterpret_cast<uint64_t>(g_mirrorBase) + VIEW_C;
    if (fault < lo || fault >= lo + uint64_t(VIEW_SIZE) * ALIASES.size())
        return PhysMirrorFaultResult::Unhandled;
    const uint64_t offset = fault - lo;
    const unsigned alias = unsigned(offset / VIEW_SIZE);
    const uint32_t chunk = uint32_t((offset % VIEW_SIZE) / CHUNK);
    for (;;)
    {
        // The writeback takes the GPU backend lock; never hold the mirror lock across it
        // (the command processor invalidates with the backend idle but may hold it later).
        const uint32_t generation = g_generation.load(std::memory_order_acquire);
        GpuHwWriteBackRange(VIEW_A + chunk * CHUNK, CHUNK);
        std::lock_guard<std::mutex> lock(g_mirrorMutex);
        if (g_open[alias][chunk]) return PhysMirrorFaultResult::AlreadyOpen; // filled meanwhile
        // A resolve landed between the writeback and the lock: write that one back too.
        if (g_generation.load(std::memory_order_acquire) != generation) continue;
        if (g_open[alias ^ 1][chunk]) Close(alias ^ 1, chunk);
        Fill(alias, chunk);
        return PhysMirrorFaultResult::Filled;
    }
}

bool PhysMirrorInit(uint8_t* base)
{
    for (unsigned alias = 0; alias < ALIASES.size(); ++alias)
    {
        g_open[alias].assign(CHUNKS, 0);
        g_listed[alias].assign(CHUNKS, 0);
    }
    g_openList.clear();
    g_openList.reserve(ALIASES.size() * CHUNKS);
    g_mirrorBase = base;
    const size_t cachedBytes = size_t(VIEW_SIZE) * ALIASES.size();
    if (!host_vm::InstallMirrorBacking(base + VIEW_C, cachedBytes))
    {
        std::fprintf(stderr, "[phys-mirror] cannot prepare cached views (%lu)\n",
                     host_vm::LastError());
        g_mirrorBase = nullptr;
        return false;
    }
    if (!PhysMirrorInstallFaultHandler(base + VIEW_C, cachedBytes))
    {
        std::fprintf(stderr, "[phys-mirror] cannot install cached-view fault handler\n");
        g_mirrorBase = nullptr;
        return false;
    }
    if (!host_vm::Protect(base + VIEW_C, cachedBytes, host_vm::Protection::None))
    {
        std::fprintf(stderr, "[phys-mirror] cannot close the cached views (%lu)\n",
                     host_vm::LastError());
        g_mirrorBase = nullptr;
        return false;
    }
    return true;
}

void PhysMirrorInvalidate(uint32_t start, uint32_t size)
{
    CloseRange(start, size, true);
}

void PhysMirrorFlushRange(uint32_t start, uint32_t size)
{
    CloseRange(start, size, false);
}

void PhysMirrorInvalidateAll()
{
    if (!g_mirrorBase) return;
    std::lock_guard<std::mutex> lock(g_mirrorMutex);
    g_generation.fetch_add(1, std::memory_order_acq_rel);
    for (uint32_t id : g_openList)
    {
        const unsigned alias = id / CHUNKS;
        const uint32_t chunk = id % CHUNKS;
        if (g_open[alias][chunk]) Close(alias, chunk);
        g_listed[alias][chunk] = 0;
    }
    g_openList.clear();
    static uint64_t reported = 0;
    const uint64_t fills = g_fills.load(std::memory_order_relaxed);
    if (fills >= reported + 256)              // occasional activity line, not per frame
    {
        reported = fills;
        std::fprintf(stderr, "[phys-mirror] cached-view chunk fills=%llu page writebacks=%llu\n",
                     (unsigned long long)fills, (unsigned long long)g_writeBacks.load());
    }
}
