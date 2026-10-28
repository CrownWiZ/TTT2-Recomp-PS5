// PS5 host threads (interface: host_thread.h) — pthreads without SDL.
//
// Semantics mirror the SDL backend exactly: managed threads start behind a
// gate whose handle the caller can publish first (ReleaseStart / cancel-on-
// Close), detached fire-and-forget threads carry an explicit stack
// reservation, and closing from a managed callback never waits for it. The
// payload SDK's platform layer wraps pthread_create/join/detach with 2 MiB
// stacks in direct memory (the radv-link --wrap set), so the guest's call
// depth needs no attribute sizing; the explicit attr stays harmless.
#include "host_thread.h"

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <new>
#include <pthread.h>
#include <thread>
#include <unistd.h>
#if defined(__x86_64__)
#include <immintrin.h>
#endif

namespace host_thread {

struct StartGate
{
    std::mutex mutex;
    std::condition_variable ready;
    bool released = false;
    bool cancelled = false;
    Procedure procedure = nullptr;
    void* argument = nullptr;
};

struct Handle
{
    pthread_t thread = 0;
    bool hasThread = false;
    std::shared_ptr<StartGate> gate;
};

namespace {
struct StartContext { Procedure procedure; void* argument; };

void* DetachedEntry(void* raw)
{
    std::unique_ptr<StartContext> context(static_cast<StartContext*>(raw));
    return reinterpret_cast<void*>(static_cast<intptr_t>(context->procedure(context->argument)));
}

void* ManagedEntry(void* raw)
{
    std::unique_ptr<std::shared_ptr<StartGate>> owner(
        static_cast<std::shared_ptr<StartGate>*>(raw));
    auto gate = std::move(*owner);
    owner.reset();
    Procedure procedure = nullptr;
    void* argument = nullptr;
    {
        std::unique_lock lock(gate->mutex);
        gate->ready.wait(lock, [&] { return gate->released; });
        if (gate->cancelled) return nullptr;
        procedure = gate->procedure;
        argument = gate->argument;
    }
    return reinterpret_cast<void*>(static_cast<intptr_t>(procedure(argument)));
}
} // namespace

bool StartDetached(size_t stackBytes, Procedure procedure, void* argument)
{
    if (!procedure) return false;
    auto context = std::unique_ptr<StartContext>(new (std::nothrow) StartContext{procedure, argument});
    if (!context) return false;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return false;
    if (stackBytes) pthread_attr_setstacksize(&attributes, stackBytes);
    pthread_t thread = 0;
    const int error = pthread_create(&thread, &attributes, DetachedEntry, context.get());
    pthread_attr_destroy(&attributes);
    if (error != 0) return false;
    context.release();
    pthread_detach(thread);
    return true;
}

uint32_t CurrentId()
{
    // pthread_t is opaque. Hash its representation for a run-stable value; the
    // guest only compares ids that outlived one kernel thread each.
    const pthread_t self = pthread_self();
    const unsigned long raw = *reinterpret_cast<const unsigned long*>(&self);
    return static_cast<uint32_t>(std::hash<unsigned long>{}(raw));
}

void Pause()
{
#if defined(__x86_64__)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

unsigned AvailableProcessors()
{
    const long online = sysconf(_SC_NPROCESSORS_ONLN);
    return online > 0 ? static_cast<unsigned>(online) : 1;
}

Handle* StartManaged(size_t stackBytes, Procedure procedure, void* argument)
{
    if (!procedure) return nullptr;
    auto handle = std::unique_ptr<Handle>(new (std::nothrow) Handle);
    if (!handle) return nullptr;
    try { handle->gate = std::make_shared<StartGate>(); }
    catch (const std::bad_alloc&) { return nullptr; }
    handle->gate->procedure = procedure;
    handle->gate->argument = argument;
    auto owner = std::unique_ptr<std::shared_ptr<StartGate>>(
        new (std::nothrow) std::shared_ptr<StartGate>(handle->gate));
    if (!owner) return nullptr;
    pthread_attr_t attributes;
    if (pthread_attr_init(&attributes) != 0) return nullptr;
    if (stackBytes) pthread_attr_setstacksize(&attributes, stackBytes);
    const int error = pthread_create(&handle->thread, &attributes, ManagedEntry, owner.get());
    pthread_attr_destroy(&attributes);
    if (error != 0) return nullptr;
    owner.release();
    handle->hasThread = true;
    return handle.release();
}

bool ReleaseStart(Handle* handle)
{
    if (!handle || !handle->gate) return false;
    {
        std::lock_guard lock(handle->gate->mutex);
        if (handle->gate->cancelled) return false;
        handle->gate->released = true;
    }
    handle->gate->ready.notify_one();
    return true;
}

Handle* Current()
{
    // Managed identity lives in the kernel object model; a stateless handle
    // keeps the SDL contract (a non-null pointer with no pending callback).
    auto* handle = new (std::nothrow) Handle;
    return handle;
}

void Close(Handle* handle)
{
    if (!handle) return;
    if (auto gate = std::move(handle->gate))
    {
        {
            std::lock_guard lock(gate->mutex);
            if (!gate->released) { gate->cancelled = true; gate->released = true; }
        }
        gate->ready.notify_one();
    }
    // Closing from the managed callback itself must not wait for that callback.
    if (handle->hasThread) pthread_detach(handle->thread);
    delete handle;
}

bool PinToGuestCpu(Handle* handle, uint32_t guestMask)
{
    // Same contract as the SDL/Linux build: the Relapse payload scheduler owns
    // placement, and the guest translation table retains its own CPU model.
    (void)handle; (void)guestMask;
    return false;
}

bool SetRelativePriority(Handle* handle, int32_t priority)
{
    // Guest priority is retained by the KThread object (SDL comment); the
    // console scheduler offers no equivalent unprivileged relative priority.
    (void)handle; (void)priority;
    return true;
}

} // namespace host_thread
