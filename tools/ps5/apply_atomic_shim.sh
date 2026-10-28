#!/bin/sh
# Apply the atomic_ref shim to the three kernel users of it.
cd /mnt/c/ttt2/port/src/runtime/kernel || exit 1
for f in kernel_misc.cpp kernel_video.cpp d3dx_ctab_cache.cpp; do
  sed -i 's/std::atomic_ref<uint32_t>/ttt2_c20::atomic_ref<uint32_t>/g' "$f"
  grep -q 'atomic_ref_compat' "$f" || sed -i '0,/#include <atomic>/s||#include <atomic>\n#include "atomic_ref_compat.h"|' "$f"
done
grep -c 'ttt2_c20::atomic_ref' kernel_misc.cpp kernel_video.cpp d3dx_ctab_cache.cpp
grep -c 'atomic_ref_compat' kernel_misc.cpp kernel_video.cpp d3dx_ctab_cache.cpp
