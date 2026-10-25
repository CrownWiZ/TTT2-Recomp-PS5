#include "kernel/kernel_shared.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <sys/mman.h>

extern "C" PPC_FUNC(__imp__sub_82B1D970);
namespace {
constexpr uint32_t control = 0x830B8E70, pending = control + 88;
constexpr uint32_t queue = 0x100000, storage = queue + 32, capacity = 256;
std::recursive_mutex queueMutex;
thread_local int lockDepth = 0;
bool checkProducerLock = true;
bool raceEnabled = false;
thread_local bool raceProducer = false;
std::atomic<bool> consumerCountRead{false}, producerLockAttempted{false};
}

PPC_FUNC(sub_82CFEF90) {
    assert(ctx.r3.u32 == control);
    if (raceEnabled && raceProducer) {
        producerLockAttempted.store(true);
        producerLockAttempted.notify_one();
    }
    queueMutex.lock(); ++lockDepth;
    ctx.r3.u64 = 0;
}
PPC_FUNC(sub_82CFEFA8) {
    assert(ctx.r3.u32 == control && lockDepth > 0);
    --lockDepth; queueMutex.unlock();
    ctx.r3.u64 = 0;
}
PPC_FUNC(sub_82508EF8) {
    if (checkProducerLock) assert(lockDepth > 0);
    ctx.r3.u64 = 0;
}
PPC_FUNC(sub_82B1C6D8) { assert(false); } // Frame markers have no object reference.
PPC_FUNC(sub_82BD3DF0) {
    std::memcpy(base + ctx.r3.u32, base + ctx.r4.u32, ctx.r5.u32);
}
PPC_FUNC(__savegprlr_29) {
    PPC_STORE_U64(ctx.r1.u32 - 32, ctx.r29.u64);
    PPC_STORE_U64(ctx.r1.u32 - 24, ctx.r30.u64);
    PPC_STORE_U64(ctx.r1.u32 - 16, ctx.r31.u64);
    PPC_STORE_U32(ctx.r1.u32 - 8, ctx.r12.u32);
}
PPC_FUNC(__restgprlr_29) {
    ctx.r29.u64 = PPC_LOAD_U64(ctx.r1.u32 - 32);
    ctx.r30.u64 = PPC_LOAD_U64(ctx.r1.u32 - 24);
    ctx.r31.u64 = PPC_LOAD_U64(ctx.r1.u32 - 16);
    ctx.lr = PPC_LOAD_U32(ctx.r1.u32 - 8);
}

// Pause the generated consumer after loading the count, while it still owns
// the queue mutex. Let the producer run until it tries to acquire that mutex.
// The original has already incremented by then; the fixed producer has not.
void SoundQueueTestAfterCountRead() {
    if (!raceEnabled) return;
    consumerCountRead.store(true); consumerCountRead.notify_one();
    producerLockAttempted.wait(false);
}

static void Reset(uint8_t* base) {
    std::memset(base + queue, 0, 32 + capacity);
    GWrite32(base, queue, 0x626E7175);
    GWrite32(base, queue + 4, storage);
    GWrite32(base, queue + 8, capacity);
    GWrite32(base, control + 120, queue);
    GWrite32(base, pending, 0);
}
static uint32_t Count(uint8_t* base) {
    uint32_t read = GRead32(base, queue + 16), write = GRead32(base, queue + 12);
    uint32_t count = 0;
    while (read != write) {
        assert(read < capacity && count < capacity / 8);
        uint32_t magic = GRead32(base, storage + read);
        if (magic == 0x626E7164) { read = 0; continue; }
        assert(magic == 0x626E7170);
        assert(GRead32(base, storage + read + 8) == 0x66656E63);
        assert(GRead32(base, storage + read + 4) == 16);
        ++count; read = (read + 24) % capacity;
    }
    return count;
}
static bool Produce(uint8_t* base, bool fixed) {
    PPCContext ctx{};
    ctx.r1.u64 = 0x200000; ctx.lr = 0x12345678;
    ctx.r29.u64 = 29; ctx.r30.u64 = 30; ctx.r31.u64 = 31;
    if (fixed) sub_82B1D970(ctx, base); else __imp__sub_82B1D970(ctx, base);
    assert(ctx.r1.u32 == 0x200000 && ctx.lr == 0x12345678);
    assert(ctx.r29.u64 == 29 && ctx.r30.u64 == 30 && ctx.r31.u64 == 31);
    assert(lockDepth == 0);
    return ctx.r3.s32 >= 0;
}
static void Consume(uint8_t* base) {
    // The real consumer holds this same mutex while peeking, applying and
    // removing commands. Execute its generated fence handler and ring helpers.
    std::lock_guard guard(queueMutex);
    if (!GRead32(base, pending)) return;
    PPCContext ctx{}; ctx.r1.u64 = 0x300000;
    ctx.r3.u64 = queue; ctx.r4.u64 = 0x300100; ctx.r5.u64 = 0x300104;
    sub_82CFF228(ctx, base);
    assert(ctx.r3.s32 == 0);
    ctx.r3.u64 = GRead32(base, 0x300100);
    sub_82B1D298(ctx, base);
    assert(ctx.r3.s32 == 0);
    ctx.r3.u64 = queue; ctx.r4.u64 = 0x300100; ctx.r5.u64 = 0x300104;
    sub_82CFF2D8(ctx, base);
    assert(ctx.r3.s32 == 0);
    if (!raceEnabled) assert(GRead32(base, pending) == Count(base));
}
int main() {
    constexpr size_t bytes = size_t{1} << 32;
    auto* base = static_cast<uint8_t*>(mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    assert(base != MAP_FAILED);

    for (bool fixed : {false, true}) {
        Reset(base); checkProducerLock = fixed;
        assert(Produce(base, fixed));
        raceEnabled = true;
        consumerCountRead.store(false); producerLockAttempted.store(false);
        std::thread consumer([&] { Consume(base); });
        consumerCountRead.wait(false);
        raceProducer = true;
        assert(Produce(base, fixed));
        raceProducer = false;
        consumer.join(); raceEnabled = false;
        assert(Count(base) == 1);
        assert(GRead32(base, pending) == (fixed ? 1u : 0u));
    }

    // Demonstrate the original bounded-ring failure before checking the fix.
    Reset(base); checkProducerLock = false;
    while (Produce(base, false)) {}
    assert(GRead32(base, pending) == Count(base) + 1);

    Reset(base); checkProducerLock = true;
    while (Produce(base, true)) {}
    assert(GRead32(base, pending) == Count(base));
    while (GRead32(base, pending)) Consume(base);
    assert(Count(base) == 0);

    // Exercise wraparound, backpressure and concurrent producer/consumer use.
    Reset(base);
    std::atomic<bool> done{false};
    std::thread consumer([&] {
        for (;;) {
            Consume(base);
            if (done.load()) {
                std::lock_guard guard(queueMutex);
                if (!GRead32(base, pending)) break;
            }
            std::this_thread::yield();
        }
    });
    for (unsigned n = 0; n < 20000; ++n)
        while (!Produce(base, true)) std::this_thread::yield();
    done.store(true); consumer.join();
    assert(Count(base) == 0 && GRead32(base, pending) == 0);
    munmap(base, bytes);
    std::puts("sound queue: original lost increment and overflow reproduced; fixed race, overflow, ABI and concurrent wraparound passed");
}
