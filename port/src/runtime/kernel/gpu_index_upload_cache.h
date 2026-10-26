#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ttt2_hw
{
// Reuse only uploads in the current append-only chunk. Earlier chunks may
// already have completion queries queued, so they must never be read again.
class IndexUploadCache
{
    struct Entry
    {
        uint64_t identity = 0, epoch = 0;
        uint32_t first = 0, words = 0;
    };
    static constexpr size_t Slots = 2048;
    std::array<Entry, Slots> entries_{};
    uint64_t epoch_ = 1;
    size_t chunkWords_, chunk_ = SIZE_MAX;
    static size_t Slot(uint64_t identity)
    {
        return size_t(identity ^ (identity >> 32)) & (Slots - 1);
    }

  public:
    explicit IndexUploadCache(size_t chunkWords) : chunkWords_(chunkWords)
    {
    }
    void Invalidate()
    {
        if (++epoch_ == 0)
        {
            entries_ = {};
            epoch_ = 1;
        }
    }
    bool Find(uint64_t identity, size_t words, uint32_t &first) const
    {
        if (!identity || words > UINT32_MAX)
            return false;
        const auto &entry = entries_[Slot(identity)];
        if (entry.epoch != epoch_ || entry.identity != identity || entry.words != words)
            return false;
        first = entry.first;
        return true;
    }
    // Call before every write, including CPU-vertex draws and uncached draws.
    // Advancing exactly to a boundary also retires the previous chunk.
    void BeginWrite(size_t first, size_t words, bool wrapped)
    {
        if (!chunkWords_ || words > SIZE_MAX - first)
        {
            Invalidate();
            chunk_ = SIZE_MAX;
            return;
        }
        const size_t endChunk = (first + words) / chunkWords_;
        if (wrapped || chunk_ != endChunk)
            Invalidate();
        chunk_ = endChunk;
    }
    void Store(uint64_t identity, size_t first, size_t words)
    {
        if (!identity || !words || !chunkWords_ || first > UINT32_MAX || words > UINT32_MAX ||
            first / chunkWords_ != chunk_ || words >= chunkWords_ - first % chunkWords_)
            return;
        entries_[Slot(identity)] = {identity, epoch_, uint32_t(first), uint32_t(words)};
    }
};
} // namespace ttt2_hw
