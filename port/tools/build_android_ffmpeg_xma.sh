#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
ndk_root=${TTT2_ANDROID_NDK:-${ANDROID_NDK_HOME:-}}
ffmpeg_source=${TTT2_FFMPEG_SOURCE:-}
if [[ -z $ndk_root || -z $ffmpeg_source ]]; then
    printf 'Set TTT2_ANDROID_NDK and TTT2_FFMPEG_SOURCE (FFmpeg n7.1 checkout).\n' >&2
    exit 1
fi
build_root=${TTT2_FFMPEG_ANDROID_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/android-ffmpeg-xma}
source_dir=$build_root/source
build_dir=$build_root/build
expected_commit=b08d7969c550a804a59511c7b83f2dd8cc0499b8
mkdir -p "$build_root" "$build_dir"
if [[ ! -d $source_dir/.git ]]; then
    git clone --shared "$ffmpeg_source" "$source_dir"
fi
if [[ $(git -C "$source_dir" rev-parse HEAD) != "$expected_commit" ]]; then
    printf 'Unexpected FFmpeg n7.1 source revision.\n' >&2
    exit 1
fi
python3 "$repo_root/port/tools/patch_android_ffmpeg_xma.py" "$source_dir"

toolchain=$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin
if [[ ! -f $build_dir/ffbuild/config.mak ]]; then
    (
        cd "$build_dir"
        "$source_dir/configure" --target-os=android --arch=aarch64 \
            --enable-cross-compile \
            --cc="$toolchain/aarch64-linux-android33-clang" \
            --cxx="$toolchain/aarch64-linux-android33-clang++" \
            --ar="$toolchain/llvm-ar" --ranlib="$toolchain/llvm-ranlib" \
            --strip="$toolchain/llvm-strip" \
            --enable-static --disable-shared --disable-everything \
            --disable-programs --disable-doc --disable-avdevice \
            --disable-avformat --disable-swresample --disable-swscale \
            --disable-postproc --disable-avfilter --disable-network \
            --disable-asm --disable-inline-asm --disable-debug \
            --disable-autodetect --enable-decoder=wmapro,xma1,xma2 \
            --enable-parser=xma > "$build_root/configure.log" 2>&1
    )
fi
for feature in CONFIG_WMAPRO_DECODER CONFIG_XMA1_DECODER CONFIG_XMA2_DECODER CONFIG_XMA_PARSER; do
    if ! rg -q "^$feature=yes$" "$build_dir/ffbuild/config.mak"; then
        printf 'Android FFmpeg configuration is missing %s.\n' "$feature" >&2
        exit 1
    fi
done
make -C "$build_dir" -j "${TTT2_BUILD_JOBS:-4}" \
    libavcodec/libavcodec.a libavutil/libavutil.a \
    > "$build_root/build.log" 2>&1
printf 'Android FFmpeg XMA archives: %s and %s\n' \
    "$build_dir/libavcodec/libavcodec.a" "$build_dir/libavutil/libavutil.a"
