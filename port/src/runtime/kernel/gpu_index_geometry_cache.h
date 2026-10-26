#pragma once

#include "gpu_hw_indices.h"
#include "gpu_index_decode.h"
#include <cstring>
#include <atomic>
#include <unordered_map>

namespace ttt2_hw {

// CPU topology cache. Mesh indices often stay constant while vertex
// positions animate. Check every source byte on every use and assign a new
// identity whenever the decoded result is rebuilt.
class IndexGeometryCache
{
public:
    struct Key
    {
        uint32_t address, count, offset, minimum, maximum, restartIndex;
        uint32_t index32, restart, primitive;
        bool operator==(const Key&) const = default;
    };
    struct Result
    {
        DecodedIndices decoded;
        TriangleIndices assembled;
        uint32_t stripTriangles = 0;
        uint64_t identity = 0; // Version of these verified indices; zero disables GPU reuse.
    };
private:
    // GPU uploads may outlive a CPU cache entry. Do not recycle IDs on Clear().
    static uint64_t NextIdentity()
    {
        static std::atomic<uint64_t> next{0};
        auto value = next.load(std::memory_order_relaxed);
        while (value != UINT64_MAX)
            if (next.compare_exchange_weak(value, value + 1, std::memory_order_relaxed)) return value + 1;
        return 0;
    }
    struct Hash
    {
        size_t operator()(const Key& key) const
        {
            uint64_t h = 0xcbf29ce484222325ull;
            for (uint32_t v : {key.address, key.count, key.offset, key.minimum, key.maximum,
                               key.restartIndex, key.index32, key.restart, key.primitive})
                h = (h ^ v) * 0x100000001b3ull;
            return size_t(h);
        }
    };
    struct Entry
    {
        std::vector<uint8_t> source;
        IndexDecodeStorage storage;
        std::vector<uint32_t> scratch;
        Result result;
        bool valid = false;
    };
    std::unordered_map<Key, Entry, Hash> entries_;
    size_t retained_ = 0;
    size_t limit_;
public:
    explicit IndexGeometryCache(size_t limit = 64u << 20) : limit_(limit) {}
    void Clear() { entries_.clear(); retained_ = 0; }

    const Result* Get(const uint8_t* guest, const Key& key)
    {
        const auto primitive = IndexPrimitive(key.primitive);
        if (primitive != IndexPrimitive::Triangles && primitive != IndexPrimitive::Fan &&
            primitive != IndexPrimitive::Strip && primitive != IndexPrimitive::Quads) return nullptr;
        const uint64_t bytes = uint64_t(key.count) * (key.index32 ? 4 : 2);
        // The general decoder preserves 32-bit guest address wrapping. Keep
        // that uncommon case on its original path instead of assuming a
        // contiguous snapshot can represent a wrapped source.
        if (!key.count || uint64_t(key.address) + bytes > 0x100000000ull) return nullptr;
        const uint64_t reserved = bytes + uint64_t(key.count) * 17;
        if (reserved > limit_) return nullptr;
        auto found = entries_.find(key);
        if (found == entries_.end())
        {
            if (retained_ + reserved > limit_ || entries_.size() >= 4096) Clear();
            found = entries_.try_emplace(key).first;
            retained_ += size_t(reserved);
        }
        auto& entry = found->second;
        if (entry.valid && std::memcmp(guest + key.address, entry.source.data(), size_t(bytes)) == 0)
            return &entry.result;
        entry.source.resize(size_t(bytes));
        std::memcpy(entry.source.data(), guest + key.address, size_t(bytes));
        auto& result = entry.result;
        result.decoded = DecodeIndices(entry.source.data(), 0, key.count, false, key.index32,
            key.restart, key.restartIndex, key.offset, key.minimum, key.maximum, entry.storage);
        const auto cuts = result.decoded.anyCut ? result.decoded.cuts : std::span<const uint8_t>{};
        if (primitive == IndexPrimitive::Strip)
        {
            const auto strip = BuildNativeStripIndices(result.decoded.indices, cuts, entry.scratch);
            result.assembled = strip;
            result.stripTriangles = strip.triangles;
        }
        else
            result.assembled = BuildTriangleIndices(primitive, result.decoded.indices, cuts, entry.scratch);
        result.identity = NextIdentity();
        entry.valid = true;
        return &result;
    }
};
} // namespace ttt2_hw
