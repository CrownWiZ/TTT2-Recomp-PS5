#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-$repo_root}
build_dir=${TTT2_DEPTH_VARIANT_TEST_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/depth-variant-test}
mkdir -p "$build_dir"
"${CXX:-c++}" -std=c++20 -O2 -ffunction-sections -fdata-sections \
    -I"$game_source/re/ppc" -I"$game_source/tools/XenonRecomp/XenonUtils" \
    -I"$game_source/tools/XenonRecomp/thirdparty/simde" \
    -I"$repo_root/port/src/runtime" -I"$repo_root/port/src/runtime/kernel" \
    "$repo_root/port/tools/test_depth_texture_variant.cpp" \
    -Wl,--gc-sections -pthread -o "$build_dir/test_depth_texture_variant"
"$build_dir/test_depth_texture_variant" "$@"
