#!/bin/sh
# Sweep 2026-10-28: cross-libc++ compatibility edits for the PS5 port build.
cd /mnt/c/ttt2 || exit 1

# std::bit_cast -> clang's builtin (the pinned libc++ lacks the std form);
# GCC would need a polyfill, so this stays a PS5-build tree decision.
grep -rl 'std::bit_cast' port/src | while IFS= read -r f; do
  sed -i 's/std::bit_cast</__builtin_bit_cast</g' "$f"
done

# The settings backend builds on the console as a posix host.
sed -i 's|!defined(__linux__)|!defined(__linux__) \&\& !defined(TTT2_HOST_PS5)|' \
    port/src/runtime/kernel/host_settings_linux.cpp \
    port/src/runtime/kernel/host_random_linux.cpp \
    port/src/runtime/kernel/host_file_linux.cpp 2>/dev/null || true

# menu_ext includes SDL at line 1 for the clipboard glue only; guard it.
sed -i 's|^\#include <SDL3/SDL.h>$|#if !defined(TTT2_HOST_PS5)\n\#include <SDL3/SDL.h>\n\#endif|' \
    port/src/runtime/kernel/menu_ext.cpp

# The pragma comment(lib)s duplicate the CMake link line; with the ELF link
# they surface as dependent-library specifiers that no ps5 toolchain carries.
grep -rl 'pragma comment' port/src/runtime | while IFS= read -r f; do
  sed -i 's|^#pragma comment|// (cross: dropped; the link line carries the lib)|' "$f"
done

echo SWEEP_DONE
