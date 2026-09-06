set(cognition_discourse_deposit_sql
    "${CMAKE_CURRENT_BINARY_DIR}/cognition_discourse_deposit.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cognition_discourse_deposit.sql.in"
    "${cognition_discourse_deposit_sql}"
    @ONLY)
set(cognition_discourse_readback_sql
    "${CMAKE_CURRENT_BINARY_DIR}/cognition_discourse_readback.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/cognition_discourse_readback.sql.in"
    "${cognition_discourse_readback_sql}"
    @ONLY)

add_executable(laplace_postgres_cognition_discourse_probe
    tests/cognition_discourse_probe.cpp)
target_link_libraries(laplace_postgres_cognition_discourse_probe PRIVATE
    Laplace::Persistence)
target_compile_options(laplace_postgres_cognition_discourse_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

add_test(
    NAME postgres.cognition-discourse-restart-readback
    COMMAND "${LAPLACE_OBSERVATION_COGNITION_BASH}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_cognition_discourse_test.sh"
        "${LAPLACE_POSTGRES_BINDIR}"
        "$<TARGET_FILE_DIR:laplace_pg>/share"
        "$<TARGET_FILE_DIR:laplace_pg>"
        "$<TARGET_FILE_DIR:laplace_engine>"
        "$<TARGET_FILE:laplace_postgres_cognition_discourse_probe>"
        "${cognition_discourse_deposit_sql}"
        "${cognition_discourse_readback_sql}"
        "${laplace_sanitizer_preload}"
        "v1")
set_tests_properties(postgres.cognition-discourse-restart-readback PROPERTIES
    LABELS "implementation;postgresql;cognition;conversation;persistence;restart;receipt"
    RUN_SERIAL TRUE
    TIMEOUT 120
    ENVIRONMENT
        "RUNNER_TEMP=${CMAKE_CURRENT_BINARY_DIR}/cognition-discourse;LAPLACE_POSTGRES_DISCOURSE_TEST_PORT=55443")
