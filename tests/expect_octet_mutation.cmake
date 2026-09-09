if(NOT DEFINED PROBE)
    message(FATAL_ERROR "PROBE is required")
endif()
execute_process(COMMAND "${PROBE}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
string(STRIP "${error}" error)
if(NOT "${status}" STREQUAL "1" OR
   NOT "${error}" STREQUAL "octet boundary rejection failure position=256")
    message(FATAL_ERROR "Unexpected mutation result: status=${status}; stdout=${output}; stderr=${error}")
endif()
message(STATUS "Octet truncation mutant detected at position 256")
