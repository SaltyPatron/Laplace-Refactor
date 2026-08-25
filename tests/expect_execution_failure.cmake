execute_process(COMMAND "${PROBE}" RESULT_VARIABLE result)
if(result EQUAL 0)
    message(FATAL_ERROR "resource-duplication mutant unexpectedly satisfied conservation")
endif()
