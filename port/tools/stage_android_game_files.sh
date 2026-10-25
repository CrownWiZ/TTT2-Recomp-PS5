#!/usr/bin/env bash
set -euo pipefail

game_source=${TTT2_GAME_SOURCE:-}
if [[ -z $game_source ]]; then
    printf 'Set TTT2_GAME_SOURCE to the checkout containing re/game_files_x.\n' >&2
    exit 1
fi
image=$game_source/re/decrypted/Default_patched_tu3.bin
content=$game_source/re/game_files_x
if [[ ! -f $image || ! -d $content ]]; then
    printf 'Missing local game image or extracted files.\n' >&2
    exit 1
fi
remote=/sdcard/Android/data/org.ttt2.androidgame/files
available_kb=$(adb shell df -k /sdcard | awk 'NR == 2 { print $4 }' | tr -d '\r')
needed_kb=$(du -sk "$content" | awk '{ print $1 }')
needed_kb=$((needed_kb + $(stat -c %s "$image") / 1024 + 1048576))
if [[ -z $available_kb || $available_kb -lt $needed_kb ]]; then
    printf 'Android app storage needs about %s KiB; available: %s KiB.\n' \
        "$needed_kb" "${available_kb:-unknown}" >&2
    exit 1
fi
adb shell "mkdir -p '$remote'"
adb push --sync -Z "$image" "$remote/tekken.img"
adb push --sync -Z "$content" "$remote/"
printf 'Game image and extracted files staged in %s\n' "$remote"
