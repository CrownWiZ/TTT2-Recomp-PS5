#!/bin/sh
# Diagnose the suspiciously small static archives.
BUILD=/root/m4/dxvk-ps5-build
echo "== build-side sizes =="
ls -la "$BUILD"/src/dxvk/libdxvk.a "$BUILD"/src/d3d11/d3d11_static.a "$BUILD"/src/dxbc/libdxbc.a 2>&1
echo "== members of dxvk core =="
llvm-ar t "$BUILD/src/dxvk/libdxvk.a" 2>&1 | head -5
llvm-ar t "$BUILD/src/dxvk/libdxvk.a" 2>&1 | wc -l
echo "== members of d3d11_static =="
llvm-ar t "$BUILD/src/d3d11/d3d11_static.a" 2>&1 | wc -l
echo "== total symbol count core =="
llvm-nm "$BUILD/src/dxvk/libdxvk.a" 2>/dev/null | grep -c ' T ' || true
