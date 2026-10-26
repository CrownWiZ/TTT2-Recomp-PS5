cmake_minimum_required(VERSION 3.22)
project(ttt2_android_game LANGUAGES C CXX)

if(NOT ANDROID OR NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "This target requires Android ARM64")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Opt-in benchmark variants; keep the ordinary Android build unchanged.
option(TTT2_PAGE_SUMMARY_EXPERIMENT "Benchmark block page-sequence summaries" OFF)
option(TTT2_VERTEX_SLOT_SCAN_EXPERIMENT "Benchmark grouped vertex-slot scanning" OFF)
option(TTT2_ALU_BATCH_EXPERIMENT "Benchmark batched ALU constant writes" OFF)

set(TTT2_GAME_SOURCE "" CACHE PATH "Checkout containing generated PPC and game files")
set(TTT2_SDL_SOURCE "" CACHE PATH "SDL3 source directory")
set(TTT2_SDL_LIBRARY "" CACHE FILEPATH "Android libSDL3.so")
set(TTT2_DXVK_ROOT "" CACHE PATH "Android DXVK Native build root")
set(TTT2_VKD3D_ROOT "" CACHE PATH "Android vkd3d-shader build root")
set(TTT2_FFMPEG_ROOT "" CACHE PATH "Android FFmpeg XMA build root")
set(TTT2_OPENSSL_ROOT "" CACHE PATH "Android ARM64 OpenSSL installation prefix")
set(TTT2_PPC_ARCHIVE "" CACHE FILEPATH "Optimized Android PPC archive")
set(TTT2_PPC_HEADERS "" CACHE PATH "Android PPC generated-header overlay")
foreach(required TTT2_GAME_SOURCE TTT2_SDL_SOURCE TTT2_SDL_LIBRARY
        TTT2_DXVK_ROOT TTT2_VKD3D_ROOT TTT2_FFMPEG_ROOT TTT2_PPC_ARCHIVE
        TTT2_PPC_HEADERS TTT2_OPENSSL_ROOT)
    if(NOT EXISTS "${${required}}")
        message(FATAL_ERROR "Missing ${required}: ${${required}}")
    endif()
endforeach()

foreach(openssl_input include/openssl/ssl.h lib/libssl.a lib/libcrypto.a)
    if(NOT EXISTS "${TTT2_OPENSSL_ROOT}/${openssl_input}")
        message(FATAL_ERROR "Missing Android OpenSSL input: ${TTT2_OPENSSL_ROOT}/${openssl_input}; run port/tools/build_android_openssl.sh")
    endif()
endforeach()
add_library(ttt2_openssl_crypto STATIC IMPORTED)
set_target_properties(ttt2_openssl_crypto PROPERTIES
    IMPORTED_LOCATION "${TTT2_OPENSSL_ROOT}/lib/libcrypto.a"
    INTERFACE_INCLUDE_DIRECTORIES "${TTT2_OPENSSL_ROOT}/include")
add_library(ttt2_openssl_ssl STATIC IMPORTED)
set_target_properties(ttt2_openssl_ssl PROPERTIES
    IMPORTED_LOCATION "${TTT2_OPENSSL_ROOT}/lib/libssl.a"
    INTERFACE_INCLUDE_DIRECTORIES "${TTT2_OPENSSL_ROOT}/include"
    INTERFACE_LINK_LIBRARIES ttt2_openssl_crypto)

