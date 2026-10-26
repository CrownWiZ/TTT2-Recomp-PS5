cmake_minimum_required(VERSION 3.22)
project(ttt2_setup LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(TTT2_GAME_SOURCE "" CACHE PATH "Checkout with XenonRecomp dependencies")
get_filename_component(port_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${port_root}/cmake/ShaderBuildId.cmake")
set(xenon "${TTT2_GAME_SOURCE}/tools/XenonRecomp")
set(third "${xenon}/thirdparty")
add_library(ttt2_setup_core STATIC
    "${port_root}/src/setup/xdvdfs.cpp" "${port_root}/src/setup/stfs.cpp"
    "${port_root}/src/setup/game_data.cpp" "${port_root}/src/setup/mobile_install.cpp"
    "${xenon}/XenonUtils/xex_patcher.cpp" "${xenon}/XenonUtils/memory_mapped_file.cpp"
    "${third}/libmspack/libmspack/mspack/lzxd.c" "${third}/tiny-AES-c/aes.c")
set_target_properties(ttt2_setup_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(ttt2_setup_core PUBLIC "${port_root}/src" PRIVATE
    "${xenon}/XenonUtils" "${third}/libmspack/libmspack/mspack"
    "${third}/tiny-AES-c" "${third}/TinySHA1")
target_compile_options(ttt2_setup_core PRIVATE -O2)
if(ANDROID)
    add_library(ttt2_setup SHARED setup_jni.cpp)
    target_link_libraries(ttt2_setup PRIVATE ttt2_setup_core)
    # CPU-only translator; hidden unused renderer sections are discarded at link time.
    add_library(ttt2_shader_setup SHARED "${port_root}/tools/compile_archive_shaders.cpp")
    ttt2_shader_build_id(ttt2_shader_setup "${port_root}/src/runtime")
    target_include_directories(ttt2_shader_setup PRIVATE
        "${port_root}/android/include" "${TTT2_SDL_SOURCE}/include" "${TTT2_PPC_HEADERS}" "${TTT2_GAME_SOURCE}/re/ppc"
        "${xenon}/XenonUtils" "${third}/simde"
        "${port_root}/src/runtime" "${port_root}/src/runtime/kernel"
        "${TTT2_VKD3D_ROOT}/source/include" "${TTT2_VKD3D_ROOT}/build/include")
    target_compile_definitions(ttt2_shader_setup PRIVATE TTT2_ANDROID_SHADER_SETUP=1)
    target_compile_options(ttt2_shader_setup PRIVATE -O2 -ffunction-sections -fdata-sections
        -fvisibility=hidden -fvisibility-inlines-hidden -ffp-contract=off
        "-include${TTT2_PPC_HEADERS}/ppc_arm64_compat.h")
    target_link_options(ttt2_shader_setup PRIVATE -Wl,--gc-sections -Wl,--no-undefined)
    target_link_libraries(ttt2_shader_setup PRIVATE vkd3d_shader)
else()
    add_executable(ttt2_extract extract_cli.cpp)
    target_link_libraries(ttt2_extract PRIVATE ttt2_setup_core)
endif()
