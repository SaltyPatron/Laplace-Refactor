include("${CMAKE_SOURCE_DIR}/cmake/LaplacePostgreSQLBindings.cmake")

set(binding_header
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/postgresql_bindings.h")
set(extension_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--${PROJECT_VERSION}.sql")
set(extension_control
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace.control")
laplace_configure_postgresql_bindings(
    "${CMAKE_SOURCE_DIR}/contracts/isa.json"
    "${CMAKE_SOURCE_DIR}/contracts/persistence.json"
    "${CMAKE_SOURCE_DIR}/contracts/composition.json"
    "${CMAKE_SOURCE_DIR}/contracts/unicode-postgresql.json"
    "${CMAKE_SOURCE_DIR}/contracts/unicode-root-stream.json"
    "${CMAKE_SOURCE_DIR}/cmake/postgresql_bindings.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/laplace--version.sql.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/laplace.control.in"
    "${binding_header}"
    "${extension_sql}"
    "${extension_control}")

# The generated readback SQL is also the verified recurring reconciliation program.
# Keep the fresh base extension's readback body unchanged; only the standalone copy
# receives the product-cognition prefix plus idempotent materialization-route repair
# used by an already-persistent product cluster.
set(public_readback_reconcile_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace-public-readback.sql")
file(READ "${public_readback_reconcile_sql}" public_readback_reconcile_bindings)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/product_cognition_upgrade_prefix.sql"
    product_cognition_upgrade_prefix)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cognition_materialization_route.sql"
    cognition_materialization_route)
file(WRITE "${public_readback_reconcile_sql}"
    "${product_cognition_upgrade_prefix}\n${cognition_materialization_route}\n${public_readback_reconcile_bindings}")

file(READ "${CMAKE_SOURCE_DIR}/contracts/isa.json"
    laplace_source_structural_witness_contract)
string(JSON LAPLACE_SOURCE_STRUCTURAL_WITNESS_SCHEMA GET
    "${laplace_source_structural_witness_contract}" bindings postgresql schema)
set(source_structural_witness_sql
    "${CMAKE_CURRENT_BINARY_DIR}/source_structural_witness.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/source_structural_witness.sql.in"
    "${source_structural_witness_sql}"
    @ONLY)
file(READ "${source_structural_witness_sql}"
    LAPLACE_SOURCE_STRUCTURAL_WITNESS_SQL)
file(APPEND "${extension_sql}"
    "\
${LAPLACE_SOURCE_STRUCTURAL_WITNESS_SQL}\
")

set(LAPLACE_MACHINE_EXCEPTION_SCHEMA
    "${LAPLACE_SOURCE_STRUCTURAL_WITNESS_SCHEMA}")
set(machine_exception_sql
    "${CMAKE_CURRENT_BINARY_DIR}/machine_exception.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/machine_exception.sql.in"
    "${machine_exception_sql}"
    @ONLY)
file(READ "${machine_exception_sql}"
    LAPLACE_MACHINE_EXCEPTION_SQL)
file(APPEND "${extension_sql}"
    "\
${LAPLACE_MACHINE_EXCEPTION_SQL}\
")

set(LAPLACE_SOURCE_ADMISSION_METRICS_SCHEMA
    "${LAPLACE_SOURCE_STRUCTURAL_WITNESS_SCHEMA}")
set(source_admission_metrics_sql
    "${CMAKE_CURRENT_BINARY_DIR}/source_admission_metrics.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/source_admission_metrics.sql.in"
    "${source_admission_metrics_sql}"
    @ONLY)
file(READ "${source_admission_metrics_sql}"
    LAPLACE_SOURCE_ADMISSION_METRICS_SQL)
file(APPEND "${extension_sql}"
    "\
${LAPLACE_SOURCE_ADMISSION_METRICS_SQL}\
")

set(LAPLACE_OBSERVATION_COGNITION_SCHEMA
    "${LAPLACE_SOURCE_STRUCTURAL_WITNESS_SCHEMA}")
set(observation_cognition_sql
    "${CMAKE_CURRENT_BINARY_DIR}/observation_cognition.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/observation_cognition.sql.in"
    "${observation_cognition_sql}"
    @ONLY)
file(READ "${observation_cognition_sql}"
    LAPLACE_OBSERVATION_COGNITION_SQL)
file(APPEND "${extension_sql}"
    "\
${LAPLACE_OBSERVATION_COGNITION_SQL}\
")

set(LAPLACE_SEMANTIC_COGNITION_SCHEMA
    "${LAPLACE_SOURCE_STRUCTURAL_WITNESS_SCHEMA}")
set(semantic_cognition_sql
    "${CMAKE_CURRENT_BINARY_DIR}/semantic_cognition.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/semantic_cognition.sql.in"
    "${semantic_cognition_sql}"
    @ONLY)
file(READ "${semantic_cognition_sql}"
    LAPLACE_SEMANTIC_COGNITION_SQL)
file(APPEND "${extension_sql}"
    "\
${LAPLACE_SEMANTIC_COGNITION_SQL}\
")

set(LAPLACE_COGNITION_DISCOURSE_SCHEMA
    "${LAPLACE_SOURCE_STRUCTURAL_WITNESS_SCHEMA}")
set(cognition_discourse_sql
    "${CMAKE_CURRENT_BINARY_DIR}/cognition_discourse.sql")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cognition_discourse.sql.in"
    "${cognition_discourse_sql}"
    @ONLY)
file(READ "${cognition_discourse_sql}"
    LAPLACE_COGNITION_DISCOURSE_SQL)
file(APPEND "${extension_sql}"
    "\
${LAPLACE_COGNITION_DISCOURSE_SQL}\
")

set(laplace_postgresql_module_sources
    src/cognition_discourse_pg.c
    src/cognition_isa_pg.c
    src/cognition_firmware_pg.c
    src/cognition_product_pg.c
    src/cognition_materialization_route_pg.c
    src/composition_pg.c
    src/content_admission_pg.c
    src/evidence_lineage_pg.c
    src/evidence_testimony_pg.c
    src/standing_calculation_pg.c
    src/highway_registry_pg.c
    src/laplace_pg.c
    src/machine_exception_pg.c
    src/materialization_pg.c
    src/observation_cognition_pg.c
    src/observation_cognition_persisted_pg.c
    src/prompt_admission_pg.c
    src/semantic_cognition_pg.c
    src/persistence_pg.c
    src/persistence_rows_pg.c
    src/perfcache_pg.c
    src/reference_topology_pg.c
    src/reference_mapping_pg.c
    src/set_pg.c
    src/source_admission_pg.c
    src/source_profile_pg.c
    src/stock_recipe_pg.c
    src/source_structural_witness_pg.c
    src/unicode_perfcache_pg.c
    src/unicode_atoms_pg.c
    src/unicode_root_pg.c
    src/world_admission_pg.c)

function(laplace_configure_postgresql_module target)
    target_include_directories(${target} SYSTEM PRIVATE
        "${LAPLACE_POSTGRES_INCLUDE_SERVER}"
        "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
    target_include_directories(${target} PRIVATE
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${target} PRIVATE
        BLAKE3::blake3 Laplace::Composition Laplace::Isa Laplace::MachineException
        Laplace::Persistence Laplace::Perfcache Laplace::UnicodeNumeric)
    target_compile_definitions(${target} PRIVATE
        LAPLACE_UNICODE_SOURCE_ROOT="${LAPLACE_UNICODE_SOURCE_ROOT}")
    target_compile_options(${target} PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wshadow;-Wstrict-prototypes;-Wno-declaration-after-statement;-Wno-gnu-statement-expression-from-macro-expansion;-ffp-contract=off>)
    set_target_properties(${target} PROPERTIES
        C_EXTENSIONS ON
        PREFIX ""
        OUTPUT_NAME "${target}"
        BUILD_RPATH "$<TARGET_FILE_DIR:laplace_engine>")
endfunction()

add_library(laplace_pg MODULE ${laplace_postgresql_module_sources})
laplace_configure_postgresql_module(laplace_pg)
set_target_properties(laplace_pg PROPERTIES
    OUTPUT_NAME "laplace_pg"
    INSTALL_RPATH "$ORIGIN/../../lib"
    INSTALL_RPATH_USE_LINK_PATH FALSE)

if(BUILD_TESTING)
    target_compile_definitions(laplace_pg PRIVATE
        LAPLACE_POSTGRES_TESTING=1)
    target_sources(laplace_pg PRIVATE tests/cognition_firmware_provider_pg.c)

    if(NOT DEFINED LAPLACE_FRAMEWORK_EPOCH_FIRMWARE)
        message(FATAL_ERROR "Firmware SQL context binding lacks its generated epoch slot")
    endif()
    set(observation_cognition_contract_sql
        "${CMAKE_CURRENT_BINARY_DIR}/observation_cognition_contract.sql")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/observation_cognition_contract.sql.in"
        "${observation_cognition_contract_sql}"
        @ONLY)
    find_program(LAPLACE_OBSERVATION_COGNITION_BASH bash REQUIRED)
    add_executable(laplace_postgres_observation_cognition_probe
        tests/observation_cognition_probe.cpp)
    target_link_libraries(laplace_postgres_observation_cognition_probe PRIVATE
        Laplace::Isa Laplace::Persistence)
    target_compile_options(laplace_postgres_observation_cognition_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
    add_test(
        NAME postgres.observation-cognition-native-parity
        COMMAND "${LAPLACE_OBSERVATION_COGNITION_BASH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_observation_cognition_test.sh"
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_observation_cognition_probe>"
            "${observation_cognition_contract_sql}"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.observation-cognition-native-parity PROPERTIES
        LABELS "implementation;postgresql;cognition;observation;query;forward-pass;receipt"
        RUN_SERIAL TRUE
        TIMEOUT 120
        ENVIRONMENT
            "RUNNER_TEMP=${CMAKE_CURRENT_BINARY_DIR}/observation-cognition;LAPLACE_POSTGRES_OBSERVATION_TEST_PORT=55442")

    set(semantic_cognition_contract_sql
        "${CMAKE_CURRENT_BINARY_DIR}/semantic_cognition_contract.sql")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/semantic_cognition_contract.sql.in"
        "${semantic_cognition_contract_sql}"
        @ONLY)
    add_test(
        NAME postgres.semantic-cognition-live-mapping-provider
        COMMAND "${LAPLACE_OBSERVATION_COGNITION_BASH}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_semantic_cognition_test.sh"
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "${semantic_cognition_contract_sql}"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.semantic-cognition-live-mapping-provider PROPERTIES
        LABELS "implementation;postgresql;cognition;semantic;reference-mapping;receipt"
        RUN_SERIAL TRUE
        TIMEOUT 120
        ENVIRONMENT
            "RUNNER_TEMP=${CMAKE_CURRENT_BINARY_DIR}/semantic-cognition;LAPLACE_POSTGRES_SEMANTIC_TEST_PORT=55443")

    include("${CMAKE_CURRENT_SOURCE_DIR}/tests/cognition_discourse.cmake")

    function(laplace_add_postgresql_mutant target definition)
        add_library(${target} MODULE ${laplace_postgresql_module_sources})
        laplace_configure_postgresql_module(${target})
        target_compile_definitions(${target} PRIVATE
            LAPLACE_POSTGRES_TESTING=1
            ${definition}=1)
    endfunction()

    laplace_add_postgresql_mutant(
        laplace_pg_blind_conflict_mutant LAPLACE_TEST_BLIND_CONFLICT)
    laplace_add_postgresql_mutant(
        laplace_pg_composition_presence_order_mutant
        LAPLACE_TEST_COMPOSITION_PRESENCE_REORDER)
    target_compile_definitions(
        laplace_pg_composition_presence_order_mutant PRIVATE
        LAPLACE_PG_COMPOSITION_ENTRYPOINT=laplace_pg_composition_deposit_presence_order_mutant
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL=laplace_pg_composition_execute_presence_order_mutant
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL=laplace_pg_composition_destroy_presence_order_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_composition_presence_blind_mutant
        LAPLACE_TEST_COMPOSITION_PRESENCE_BLIND)
    target_compile_definitions(
        laplace_pg_composition_presence_blind_mutant PRIVATE
        LAPLACE_PG_COMPOSITION_ENTRYPOINT=laplace_pg_composition_deposit_presence_blind_mutant
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL=laplace_pg_composition_execute_presence_blind_mutant
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL=laplace_pg_composition_destroy_presence_blind_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_composition_presence_per_row_mutant
        LAPLACE_TEST_COMPOSITION_PRESENCE_PER_ROW)
    target_compile_definitions(
        laplace_pg_composition_presence_per_row_mutant PRIVATE
        LAPLACE_PG_COMPOSITION_ENTRYPOINT=laplace_pg_composition_deposit_presence_per_row_mutant
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL=laplace_pg_composition_execute_presence_per_row_mutant
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL=laplace_pg_composition_destroy_presence_per_row_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_composition_presence_partial_mutant
        LAPLACE_TEST_COMPOSITION_PRESENCE_PARTIAL)
    target_compile_definitions(
        laplace_pg_composition_presence_partial_mutant PRIVATE
        LAPLACE_PG_COMPOSITION_ENTRYPOINT=laplace_pg_composition_deposit_presence_partial_mutant
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL=laplace_pg_composition_execute_presence_partial_mutant
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL=laplace_pg_composition_destroy_presence_partial_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_composition_presence_semantic_drift_mutant
        LAPLACE_TEST_COMPOSITION_PRESENCE_SEMANTIC_DRIFT)
    target_compile_definitions(
        laplace_pg_composition_presence_semantic_drift_mutant PRIVATE
        LAPLACE_PG_COMPOSITION_ENTRYPOINT=laplace_pg_composition_deposit_presence_semantic_drift_mutant
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL=laplace_pg_composition_execute_presence_semantic_drift_mutant
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL=laplace_pg_composition_destroy_presence_semantic_drift_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_composition_replay_receipt_mutant
        LAPLACE_TEST_COMPOSITION_REPLAY_RECEIPT_VERIFY_BYPASS)
    target_compile_definitions(
        laplace_pg_composition_replay_receipt_mutant PRIVATE
        LAPLACE_PG_COMPOSITION_ENTRYPOINT=laplace_pg_composition_deposit_replay_receipt_mutant
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL=laplace_pg_composition_execute_replay_receipt_mutant
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL=laplace_pg_composition_destroy_replay_receipt_mutant
        LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL=laplace_pg_composition_persist_receipt_replay_receipt_mutant
        LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL=laplace_pg_persistence_run_producer_replay_receipt_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_evidence_root_replay_mutant
        LAPLACE_TEST_EVIDENCE_ROOT_REPLAY_VERIFY_BYPASS)
    target_compile_definitions(
        laplace_pg_evidence_root_replay_mutant PRIVATE
        LAPLACE_PG_EVIDENCE_ENTRYPOINT=laplace_pg_evidence_record_lineage_root_replay_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_evidence_testimony_replay_mutant
        LAPLACE_TEST_EVIDENCE_TESTIMONY_REPLAY_VERIFY_BYPASS)
    target_compile_definitions(
        laplace_pg_evidence_testimony_replay_mutant PRIVATE
        LAPLACE_PG_EVIDENCE_TESTIMONY_ENTRYPOINT=laplace_pg_evidence_record_testimony_replay_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_standing_replay_mutant
        LAPLACE_TEST_STANDING_REPLAY_VERIFY_BYPASS)
    target_compile_definitions(
        laplace_pg_standing_replay_mutant PRIVATE
        LAPLACE_PG_STANDING_ENTRYPOINT=laplace_pg_evidence_calculate_standing_replay_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_standing_admission_mutant
        LAPLACE_TEST_STANDING_ADMISSION_BYPASS)
    target_compile_definitions(
        laplace_pg_standing_admission_mutant PRIVATE
        LAPLACE_PG_STANDING_ENTRYPOINT=laplace_pg_evidence_calculate_standing_admission_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_source_profile_replay_mutant
        LAPLACE_TEST_SOURCE_PROFILE_REPLAY_VERIFY_BYPASS)
    target_compile_definitions(
        laplace_pg_source_profile_replay_mutant PRIVATE
        LAPLACE_PG_SOURCE_PROFILE_ENTRYPOINT=laplace_pg_source_profile_validate_replay_mutant)
    laplace_add_postgresql_mutant(
        laplace_pg_perfcache_epoch_mutant
        LAPLACE_TEST_SKIP_PG_PERFCACHE_EXPECTED_EPOCH)
    laplace_add_postgresql_mutant(
        laplace_pg_plan_manifest_mutant
        LAPLACE_TEST_UNICODE_PG_PLAN_SUBSTITUTION)
    laplace_add_postgresql_mutant(
        laplace_pg_unicode_access_epoch_mutant
        LAPLACE_TEST_UNICODE_ACCESS_IGNORE_EXPECTED_EPOCH)
    laplace_add_postgresql_mutant(
        laplace_pg_machine_exception_drift_mutant
        LAPLACE_TEST_PG_MACHINE_EXCEPTION_CAPABILITY_DRIFT)
endif()

# Retain the original install script and the 1.0.1 cognition upgrade for historical
# package validation. The product cognition route is a 1.0.2 delta. The recovered
# standalone materialization route is appended to that same delta and is also
# replayed idempotently by laplace-public-readback.sql for historical 1.0.2 clusters.
set(persisted_cognition_upgrade_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.0--1.0.1.sql")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/observation_cognition_persisted.sql.in"
    "${persisted_cognition_upgrade_sql}" @ONLY)
set(extension_101_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.1.sql")
file(READ "${extension_sql}" previous_schema)
file(READ "${persisted_cognition_upgrade_sql}" persisted_cognition_delta)
file(WRITE "${extension_101_sql}" "${previous_schema}\n${persisted_cognition_delta}")

set(product_cognition_upgrade_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.1--1.0.2.sql")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/cognition_product_route.sql.in"
    "${product_cognition_upgrade_sql}" @ONLY)
file(APPEND "${product_cognition_upgrade_sql}"
    "\n${cognition_materialization_route}\n")
set(current_extension_sql
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace--1.0.2.sql")
file(READ "${product_cognition_upgrade_sql}" product_cognition_delta)
file(WRITE "${current_extension_sql}"
    "${previous_schema}\n${persisted_cognition_delta}\n${product_cognition_delta}")

install(TARGETS laplace_pg
    LIBRARY DESTINATION "pgsql-${LAPLACE_POSTGRES_MAJOR}/lib")
install(FILES "${extension_control}" "${extension_sql}"
    "${extension_101_sql}" "${current_extension_sql}"
    "${persisted_cognition_upgrade_sql}" "${product_cognition_upgrade_sql}"
    "${CMAKE_CURRENT_BINARY_DIR}/share/extension/laplace-public-readback.sql"
    DESTINATION "pgsql-${LAPLACE_POSTGRES_MAJOR}/share/extension")

set(LAPLACE_POSTGRES_EXTENSION_TARGET laplace_pg PARENT_SCOPE)
set(LAPLACE_POSTGRES_EXTENSION_SQL "${current_extension_sql}" PARENT_SCOPE)
set(LAPLACE_POSTGRES_EXTENSION_CONTROL "${extension_control}" PARENT_SCOPE)
