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

set(laplace_postgresql_module_sources
    src/cognition_isa_pg.c
    src/composition_pg.c
    src/evidence_lineage_pg.c
    src/evidence_testimony_pg.c
    src/highway_registry_pg.c
    src/laplace_pg.c
    src/persistence_pg.c
    src/persistence_rows_pg.c
    src/perfcache_pg.c
    src/reference_topology_pg.c
    src/reference_mapping_pg.c
    src/set_pg.c
    src/source_admission_pg.c
    src/source_profile_pg.c
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
        BLAKE3::blake3 Laplace::Composition Laplace::Isa Laplace::Persistence
        Laplace::Perfcache Laplace::UnicodeNumeric)
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
        LAPLACE_TEST_EVIDENCE_TESTIMONY_REPLAY_VERIFY_BYPASS=1
        LAPLACE_PG_EVIDENCE_TESTIMONY_ENTRYPOINT=laplace_pg_evidence_record_testimony_replay_mutant)
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
endif()

install(TARGETS laplace_pg
    LIBRARY DESTINATION "pgsql-${LAPLACE_POSTGRES_MAJOR}/lib")
install(FILES "${extension_control}" "${extension_sql}"
    DESTINATION "pgsql-${LAPLACE_POSTGRES_MAJOR}/share/extension")

set(LAPLACE_POSTGRES_EXTENSION_TARGET laplace_pg PARENT_SCOPE)
set(LAPLACE_POSTGRES_EXTENSION_SQL "${extension_sql}" PARENT_SCOPE)
set(LAPLACE_POSTGRES_EXTENSION_CONTROL "${extension_control}" PARENT_SCOPE)
