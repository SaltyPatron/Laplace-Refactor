execute_process(COMMAND "${PROBE}" RESULT_VARIABLE result)
if(result EQUAL 0)
    message(FATAL_ERROR
        "wrong-eigenvalue dependency mutant unexpectedly passed the provider probe")
endif()
