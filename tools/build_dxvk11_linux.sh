#!/usr/bin/env bash
set -euo pipefail

# Linux build from the same source tree and DXVK patch set as Android.
checkout_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
base_root=${TTT2_BASE_ROOT:-$checkout_root}
cache_root=${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform
source_root=$(python3 "$checkout_root/tools/prepare_dxvk_native.py")
dxvk_build=${TTT2_DXVK_LINUX_BUILD:-$cache_root/shared-dxvk/linux/build}
dxvk_prefix=${TTT2_DXVK_LINUX_PREFIX:-$cache_root/shared-dxvk/linux/install}
game_build=${TTT2_LINUX_GAME_BUILD:-$cache_root/build-shared-linux}

if command -v meson >/dev/null 2>&1; then
    meson_cmd=(meson)
elif command -v uvx >/dev/null 2>&1; then
    meson_cmd=(uvx --from meson meson)
else
    printf 'Meson or uvx is required to build DXVK Native.\n' >&2
    exit 1
fi

if [[ -f $dxvk_build/meson-private/coredata.dat ]]; then
    "${meson_cmd[@]}" setup "$dxvk_build" "$source_root" --reconfigure \
        --buildtype release --prefix "$dxvk_prefix" -Denable_tests=false \
        -Denable_d3d9=false -Denable_d3d10=false -Ddxvk_native_wsi=sdl3
else
    "${meson_cmd[@]}" setup "$dxvk_build" "$source_root" \
        --buildtype release --prefix "$dxvk_prefix" -Denable_tests=false \
        -Denable_d3d9=false -Denable_d3d10=false -Ddxvk_native_wsi=sdl3
fi
ninja -C "$dxvk_build" -j "${TTT2_BUILD_JOBS:-8}"
"${meson_cmd[@]}" install -C "$dxvk_build"

native_headers=$source_root/include/native
vkd3d_root=${TTT2_VKD3D_ROOT:-$cache_root/vkd3d}
ppc_archive=${TTT2_PPC_ARCHIVE:-}
for required in "$native_headers/directx/d3d11.h" "$vkd3d_root/usr/include/vkd3d/vkd3d_shader.h" \
    "$base_root/re/ppc"; do
    if [[ ! -e $required ]]; then
        printf 'Missing build input: %s\n' "$required" >&2
        exit 1
    fi
done

cmake -S "$checkout_root/port" -B "$game_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    -DTTT2_DXVK_NATIVE_ROOT="$dxvk_prefix" \
    -DTTT2_DXVK_NATIVE_INCLUDE="$native_headers" \
    -DTTT2_DXVK_NATIVE_D3D11="$dxvk_prefix/lib/libdxvk_d3d11.so" \
    -DTTT2_DXVK_NATIVE_DXGI="$dxvk_prefix/lib/libdxvk_dxgi.so" \
    -DTTT2_VKD3D_ROOT="$vkd3d_root" \
    -DTTT2_RENDERER_PPC_DIR="$base_root/re/ppc" \
    -DTTT2_RENDERER_XENON_UTILS="$base_root/tools/XenonRecomp/XenonUtils" \
    -DTTT2_RENDERER_SIMDE_DIR="$base_root/tools/XenonRecomp/thirdparty/simde" \
    -DTTT2_LINUX_PPC_PREBUILT="$ppc_archive" \
    -DFFMPEG_SRC="$base_root/tools/ffmpeg_dl/FFmpeg-n7.1" \
    -DFFMPEG_BUILD="$base_root/tools/ffmpeg_dl/build-linux-native" \
    -DTTT2_LINUX_STEAM="${TTT2_LINUX_STEAM:-OFF}" \
    -DTTT2_LINUX_STEAMWORKS="${TTT2_STEAMWORKS_SDK:-}"
cmake --build "$game_build" --target tekken_recomp_linux \
    -j "${TTT2_BUILD_JOBS:-8}"
printf 'Built shared Linux game: %s/tekken_recomp_linux\n' "$game_build"

python3 "$checkout_root/tools/write_shared_build_manifest.py" --platform linux \
    --build "$game_build" --dxvk-source "$source_root"
