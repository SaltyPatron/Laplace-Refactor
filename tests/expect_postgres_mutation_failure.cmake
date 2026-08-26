execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "LAPLACE_MUTANT_MODULE=${MUTANT_MODULE}"
        "${BASH}" "${RUNNER}" persistence-mutation
        "${PG_BINDIR}" "${CONTROL_ROOT}" "${MODULE_DIRECTORY}"
        "${ENGINE_DIRECTORY}" "${NATIVE_PROBE}" "${PERFCACHE_PROBE}"
        "${SQL_FILE}" "${SANITIZER_PRELOAD}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "${MUTANT_NAME} unexpectedly passed")
endif()
string(CONCAT evidence "${output}" "${error}")
if(NOT evidence MATCHES "${EXPECTED_PATTERN}")
    message(FATAL_ERROR
        "${MUTANT_NAME} failed for an unrelated reason:\n${evidence}")
endif()
