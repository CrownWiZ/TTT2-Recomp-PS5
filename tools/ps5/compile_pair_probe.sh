#!/bin/sh
sdk=/root/m4/native/ps5-payload-sdk-mihawk
export PATH=/usr/lib/llvm-18/bin:/usr/bin:/bin
export PS5_PAYLOAD_SDK=$sdk
cache=/root/.cache/ttt2-platform
dxvk_src=$(ls -dt $cache/shared-dxvk/source-*/ | head -n 1)
ROOT=/mnt/c/ttt2
runtime=$ROOT/port/src/runtime
ppc_dir=$ROOT/re/ppc
for src in kernel/diag_hooks_ps5.cpp kernel/gpu_render.cpp; do
  echo "=== $src ==="
  "$sdk/bin/prospero-clang++" -O2 -Wno-everything \
      -DTTT2_HOST_PS5=1 -DTTT2_LOCAL_SAVES=1 -DTTT2_HAVE_STEAM=0 \
      -DTTT2_GPU_HW_BACKEND=1 -DTTT2_GPU_HW_D3D11=1 -DTTT2_D3D11_MOVIE_BACKEND=1 \
      -I"$ppc_dir" \
      -I"$ROOT/tools/XenonRecomp/XenonUtils" \
      -I"$ROOT/tools/XenonRecomp/thirdparty/simde" \
      -I"$runtime" -I"$runtime/kernel" \
      -I"$dxvk_src/include" -I"$dxvk_src/include/native/directx" -I"$dxvk_src/include/native/windows" \
      -I"$ROOT/port/native/include" \
      -I"$cache/ps5-ffmpeg-xma/build" -I"$cache/ps5-ffmpeg-xma/source" \
      -c "$runtime/$src" -o /tmp/probe_pair.o 2>&1 | grep -E 'error:' | head -6
done
