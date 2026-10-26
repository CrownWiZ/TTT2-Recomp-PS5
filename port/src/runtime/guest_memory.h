#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Guest low memory is readable but ignores writes. Keep host imports and the
// generated Linux PPC adapter on the same boundary and preserve bytes of a
// scalar store that cross into ordinary guest memory.
namespace guest_memory {
constexpr uint32_t NullGuardBytes = 0x10000;

inline size_t GuardedPrefix(const uint8_t* base, const void* destination, size_t bytes)
{
    const uintptr_t offset = reinterpret_cast<uintptr_t>(destination) -
                             reinterpret_cast<uintptr_t>(base);
    if (offset >= NullGuardBytes) return 0;
    const size_t guarded = NullGuardBytes - offset;
    return bytes < guarded ? bytes : guarded;
}

inline void CopyToGuest(uint8_t* base, void* destination, const void* source, size_t bytes)
{
    const size_t ignored = GuardedPrefix(base, destination, bytes);
    if (ignored < bytes)
        std::memcpy(static_cast<uint8_t*>(destination) + ignored,
                    static_cast<const uint8_t*>(source) + ignored, bytes - ignored);
}

inline void FillGuest(uint8_t* base, void* destination, int value, size_t bytes)
{
    const size_t ignored = GuardedPrefix(base, destination, bytes);
    if (ignored < bytes)
        std::memset(static_cast<uint8_t*>(destination) + ignored, value, bytes - ignored);
}

template <typename T>
inline void StoreScalar(uint8_t* base, uint32_t address, T value)
{
    if (address >= NullGuardBytes)
    {
        *reinterpret_cast<volatile T*>(base + address) = value;
        return;
    }
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    for (size_t i = NullGuardBytes - address; i < sizeof(T); ++i)
        *reinterpret_cast<volatile uint8_t*>(base + address + i) = bytes[i];
}
} // namespace guest_memory
