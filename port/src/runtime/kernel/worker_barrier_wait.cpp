#include "kernel_shared.h"
#include "worker_barrier_wait.h"
#include <cstdlib>

extern "C" PPC_FUNC(__imp__sub_82AD0040);
PPC_FUNC(sub_82AD0040) {
    static const int delay = [] {
        const char* value = std::getenv("TTT2_WORKER_BARRIER_WAIT_US");
        const int requested = value ? std::atoi(value) : 0;
        return requested >= -1 && requested <= 1000 ? requested : 0;
    }();
    if (delay) worker_barrier::Wait(base, ctx.r3.u32, delay);
    // The original routine rechecks the condition and owns all guest register,
    // stack, critical-section, counter, and semaphore side effects.
    __imp__sub_82AD0040(ctx, base);
}
