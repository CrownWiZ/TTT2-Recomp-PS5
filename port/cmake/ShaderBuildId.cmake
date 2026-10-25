# Keep persistent shader caches tied to the translator implementation.
function(ttt2_shader_build_id target runtime_root)
    set(source_digests "")
    foreach(source kernel/gpu_render.cpp kernel/gpu_hw_translate.h
                   kernel/gpu_hw_api.h native_d3dcompiler.cpp)
        set(path "${runtime_root}/${source}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${path}")
        file(SHA256 "${path}" digest)
        string(APPEND source_digests "${digest}")
    endforeach()
    string(SHA256 build_id "${source_digests}")
    target_compile_definitions(${target} PRIVATE "TTT2_SHADER_BUILD_ID=\"${build_id}\"")
endfunction()
