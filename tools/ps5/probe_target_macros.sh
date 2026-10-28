#!/bin/sh
# Which target macros does the PS5 cross toolchain define?
PS5_PAYLOAD_SDK=/root/m4/native/ps5-payload-sdk-mihawk \
  sh /mnt/c/ttt2/.superpowers/m4-downloads/PS5_Vulkan/tooling/prospero-clang18 \
  -dM -E - </dev/null 2>/dev/null | grep -iE 'ps5|prospero|sce|linux|freebsd|unix|playstation' | sort
PS5_PAYLOAD_SDK=/root/m4/native/ps5-payload-sdk-mihawk \
  sh /mnt/c/ttt2/.superpowers/m4-downloads/PS5_Vulkan/tooling/prospero-clang18 \
  -dM -E -x c++ - </dev/null 2>/dev/null | grep -cE '__cplusplus'
