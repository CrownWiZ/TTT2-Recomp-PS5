#!/usr/bin/env bash
# Shared path defaults for the manual Linux game and diagnostic launchers.
# TTT2_GAME_ROOT, TTT2_BUILD_ROOT, TTT2_BINARY, TTT2_IMAGE,
# TTT2_VFS_ROOT, TTT2_LOG_ROOT, and TTT2_RUNTIME_LIB_DIRS override them.

ttt2_step8_paths() {
    local build_name=$1
    local checkout_root cache_base runtime_dirs
    checkout_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
    if [[ -n ${TTT2_GAME_ROOT:-} ]]; then
        project_root=$TTT2_GAME_ROOT
    elif [[ -d $checkout_root/re/game_files_x ]]; then
        project_root=$checkout_root
    else
        project_root=$checkout_root
    fi
    project_root=$(cd -- "$project_root" && pwd)
    cache_base=${XDG_CACHE_HOME:-${HOME:-$checkout_root}/.cache}/ttt2-platform
    build_root=${TTT2_BUILD_ROOT:-$cache_base}
    binary=${TTT2_BINARY:-$build_root/$build_name/tekken_recomp_linux}
    image=${TTT2_IMAGE:-$project_root/re/decrypted/Default_patched_tu3.bin}
    vfs_root=${TTT2_VFS_ROOT:-$project_root/re/game_files_x}
    log_root=${TTT2_LOG_ROOT:-$build_root}

    # Packaged libraries beside the executable take precedence. Development
    # builds can still find the locally built DXVK Native, vkd3d, and SDK libs.
    runtime_dirs=${TTT2_RUNTIME_LIB_DIRS:-$build_root/dxvk-native/usr/lib:$build_root/vkd3d/usr/lib:$build_root/steamworks-sdk-165/sdk/redistributable_bin/linux64}
    export LD_LIBRARY_PATH="$(dirname -- "$binary")/lib:$runtime_dirs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
}
