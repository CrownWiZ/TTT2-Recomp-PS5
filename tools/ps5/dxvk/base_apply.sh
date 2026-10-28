#!/bin/sh
# Apply the pinned DXVK patch set onto the mirror and commit it as a base
# commit, so the ps5-display-wsi patch can be generated as a clean staged diff.
cd /mnt/c/ttt2/.superpowers/dxvk-native-mirror || exit 1
for p in \
  dxvk-native-1.9.2b-sdl3.patch \
  dxvk-native-1.9.2b-android.patch \
  dxvk-1.9.2b-suboptimal-present.patch \
  dxvk-1.9.2b-raw-cbuffer-words.patch \
  dxvk-1.9.2b-ttt2-hud.patch \
  dxvk-1.9.2b-live-hud-toggle.patch \
  dxvk-1.9.2b-persistent-pipeline-cache.patch \
  dxvk-native-1.9.2b-shared-wsi.patch \
  dxvk-1.9.2b-nonblocking-readback.patch \
  dxvk-1.9.2b-framebuffer-reuse.patch \
  dxvk-1.9.2b-reuse-reference.patch
do
  git apply --ignore-whitespace "/mnt/c/ttt2/tools/patches/$p" || { echo "APPLY FAILED: $p"; exit 1; }
done
git add -A
git commit -m base | head -1
echo "BASE_READY"
