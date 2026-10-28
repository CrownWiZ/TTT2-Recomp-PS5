#!/bin/sh
# Sweep v2: span polyfill + the late include guards; tests stay visible.
cd /mnt/c/ttt2 || exit 1

# menu_ext's SDL include guard: check whether the earlier sweep landed it.
head -c 400 port/src/runtime/kernel/menu_ext.cpp | grep -q 'TTT2_HOST_PS5' || \
    sed -i '1s|^#include <SDL3/SDL.h>$|#if !defined(TTT2_HOST_PS5)\n\#include <SDL3/SDL.h>\n\#endif|' \
        port/src/runtime/kernel/menu_ext.cpp
# (line-1-only sed: the SDL include is the first line of the file.)

# std::span -> the compat alias; include the compat header where missing.
grep -rl 'std::span' port/src | while IFS= read -r f; do
  sed -i 's/std::span</ttt2_c20::span</g; s/std::spandynamic/ttt2_c20::span/g' "$f"
  grep -q 'cxx20_compat' "$f" || sed -i 's|#include <atomic>|#include <atomic>\n\#include "cxx20_compat.h"|' "$f"
  grep -q 'cxx20_compat' "$f" || sed -i '1s|^|#include "cxx20_compat.h"\n|' "$f"
done
grep -rl 'std::span' port/src 2>/dev/null | head -3
echo SPAN_SWEEP_DONE
