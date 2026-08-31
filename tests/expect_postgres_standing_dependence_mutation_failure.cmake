execute_process(
    COMMAND "${BASH}" "${RUNNER}" standing
        "${PG_BINDIR}" "${CONTROL_ROOT}" "${MODULE_DIRECTORY}"
        "${ENGINE_DIRECTORY}" "${NATIVE_PROBE}" "${PERFCACHE_PROBE}"
        "${SQL_FILE}" "${SANITIZER_PRELOAD}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "standing dependence-history mutant unexpectedly passed")
endif()
string(CONCAT evidence "${output}" "${error}")
if(NOT evidence MATCHES
   "standing accepted reuse of a dependence root across periods")
    message(FATAL_ERROR
        "standing dependence-history mutant failed for an unrelated reason:\n${evidence}")
endif()
