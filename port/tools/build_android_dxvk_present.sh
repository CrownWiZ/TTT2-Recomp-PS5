#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cache_root=${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform
project=${TTT2_ANDROID_SHELL_BUILD:-$cache_root/android-sdl-shell}/project
dxvk_root=${TTT2_DXVK_ANDROID_ROOT:-$cache_root/shared-dxvk/android}
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
ndk_root=${TTT2_ANDROID_NDK:-${ANDROID_NDK_HOME:-}}
if [[ -z $sdk_root || -z $ndk_root ]]; then
    printf 'Set ANDROID_HOME and TTT2_ANDROID_NDK.\n' >&2
    exit 1
fi
for required in "$project/gradlew" "$dxvk_root/source/include/native/wsi/native_sdl3.h" \
    "$dxvk_root/build/src/d3d11/libdxvk_d3d11.so" \
    "$dxvk_root/build/src/dxgi/libdxvk_dxgi.so"; do
    if [[ ! -f $required ]]; then
        printf 'Missing Android build input: %s\n' "$required" >&2
        exit 1
    fi
done

native_root=$project/app/jni/src
mkdir -p "$native_root/prebuilt"
cp "$repo_root/port/tools/android_dxvk_present.cpp" "$native_root/android_dxvk_present.cpp"
cp "$repo_root/port/tools/android_dxvk_Android.mk" "$native_root/Android.mk"
cp "$dxvk_root/build/src/d3d11/libdxvk_d3d11.so" "$native_root/prebuilt/"
cp "$dxvk_root/build/src/dxgi/libdxvk_dxgi.so" "$native_root/prebuilt/"
ln -sfn "$dxvk_root/source/include" "$native_root/prebuilt/include"
if ! rg -q '^APP_STL := c\+\+_shared$' "$project/app/jni/Application.mk"; then
    printf '\nAPP_STL := c++_shared\n' >> "$project/app/jni/Application.mk"
fi
sed -i 's/APP_PLATFORM=android-21/APP_PLATFORM=android-26/g' \
    "$project/app/jni/Application.mk" "$project/app/build.gradle"
sed -i 's/minSdkVersion 21/minSdkVersion 26/' "$project/app/build.gradle"

ANDROID_HOME="$sdk_root" ANDROID_SDK_ROOT="$sdk_root" "$project/gradlew" \
    --project-dir "$project" --no-daemon assembleDebug
printf 'APK: %s\n' "$project/app/build/outputs/apk/debug/app-debug.apk"
