#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-$repo_root}
ppc_dir=$game_source/re/ppc
xenon=$game_source/tools/XenonRecomp
build_dir=${TTT2_PPC_STORE_TEST_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/ppc-store-test}
mkdir -p "$build_dir"

compiler=${CXX:-clang++}
flags=(-std=c++20 -O2 -ffp-contract=off)
binary=$build_dir/test_ppc_store_guard
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
    "$repo_root/port/tools/test_ppc_store_guard.cpp" \
    -o "$binary"
if [[ ${1:-} == --android ]]; then
    serial=${TTT2_ANDROID_SERIAL:?Set TTT2_ANDROID_SERIAL}
    "${ADB:-adb}" -s "$serial" push "$binary" /data/local/tmp/ttt2-ppc-store-test
    "${ADB:-adb}" -s "$serial" shell /data/local/tmp/ttt2-ppc-store-test
else
    "$binary"
fi
