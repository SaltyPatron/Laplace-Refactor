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
    "${CMAKE_SOURCE_DIR}/cmake/postgresql_bindings.h.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/laplace--version.sql.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/laplace.control.in"
    "${binding_header}"
    "${extension_sql}"
    "${extension_control}")

add_library(laplace_pg MODULE
    src/laplace_pg.c
    src/persistence_pg.c
    src/perfcache_pg.c)
target_include_directories(laplace_pg SYSTEM PRIVATE
    "${LAPLACE_POSTGRES_INCLUDE_SERVER}"
    "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
target_include_directories(laplace_pg PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_pg PRIVATE
    Laplace::Isa Laplace::Persistence Laplace::Perfcache)
target_compile_options(laplace_pg PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wshadow;-Wstrict-prototypes;-Wno-declaration-after-statement;-ffp-contract=off>)
set_target_properties(laplace_pg PROPERTIES
    C_EXTENSIONS ON
    PREFIX ""
    OUTPUT_NAME "laplace_pg"
    BUILD_RPATH "$<TARGET_FILE_DIR:laplace_engine>"
    INSTALL_RPATH "$ORIGIN/../..")

if(BUILD_TESTING)
    target_compile_definitions(laplace_pg PRIVATE
        LAPLACE_POSTGRES_TESTING=1)

    add_library(laplace_pg_blind_conflict_mutant MODULE
        src/laplace_pg.c
        src/persistence_pg.c
        src/perfcache_pg.c)
    target_include_directories(laplace_pg_blind_conflict_mutant SYSTEM PRIVATE
        "${LAPLACE_POSTGRES_INCLUDE_SERVER}"
        "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
    target_include_directories(laplace_pg_blind_conflict_mutant PRIVATE
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_pg_blind_conflict_mutant PRIVATE
        Laplace::Isa Laplace::Persistence Laplace::Perfcache)
    target_compile_definitions(laplace_pg_blind_conflict_mutant PRIVATE
        LAPLACE_POSTGRES_TESTING=1
        LAPLACE_TEST_BLIND_CONFLICT=1)
    target_compile_options(laplace_pg_blind_conflict_mutant PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wshadow;-Wstrict-prototypes;-Wno-declaration-after-statement;-ffp-contract=off>)
    set_target_properties(laplace_pg_blind_conflict_mutant PROPERTIES
        C_EXTENSIONS ON
        PREFIX ""
        OUTPUT_NAME "laplace_pg_blind_conflict_mutant"
        BUILD_RPATH "$<TARGET_FILE_DIR:laplace_engine>")

    add_library(laplace_pg_perfcache_epoch_mutant MODULE
        src/laplace_pg.c
        src/persistence_pg.c
        src/perfcache_pg.c)
    target_include_directories(laplace_pg_perfcache_epoch_mutant SYSTEM PRIVATE
        "${LAPLACE_POSTGRES_INCLUDE_SERVER}"
        "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
    target_include_directories(laplace_pg_perfcache_epoch_mutant PRIVATE
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_pg_perfcache_epoch_mutant PRIVATE
        Laplace::Isa Laplace::Persistence Laplace::Perfcache)
    target_compile_definitions(laplace_pg_perfcache_epoch_mutant PRIVATE
        LAPLACE_POSTGRES_TESTING=1
        LAPLACE_TEST_SKIP_PG_PERFCACHE_EXPECTED_EPOCH=1)
    target_compile_options(laplace_pg_perfcache_epoch_mutant PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wshadow;-Wstrict-prototypes;-Wno-declaration-after-statement;-ffp-contract=off>)
    set_target_properties(laplace_pg_perfcache_epoch_mutant PROPERTIES
        C_EXTENSIONS ON
        PREFIX ""
        OUTPUT_NAME "laplace_pg_perfcache_epoch_mutant"
        BUILD_RPATH "$<TARGET_FILE_DIR:laplace_engine>")
endif()

install(TARGETS laplace_pg
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/postgresql/${LAPLACE_POSTGRES_MAJOR}")
install(FILES "${extension_control}" "${extension_sql}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/postgresql/${LAPLACE_POSTGRES_MAJOR}/extension")

set(LAPLACE_POSTGRES_EXTENSION_TARGET laplace_pg PARENT_SCOPE)
set(LAPLACE_POSTGRES_EXTENSION_SQL "${extension_sql}" PARENT_SCOPE)
set(LAPLACE_POSTGRES_EXTENSION_CONTROL "${extension_control}" PARENT_SCOPE)
