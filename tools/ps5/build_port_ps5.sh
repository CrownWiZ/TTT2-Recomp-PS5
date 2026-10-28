#!/usr/bin/env bash
# PS5 port build (Task 5) — RESUMABLE: compiles only missing objects on rerun.
# Sources mirror the Linux CMake target with the SDL pieces replaced by the
# Task-5 PS5 host package. Link = radv-link recipe + DXVK statics + vkd3d + FFmpeg.
set -euo pipefail

ROOT=/mnt/c/ttt2
sdk=/root/m4/native/ps5-payload-sdk-mihawk
radv_root=/mnt/c/ttt2/.superpowers/m4-downloads/PS5_Vulkan
radv_archive=/root/m4/radv-build2/src/amd/vulkan/libvulkan_radeon.a
cache=/root/.cache/ttt2-platform
work=/root/m4/port-ps5
stubs=/root/m4/dxvk-stubs
dxvk_build=/root/m4/dxvk-ps5-build
dxvk_src=$(ls -dt $cache/shared-dxvk/source-*/ | head -n 1)

export PATH="/usr/lib/llvm-18/bin:/usr/bin:/bin"
export PS5_PAYLOAD_SDK="$sdk"
export PS5_CLANG="$sdk/bin/prospero-clang"

ppc_dir=$ROOT/re/ppc
runtime=$ROOT/port/src/runtime

inc=(
    "-I$ppc_dir"
    "-I$ROOT/tools/XenonRecomp/XenonUtils"
    "-I$ROOT/tools/XenonRecomp/thirdparty/simde"
    "-I$runtime"
    "-I$runtime/kernel"
    "-I$dxvk_src/include"
    "-I$dxvk_src/include/native"
    "-I$dxvk_src/include/native/directx"
    "-I$dxvk_src/include/native/windows"
    "-I$ROOT/port/native/include"
    "-I$cache/ps5-ffmpeg-xma/build"
    "-I$cache/ps5-ffmpeg-xma/source"
    "-I$cache/ps5-ffmpeg-xma/source/compat/stdbit"
    "-I$cache/vkd3d-ps5/usr/include"
    "-I$cache/vkd3d-1.19-ps5/include"
    "-I$cache/ps5-openssl/include"
)

defines=(
    -DTTT2_HOST_PS5=1 -DTTT2_LOCAL_SAVES=1 -DTTT2_HAVE_STEAM=0
    -DTTT2_GPU_HW_BACKEND=1 -DTTT2_GPU_HW_D3D11=1 -DTTT2_D3D11_MOVIE_BACKEND=1
    -DDXVK_WSI_DISPLAY=1
)

cxx() { "$sdk/bin/prospero-clang++" "$@"; }

if [[ ! -f $work/kernel_stubs.gen.cpp ]] || [[ ! -d $work/ppc_split ]]; then
    rm -rf "$work"
    mkdir -p "$work/obj" "$work/ppc_split"
fi
cd "$ROOT/port"

# --- kernel stubs ----------------------------------------------------------------------
if [[ ! -f $work/kernel_stubs.gen.cpp ]]; then
    python3 "$ROOT/port/tools/gen_kernel_stubs.py" \
        --ppc-dir "$ppc_dir" --runtime "$runtime" --output "$work/kernel_stubs.gen.cpp" \
        > "$work/stubs.log"
fi

# --- generated PPC archive: split (once) + parallel bounded compile with skip -----------
if [[ ! -s $work/ppc_split.list ]]; then
    python3 "$ROOT/port/tools/split_ppc_units.py" \
        --output-dir "$work/ppc_split" --threshold-bytes 4194304 --chunk-bytes 2097152 \
        "$ppc_dir"/ppc_recomp.*.cpp "$ppc_dir"/ppc_func_mapping.cpp \
        > "$work/ppc_split.list"
fi

ppc_failed=0
while IFS= read -r src; do
    [[ -n $src ]] || continue
    out=$work/obj/ppc_$(basename "$src" .cpp).o
    if [[ -f $out && $out -nt $src ]]; then continue; fi
    cxx -O1 -march=x86-64-v3 -ffp-contract=off -Wno-everything -finline \
        "${defines[@]}" "${inc[@]}" \
        -include "$runtime/ppc_store_guard_linux.h" \
        -c "$src" -o "$out" || { ppc_failed=1; break; }
    while [ "$(jobs -rp | wc -l)" -ge "${TTT2_BUILD_JOBS:-6}" ]; do wait -n || true; done
done < <(cat "$work/ppc_split.list")
# Drain pending jobs and catch a failure from any of them.
while [ "$(jobs -rp | wc -l)" -gt 0 ]; do
    wait -n || { ppc_failed=1; break; }
done
[ $ppc_failed -eq 1 ] && { echo PPC_COMPILE_FAILED; exit 1; }

# --- hand-written runtime + host package + posix pieces ---------------------------------
runtime_sources=(
    "runtime_math.cpp" "guest_time.cpp" "skin_profile.cpp"
    "host_sleep.cpp" "host_status.cpp" "host_process.cpp"
    "host_file_common.cpp" "host_file_linux.cpp" "host_storage_linux.cpp"
    "host_random_linux.cpp" "host_fault_linux.cpp" "phys_mirror_linux.cpp"
    "host_udp_linux.cpp"
    "host_vm_ps5.cpp" "host_thread_ps5.cpp" "audio_output_ps5.cpp"
    "host_window_ps5.cpp" "host_input_ps5.cpp" "host_runtime_ps5.cpp"
    "ps5_entry.cpp"
    "main.cpp"
)
host_failed=0
for src in "${runtime_sources[@]}"; do
    out=$work/obj/rt_$src.o
    if [[ -f $out && $out -nt "$runtime/$src" ]]; then continue; fi
    cxx -O2 -Wno-everything "${defines[@]}" "${inc[@]}" \
        -c "$runtime/$src" -o "$out" || host_failed=1 &
    while [ "$(jobs -rp | wc -l)" -ge "${TTT2_BUILD_JOBS:-6}" ]; do wait -n || true; done
