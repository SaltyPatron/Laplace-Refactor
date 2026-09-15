# Exercise the actual PostgreSQL entity adapter callbacks with the native
# framework. Unreferenced PostgreSQL entrypoints are collected by the linker;
# this test does not substitute a server or claim a durable deposit.
add_executable(laplace_persistence_entities_native_probe
    "${CMAKE_SOURCE_DIR}/tests/postgres/persistence_entities_native_probe.c"
    "${CMAKE_SOURCE_DIR}/integrations/postgresql/extension/src/persistence_pg.c")
target_include_directories(laplace_persistence_entities_native_probe SYSTEM PRIVATE
    "${LAPLACE_POSTGRES_INCLUDE_SERVER}"
    "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
target_include_directories(laplace_persistence_entities_native_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_persistence_entities_native_probe PRIVATE
    Laplace::Persistence BLAKE3::blake3)
target_compile_options(laplace_persistence_entities_native_probe PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wshadow;-Wstrict-prototypes;-Wno-declaration-after-statement;-Wno-gnu-statement-expression-from-macro-expansion;-ffunction-sections;-fdata-sections;-ffp-contract=off>)
if(APPLE)
    target_link_options(laplace_persistence_entities_native_probe PRIVATE
        "LINKER:-dead_strip")
else()
    target_link_options(laplace_persistence_entities_native_probe PRIVATE
        "LINKER:--gc-sections")
endif()
set_target_properties(laplace_persistence_entities_native_probe PROPERTIES
    C_EXTENSIONS ON)
add_test(NAME persistence.entity-producer-native-contract
    COMMAND laplace_persistence_entities_native_probe)
set_tests_properties(persistence.entity-producer-native-contract PROPERTIES
    LABELS "implementation;persistence;postgresql;producer;receipt;identity")
