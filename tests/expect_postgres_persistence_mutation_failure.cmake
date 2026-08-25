execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "LAPLACE_MUTANT_MODULE=${MUTANT_MODULE}"
        "${BASH}" "${RUNNER}" persistence-mutation
        "${PG_BINDIR}" "${CONTROL_ROOT}" "${MODULE_DIRECTORY}"
        "${ENGINE_DIRECTORY}" "${NATIVE_PROBE}" "${SQL_FILE}"
        "${SANITIZER_PRELOAD}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
if(result EQUAL 0)
    message(FATAL_ERROR "blind-conflict mutant unexpectedly passed")
endif()
string(CONCAT evidence "${output}" "${error}")
if(NOT evidence MATCHES "blind conflict mutant accepted a same-128/different-witness identity")
    message(FATAL_ERROR "blind-conflict mutant failed for an unrelated reason:\n${evidence}")
endif()
