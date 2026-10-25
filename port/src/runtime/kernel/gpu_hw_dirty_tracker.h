#pragma once

#include "../host_vm.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <span>
#if defined(TTT2_PAGE_SUMMARY_EXPERIMENT) && TTT2_PAGE_SUMMARY_EXPERIMENT
#include "gpu_page_sequences.h"
#endif
#if defined(TTT2_GEOMETRY_AB_PROBE)
#include "gpu_geometry_probe.h"
#endif

void WriteWatchPublish(int self, void* const* pages, size_t count);
void WriteWatchDrain(int self, uint32_t firstPage, uint32_t lastPage, std::vector<uint32_t>& out);

namespace ttt2_hw {

// Clip once so the remaining unsigned maximum reduction can be vectorized on
// both x86 and ARM. These are page sequence counters, not texture pixels.
inline uint32_t NewestPageSequence(std::span<const uint32_t> pages,
                                   uint32_t start, uint32_t size)
{
    if (!size) return 0;
    const uint64_t end = std::min<uint64_t>(
        (uint64_t(start) + size + 4095) >> 12, pages.size());
    uint32_t newest = 0;
    for (uint64_t p = start >> 12; p < end; ++p)
        newest = std::max(newest, pages[p]);
    return newest;
}

// Consume host write-watch bits once and stamp guest pages with a sequence.
// Multiple GPU caches can then ask whether their ranges changed since the
// sequence they last uploaded, without stealing write-watch bits from each other.
class DirtyTracker
{
public:
    explicit DirtyTracker(uint8_t* guestBase) : guestBase_(guestBase) {}

    uint32_t Sync(uint32_t start, uint32_t size)
    {
        if (!size) return seq_;
        const uint64_t first = start >> 12;
        const uint64_t last = (uint64_t(start) + size + 4095) >> 12;
        if (pageSeq_.empty()) {
#if defined(TTT2_PAGE_SUMMARY_EXPERIMENT) && TTT2_PAGE_SUMMARY_EXPERIMENT
            pageSeq_.Ensure(1u << 20);
#else
            pageSeq_.assign(1u << 20, 0);
#endif
        }
        const size_t pages = size_t(last - first);
        if (addresses_.size() < pages) addresses_.resize(pages);
        size_t count = 0;
        if (!usable_ || !guestBase_) { MarkAll(first, last); return seq_; }
        const bool watched = host_vm::TakeWriteWatch(guestBase_ + (first << 12), pages << 12,
                                                     addresses_.data(), addresses_.size(), count, true);
        if (!watched)
        {
            usable_ = false;
            std::fprintf(stderr,
                         "[gpu-hw] write watch unavailable (%lu); textures re-upload on every use\n",
                         host_vm::LastError());
            MarkAll(first, last);
            return seq_;
        }
        WriteWatchPublish(0, addresses_.data(), count);
        drained_.clear();
        WriteWatchDrain(0, uint32_t(first), uint32_t(last), drained_);
        if (count || !drained_.empty())
        {
            ++seq_;
            for (size_t i = 0; i < count; ++i)
            {
                const uint64_t page = (static_cast<uint8_t*>(addresses_[i]) - guestBase_) >> 12;
#if defined(TTT2_PAGE_SUMMARY_EXPERIMENT) && TTT2_PAGE_SUMMARY_EXPERIMENT
                pageSeq_.Assign(size_t(page), seq_);
#else
                if (page < pageSeq_.size()) pageSeq_[page] = seq_;
#endif
            }
            for (uint32_t page : drained_)
            {
#if defined(TTT2_PAGE_SUMMARY_EXPERIMENT) && TTT2_PAGE_SUMMARY_EXPERIMENT
                pageSeq_.Assign(page, seq_);
#else
                if (page < pageSeq_.size()) pageSeq_[page] = seq_;
#endif
            }
        }
        return seq_;
    }

    // Highest write sequence on any page of the range (Sync it first).
    uint32_t Newest(uint32_t start, uint32_t size) const
    {
#if defined(TTT2_GEOMETRY_AB_PROBE)
        if (geometry_probe::ReferenceMask() & 262144)
        {
#if defined(TTT2_PAGE_SUMMARY_EXPERIMENT) && TTT2_PAGE_SUMMARY_EXPERIMENT
            const auto pages = pageSeq_.Raw();
#else
            const auto& pages = pageSeq_;
#endif
            if (pages.empty() || !size) return 0;
            const uint64_t end = (uint64_t(start) + size + 4095) >> 12;
            uint32_t newest = 0;
            for (uint64_t p = start >> 12; p < end && p < pages.size(); ++p)
                newest = std::max(newest, pages[p]);
            return newest;
        }
#endif
#if defined(TTT2_PAGE_SUMMARY_EXPERIMENT) && TTT2_PAGE_SUMMARY_EXPERIMENT
        return pageSeq_.Newest(start, size);
#else
        return NewestPageSequence(pageSeq_, start, size);
#endif
    }

    uint32_t Current() const { return seq_; }

private:
    void MarkAll(uint64_t first, uint64_t last)
    {
        ++seq_;
#if defined(TTT2_PAGE_SUMMARY_EXPERIMENT) && TTT2_PAGE_SUMMARY_EXPERIMENT
        pageSeq_.MarkAll(first, last, seq_);
#else
        for (uint64_t p = first; p < last && p < pageSeq_.size(); ++p)
            pageSeq_[p] = seq_;
#endif
    }

    uint8_t* guestBase_ = nullptr;
#if defined(TTT2_PAGE_SUMMARY_EXPERIMENT) && TTT2_PAGE_SUMMARY_EXPERIMENT
    PageSequenceTable pageSeq_;
#else
    std::vector<uint32_t> pageSeq_;
#endif
    std::vector<void*> addresses_;
    std::vector<uint32_t> drained_;
    uint32_t seq_ = 1;
    bool usable_ = true;
};

} // namespace ttt2_hw
