#!/usr/bin/env bash
set -euo pipefail
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-$repo_root}
xenon=$game_source/tools/XenonRecomp
build_dir=${TTT2_SOUND_ROLLBACK_TEST_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/sound-rollback-test}
mkdir -p "$build_dir"

"${CXX:-clang++}" -std=c++20 -O2 -march=x86-64-v2 -ffp-contract=off \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -I "$repo_root/port/src/runtime" -I "$game_source/re/ppc" \
    -I "$xenon/XenonUtils" -I "$xenon/thirdparty/simde" \
    "$repo_root/port/tools/test_sound_rollback.cpp" \
    -o "$build_dir/test_sound_rollback"
"$build_dir/test_sound_rollback"
