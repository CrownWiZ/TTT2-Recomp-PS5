#!/usr/bin/env bash
set -euo pipefail

checkout_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cache_root=${XDG_CACHE_HOME:-$HOME/.cache}/ttt2-platform
export TTT2_RUNTIME_LIB_DIRS="${TTT2_RUNTIME_LIB_DIRS:-$cache_root/shared-dxvk/linux/install/lib:$cache_root/vkd3d/usr/lib}"
source "$checkout_root/tools/step8_paths.sh"
ttt2_step8_paths build-shared-linux
for required in "$binary" "$image" "$vfs_root"; do
    if [[ ! -e $required ]]; then
        printf 'Missing required path: %s\n' "$required" >&2
        exit 1
    fi
done
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-pulseaudio}"
export TTT2_VFS_ROOT="$vfs_root"
export TTT2_FBDUMP="${TTT2_FBDUMP:-0}"
export TTT2_GPU_HW=1
export DXVK_WSI_DRIVER=SDL3
cd "$project_root"
exec "$binary" "$image" "$@"
