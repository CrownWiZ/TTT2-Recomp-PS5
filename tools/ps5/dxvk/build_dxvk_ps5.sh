#!/bin/sh
# PS5 cross build of DXVK-native 1.9.2b with the display WSI backend (Task 4).
# Windows prepares the patched source (prepare_dxvk_native.py); this script
# consumes it via /mnt/c (M2 rule), cross-configures against the mihawk SDK
# and builds ONLY the static libraries the port links (Task 5).
set -eu

SDK=/root/m4/native/ps5-payload-sdk-mihawk
BUILD=/root/m4/dxvk-ps5-build
STUBS=/root/m4/dxvk-stubs

# Prepare the patched source WSL-side (native filesystem: fast compiles, and
# the newest-dir mtime cannot pick up a stale cache).
rm -rf /root/.cache/ttt2-platform/shared-dxvk
export TTT2_DXVK_MIRROR=/mnt/c/ttt2/.superpowers/dxvk-native-mirror
python3 /mnt/c/ttt2/tools/prepare_dxvk_native.py
SRC=$(ls -dt /root/.cache/ttt2-platform/shared-dxvk/source-*/ | head -n 1)

echo "SDK=$SDK"
echo "SRC=$SRC"

export PS5_PAYLOAD_SDK="$SDK"
export PATH="/usr/lib/llvm-18/bin:$PATH"

# Resolve-at-configure stubs: DXVK's meson requires a 'vulkan' library and a
# 'threads' dependency to configure; on the console they are provided by the
# port's trampolines and the SDK's pthread wraps at Task-5 link time.
mkdir -p "$STUBS"
llvm-ar cr "$STUBS/libvulkan.a"
llvm-ar cr "$STUBS/libpthread.a"

# The build dir binds to the source path at first configure; a new prepared
# cache needs a fresh build dir.
rm -rf "$BUILD"
meson setup "$BUILD" "$SRC" \
  --cross-file /mnt/c/ttt2/tools/ps5/dxvk/ps5-cross-dxvk.ini \
  -Dbuildtype=release \
  -Ddxvk_native_force=true \
  -Ddxvk_native_wsi=display \
  -Denable_d3d9=false \
  -Denable_d3d10=false

# Static libraries the port links (Task 5): core + wsi + vkcommon + dxbc +
# dxgi_static + d3d11_static. The .so products are never built.
ninja -C "$BUILD" \
  src/util/libutil.a \
  src/spirv/libspirv.a \
  src/wsi/libwsi.a \
  src/vulkan/libvkcommon.a \
  src/dxvk/libdxvk.a \
  src/dxbc/libdxbc.a \
  src/dxgi/dxgi_static.a \
  src/d3d11/d3d11_static.a

echo "== nm check =="
llvm-nm "$BUILD/src/d3d11/d3d11_static.a" 2>/dev/null | grep -E ' T (D3D11CreateDevice|D3D11CreateDeviceAndSwapChain)$' || echo "NO_D3D11_EXPORTS"
llvm-nm "$BUILD/src/dxgi/dxgi_static.a" 2>/dev/null | grep -E ' T CreateDXGIFactory(1|2)?$' || echo "NO_DXGI_EXPORTS"

OUT=/mnt/c/ttt2/.superpowers/m4-dxvk-ps5
mkdir -p "$OUT"
cp "$BUILD/src/util/libutil.a" "$BUILD/src/spirv/libspirv.a" "$BUILD/src/wsi/libwsi.a" \
   "$BUILD/src/vulkan/libvkcommon.a" "$BUILD/src/dxvk/libdxvk.a" "$BUILD/src/dxbc/libdxbc.a" \
   "$BUILD/src/dxgi/dxgi_static.a" "$BUILD/src/d3d11/d3d11_static.a" "$OUT/"
echo "TASK4_BUILD_OK"
