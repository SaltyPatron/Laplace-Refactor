function(laplace_compile_tabular_source_profile contract generator output)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${generator}"
            --contract "${contract}"
            --output "${output}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "Tabular source profile generation failed (${result})\n${stdout}\n${stderr}")
    endif()
endfunction()
