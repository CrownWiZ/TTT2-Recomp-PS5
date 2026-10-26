#include "host_vm.h"

#if !defined(_WIN32)
#error "The Windows guest VM backend requires Windows"
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace host_vm {
namespace {
uint8_t* g_base = nullptr;
size_t g_bytes = 0;

bool InGuest(const void* address, size_t bytes)
{
    if (!g_base || !address || !bytes) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    const uintptr_t base = reinterpret_cast<uintptr_t>(g_base);
    const uintptr_t start = reinterpret_cast<uintptr_t>(address);
    if (start < base || start - base >= g_bytes || bytes > g_bytes - (start - base))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    return true;
}

bool Readable(DWORD protection)
{
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    switch (protection & 0xFF)
    {
    case PAGE_READONLY: case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ: case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
        return true;
    default: return false;
    }
}

bool Writable(DWORD protection)
{
    if (protection & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    switch (protection & 0xFF)
    {
    case PAGE_READWRITE: case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READWRITE: case PAGE_EXECUTE_WRITECOPY:
        return true;
    default: return false;
    }
}
} // namespace

uint8_t* ReserveGuest(size_t bytes)
{
    if (g_base || !bytes || (bytes & 4095))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    auto* base = static_cast<uint8_t*>(VirtualAlloc(nullptr, bytes,
        MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
    if (base) { g_base = base; g_bytes = bytes; }
    return base;
}

void ReleaseGuest(uint8_t* base, size_t bytes)
{
    if (base != g_base || bytes != g_bytes || !base) return;
    if (VirtualFree(base, 0, MEM_RELEASE)) { g_base = nullptr; g_bytes = 0; }
}

bool Protect(void* address, size_t bytes, Protection protection)
{
    if (!InGuest(address, bytes)) return false;
    const DWORD native = protection == Protection::None ? PAGE_NOACCESS :
        protection == Protection::ReadOnly ? PAGE_READONLY : PAGE_READWRITE;
    DWORD old = 0;
    return VirtualProtect(address, bytes, native, &old) != 0;
}

bool QueryReadRegion(const void* address, size_t maxBytes,
                     size_t& regionBytes, bool& readable)
{
    regionBytes = 0;
    readable = false;
    if (!InGuest(address, 1) || !maxBytes) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(address, &region, sizeof(region))) return false;
    const auto start = reinterpret_cast<uintptr_t>(address);
    const auto end = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
    const auto guestEnd = reinterpret_cast<uintptr_t>(g_base) + g_bytes;
    regionBytes = (std::min)(maxBytes, size_t((std::min)(end, guestEnd) - start));
    readable = region.State == MEM_COMMIT && Readable(region.Protect);
    return regionBytes != 0;
}

bool ReadMemory(const void* address, void* output, size_t bytes)
{
    if (!bytes) return true;
    if (!output || !InGuest(address, bytes)) return false;
    SIZE_T read = 0;
    return ReadProcessMemory(GetCurrentProcess(), address, output, bytes, &read) && read == bytes;
}

bool CanWrite(const void* address, size_t bytes)
{
    if (!bytes) return true;
    if (!InGuest(address, bytes)) return false;
    auto* at = static_cast<const uint8_t*>(address);
    const auto* end = at + bytes;
    while (at < end)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(at, &region, sizeof(region)) || region.State != MEM_COMMIT ||
            !Writable(region.Protect)) return false;
        const auto* next = static_cast<const uint8_t*>(region.BaseAddress) + region.RegionSize;
        if (next <= at) return false;
        at = next;
    }
    return true;
}

bool PrepareHostWrite(void* address, size_t bytes)
{
    // Guest writes fault through the rollback VEH. Explicit host writes use the
    // same observer before the OS writes into a protected guest destination.
    extern void ObserveRollbackHostWrite(void*, size_t);
    ObserveRollbackHostWrite(address, bytes);
    return CanWrite(address, bytes);
}

bool ResetWriteWatch(void* address, size_t bytes)
{
    if (!InGuest(address, bytes)) return false;
    const UINT result = ::ResetWriteWatch(address, bytes);
    if (result) SetLastError(result);
    return result == 0;
}

bool TakeWriteWatch(void* address, size_t bytes, void** pages,
                    size_t capacity, size_t& count, bool reset)
{
    count = 0;
    if (!InGuest(address, bytes) || (!pages && capacity))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    ULONG_PTR found = capacity;
    DWORD granularity = 0;
    const UINT result = GetWriteWatch(reset ? WRITE_WATCH_FLAG_RESET : 0,
        address, bytes, pages, &found, &granularity);
    if (result) { SetLastError(result); return false; }
    count = found;
    return true;
}

bool InstallMirrorBacking(void* address, size_t bytes)
{
    // Windows fills the cached view page by page; it does not need a second mapping.
    return InGuest(address, bytes);
}

bool FillMirrorView(void* address, const void* source, size_t bytes)
{
    if (!source || !InGuest(address, bytes) ||
        (reinterpret_cast<uintptr_t>(address) & 4095) || (bytes & 4095))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }
    auto* destination = static_cast<uint8_t*>(address);
    auto* input = static_cast<const uint8_t*>(source);
    for (size_t offset = 0; offset < bytes; offset += 4096)
    {
        if (!Protect(destination + offset, 4096, Protection::ReadWrite)) return false;
        std::memcpy(destination + offset, input + offset, 4096);
    }
    return ResetWriteWatch(address, bytes);
}

namespace {
CaptureRollbackWriter g_captureRollback = nullptr;
RollbackWriteHandler g_rollbackWrite = nullptr;
LONG CALLBACK RollbackVeh(EXCEPTION_POINTERS* ep)
{
    const auto* r = ep->ExceptionRecord;
    if (r->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || r->NumberParameters < 2 ||
        r->ExceptionInformation[0] != 1 || !g_rollbackWrite) return EXCEPTION_CONTINUE_SEARCH;
    const uintptr_t address = r->ExceptionInformation[1];
    if (g_rollbackWrite(address, g_captureRollback(address))) return EXCEPTION_CONTINUE_EXECUTION;
    // Another writer can open the page before this queued fault is delivered.
    return InGuest(reinterpret_cast<void*>(address), 1) && CanWrite(reinterpret_cast<void*>(address), 1)
        ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}
}
void ObserveRollbackHostWrite(void* address, size_t bytes)
{
    if (!g_rollbackWrite || !bytes || !InGuest(address, bytes)) return;
    const uintptr_t first = uintptr_t(address) & ~uintptr_t(4095);
    for (uintptr_t a = first; a < uintptr_t(address) + bytes; a += 4096)
        g_rollbackWrite(a, g_captureRollback(a));
}
bool RegisterRollbackWriteHandler(CaptureRollbackWriter capture, RollbackWriteHandler handler)
{
    if (!capture || !handler || g_rollbackWrite) return false;
    g_captureRollback = capture;
    g_rollbackWrite = handler;
    if (AddVectoredExceptionHandler(1, RollbackVeh)) return true;
    g_rollbackWrite = nullptr;
    g_captureRollback = nullptr;
    return false;
}
bool ProtectForRollback(void* address, size_t bytes, bool readOnly)
{
    DWORD old = 0;
    return InGuest(address, bytes) && VirtualProtect(address, bytes,
        readOnly ? PAGE_READONLY : PAGE_READWRITE, &old) != 0;
}

unsigned long LastError() { return GetLastError(); }
} // namespace host_vm
