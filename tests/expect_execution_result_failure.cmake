execute_process(COMMAND "${PROBE}" RESULT_VARIABLE result)
if(result EQUAL 0)
    message(FATAL_ERROR "missing-chunk-result mutant unexpectedly rejected incomplete work")
endif()
