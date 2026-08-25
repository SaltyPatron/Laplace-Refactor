if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE was not provided")
endif()
if(NOT DEFINED EXPECTED_ERROR)
    set(EXPECTED_ERROR "framework-activation-stale-epoch\n")
endif()

execute_process(
    COMMAND "${PROBE}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_stdout
    ERROR_VARIABLE probe_stderr)

if(NOT probe_result EQUAL 2)
    message(FATAL_ERROR
        "framework activation defect probe returned ${probe_result}; expected exact return 2\n"
        "stdout=${probe_stdout}\nstderr=${probe_stderr}")
endif()
if(NOT probe_stderr STREQUAL "${EXPECTED_ERROR}")
    message(FATAL_ERROR
        "framework activation defect probe failed unexpectedly: ${probe_stderr}")
endif()
