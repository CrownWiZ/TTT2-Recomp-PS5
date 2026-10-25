#include "kernel_shared.h"

extern "C" PPC_FUNC(__imp__sub_82B1D970);

// NUSound's frame-end producer increments its pending-frame count before
// sub_82B1D228 takes the command-queue mutex. The audio thread decrements the
// same count under that mutex in sub_82B1D298. Protect the whole transaction:
// a lost increment leaves commands queued after the consumer believes it has
// caught up, and every subsequent frame only advances the old backlog.
PPC_FUNC(sub_82B1D970)
{
    constexpr uint32_t queueMutex = 0x830B8E70;
    constexpr uint32_t pendingFrames = queueMutex + 88;

    // Guest mutants are recursive; the original enqueue takes this mutex
    // again. Use a separate register context for lock calls so the original
    // function retains its arguments, return value and guest calling convention.
    PPCContext lockContext = ctx;
    lockContext.r3.u64 = queueMutex;
    sub_82CFEF90(lockContext, base);

    const uint32_t before = GRead32(base, pendingFrames);
    __imp__sub_82B1D970(ctx, base);
    // The original increments even when the bounded command ring is full.
    // No frame marker was published on failure, so no credit may remain.
    if (ctx.r3.s32 < 0) GWrite32(base, pendingFrames, before);

    lockContext = ctx;
    lockContext.r3.u64 = queueMutex;
    sub_82CFEFA8(lockContext, base);
}
