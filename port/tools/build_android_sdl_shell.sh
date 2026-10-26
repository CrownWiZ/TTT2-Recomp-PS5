#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build_root=${TTT2_ANDROID_SHELL_BUILD:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/android-sdl-shell}
sdl_source=${TTT2_SDL_SOURCE:-$build_root/SDL-3.4.18}
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
ndk_root=${TTT2_ANDROID_NDK:-${ANDROID_NDK_HOME:-}}
if [[ -z $sdk_root || -z $ndk_root ]]; then
    printf 'Set ANDROID_HOME and TTT2_ANDROID_NDK to the Android SDK and NDK roots.\n' >&2
    exit 1
fi
if [[ ! -f $sdl_source/android-project/app/build.gradle ]]; then
    git clone --depth 1 --branch release-3.4.18 https://github.com/libsdl-org/SDL.git "$sdl_source"
fi

project=$build_root/project
mkdir -p "$build_root"
if [[ ! -f $project/settings.gradle ]]; then
    cp -a "$sdl_source/android-project" "$project"
fi
ln -sfn "$sdl_source" "$project/app/jni/SDL"
cp "$repo_root/port/tools/android_sdl_shell.c" "$project/app/jni/src/android_sdl_shell.c"
sed -i 's/YourSourceHere.c/android_sdl_shell.c/' "$project/app/jni/src/Android.mk"

ndk_version=$(sed -n 's/^Pkg.Revision = //p' "$ndk_root/source.properties")
mkdir -p "$sdk_root/ndk"
ln -sfn "$ndk_root" "$sdk_root/ndk/$ndk_version"
python3 - "$project/app/build.gradle" "$project/app/src/main/AndroidManifest.xml" "$ndk_version" <<'PY'
from pathlib import Path
import re
import sys

build_file, manifest_file, ndk_version = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
build = build_file.read_text()
build = re.sub(r'ndkVersion = "[^"]+"', f'ndkVersion = "{ndk_version}"', build)
if 'applicationId "org.ttt2.androidprobe"' not in build:
    build = build.replace('defaultConfig {', 'defaultConfig {\n        applicationId "org.ttt2.androidprobe"', 1)
build_file.write_text(build)
manifest = manifest_file.read_text()
manifest = manifest.replace('android:name="SDLActivity"', 'android:name="org.libsdl.app.SDLActivity"')
manifest_file.write_text(manifest)
PY

ANDROID_HOME="$sdk_root" ANDROID_SDK_ROOT="$sdk_root" "$project/gradlew" \
    --project-dir "$project" --no-daemon assembleDebug
printf 'APK: %s\n' "$project/app/build/outputs/apk/debug/app-debug.apk"
