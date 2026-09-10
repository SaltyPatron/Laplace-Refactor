find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(target_attention_contract_sql
    "${CMAKE_CURRENT_BINARY_DIR}/target_attention_contract.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/target_attention_contract.sql.in"
    "${target_attention_contract_sql}"
    @ONLY)

set(LAPLACE_TARGET_ATTENTION_CONTRACT_SQL "${target_attention_contract_sql}")
set(target_attention_export_contract_sql
    "${CMAKE_CURRENT_BINARY_DIR}/target_attention_export_contract.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/target_attention_export_contract.sql.in"
    "${target_attention_export_contract_sql}"
    @ONLY)

add_executable(laplace_postgres_target_attention_probe
    tests/target_attention_probe.cpp)
target_link_libraries(laplace_postgres_target_attention_probe PRIVATE
    Laplace::Framework Laplace::CognitionOperator)
target_compile_options(laplace_postgres_target_attention_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

add_test(
    NAME postgres.target-attention-native-parity
    COMMAND "${LAPLACE_OBSERVATION_COGNITION_BASH}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_target_attention_test.sh"
        "${LAPLACE_POSTGRES_BINDIR}"
        "$<TARGET_FILE_DIR:laplace_pg>/share"
        "$<TARGET_FILE_DIR:laplace_pg>"
        "$<TARGET_FILE_DIR:laplace_engine>"
        "$<TARGET_FILE:laplace_postgres_target_attention_probe>"
        "${target_attention_contract_sql}"
        "${laplace_sanitizer_preload}")
set_tests_properties(postgres.target-attention-native-parity PROPERTIES
    LABELS "implementation;postgresql;cognition;target-compile;target-factorization;target-attention;qk;vo;receipt"
    RUN_SERIAL TRUE
    TIMEOUT 120
    ENVIRONMENT
        "RUNNER_TEMP=${CMAKE_CURRENT_BINARY_DIR}/target-attention;LAPLACE_POSTGRES_TARGET_ATTENTION_TEST_PORT=55444")

add_test(
    NAME postgres.target-attention-safetensors-export
    COMMAND "${LAPLACE_OBSERVATION_COGNITION_BASH}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_target_attention_test.sh"
        "${LAPLACE_POSTGRES_BINDIR}"
        "$<TARGET_FILE_DIR:laplace_pg>/share"
        "$<TARGET_FILE_DIR:laplace_pg>"
        "$<TARGET_FILE_DIR:laplace_engine>"
        "$<TARGET_FILE:laplace_postgres_target_attention_probe>"
        "${target_attention_export_contract_sql}"
        "${laplace_sanitizer_preload}")
set_tests_properties(postgres.target-attention-safetensors-export PROPERTIES
    LABELS "implementation;postgresql;cognition;target-compile;target-attention;target-attention-safetensors;artifact;export;qk;vo;receipt"
    RUN_SERIAL TRUE
    TIMEOUT 120
    ENVIRONMENT
        "RUNNER_TEMP=${CMAKE_CURRENT_BINARY_DIR}/target-attention-export;LAPLACE_POSTGRES_TARGET_ATTENTION_TEST_PORT=55445")

add_test(
    NAME postgres.target-attention-model-export-cli
    COMMAND "${LAPLACE_OBSERVATION_COGNITION_BASH}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_target_attention_cli_test.sh"
        "${LAPLACE_POSTGRES_BINDIR}"
        "$<TARGET_FILE_DIR:laplace_pg>/share"
        "$<TARGET_FILE_DIR:laplace_pg>"
        "$<TARGET_FILE_DIR:laplace_engine>"
        "$<TARGET_FILE:laplace_postgres_target_attention_probe>"
        "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/tools/model_export.py"
        "${LAPLACE_FRAMEWORK_MAJOR}"
        "${LAPLACE_FRAMEWORK_MINOR}"
        "${LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY}"
        "${laplace_sanitizer_preload}")
set_tests_properties(postgres.target-attention-model-export-cli PROPERTIES
    LABELS "implementation;postgresql;cognition;target-compile;target-attention;target-attention-safetensors;artifact;export;cli;product-surface;receipt"
    RUN_SERIAL TRUE
    TIMEOUT 120
    ENVIRONMENT
        "RUNNER_TEMP=${CMAKE_CURRENT_BINARY_DIR}/target-attention-cli;LAPLACE_POSTGRES_TARGET_ATTENTION_CLI_TEST_PORT=55446")