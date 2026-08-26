if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE was not provided")
endif()

execute_process(
    COMMAND "${PROBE}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_stdout
    ERROR_VARIABLE probe_stderr)

if(NOT probe_result EQUAL 2)
    message(FATAL_ERROR
        "framework sink-artifact defect probe returned ${probe_result}; expected exact return 2\n"
        "stdout=${probe_stdout}\nstderr=${probe_stderr}")
endif()
if(NOT probe_stderr STREQUAL "framework-sink-artifact-output\n")
    message(FATAL_ERROR
        "framework sink-artifact defect probe failed unexpectedly: ${probe_stderr}")
endif()
