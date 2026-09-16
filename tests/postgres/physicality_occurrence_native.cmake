# Exercise the production adapter's interval helpers with actual native plans.
# Unreferenced SQL/SPI entrypoints are discarded; allocator/error shims do not
# establish PostgreSQL execution or durable admission.
add_executable(laplace_physicality_occurrence_native_probe
    "${CMAKE_SOURCE_DIR}/tests/postgres/physicality_occurrence_native_probe.c")
target_include_directories(laplace_physicality_occurrence_native_probe SYSTEM PRIVATE
    "${LAPLACE_POSTGRES_INCLUDE_SERVER}" "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
target_include_directories(laplace_physicality_occurrence_native_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_physicality_occurrence_native_probe PRIVATE
    Laplace::Composition BLAKE3::blake3)
target_compile_options(laplace_physicality_occurrence_native_probe PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wshadow;-Wstrict-prototypes;-Wno-declaration-after-statement;-ffunction-sections;-fdata-sections;-ffp-contract=off>)
if(APPLE)
    target_link_options(laplace_physicality_occurrence_native_probe PRIVATE "LINKER:-dead_strip")
else()
    target_link_options(laplace_physicality_occurrence_native_probe PRIVATE "LINKER:--gc-sections")
endif()
set_target_properties(laplace_physicality_occurrence_native_probe PROPERTIES C_EXTENSIONS ON)
add_test(NAME physicality-occurrence.pg-helper-native-contract
    COMMAND laplace_physicality_occurrence_native_probe)
set_tests_properties(physicality-occurrence.pg-helper-native-contract PROPERTIES
    LABELS "implementation;physicality;postgresql;occurrence;identity;materialization")

# Execute actual PG exception macros and the production Unicode metadata owner
# with a controlled SPI context/connection boundary, including error returns.
add_executable(laplace_spi_context_native_probe
    "${CMAKE_SOURCE_DIR}/tests/postgres/spi_context_native_probe.c")
target_include_directories(laplace_spi_context_native_probe SYSTEM PRIVATE
    "${LAPLACE_POSTGRES_INCLUDE_SERVER}" "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
target_include_directories(laplace_spi_context_native_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated" "${CMAKE_SOURCE_DIR}/engine/include")
target_compile_options(laplace_spi_context_native_probe PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wshadow;-Wstrict-prototypes;-Wno-declaration-after-statement;-ffunction-sections;-fdata-sections>)
if(APPLE)
    target_link_options(laplace_spi_context_native_probe PRIVATE "LINKER:-dead_strip")
else()
    target_link_options(laplace_spi_context_native_probe PRIVATE "LINKER:--gc-sections")
endif()
set_target_properties(laplace_spi_context_native_probe PROPERTIES C_EXTENSIONS ON)
add_test(NAME postgres.spi-context-native-contract COMMAND laplace_spi_context_native_probe)
set_tests_properties(postgres.spi-context-native-contract PROPERTIES
    LABELS "implementation;postgresql;materialization;lifecycle")

# Exercise the production perfcache nested SPI and registered-pin error owners.
add_executable(laplace_perfcache_spi_lifetime_native_probe
    "${CMAKE_SOURCE_DIR}/tests/postgres/perfcache_spi_lifetime_native_probe.c")
target_include_directories(laplace_perfcache_spi_lifetime_native_probe SYSTEM PRIVATE
    "${LAPLACE_POSTGRES_INCLUDE_SERVER}" "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
target_include_directories(laplace_perfcache_spi_lifetime_native_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated" "${CMAKE_SOURCE_DIR}/engine/include")
target_compile_options(laplace_perfcache_spi_lifetime_native_probe PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang,IntelLLVM>:-Wall;-Wextra;-Wpedantic;-Werror;-Wshadow;-Wstrict-prototypes;-Wno-declaration-after-statement;-ffunction-sections;-fdata-sections>)
if(APPLE)
    target_link_options(laplace_perfcache_spi_lifetime_native_probe PRIVATE "LINKER:-dead_strip")
else()
    target_link_options(laplace_perfcache_spi_lifetime_native_probe PRIVATE "LINKER:--gc-sections")
endif()
set_target_properties(laplace_perfcache_spi_lifetime_native_probe PROPERTIES C_EXTENSIONS ON)
add_test(NAME postgres.perfcache-spi-lifetime-native-contract
    COMMAND laplace_perfcache_spi_lifetime_native_probe)
set_tests_properties(postgres.perfcache-spi-lifetime-native-contract PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    LABELS "implementation;postgresql;perfcache;materialization;lifecycle")
