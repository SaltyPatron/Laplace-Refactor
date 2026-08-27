execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "LAPLACE_MUTANT_MODULE=${MUTANT_MODULE}"
        "LAPLACE_UNICODE_SOURCE_ROOT=${UNICODE_SOURCE_ROOT}"
        "${BASH}" "${RUNNER}" unicode-access-mutation
        "${PG_BINDIR}" "${CONTROL_ROOT}" "${MODULE_DIRECTORY}"
        "${ENGINE_DIRECTORY}" "${NATIVE_PROBE}" "${PERFCACHE_PROBE}" "${SQL_FILE}"
        "${SANITIZER_PRELOAD}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "Unicode access expected-epoch mutant unexpectedly passed")
endif()
string(CONCAT evidence "${output}" "${error}")
if(NOT evidence MATCHES
   "Unicode access expected-epoch mutant accepted a stale generation")
    message(FATAL_ERROR
        "Unicode access expected-epoch mutant failed for an unrelated reason:\n${evidence}")
endif()
