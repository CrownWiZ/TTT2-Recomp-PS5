#!/bin/sh
sdk=/root/m4/native/ps5-payload-sdk-mihawk
radv_root=/mnt/c/ttt2/.superpowers/m4-downloads/PS5_Vulkan
export PATH=/usr/lib/llvm-18/bin:/usr/bin:/bin
cd /root

echo "--- with the PIE unwind script ---"
$sdk/bin/prospero-lld -T "$radv_root/tooling/psbc/ps5-pie-unwind.ld" \
    -L "$radv_root/tooling/native" --eh-frame-hdr -e _start -o /root/t3.elf \
    /root/t_pad.o --start-group "$sdk"/target/lib/libc++.a "$sdk"/target/lib/libc++abi.a \
    "$sdk"/target/lib/libunwind.a "$sdk"/target/lib/libps5platform.a --end-group \
    --no-as-needed "$sdk"/target/lib/libScePad.so 2>&1 | head -8

echo "--- with the version script too ---"
$sdk/bin/prospero-lld -T "$radv_root/tooling/psbc/ps5-pie-unwind.ld" \
    -L "$radv_root/tooling/native" --eh-frame-hdr \
    --version-script "$radv_root/tooling/native/app-symbols.map" -e _start -o /root/t4.elf \
    /root/t_pad.o --no-as-needed "$sdk"/target/lib/libScePad.so 2>&1 | head -8

echo "--- what app-symbols.map says ---"
cat "$radv_root/tooling/native/app-symbols.map"
