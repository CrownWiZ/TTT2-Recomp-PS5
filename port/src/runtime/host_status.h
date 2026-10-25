#pragma once

#include <cstdint>

// Translate an Xbox/NTSTATUS value to the Win32-compatible DOS error number
// expected by the guest's RtlNtStatusToDosError import.
namespace host_status {
uint32_t NtStatusToDosError(uint32_t status);
}
