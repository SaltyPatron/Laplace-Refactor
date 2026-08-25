if(NOT DEFINED PROBE OR NOT EXISTS "${PROBE}")
    message(FATAL_ERROR "zero-sentinel mutation probe is missing")
endif()

execute_process(
    COMMAND "${PROBE}"
    RESULT_VARIABLE probe_result
    OUTPUT_VARIABLE probe_output
    ERROR_VARIABLE probe_error)

if(NOT probe_result EQUAL 2 OR
   NOT probe_output STREQUAL "" OR
   NOT probe_error STREQUAL "zero-sentinel-regression\n")
    message(FATAL_ERROR
        "zero-sentinel mutant escaped or failed unexpectedly: "
        "result=${probe_result} output=${probe_output} error=${probe_error}")
endif()

message(STATUS "zero-bit-pattern sentinel regression detected")
