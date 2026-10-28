#!/bin/sh
sdk=/root/m4/native/ps5-payload-sdk-mihawk
export PATH=/usr/lib/llvm-18/bin:/usr/bin:/bin
export PS5_PAYLOAD_SDK=$sdk
ROOT=/mnt/c/ttt2
runtime=$ROOT/port/src/runtime
for src in audio_output_ps5.cpp host_input_ps5.cpp host_udp_linux.cpp; do
  echo "=== $src ==="
  "$sdk/bin/prospero-clang++" -O2 -Wno-everything \
      -DTTT2_HOST_PS5=1 -DTTT2_LOCAL_SAVES=1 -DTTT2_HAVE_STEAM=0 \
      -I"$runtime" -I"$runtime/kernel" \
      -c "$runtime/$src" -o /tmp/probe_$src.o 2>&1 | grep -E 'error:' | head -5
done
