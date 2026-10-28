#!/bin/sh
export PATH="/usr/lib/llvm-18/bin:$PATH"
BUILD=/root/m4/dxvk-ps5-build
echo "== thin archive flags =="
llvm-ar tv "$BUILD/src/dxbc/libdxbc.a" 2>&1 | head -6
echo "== object sizes on disk =="
du -sh "$BUILD"/src/dxbc/libdxbc.a.p 2>/dev/null
du -sh "$BUILD"/src/d3d11/d3d11_static.a.p 2>/dev/null
du -sh "$BUILD"/src/dxvk/libdxvk.a.p 2>/dev/null
ls -la "$BUILD/src/dxbc/libdxbc.a.p" 2>/dev/null | head -6
echo "== symbol counts (with PATH fixed) =="
llvm-nm "$BUILD/src/dxvk/libdxvk.a" 2>/dev/null | grep -c ' T '
llvm-nm "$BUILD/src/d3d11/d3d11_static.a" 2>/dev/null | grep -c ' T '
