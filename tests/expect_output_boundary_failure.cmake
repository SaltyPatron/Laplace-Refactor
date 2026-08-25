execute_process(
    COMMAND "${CMAKE_COMMAND}"
        "-DMODULE=${MODULE}"
        -P "${PROBE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "repository-local output probe unexpectedly succeeded")
endif()
if(NOT error MATCHES "Laplace build output must remain outside the repository")
    message(FATAL_ERROR "repository-local output probe failed for an unexpected reason: ${output}${error}")
endif()