done

# --- renderer ---------------------------------------------------------------------------
for src in "kernel/gpu_render.cpp" "native_d3dcompiler.cpp"; do
    out=$work/obj/rdr_$(basename "$src" .cpp).o
    if [[ -f $out && $out -nt "$runtime/$src" ]]; then continue; fi
    cxx -O2 -Wno-everything "${defines[@]}" "${inc[@]}" \
        -c "$runtime/$src" -o "$out" || host_failed=1 &
    while [ "$(jobs -rp | wc -l)" -ge "${TTT2_BUILD_JOBS:-6}" ]; do wait -n || true; done
done

# --- kernel glob (Linux exclusions mirrored; steam stub kept) ----------------------------
kernel_files=()
for f in "$runtime"/kernel/*.cpp; do
    case "$(basename "$f")" in
        data_watch.cpp|diag_hooks.cpp|diag_hooks_linux.cpp|steam_bridge.cpp|steam_bridge_linux.cpp| \
        host_settings_windows.cpp|gpu_render.cpp|xma.cpp)
            continue ;;
    esac
    kernel_files+=("$f")
done
for f in "${kernel_files[@]}" "$runtime/kernel/xma.cpp" \
         "$runtime/kernel/vk_trampolines_ps5.cpp" \
         "$work/kernel_stubs.gen.cpp"; do
    out=$work/obj/k_$(basename "$f").o
    if [[ -f $out && $out -nt "$f" ]]; then continue; fi
    cxx -O2 -Wno-everything "${defines[@]}" "${inc[@]}" \
        -c "$f" -o "$out" || host_failed=1 &
    while [ "$(jobs -rp | wc -l)" -ge "${TTT2_BUILD_JOBS:-6}" ]; do wait -n || true; done
done

while [ "$(jobs -rp | wc -l)" -gt 0 ]; do
    wait -n || host_failed=1
done
[ $host_failed -eq 1 ] && { echo HOST_COMPILE_FAILED; exit 1; }

# --- the link: radv-link recipe over everything ---------------------------------------
source "$radv_root/tools/radv-link.sh"
radv_link_recipe "$radv_root" "$sdk" "$radv_archive"

# The AGC imports the RADV winsys needs: link stubs name the imports, the
# system module supplies them at load time (tools/build-radv-title.sh's stub
# flow verbatim).
mkdir -p "$work/stubs"
for entry in "libSceAgc:vendor/ps5/sdk/stubs/agc_canary_link_stub.c" \
             "libSceAgcDriver:vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c"; do
    lib=${entry%%:*}
    source=${entry#*:}
    "$sdk/bin/prospero-clang" -std=c11 -O2 -fPIC -c "$radv_root/$source" -o "$work/stubs/${lib}_stub.o"
    "$sdk/bin/prospero-lld" --shared -soname "${lib}.prx" -o "$work/stubs/${lib}.so" "$work/stubs/${lib}_stub.o"
done

"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
    --version-script "$radv_root/tooling/native/app-symbols.map" \
    --exclude-libs=ALL \
    --allow-multiple-definition \
    -e _start -o "$work/llvm-pie.elf" \
    "$work"/obj/*.o \
    "$cache/vkd3d-ps5-build/.libs/libvkd3d-shader.a" \
    "$cache/ps5-ffmpeg-xma/build/libavcodec/libavcodec.a" \
    "$cache/ps5-ffmpeg-xma/build/libavutil/libavutil.a" \
    "$cache/ps5-openssl/lib/libssl.a" \
    "$cache/ps5-openssl/lib/libcrypto.a" \
    "$work/stubs/libSceAgc.so" \
    "$work/stubs/libSceAgcDriver.so" \
    --start-group \
        "$dxvk_build"/src/d3d11/d3d11_static.a \
        "$dxvk_build"/src/dxgi/dxgi_static.a \
        "$dxvk_build"/src/dxvk/libdxvk.a \
        "$dxvk_build"/src/dxbc/libdxbc.a \
        "$dxvk_build"/src/spirv/libspirv.a \
        "$dxvk_build"/src/util/libutil.a \
        "$dxvk_build"/src/vulkan/libvkcommon.a \
        "$dxvk_build"/src/wsi/libwsi.a \
    --end-group \
    "${radv_link_inputs[@]}" \
    --as-needed "$stubs"/libvulkan.a "$stubs"/libpthread.a "$stubs"/libm.a \
    --as-needed "$sdk"/target/lib/*.so
# The system module stub libraries go last: a .so listed before the objects that
# import it satisfies nothing yet, and --as-needed then drops it — which is what
# left every sceVideoOut/scePad/sceAudioOut import undefined when they sat first.
# They stay --as-needed so only the modules actually imported end up as NEEDED:
# listing all 32 makes the loader demand libmonosgen/libSceHttp on a title that
# never calls them.

printf 'PORT_ELF: %s\n' "$work/llvm-pie.elf"
