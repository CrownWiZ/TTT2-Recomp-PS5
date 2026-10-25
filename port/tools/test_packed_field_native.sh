#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-$repo_root}
ppc_dir=$game_source/re/ppc
xenon=$game_source/tools/XenonRecomp
build_dir=${TTT2_PACKED_FIELD_TEST_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/packed-field-test}
image=${TTT2_GAME_IMAGE:-$game_source/re/decrypted/Default_patched_tu3.bin}
mkdir -p "$build_dir"

python3 - "$ppc_dir" "$build_dir/reference.cpp" <<'PY'
from pathlib import Path
import re
import sys

source = '\n'.join((Path(sys.argv[1]) / f'ppc_recomp.{unit}.cpp').read_text()
                   for unit in (235, 263, 264, 265))
names = ('sub_82AE9C28', '__restfpr_16', '__restfpr_25', '__restgprlr_14',
         '__restvmx_120', '__savefpr_16', '__savefpr_25', '__savegprlr_14',
         '__savevmx_120', 'sub_82BD3DF0', 'sub_82BD4D00', 'sub_82BD8AC0',
         'sub_82BD8B30', 'sub_82BD8CB8', 'sub_82BD9370', 'sub_82BDA8D8',
         'sub_82BDED60', 'sub_82BDED88', 'sub_82BDEE28')
bodies = ['#include "ppc_recomp_shared.h"']
for name in names:
    symbol = '__imp__' + name
    match = re.search(r'PPC_FUNC_IMPL\(' + re.escape(symbol) + r'\) \{.*?^\}',
                      source, re.MULTILINE | re.DOTALL)
    if not match:
        sys.exit('Missing generated reference function: ' + symbol)
    bodies.append(f'__attribute__((alias("{symbol}"))) PPC_WEAK_FUNC({name});')
    bodies.append(match[0])
Path(sys.argv[2]).write_text('\n'.join(bodies) + '\n')
PY

compiler=${CXX:-clang++}
flags=(-std=c++20 -O2 -ffp-contract=off)
binary=$build_dir/test_packed_field_native
if [[ ${1:-} == --android ]]; then
    ndk=${TTT2_ANDROID_NDK:?Set TTT2_ANDROID_NDK}
    headers=${TTT2_ANDROID_PPC_HEADERS:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/android-ppc-arm64-o2/headers}
    compiler=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang++
    flags+=(-march=armv8-a -static-libstdc++ -I "$headers"
            -include "$headers/ppc_arm64_compat.h")
else
    flags+=(-march=x86-64-v2)
fi
includes=(-I "$repo_root/port/src/runtime" -I "$ppc_dir"
          -I "$xenon/XenonUtils" -I "$xenon/thirdparty/simde")
"$compiler" "${flags[@]}" "${includes[@]}" \
    -include "$repo_root/port/src/runtime/ppc_store_guard_linux.h" \
    -c "$build_dir/reference.cpp" -o "$build_dir/reference.o"
"$compiler" "${flags[@]}" "${includes[@]}" \
    "$repo_root/port/tools/test_packed_field_native.cpp" "$build_dir/reference.o" \
    -o "$binary"
if [[ ${1:-} == --android ]]; then
    serial=${TTT2_ANDROID_SERIAL:?Set TTT2_ANDROID_SERIAL}
    adb=${ADB:-adb}
    "$adb" -s "$serial" push "$binary" /data/local/tmp/ttt2-packed-field-test
    "$adb" -s "$serial" push "$image" /data/local/tmp/ttt2-packed-field-image.bin
    "$adb" -s "$serial" shell /data/local/tmp/ttt2-packed-field-test /data/local/tmp/ttt2-packed-field-image.bin
else
    "$binary" "$image"
fi
