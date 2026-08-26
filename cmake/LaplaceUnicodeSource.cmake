include_guard(GLOBAL)

function(laplace_configure_unicode_source contracts_root generator output_path)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${generator}"
        "${contracts_root}/unicode-source.json"
        "${contracts_root}/unicode-atom-record.json"
        "${contracts_root}/unicode-root-stream.json"
        "${contracts_root}/ducet-totalization.json"
        "${contracts_root}/super-fibonacci-hopf.json"
        "${contracts_root}/hilbert-numeric.json"
        "${contracts_root}/unicode-atomic-physicality.json")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${generator}"
            --contracts "${contracts_root}"
            --output "${output_path}"
        RESULT_VARIABLE generator_status
        OUTPUT_VARIABLE generator_stdout
        ERROR_VARIABLE generator_stderr)
    if(NOT generator_status EQUAL 0)
        message(FATAL_ERROR
            "Unicode source manifest generation failed:\n"
            "${generator_stdout}${generator_stderr}")
    endif()
endfunction()
