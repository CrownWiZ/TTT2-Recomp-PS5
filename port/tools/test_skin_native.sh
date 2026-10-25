#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-$repo_root}
ppc_dir=$game_source/re/ppc
xenon=$game_source/tools/XenonRecomp
build_dir=${TTT2_SKIN_TEST_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/skin-native-test}
mkdir -p "$build_dir"

python3 - "$ppc_dir/ppc_recomp.239.cpp" "$build_dir/reference.cpp" <<'PY'
from pathlib import Path
import re
import sys

source = Path(sys.argv[1]).read_text()
bodies = []
for name in ("__imp__sub_82B10A70", "__imp__sub_82B10F40"):
    match = re.search(r"PPC_FUNC_IMPL\(" + re.escape(name) + r"\) \{.*?^\}",
                      source, re.MULTILINE | re.DOTALL)
    if not match:
        sys.exit("Missing generated reference function: " + name)
    bodies.append(match[0])
Path(sys.argv[2]).write_text('#include "ppc_recomp_shared.h"\n' + '\n'.join(bodies))
PY

compiler=${CXX:-clang++}
flags=(-std=c++20 -O2 -ffp-contract=off)
binary=$build_dir/test_skin_native
if [[ ${1:-} == --android ]]; then
    ndk=${TTT2_ANDROID_NDK:?Set TTT2_ANDROID_NDK}
    headers=${TTT2_ANDROID_PPC_HEADERS:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/android-ppc-arm64-o2/headers}
    compiler=$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android33-clang++
    flags+=(-march=armv8-a -static-libstdc++ -I "$headers"
            -include "$headers/ppc_arm64_compat.h")
else
    flags+=(-march=x86-64-v2)
fi
"$compiler" "${flags[@]}" \
    -I "$repo_root/port/src/runtime" -I "$ppc_dir" \
    -I "$xenon/XenonUtils" -I "$xenon/thirdparty/simde" \
    -include "$repo_root/port/src/runtime/ppc_store_guard_linux.h" \
    "$repo_root/port/tools/test_skin_native.cpp" "$build_dir/reference.cpp" \
    -o "$binary"
if [[ ${1:-} == --android ]]; then
    serial=${TTT2_ANDROID_SERIAL:?Set TTT2_ANDROID_SERIAL}
    "${ADB:-adb}" -s "$serial" push "$binary" /data/local/tmp/ttt2-skin-native-test
    "${ADB:-adb}" -s "$serial" shell /data/local/tmp/ttt2-skin-native-test
else
    "$binary"
fi
