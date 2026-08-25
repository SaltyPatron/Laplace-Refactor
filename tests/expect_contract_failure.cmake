if(NOT DEFINED PROBE OR NOT EXISTS "${PROBE}")
    message(FATAL_ERROR "mutation probe executable is missing")
endif()

execute_process(
    COMMAND "${PROBE}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error)

if(NOT probe_result EQUAL 2)
    message(FATAL_ERROR
        "broken implementation did not fail with the contract mismatch status: "
        "result=${probe_result} output=${probe_output} error=${probe_error}")
endif()

if(NOT probe_output MATCHES "contract-vector-mismatch")
    message(FATAL_ERROR
        "broken implementation failed without the expected contract evidence: "
        "output=${probe_output} error=${probe_error}")
endif()

message(STATUS "deliberate composite-domain defect was detected by the contract probe")
