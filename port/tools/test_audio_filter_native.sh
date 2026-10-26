#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-$repo_root}
ppc_dir=$game_source/re/ppc
xenon=$game_source/tools/XenonRecomp
build_dir=${TTT2_AUDIO_FILTER_TEST_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/audio-filter-test}
mkdir -p "$build_dir"

python3 - "$ppc_dir/ppc_recomp.292.cpp" "$build_dir/reference.cpp" <<'PYREF'
from pathlib import Path
import re
import sys
source = Path(sys.argv[1]).read_text()
match = re.search(r"PPC_FUNC_IMPL\(__imp__sub_82D0DB08\) \{.*?^\}", source, re.MULTILINE | re.DOTALL)
if not match:
    sys.exit("Missing generated audio filter reference")
Path(sys.argv[2]).write_text('#include "ppc_store_guard_linux.h"\n' + match[0].replace('__imp__sub_82D0DB08', 'filter_reference'))
PYREF

compiler=${CXX:-clang++}
flags=(-std=c++20 -O2 -ffp-contract=off)
binary=$build_dir/test_audio_filter_native
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
    "$repo_root/port/tools/test_audio_filter_native.cpp" "$build_dir/reference.cpp" \
    -o "$binary"
if [[ ${1:-} == --android ]]; then
    serial=${TTT2_ANDROID_SERIAL:?Set TTT2_ANDROID_SERIAL}
    "${ADB:-adb}" -s "$serial" push "$binary" /data/local/tmp/ttt2-audio-filter-test
    "${ADB:-adb}" -s "$serial" shell /data/local/tmp/ttt2-audio-filter-test
else
    "$binary"
fi
