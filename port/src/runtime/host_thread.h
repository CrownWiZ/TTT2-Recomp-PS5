#pragma once

#include <cstddef>
#include <cstdint>

// Fire-and-forget host threads with an explicit stack reservation. The guest's
// recompiled call chains need more stack than a typical host default.
namespace host_thread {
using Procedure = unsigned (*)(void*);
bool StartDetached(size_t stackBytes, Procedure procedure, void* argument);
uint32_t CurrentId();
void Pause();
// Logical CPUs available to this process for host-side worker pools.
unsigned AvailableProcessors();

// A managed thread starts behind a gate so the caller can publish its handle
// before the guest callback runs. Guest suspension after startup is handled by
// the kernel object model, not by stopping the host thread asynchronously.
// Closing an unreleased handle cancels its pending callback.
struct Handle;
Handle* StartManaged(size_t stackBytes, Procedure procedure, void* argument);
bool ReleaseStart(Handle* handle);
Handle* Current();
void Close(Handle* handle);
bool PinToGuestCpu(Handle* handle, uint32_t guestMask);
bool SetRelativePriority(Handle* handle, int32_t priority);
}
