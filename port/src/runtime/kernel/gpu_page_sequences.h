#include "cxx20_compat.h"
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>

namespace ttt2_hw {

// Page sequence storage with a small block-max index.  The page values remain
// authoritative; the index only accelerates repeated full-block reductions.
// A block is invalidated when replacing its maximum with a smaller value.
// This preserves the existing uint32_t numeric max semantics, including wrap.
class PageSequenceTable {
public:
    static constexpr uint32_t kPageShift = 12;
    static constexpr size_t kBlockPages = 256;

    bool empty() const { return pages_.empty(); }
    size_t size() const { return pages_.size(); }
    ttt2_c20::span<const uint32_t> Raw() const { return pages_; }

    void Ensure(size_t count) {
        if (pages_.size() >= count) return;
        pages_.resize(count, 0);
        const size_t blocks = (count + kBlockPages - 1) / kBlockPages;
        blockMax_.resize(blocks, 0);
        blockValid_.resize(blocks, 1);
    }

    uint32_t at(size_t page) const { return pages_[page]; }

    void Assign(size_t page, uint32_t value) {
        if (page >= pages_.size()) return;
        const uint32_t old = pages_[page];
        pages_[page] = value;
        const size_t block = page / kBlockPages;
        if (!blockValid_[block]) return;
        if (value >= blockMax_[block]) {
            blockMax_[block] = value;
        } else if (old == blockMax_[block]) {
            // The cached maximum may have been the overwritten page.
            blockValid_[block] = 0;
            ++invalidBlocks_;
        }
    }

    void BeginSequence(uint32_t) {
        // Assign handles arbitrary decreases, including counter wrap. Numeric
        // max deliberately keeps the existing uint32_t comparison semantics.
    }

    void MarkAll(uint64_t first, uint64_t last, uint32_t value) {
        for (uint64_t p = first; p < last && p < pages_.size(); ++p)
            Assign(size_t(p), value);
    }

    uint32_t Newest(uint32_t start, uint32_t size) const {
        if (!size || pages_.empty()) return 0;
        const uint64_t begin = uint64_t(start) >> kPageShift;
        const uint64_t end = std::min<uint64_t>(
            (uint64_t(start) + size + ((1u << kPageShift) - 1)) >> kPageShift,
            pages_.size());
        if (begin >= end) return 0;

        uint32_t newest = 0;
        uint64_t p = begin;
        // Boundary pages are cheap and avoid requiring aligned guest ranges.
        const uint64_t aligned = (p + kBlockPages - 1) / kBlockPages * kBlockPages;
        const uint64_t boundary = std::min(end, aligned);
        // Clip once so both boundary reductions can use native SIMD.
        for (; p < boundary; ++p) newest = std::max(newest, pages_[p]);
        const uint64_t blockEnd = end / kBlockPages;
        const uint64_t blockBegin = p / kBlockPages;
        if (p % kBlockPages == 0 && blockBegin < blockEnd) {
            if (invalidBlocks_) {
                for (size_t block = size_t(blockBegin); block < blockEnd; ++block) {
                    if (blockValid_[block]) continue;
                    uint32_t value = 0;
                    for (size_t i = 0; i < kBlockPages; ++i)
                        value = std::max(value, pages_[block * kBlockPages + i]);
                    blockMax_[block] = value;
                    blockValid_[block] = 1;
                    --invalidBlocks_;
                }
            }
            // In the usual increasing-sequence case all summaries are valid.
            // Keep this reduction separate from validation so it vectorizes.
            for (uint64_t block = blockBegin; block < blockEnd; ++block)
                newest = std::max(newest, blockMax_[block]);
            p = blockEnd * kBlockPages;
        }
        while (p < end) newest = std::max(newest, pages_[p++]);
        return newest;
    }

private:
    std::vector<uint32_t> pages_;
    mutable std::vector<uint32_t> blockMax_;
    mutable std::vector<uint8_t> blockValid_;
    mutable size_t invalidBlocks_ = 0;
};

} // namespace ttt2_hw
