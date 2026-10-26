#!/usr/bin/env bash
set -euo pipefail

ndk_root=${TTT2_ANDROID_NDK:-${ANDROID_NDK_HOME:-}}
if [[ -z $ndk_root ]]; then
    printf 'Set TTT2_ANDROID_NDK to the Android NDK root.\n' >&2
    exit 1
fi
if ! command -v autoreconf >/dev/null || ! command -v widl >/dev/null; then
    printf 'Building vkd3d-shader requires autoreconf and widl.\n' >&2
    exit 1
fi
build_root=${TTT2_VKD3D_ANDROID_ROOT:-${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform/android-vkd3d-1.17}
source_dir=$build_root/source
build_dir=$build_root/build
expected_commit=f732fb78899aea8e3d4107b72b23f7f54500ffb7
mkdir -p "$build_root/perl" "$build_dir"
if [[ ! -d $source_dir/.git ]]; then
    git clone --depth 1 --branch vkd3d-1.17 \
        https://gitlab.winehq.org/wine/vkd3d.git "$source_dir"
fi
if [[ $(git -C "$source_dir" rev-parse HEAD) != "$expected_commit" ]]; then
    printf 'Unexpected vkd3d 1.17 source revision.\n' >&2
    exit 1
fi

# Perl core provides JSON::PP. Upstream's grammar generator needs only
# decode_json from the optional JSON module, so supply that API locally.
cat > "$build_root/perl/JSON.pm" <<'EOF'
package JSON;
use strict;
use warnings;
use Exporter 'import';
use JSON::PP ();
our @EXPORT = qw(decode_json);
sub decode_json { JSON::PP::decode_json(@_) }
1;
EOF
export PERL5LIB="$build_root/perl${PERL5LIB:+:$PERL5LIB}"
if [[ ! -f $source_dir/configure ]]; then
    (cd "$source_dir" && autoreconf -ifv)
fi

toolchain=$ndk_root/toolchains/llvm/prebuilt/linux-x86_64/bin
spirv_include=$ndk_root/sources/third_party/shaderc/third_party/spirv-tools/external/spirv-headers/include
if [[ ! -f $spirv_include/spirv/unified1/spirv.h ]]; then
    printf 'Missing NDK SPIR-V headers.\n' >&2
    exit 1
fi
(
    cd "$build_dir"
    CC="$toolchain/aarch64-linux-android33-clang" \
    AR="$toolchain/llvm-ar" RANLIB="$toolchain/llvm-ranlib" \
    CFLAGS="${CFLAGS:--O2 -g} -ffile-prefix-map=$source_dir=. -ffile-prefix-map=$build_dir=build" \
    STRIP="$toolchain/llvm-strip" CPPFLAGS="-I$spirv_include" \
    WIDL="$(command -v widl)" SONAME_LIBVULKAN=libvulkan.so \
        "$source_dir/configure" --build=x86_64-pc-linux-gnu \
        --host=aarch64-linux-android --prefix="$build_root/install" \
        --disable-maintainer-mode --disable-tests --disable-demos \
        --with-xcb=no --with-opengl=no --with-ncurses=no \
        > "$build_root/configure.log" 2>&1

    headers=()
    for idl in "$source_dir"/include/*.idl; do
        name=${idl##*/}
        [[ $name == vkd3d_unknown.idl ]] && continue
        headers+=("include/${name%.idl}.h")
    done
    make -j "${TTT2_BUILD_JOBS:-4}" "${headers[@]}" \
        include/private/spirv_grammar.h include/private/vkd3d_version.h \
        > "$build_root/generate.log" 2>&1
    make -j "${TTT2_BUILD_JOBS:-4}" libvkd3d-shader.la \
        > "$build_root/build-shader.log" 2>&1
)
printf 'Android vkd3d-shader: %s\n' "$build_dir/.libs/libvkd3d-shader.so"
