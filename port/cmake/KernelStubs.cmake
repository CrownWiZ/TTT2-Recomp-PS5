# Generate against this runtime so new real imports cannot collide with stale stubs.
function(ttt2_generate_kernel_stubs output_variable ppc_root runtime_root)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    get_filename_component(port_root "${runtime_root}/../.." ABSOLUTE)
    set(generator "${port_root}/tools/gen_kernel_stubs.py")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/generated/kernel_stubs.gen.cpp")
    file(GLOB ppc_inputs CONFIGURE_DEPENDS "${ppc_root}/ppc_recomp.*.cpp" "${ppc_root}/ppc_func_mapping.cpp")
    file(GLOB_RECURSE runtime_inputs CONFIGURE_DEPENDS "${runtime_root}/*.cpp")
    list(FILTER runtime_inputs EXCLUDE REGEX "\\.gen\\.cpp$")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${Python3_EXECUTABLE}" "${generator}"
            --ppc-dir "${ppc_root}" --runtime "${runtime_root}" --output "${output}"
        DEPENDS "${generator}" ${ppc_inputs} ${runtime_inputs}
        VERBATIM COMMENT "Generating missing Xbox import stubs")
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()
