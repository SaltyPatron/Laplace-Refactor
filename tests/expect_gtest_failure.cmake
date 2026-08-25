if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE was not provided")
endif()
if(NOT DEFINED FILTER)
    message(FATAL_ERROR "FILTER was not provided")
endif()

execute_process(
    COMMAND "${PROBE}" "--gtest_filter=${FILTER}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_stdout
    ERROR_VARIABLE probe_stderr)

if(probe_result EQUAL 0)
    message(FATAL_ERROR
        "mutant unexpectedly passed ${FILTER}\n"
        "stdout=${probe_stdout}\nstderr=${probe_stderr}")
endif()
