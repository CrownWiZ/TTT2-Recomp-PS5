#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
ndk_root=${TTT2_ANDROID_NDK:-${ANDROID_NDK_HOME:-}}
sdk_root=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
sdl_source=${TTT2_SDL_SOURCE:-}
game_source=${TTT2_GAME_SOURCE:-}
if [[ -z $ndk_root || -z $sdk_root || -z $sdl_source || -z $game_source ]]; then
    printf 'Set TTT2_ANDROID_NDK, ANDROID_HOME, TTT2_SDL_SOURCE, and TTT2_GAME_SOURCE.\n' >&2
    exit 1
fi
cache_root=${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform
build_root=${TTT2_ANDROID_GAME_BUILD:-$cache_root/android-game}
# Use the O2 archive from the retained Android performance build by default.
ppc_build=${TTT2_ANDROID_PPC_BUILD:-$cache_root/android-ppc-arm64-o2}
dxvk_root=${TTT2_DXVK_ANDROID_ROOT:-$cache_root/shared-dxvk/android}
vkd3d_root=${TTT2_VKD3D_ANDROID_ROOT:-$cache_root/android-vkd3d-1.17}
ffmpeg_root=${TTT2_FFMPEG_ANDROID_ROOT:-$cache_root/android-ffmpeg-xma}
sdl_library=${TTT2_SDL_LIBRARY:-$cache_root/android-sdl-shell/project/app/build/intermediates/ndkBuild/debug/obj/local/arm64-v8a/libSDL3.so}
openssl_root=${TTT2_OPENSSL_ANDROID_ROOT:-$cache_root/android-openssl}
variant=${TTT2_ANDROID_VARIANT:-debug}
if [[ $variant != debug && $variant != release ]]; then
    printf 'TTT2_ANDROID_VARIANT must be debug or release.\n' >&2
    exit 1
fi
ppc_opt=$(sed -n '/^-O[123]$/ { p; q; }' "$ppc_build/compile_flags.rsp" 2>/dev/null || true)
printf 'Android PPC archive: %s (%s)\n' "$ppc_build/libttt2_android_ppc.a" "${ppc_opt:-unknown optimization}"
bash "$repo_root/port/tools/patch_android_dxvk_present.sh"
for required in "$sdl_library" "$ppc_build/libttt2_android_ppc.a" \
    "$dxvk_root/build/src/d3d11/libdxvk_d3d11.so" \
    "$dxvk_root/build/src/dxgi/libdxvk_dxgi.so" \
    "$vkd3d_root/build/.libs/libvkd3d-shader.so" \
    "$ffmpeg_root/build/libavcodec/libavcodec.a" \
    "$ffmpeg_root/build/libavutil/libavutil.a" \
    "$openssl_root/lib/libssl.a" "$openssl_root/lib/libcrypto.a" \
    "$openssl_root/include/openssl/ssl.h"; do
    if [[ ! -f $required ]]; then
        printf 'Missing Android build input: %s\n' "$required" >&2
        exit 1
    fi
done

cmake -S "$repo_root/port/android/game" -B "$build_root/native" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$ndk_root/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-33 \
    -DCMAKE_BUILD_TYPE=Release \
    -DTTT2_GAME_SOURCE="$game_source" -DTTT2_SDL_SOURCE="$sdl_source" \
    -DTTT2_SDL_LIBRARY="$sdl_library" -DTTT2_DXVK_ROOT="$dxvk_root" \
    -DTTT2_VKD3D_ROOT="$vkd3d_root" -DTTT2_FFMPEG_ROOT="$ffmpeg_root" \
    -DTTT2_OPENSSL_ROOT="$openssl_root" \
    -DTTT2_PPC_ARCHIVE="$ppc_build/libttt2_android_ppc.a" \
    -DTTT2_PPC_HEADERS="$ppc_build/headers" \
    -DTTT2_PAGE_SUMMARY_EXPERIMENT="${TTT2_PAGE_SUMMARY_EXPERIMENT:-OFF}" \
    -DTTT2_VERTEX_SLOT_SCAN_EXPERIMENT="${TTT2_VERTEX_SLOT_SCAN_EXPERIMENT:-OFF}" \
    -DTTT2_ALU_BATCH_EXPERIMENT="${TTT2_ALU_BATCH_EXPERIMENT:-OFF}"
cmake --build "$build_root/native" -j "${TTT2_BUILD_JOBS:-4}"

project=$build_root/project
if [[ ! -f $project/settings.gradle ]]; then
    cp -a "$sdl_source/android-project" "$project"
fi
ln -sfn "$sdl_source" "$project/app/jni/SDL"
ndk_version=$(sed -n 's/^Pkg.Revision = //p' "$ndk_root/source.properties")
mkdir -p "$sdk_root/ndk" "$project/app/jni/src/prebuilt" \
    "$project/app/libs/arm64-v8a"
ln -sfn "$ndk_root" "$sdk_root/ndk/$ndk_version"
cp "$build_root/native/libmain.so" "$project/app/jni/src/prebuilt/libmain.so"
cp "$build_root/native/libmain.so" "$project/app/libs/arm64-v8a/libmain.so"
cp "$build_root/native/setup/libttt2_setup.so" "$project/app/libs/arm64-v8a/"
cp "$build_root/native/setup/libttt2_shader_setup.so" "$project/app/libs/arm64-v8a/"
mkdir -p "$project/app/src/main/assets/licenses"
cp "$repo_root/port/tools/OPENSSL_LICENSE.txt" "$project/app/src/main/assets/licenses/OPENSSL_LICENSE.txt"
mkdir -p "$project/app/src/main/java/org/ttt2/setup"
cp "$repo_root/port/android/setup/java/org/ttt2/setup/SetupActivity.java" \
   "$project/app/src/main/java/org/ttt2/setup/"
cp "$dxvk_root/build/src/d3d11/libdxvk_d3d11.so" "$project/app/libs/arm64-v8a/"
cp "$dxvk_root/build/src/dxgi/libdxvk_dxgi.so" "$project/app/libs/arm64-v8a/"
cp "$vkd3d_root/build/.libs/libvkd3d-shader.so" "$project/app/libs/arm64-v8a/"
cat > "$project/app/jni/src/Android.mk" <<'EOF'
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := main
LOCAL_SRC_FILES := prebuilt/libmain.so
include $(PREBUILT_SHARED_LIBRARY)
EOF
cat > "$project/app/jni/Application.mk" <<'EOF'
APP_ABI := arm64-v8a
APP_PLATFORM := android-33
APP_STL := c++_shared
# SDL's static Android configuration omits this supported Bionic API. Enable
# its native timed wait instead of polling sem_trywait with 100 ns sleeps.
APP_CFLAGS += -DHAVE_SEM_TIMEDWAIT=1
EOF
python3 - "$project/app/build.gradle" "$project/app/src/main/AndroidManifest.xml" "$ndk_version" <<'PY'
from pathlib import Path
import re
import sys
import os

build_file, manifest_file, ndk_version = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
build = build_file.read_text()
build = re.sub(r'ndkVersion = "[^"]+"', f'ndkVersion = "{ndk_version}"', build)
default_id = 'org.ttt2.androidgame.release' if os.environ.get('TTT2_ANDROID_VARIANT') == 'release' else 'org.ttt2.androidgame'
application_id = os.environ.get('TTT2_ANDROID_APPLICATION_ID', default_id)
if not re.fullmatch(r'[a-z][a-z0-9_]*(?:\.[a-z][a-z0-9_]*)+', application_id):
    raise SystemExit('Invalid Android application ID')
if re.search(r'applicationId "[^"]+"', build):
    build = re.sub(r'applicationId "[^"]+"', f'applicationId "{application_id}"', build)
else:
    build = build.replace('defaultConfig {', f'defaultConfig {{\n        applicationId "{application_id}"', 1)
build = build.replace('minSdkVersion 21', 'minSdkVersion 33')
build = build.replace('APP_PLATFORM=android-21', 'APP_PLATFORM=android-33')
build = build.replace('ANDROID_PLATFORM=android-21', 'ANDROID_PLATFORM=android-33')
build_file.write_text(build)

manifest = manifest_file.read_text()
manifest = manifest.replace('android:name="SDLActivity"',
                            'android:name="org.libsdl.app.SDLActivity"')
# SDL's template contains a commented INTERNET permission. Inspect XML nodes,
# not raw text, so that comment cannot suppress the game's required permission.
import xml.etree.ElementTree as ET
android_name = '{http://schemas.android.com/apk/res/android}name'
if not any(node.get(android_name) == 'android.permission.INTERNET'
           for node in ET.fromstring(manifest).findall('uses-permission')):
    manifest = manifest.replace('</manifest>',
                                '    <uses-permission android:name="android.permission.INTERNET" />\n'
                                '</manifest>', 1)
# Setup owns the launcher; SDL starts only after the user presses Start game.
ET.register_namespace('android', 'http://schemas.android.com/apk/res/android')
xml = ET.fromstring(manifest)
app = xml.find('application')
app.set('{http://schemas.android.com/apk/res/android}allowBackup', 'false')
for activity in app.findall('activity'):
    if activity.get(android_name) == 'org.libsdl.app.SDLActivity':
        # Keep explicit ADB launches used by the development controller scripts working.
        activity.set('{http://schemas.android.com/apk/res/android}exported', 'true')
        for intent_filter in activity.findall('intent-filter'):
            activity.remove(intent_filter)
if not any(a.get(android_name) == 'org.ttt2.setup.SetupActivity' for a in app.findall('activity')):
    activity = ET.SubElement(app, 'activity', {
        android_name: 'org.ttt2.setup.SetupActivity',
        '{http://schemas.android.com/apk/res/android}exported': 'true',
        '{http://schemas.android.com/apk/res/android}configChanges': 'orientation|screenSize|keyboardHidden',
        '{http://schemas.android.com/apk/res/android}launchMode': 'singleTask'})
    intent_filter = ET.SubElement(activity, 'intent-filter')
    ET.SubElement(intent_filter, 'action', {android_name: 'android.intent.action.MAIN'})
    ET.SubElement(intent_filter, 'category', {android_name: 'android.intent.category.LAUNCHER'})
manifest_file.write_text(ET.tostring(xml, encoding='unicode'))
strings = manifest_file.parent / 'res/values/strings.xml'
resources = ET.fromstring(strings.read_text())
for entry in resources.findall('string'):
    if entry.get('name') == 'app_name': entry.text = 'Tekken Tag Tournament 2'
strings.write_text(ET.tostring(resources, encoding='unicode'))

PY

ANDROID_HOME="$sdk_root" ANDROID_SDK_ROOT="$sdk_root" "$project/gradlew" \
    --project-dir "$project" --no-daemon "assemble${variant^}"
if [[ $variant == release ]]; then
    bash "$repo_root/port/tools/sign_android_release.sh" \
        "$project/app/build/outputs/apk/release/app-release-unsigned.apk"
else
    printf 'Android game APK: %s\n' "$project/app/build/outputs/apk/debug/app-debug.apk"
fi

python3 "$repo_root/tools/write_shared_build_manifest.py" --platform android \
    --build "$build_root/native" --dxvk-source "$dxvk_root/source"
