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
    # This deliberate defect proves that the blind composition-presence mutant
    # can publish without observing canonical state.  Older registrations used
    # a producer/preflight wrapper message, but the executable SQL contract now
    # raises the more specific semantic failure directly.  Accept only that
    # exact failure for this exact mutant; unrelated failures remain rejected.
    if(MUTANT_NAME STREQUAL "postgres.mutation-composition-presence-blind-detected" AND
       evidence MATCHES "blind composition-presence mutant published without observing canonical state")
        return()
    endif()
    message(FATAL_ERROR
        "${MUTANT_NAME} failed for an unrelated reason:\n${evidence}")
endif()
