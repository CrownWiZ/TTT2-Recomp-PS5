#pragma once

// The generated XenonRecomp C++ uses Clang builtins. GCC does not provide
// these spellings, so the native GCC build injects this header before each
// C++ translation unit. Keep the generated game code and external tool tree
// untouched.
#if defined(__GNUC__) && !defined(__clang__)
#include <bit>
#include <cstdint>
#if defined(__x86_64__)
#include <x86intrin.h>
#endif

#define __builtin_rotateleft32(value, shift) std::rotl(static_cast<uint32_t>(value), static_cast<int>(static_cast<unsigned>(shift) & 31u))
#define __builtin_rotateleft64(value, shift) std::rotl(static_cast<uint64_t>(value), static_cast<int>(static_cast<unsigned>(shift) & 63u))
#define __builtin_assume(condition) do { if (!(condition)) __builtin_unreachable(); } while (0)
#if defined(__x86_64__)
#define __builtin_debugtrap() __asm__ __volatile__("int3")
#else
#define __builtin_debugtrap() __builtin_trap()
#endif
#endif
