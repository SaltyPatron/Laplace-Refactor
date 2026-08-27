include_guard(GLOBAL)

function(laplace_configure_highway_contract contract_path generator_path output_root)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${contract_path}" "${generator_path}")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${generator_path}"
            --contract "${contract_path}"
            --output-root "${output_root}"
        RESULT_VARIABLE generator_status
        OUTPUT_VARIABLE generator_stdout
        ERROR_VARIABLE generator_stderr)
    if(NOT generator_status EQUAL 0)
        message(FATAL_ERROR
            "Typed numerical highway generation failed:\n"
            "${generator_stdout}${generator_stderr}")
    endif()
    set(LAPLACE_HIGHWAY_GENERATED_ROOT "${output_root}" PARENT_SCOPE)
    set(LAPLACE_HIGHWAY_GENERATED_HEADER
        "${output_root}/laplace/contract/highway.h" PARENT_SCOPE)
    set(LAPLACE_HIGHWAY_GENERATED_CSHARP
        "${output_root}/csharp/HighwayContract.g.cs" PARENT_SCOPE)
endfunction()
