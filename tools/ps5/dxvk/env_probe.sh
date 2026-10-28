#!/bin/sh
# Env probe for the Task-4 cross build (runs in WSL).
for s in ps5-payload-sdk ps5-payload-sdk-mihawk; do
  echo "== $s =="
  git -C /root/m4/native/$s rev-parse HEAD 2>/dev/null || echo NO_GIT
  ls /root/m4/native/$s/target/include/vulkan/vulkan.h 2>/dev/null || echo NO_VULKAN_HDR
done
echo "== radv builds =="
ls -d /root/m4/radv-build2 2>/dev/null
ls /root/m4/radv-build2/*.a 2>/dev/null | head -5
find /root/m4/radv-build2 -name 'libvulkan_radeon*' 2>/dev/null | head -5
