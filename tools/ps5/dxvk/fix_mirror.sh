#!/bin/sh
# Normalize the dxvk-native mirror working tree to LF and verify it is clean.
cd /mnt/c/ttt2/.superpowers/dxvk-native-mirror || exit 1
git config core.autocrlf false
git ls-files -z | xargs -0 rm -f
git checkout -f HEAD
git status --short
echo "TREE_READY"
