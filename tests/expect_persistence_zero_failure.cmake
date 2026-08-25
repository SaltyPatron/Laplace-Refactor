if(NOT DEFINED PROBE OR NOT EXISTS "${PROBE}")
    message(FATAL_ERROR "persistence zero-pattern mutation probe is missing")
endif()

execute_process(
    COMMAND "${PROBE}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error)

if(NOT probe_result EQUAL 2 OR
   NOT probe_output MATCHES "zero-bit-pattern-rejected")
    message(FATAL_ERROR
        "sentinel mutant was not isolated by zero-pattern acceptance: "
        "result=${probe_result} output=${probe_output} error=${probe_error}")
endif()

message(STATUS "identity/digest zero-sentinel regression was detected")
