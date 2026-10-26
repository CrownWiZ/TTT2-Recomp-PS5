#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <memory>

namespace ttt2_hw {
// Reuse only within the ring's still-open completion-query chunk. Entries
// cannot outlive that chunk, wrap, DISCARD, or a change of upload mode. Exact
// raw-byte comparisons detect guest writes without changing VM protection.
class VertexUploadCache {
    struct Entry {
        uint64_t key = 0, generation = 0, offset = 0;
        std::unique_ptr<uint32_t[]> words;
        size_t capacity = 0;
    };
    std::array<Entry, 2048> entries_;
    size_t eviction_ = 0;
    uint64_t generation_ = 1, begin_ = 0, end_ = 0, storage_ = 0;
public:
    uint64_t requests = 0, bytes = 0, hits = 0, hitBytes = 0;
    uint64_t budget = 0;
    uint64_t tooSmall = 0, tooLarge = 0, outside = 0, stale = 0, collision = 0, changed = 0;
    void Report()
    {
        std::fprintf(stderr, "[vertex-reuse] requests=%llu bytes=%llu hits=%llu hit_bytes=%llu small_bytes=%llu large_bytes=%llu outside_bytes=%llu stale_bytes=%llu collision_bytes=%llu changed_bytes=%llu storage=%llu budget_bytes=%llu\n",
            (unsigned long long)requests, (unsigned long long)bytes, (unsigned long long)hits,
            (unsigned long long)hitBytes, (unsigned long long)tooSmall, (unsigned long long)tooLarge,
            (unsigned long long)outside, (unsigned long long)stale, (unsigned long long)collision,
            (unsigned long long)changed, (unsigned long long)storage_, (unsigned long long)budget);
        budget = requests = bytes = hits = hitBytes = tooSmall = tooLarge = outside = stale = collision = changed = 0;
    }
    void Invalidate() { ++generation_; begin_ = end_ = 0; }
    void Begin(uint64_t beginDword, uint64_t endDword, bool discard)
    {
        if (discard || beginDword != begin_ || endDword != end_) ++generation_;
        begin_ = beginDword; end_ = endDword;
    }
    // Null means hit, and offset receives the existing GPU dword location.
    // On a miss return an owned snapshot to pack, so the bytes we remember are
    // exactly the bytes uploaded even if the guest writes concurrently.
    // Small/oversized/cross-chunk copies stream directly with no snapshot.
    const uint32_t* Prepare(uint32_t address, const uint32_t* source,
                            size_t words, uint64_t destination, uint64_t& offset)
    {
        offset = destination;
        ++requests; bytes += words * 4;
        if (words < 512) { tooSmall += words * 4; return source; }
        if (words > 65536) { tooLarge += words * 4; return source; }
        if (destination < begin_ || destination + words > end_)
            { outside += words * 4; return source; }
        const uint64_t key = (uint64_t(address) << 32) | (words * 4);
        uint64_t hash = key ^ (key >> 29);
        hash *= 0x9e3779b97f4a7c15ull;
        auto& e = entries_[hash >> 53];
        if (e.generation == generation_ && e.key == key &&
            std::memcmp(e.words.get(), source, words * 4) == 0) {
            offset = e.offset;
            ++hits; hitBytes += words * 4;
            return nullptr;
        }
        if (e.generation != generation_) stale += words * 4;
        else if (e.key != key) collision += words * 4;
        else changed += words * 4;
        if (words > e.capacity) {
            const uint64_t growth = (words - e.capacity) * 4;
            for (size_t scanned = 0; storage_ + growth > (64u << 20) && scanned < entries_.size(); ++scanned) {
                auto& victim = entries_[eviction_++ % entries_.size()];
                if (&victim == &e || victim.generation == generation_) continue;
                storage_ -= victim.capacity * 4;
                victim.words.reset(); victim.capacity = 0; victim.generation = 0;
            }
            if (storage_ + growth > (64u << 20)) { budget += words * 4; return source; }
            e.words.reset(new uint32_t[words]); e.capacity = words; storage_ += growth;
        }
        std::memcpy(e.words.get(), source, words * 4);
        e.key = key; e.generation = generation_; e.offset = destination;
        return e.words.get();
    }
};
} // namespace ttt2_hw
