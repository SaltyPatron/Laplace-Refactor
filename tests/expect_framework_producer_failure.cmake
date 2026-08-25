execute_process(
    COMMAND "${PROBE}"
    RESULT_VARIABLE probe_result)
if(probe_result EQUAL 0)
    message(FATAL_ERROR
        "The producer cancellation mutant passed the cancellation contract")
endif()
