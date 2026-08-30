if(NOT DEFINED STORE OR NOT DEFINED TEST_ROOT)
    message(FATAL_ERROR "STORE and TEST_ROOT are required")
endif()

set(expected "6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85")
file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/store")
file(WRITE "${TEST_ROOT}/receipt.json" "abc")

execute_process(
    COMMAND "${STORE}" put --receipt "${TEST_ROOT}/receipt.json" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE put_result
    OUTPUT_VARIABLE digest
    ERROR_VARIABLE put_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT put_result EQUAL 0)
    message(FATAL_ERROR "receipt store put failed: ${put_error}")
endif()
if(NOT digest STREQUAL expected)
    message(FATAL_ERROR "receipt bytes were not identified by the exact BLAKE3 test vector")
endif()
set(stored "${TEST_ROOT}/store/${expected}.receipt")
if(NOT EXISTS "${stored}")
    message(FATAL_ERROR "content-addressed receipt was not published")
endif()
file(READ "${stored}" replay)
if(NOT replay STREQUAL "abc")
    message(FATAL_ERROR "published receipt bytes differ from input")
endif()

execute_process(
    COMMAND "${STORE}" verify --digest "${expected}" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE verify_result
    ERROR_VARIABLE verify_error)
if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "receipt replay verification failed: ${verify_error}")
endif()

execute_process(
    COMMAND "${STORE}" get --digest "${expected}" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE get_result
    OUTPUT_VARIABLE fetched
    ERROR_VARIABLE get_error)
if(NOT get_result EQUAL 0)
    message(FATAL_ERROR "receipt fetch by digest failed: ${get_error}")
endif()
if(NOT fetched STREQUAL "abc")
    message(FATAL_ERROR "receipt fetch did not return the exact authenticated bytes")
endif()

execute_process(
    COMMAND "${STORE}" put --receipt "${TEST_ROOT}/receipt.json" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE replay_result
    OUTPUT_VARIABLE replay_digest
    ERROR_VARIABLE replay_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT replay_result EQUAL 0 OR NOT replay_digest STREQUAL expected)
    message(FATAL_ERROR "idempotent receipt publication failed: ${replay_error}")
endif()

file(APPEND "${stored}" "mutation")
execute_process(
    COMMAND "${STORE}" verify --digest "${expected}" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE mutation_result)
if(mutation_result EQUAL 0)
    message(FATAL_ERROR "mutated stored receipt was accepted")
endif()
execute_process(
    COMMAND "${STORE}" get --digest "${expected}" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE mutated_get_result
    OUTPUT_VARIABLE mutated_fetched)
if(mutated_get_result EQUAL 0)
    message(FATAL_ERROR "mutated stored receipt was fetched as authenticated")
endif()
if(NOT mutated_fetched STREQUAL "")
    message(FATAL_ERROR "unauthenticated receipt bytes escaped through digest fetch")
endif()
execute_process(
    COMMAND "${STORE}" put --receipt "${TEST_ROOT}/receipt.json" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE collision_result)
if(collision_result EQUAL 0)
    message(FATAL_ERROR "conflicting preexisting content-addressed receipt was replaced or accepted")
endif()

file(REMOVE "${stored}")
file(CREATE_LINK "${TEST_ROOT}/receipt.json" "${stored}" SYMBOLIC)
execute_process(
    COMMAND "${STORE}" get --digest "${expected}" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE stored_symlink_result
    OUTPUT_VARIABLE stored_symlink_fetched)
if(stored_symlink_result EQUAL 0)
    message(FATAL_ERROR "symlinked stored receipt was fetched as authenticated")
endif()
if(NOT stored_symlink_fetched STREQUAL "")
    message(FATAL_ERROR "symlinked stored receipt bytes escaped through digest fetch")
endif()
file(REMOVE "${stored}")

file(CREATE_LINK "${TEST_ROOT}/receipt.json" "${TEST_ROOT}/receipt-link.json" SYMBOLIC)
execute_process(
    COMMAND "${STORE}" put --receipt "${TEST_ROOT}/receipt-link.json" --root "${TEST_ROOT}/store"
    RESULT_VARIABLE symlink_result)
if(symlink_result EQUAL 0)
    message(FATAL_ERROR "symlink receipt input was accepted")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