get_filename_component(port_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(source_root "${port_root}/.." ABSOLUTE)
# Keep build-machine paths out of diagnostics embedded in the release libraries.
add_compile_options("-ffile-prefix-map=${source_root}=."
    "-ffile-prefix-map=${TTT2_GAME_SOURCE}=guest"
    "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
set(runtime "${port_root}/src/runtime")
include("${port_root}/cmake/ShaderBuildId.cmake")
set(ppc_dir "${TTT2_GAME_SOURCE}/re/ppc")
set(xenon "${TTT2_GAME_SOURCE}/tools/XenonRecomp")
set(dxvk_include "${TTT2_DXVK_ROOT}/source/include/native")
set(ffmpeg_source "${TTT2_FFMPEG_ROOT}/source")
set(ffmpeg_build "${TTT2_FFMPEG_ROOT}/build")
include("${port_root}/cmake/KernelStubs.cmake")
ttt2_generate_kernel_stubs(stubs "${ppc_dir}" "${runtime}")

file(GLOB kernel_sources CONFIGURE_DEPENDS "${runtime}/kernel/*.cpp")
foreach(excluded data_watch.cpp diag_hooks.cpp steam_bridge.cpp
                 steam_bridge_linux.cpp host_settings_windows.cpp)
    list(REMOVE_ITEM kernel_sources "${runtime}/kernel/${excluded}")
endforeach()

add_library(ttt2_android_game SHARED
    "${runtime}/android_game_entry.cpp"
    "${runtime}/skin_profile.cpp"
    "${runtime}/main.cpp"
    "${runtime}/runtime_math.cpp"
    "${runtime}/guest_time.cpp"
    "${runtime}/audio_output.cpp"
    "${runtime}/host_window_sdl.cpp"
    "${runtime}/host_input_sdl.cpp"
    "${runtime}/host_thread_sdl.cpp"
    "${runtime}/host_sleep.cpp"
    "${runtime}/host_status.cpp"
    "${runtime}/host_process.cpp"
    "${runtime}/host_vm_android.cpp"
    "${runtime}/host_fault_android.cpp"
    "${runtime}/host_runtime_android.cpp"
    "${runtime}/host_file_common.cpp"
    "${runtime}/host_file_linux.cpp"
    "${runtime}/host_storage_linux.cpp"
    "${runtime}/host_udp_linux.cpp"
    "${runtime}/host_random_linux.cpp"
    "${runtime}/phys_mirror_linux.cpp"
    "${runtime}/native_d3dcompiler.cpp"
    "${stubs}"
    ${kernel_sources})
set_target_properties(ttt2_android_game PROPERTIES OUTPUT_NAME main)
ttt2_shader_build_id(ttt2_android_game "${runtime}")
target_include_directories(ttt2_android_game PRIVATE
    "${port_root}/android/include" "${port_root}/native/include" "${TTT2_PPC_HEADERS}" "${ppc_dir}"
    "${xenon}/XenonUtils" "${xenon}/thirdparty/simde"
    "${dxvk_include}" "${dxvk_include}/directx" "${dxvk_include}/windows"
    "${TTT2_VKD3D_ROOT}/source/include" "${TTT2_VKD3D_ROOT}/build/include"
    "${ffmpeg_source}" "${ffmpeg_build}"
    "${TTT2_SDL_SOURCE}/include" "${runtime}" "${runtime}/kernel")
target_compile_definitions(ttt2_android_game PRIVATE
    TTT2_GEOMETRY_AB_PROBE=1
    TTT2_HOST_GAME_UI=1 TTT2_LOCAL_SAVES=1 TTT2_HAVE_STEAM=0
    TTT2_GPU_HW_BACKEND=1 TTT2_GPU_HW_D3D11=1 TTT2_D3D11_MOVIE_BACKEND=1)
foreach(experiment TTT2_PAGE_SUMMARY_EXPERIMENT TTT2_VERTEX_SLOT_SCAN_EXPERIMENT
                   TTT2_ALU_BATCH_EXPERIMENT)
    if(${experiment})
        target_compile_definitions(ttt2_android_game PRIVATE ${experiment}=1)
    endif()
endforeach()
target_compile_options(ttt2_android_game PRIVATE
    -O2 -march=armv8-a -ffp-contract=off
    "-include${TTT2_PPC_HEADERS}/ppc_arm64_compat.h")
set_source_files_properties("${runtime}/main.cpp" PROPERTIES
    COMPILE_DEFINITIONS "main=ttt2_game_main")
target_link_options(ttt2_android_game PRIVATE
    -Wl,--no-undefined -Wl,--no-dependent-libraries -Wl,-Bsymbolic-functions)

add_library(sdl3 SHARED IMPORTED)
set_target_properties(sdl3 PROPERTIES IMPORTED_LOCATION "${TTT2_SDL_LIBRARY}")
add_library(dxvk_dxgi SHARED IMPORTED)
set_target_properties(dxvk_dxgi PROPERTIES IMPORTED_LOCATION
    "${TTT2_DXVK_ROOT}/build/src/dxgi/libdxvk_dxgi.so")
add_library(dxvk_d3d11 SHARED IMPORTED)
set_target_properties(dxvk_d3d11 PROPERTIES IMPORTED_LOCATION
    "${TTT2_DXVK_ROOT}/build/src/d3d11/libdxvk_d3d11.so")
add_library(vkd3d_shader SHARED IMPORTED)
set_target_properties(vkd3d_shader PROPERTIES IMPORTED_LOCATION
    "${TTT2_VKD3D_ROOT}/build/.libs/libvkd3d-shader.so")

target_link_libraries(ttt2_android_game PRIVATE
    "${TTT2_PPC_ARCHIVE}"
    "${ffmpeg_build}/libavcodec/libavcodec.a"
    "${ffmpeg_build}/libavutil/libavutil.a"
    sdl3 dxvk_d3d11 dxvk_dxgi vkd3d_shader
    ttt2_openssl_ssl ttt2_openssl_crypto android log vulkan dl m)

# Small independent JNI library: setup does not initialize SDL or the game VM.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../setup" "setup")
