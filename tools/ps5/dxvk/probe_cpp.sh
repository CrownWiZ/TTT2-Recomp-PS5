#!/bin/sh
# How did the successful RADV cross build detect its C++ compiler/linker?
grep -E "C\+\+ (compiler|linker) for the host machine" /root/m4/radv-build2/meson-logs/meson-log.txt | head -4
echo "=== SDK's own cross file ==="
cat /root/m4/native/ps5-payload-sdk-mihawk/toolchain/prospero.ini 2>/dev/null || ls /root/m4/native/ps5-payload-sdk-mihawk/toolchain/ 2>/dev/null
echo "=== prospero-clang++ identity ==="
file /root/m4/native/ps5-payload-sdk-mihawk/bin/prospero-clang++ 2>/dev/null | head -2
/root/m4/native/ps5-payload-sdk-mihawk/bin/prospero-clang++ --version 2>/dev/null | head -2
