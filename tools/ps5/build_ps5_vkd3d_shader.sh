#!/usr/bin/env bash
# PS5 cross build of vkd3d-shader for the port's D3D11 translation (Task 5).
# Pinned vkd3d 1.19 tarball (pre-generated IDL headers: no autoreconf/widl).
# SIE adaptations: the private version/grammar headers are generated first
# (the Android recipe's order), and the idl headers' ms_abi calling
# conventions are emptied — Prospero's target rejects ms_abi and the port
# calls only the plain-C vkd3d_shader_* API, never the COM interfaces.
set -euo pipefail

sdk=/root/m4/native/ps5-payload-sdk-mihawk
cache=/root/.cache/ttt2-platform
source_dir=$cache/vkd3d-1.19-ps5
build_dir=$cache/vkd3d-ps5-build
install_dir=$cache/vkd3d-ps5

export PATH="/usr/lib/llvm-18/bin:$PATH"

mesa_include=/mnt/c/ttt2/.superpowers/m4-downloads/PS5_Mesa/include
spirv_root=$cache/spirv-headers
if [[ ! -d $spirv_root/include ]]; then
    (cd "$cache" &&
     curl -fsSL -o spirv-headers.tar.gz \
         https://github.com/KhronosGroup/SPIRV-Headers/archive/refs/tags/vulkan-sdk-1.4.350.0.tar.gz &&
     rm -rf spirv-headers && tar xf spirv-headers.tar.gz &&
     mv SPIRV-Headers-vulkan-sdk-1.4.350.0 spirv-headers)
fi

# Fresh extract every time (tar merges into stale trees otherwise).
rm -rf "$source_dir" "$cache/vkd3d-1.19" "$cache/vkd3d-src"
(cd "$cache" && tar xf vkd3d-1.19.tar.xz && mv vkd3d-1.19 vkd3d-1.19-ps5)

# ms_abi is rejected by the SIE target; the COM member functions of the
# generated headers are never called by the C-only shader API.
if [[ ! -f $source_dir/.sie-patched ]]; then
    sed -i 's/__attribute__((ms_abi))//g' \
        "$source_dir"/include/vkd3d_d3d*.h \
        "$source_dir"/include/*.h
    touch "$source_dir/.sie-patched"
fi

rm -rf "$build_dir"
mkdir -p "$build_dir"
(
    cd "$build_dir"
    CC="$sdk/bin/prospero-clang" \
    AR="$sdk/bin/llvm-ar" RANLIB="$sdk/bin/llvm-ranlib" \
    STRIP="$sdk/bin/llvm-strip" \
    CFLAGS="${CFLAGS:--O2 -g} -ffile-prefix-map=$source_dir=. -ffile-prefix-map=$build_dir=build" \
    LDFLAGS="-L$cache/dxvk-stubs" \
    CPPFLAGS="-I$mesa_include -I$spirv_root/include" \
        "$source_dir/configure" --host=x86_64-pc-freebsd \
        --prefix="$install_dir" \
        --disable-maintainer-mode --disable-tests --disable-demos \
        --with-xcb=no --with-opengl=no --with-ncurses=no \
        SONAME_LIBVULKAN=libvulkan.so \
        > "$cache/vkd3d-ps5-configure.log" 2>&1
) || { tail -12 "$cache/vkd3d-ps5-configure.log"; exit 1; }

headers=()
for idl in "$source_dir"/include/*.idl; do
    name=${idl##*/}
    [[ $name == vkd3d_unknown.idl ]] && continue
    headers+=("include/${name%.idl}.h")
done
make -C "$build_dir" -j "${TTT2_BUILD_JOBS:-6}" \
    include/private/spirv_grammar.h include/private/vkd3d_version.h \
    "${headers[@]}" \
    > "$cache/vkd3d-ps5-gen.log" 2>&1 || { tail -12 "$cache/vkd3d-ps5-gen.log"; exit 1; }

make -C "$build_dir" -j "${TTT2_BUILD_JOBS:-6}" libvkd3d-shader.la \
    > "$cache/vkd3d-ps5-build.log" 2>&1 || { tail -12 "$cache/vkd3d-ps5-build.log"; exit 1; }
printf 'PS5 vkd3d-shader: %s\n' "$build_dir/.libs/libvkd3d-shader.a"
