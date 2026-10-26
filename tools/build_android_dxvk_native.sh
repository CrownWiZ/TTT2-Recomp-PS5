#!/usr/bin/env bash
set -euo pipefail

# Build the Android/ARM64 DXVK Native D3D11 and DXGI libraries used by the
# presentation probe. Build the SDL3 Android shell first for its libSDL3.so.
repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform
build_root=${TTT2_DXVK_ANDROID_ROOT:-$cache_root/shared-dxvk/android}
shell_project=${TTT2_ANDROID_SHELL_BUILD:-$cache_root/android-sdl-shell}/project
sdl_source=${TTT2_SDL_SOURCE:-${TTT2_ANDROID_SHELL_BUILD:-$cache_root/android-sdl-shell}/SDL-3.4.18}
ndk_root=${TTT2_ANDROID_NDK:-${ANDROID_NDK_HOME:-}}
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [[ -z $ndk_root || -z $sdk_root ]]; then
    printf 'Set ANDROID_HOME and TTT2_ANDROID_NDK.\n' >&2
    exit 1
fi
host_tag=linux-x86_64
clang_root=$ndk_root/toolchains/llvm/prebuilt/$host_tag/bin
sdl_lib=$shell_project/app/build/intermediates/ndkBuild/debug/obj/local/arm64-v8a/libSDL3.so
for required in "$clang_root/aarch64-linux-android33-clang++" \
    "$sdl_source/include/SDL3/SDL.h" "$sdl_lib"; do
    if [[ ! -f $required ]]; then
        printf 'Missing Android build input: %s\n' "$required" >&2
        exit 1
    fi
done

source_root=$(python3 "$repo_root/tools/prepare_dxvk_native.py")
mkdir -p "$build_root"
if [[ -e $build_root/source && $(readlink -f "$build_root/source") != "$source_root" ]]; then
    printf 'DXVK build root refers to another source; use a new TTT2_DXVK_ANDROID_ROOT.\n' >&2
    exit 1
fi
ln -sfn "$source_root" "$build_root/source"

if command -v meson >/dev/null 2>&1; then
    meson_cmd=(meson)
elif command -v uvx >/dev/null 2>&1; then
    meson_cmd=(uvx --from meson meson)
else
    printf 'Meson or uvx is required.\n' >&2
    exit 1
fi

mkdir -p "$build_root"
cross_file=$build_root/android-arm64.cross
cat > "$cross_file" <<EOF
[binaries]
c = '$clang_root/aarch64-linux-android33-clang'
cpp = '$clang_root/aarch64-linux-android33-clang++'
ar = '$clang_root/llvm-ar'
strip = '$clang_root/llvm-strip'

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'

[built-in options]
c_args = ['-I$sdl_source/include']
cpp_args = ['-I$sdl_source/include']
c_link_args = ['-L$(dirname "$sdl_lib")']
cpp_link_args = ['-L$(dirname "$sdl_lib")']

[properties]
needs_exe_wrapper = true
EOF

build_dir=$build_root/build
meson_args=(--cross-file "$cross_file" --buildtype release
    -Ddxvk_native_force=true -Ddxvk_native_wsi=sdl3
    -Denable_tests=false -Denable_d3d9=false -Denable_d3d10=false)
if [[ -f $build_dir/meson-private/coredata.dat ]]; then
    "${meson_cmd[@]}" setup "$build_dir" "$source_root" --reconfigure "${meson_args[@]}"
else
    "${meson_cmd[@]}" setup "$build_dir" "$source_root" "${meson_args[@]}"
fi
ninja -C "$build_dir" -j "${TTT2_BUILD_JOBS:-8}"
printf 'DXVK Android libraries: %s and %s\n' \
    "$build_dir/src/d3d11/libdxvk_d3d11.so" \
    "$build_dir/src/dxgi/libdxvk_dxgi.so"
