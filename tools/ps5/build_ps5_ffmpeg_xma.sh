#!/usr/bin/env bash
# PS5 cross build of the minimal FFmpeg XMA subset (Task 5).
# The repo's local FFmpeg tree is an extracted n7.1 tarball (not a git
# checkout), so the flow differs from Android's: extract the same tarball
# fresh into the WSL cache, apply the same single-stream semantics patch once
# (marker file), then configure out-of-tree with the payload SDK's C wrapper.
set -euo pipefail

repo_root=/mnt/c/ttt2
sdk=/root/m4/native/ps5-payload-sdk-mihawk
cache=/root/.cache/ttt2-platform
work_root=$cache/ps5-ffmpeg-xma
source_dir=$work_root/source
build_dir=$work_root/build
ffmpeg_tarball=${TTT2_FFMPEG_TARBALL:-/mnt/c/ttt2/tools/ffmpeg_dl/ffmpeg-7.1.tar.xz}

export PATH="/usr/lib/llvm-18/bin:$PATH"

mkdir -p "$work_root"
if [[ ! -f $source_dir/ffbuild/config.mak ]] || [[ ! -f $source_dir/.ps5-patched ]]; then
    rm -rf "$source_dir"
    mkdir -p "$source_dir"
    tar -C "$source_dir" --strip-components=1 -xf "$ffmpeg_tarball"
    python3 "$repo_root/port/tools/patch_android_ffmpeg_xma.py" "$source_dir"
    touch "$source_dir/.ps5-patched"
fi

rm -rf "$build_dir"
mkdir -p "$build_dir"
(
    cd "$build_dir"
    "$source_dir/configure" --target-os=linux --arch=x86_64 \
        --enable-cross-compile \
        --cc="$sdk/bin/prospero-clang" \
        --cxx="$sdk/bin/prospero-clang++" \
        --host-cc=clang \
        --ar="$sdk/bin/llvm-ar" --ranlib="$sdk/bin/llvm-ranlib" \
        --strip="$sdk/bin/llvm-strip" \
        --enable-static --disable-shared --disable-everything \
        --disable-programs --disable-doc --disable-avdevice \
        --disable-avformat --disable-swresample --disable-swscale \
        --disable-postproc --disable-avfilter --disable-network \
        --disable-asm --disable-inline-asm --disable-debug \
        --disable-autodetect --enable-decoder=wmapro,xma1,xma2 \
        --enable-parser=xma > "$work_root/configure.log" 2>&1
) || { tail -12 "$work_root/configure.log"; exit 1; }

for feature in CONFIG_WMAPRO_DECODER CONFIG_XMA1_DECODER CONFIG_XMA2_DECODER CONFIG_XMA_PARSER; do
    if ! grep -q "^$feature=yes$" "$build_dir/ffbuild/config.mak"; then
        printf 'PS5 FFmpeg configuration is missing %s.\n' "$feature" >&2
        exit 1
    fi
done
make -C "$build_dir" -j "${TTT2_BUILD_JOBS:-6}" \
    libavcodec/libavcodec.a libavutil/libavutil.a \
    > "$work_root/build.log" 2>&1 || { tail -12 "$work_root/build.log"; exit 1; }
printf 'PS5 FFmpeg XMA archives: %s and %s\n' \
    "$build_dir/libavcodec/libavcodec.a" "$build_dir/libavutil/libavutil.a"
