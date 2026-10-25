#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-$repo_root}
ppc_dir=$game_source/re/ppc
xenon=$game_source/tools/XenonRecomp
build_dir=${TTT2_AUDIO_MIXER_TEST_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/audio-mixer-test}
mkdir -p "$build_dir"

python3 - "$ppc_dir" "$build_dir/reference.cpp" <<'PYREF'
from pathlib import Path
import re
import sys
ppc, output = Path(sys.argv[1]), Path(sys.argv[2])
def extract(unit, name):
    source = (ppc / unit).read_text()
    match = re.search(r"PPC_FUNC_IMPL\(" + re.escape(name) + r"\) \{.*?^\}", source, re.MULTILINE | re.DOTALL)
    if not match:
        sys.exit("Missing generated reference: " + name)
    return match[0]
source = '#include "ppc_store_guard_linux.h"\n'
source += extract('ppc_recomp.293.cpp', '__imp__sub_82D0FCB0').replace('__imp__sub_82D0FCB0', 'mix_reference') + '\n'
output.write_text(source)
source = '#include "ppc_store_guard_linux.h"\n'
for helper in ('__savegprlr_17', '__restgprlr_17'):
    source += extract('ppc_recomp.263.cpp', '__imp__' + helper).replace('PPC_FUNC_IMPL(__imp__' + helper + ')', 'PPC_FUNC(' + helper + ')') + '\n'
output.with_name('helpers.cpp').write_text(source)
PYREF

compiler=${CXX:-clang++}
flags=(-std=c++20 -O2 -ffp-contract=off)
binary=$build_dir/test_audio_mixer_native
if [[ ${1:-} == --android || ${1:-} == --android-build-only ]]; then
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
    "$repo_root/port/tools/test_audio_mixer_native.cpp" "$build_dir/reference.cpp" "$build_dir/helpers.cpp" \
    -o "$binary"
if [[ ${1:-} == --android ]]; then
    serial=${TTT2_ANDROID_SERIAL:?Set TTT2_ANDROID_SERIAL}
    "${ADB:-adb}" -s "$serial" push "$binary" /data/local/tmp/ttt2-audio-mixer-test
    "${ADB:-adb}" -s "$serial" shell /data/local/tmp/ttt2-audio-mixer-test
elif [[ ${1:-} != --android-build-only ]]; then
    "$binary"
fi
