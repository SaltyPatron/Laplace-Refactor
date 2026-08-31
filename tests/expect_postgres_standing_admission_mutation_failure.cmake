execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "LAPLACE_MUTANT_MODULE=${MUTANT_MODULE}"
        "${BASH}" "${RUNNER}" standing-admission-mutation
        "${PG_BINDIR}" "${CONTROL_ROOT}" "${MODULE_DIRECTORY}"
        "${ENGINE_DIRECTORY}" "${NATIVE_PROBE}" "${PERFCACHE_PROBE}"
        "${SQL_FILE}" "${SANITIZER_PRELOAD}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
set(evidence "${output}\n${error}")
if(result EQUAL 0)
    message(FATAL_ERROR "standing admission-bypass mutant unexpectedly passed")
endif()
if(NOT evidence MATCHES "standing admission mutant accepted an unadmitted recipe")
    message(FATAL_ERROR
        "standing admission-bypass mutant failed for an unrelated reason:\n${evidence}")
endif()
