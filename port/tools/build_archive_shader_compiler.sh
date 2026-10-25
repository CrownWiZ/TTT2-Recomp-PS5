#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
game_source=${TTT2_GAME_SOURCE:-}
if [[ -z $game_source || ! -d $game_source/re/ppc || ! -d $game_source/tools/XenonRecomp ]]; then
    printf 'Set TTT2_GAME_SOURCE to the checkout with generated PPC headers and XenonRecomp.\n' >&2
    exit 1
fi
cache_root=${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform
vkd3d_root=${TTT2_VKD3D_LINUX_ROOT:-$cache_root/vkd3d/usr}
if [[ ! -f $vkd3d_root/include/vkd3d/vkd3d_shader.h || ! -f $vkd3d_root/lib/libvkd3d-shader.so ]]; then
    printf 'Set TTT2_VKD3D_LINUX_ROOT to a Linux vkd3d-shader installation prefix.\n' >&2
    exit 1
fi
output=${TTT2_SHADER_COMPILER_EXE:-$cache_root/compile_archive_shaders}
mkdir -p "$(dirname -- "$output")"
shader_build_id=$(python3 - "$repo_root/port/src/runtime" <<'PYID'
from hashlib import sha256
from pathlib import Path
import sys
root = Path(sys.argv[1])
sources = ['kernel/gpu_render.cpp', 'kernel/gpu_hw_translate.h',
           'kernel/gpu_hw_api.h', 'native_d3dcompiler.cpp']
print(sha256(''.join(sha256((root / source).read_bytes()).hexdigest()
                     for source in sources).encode('ascii')).hexdigest())
PYID
)
"${CXX:-c++}" "-DTTT2_SHADER_BUILD_ID=\"$shader_build_id\"" -std=c++20 -O2 -DNDEBUG -ffunction-sections -fdata-sections \
    -I"$game_source/re/ppc" \
    -I"$game_source/tools/XenonRecomp/XenonUtils" \
    -I"$game_source/tools/XenonRecomp/thirdparty/simde" \
    -I"$repo_root/port/src/runtime" \
    -I"$repo_root/port/src/runtime/kernel" \
    -I"$vkd3d_root/include/vkd3d" \
    "$repo_root/port/tools/compile_archive_shaders.cpp" \
    -L"$vkd3d_root/lib" -Wl,-rpath,"$vkd3d_root/lib" \
    -Wl,--gc-sections -lvkd3d-shader -pthread -o "$output"
printf '%s\n' "$output"
