add_library(laplace_composition_implicit_occurrence_mutant STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/composition.cpp")
target_include_directories(laplace_composition_implicit_occurrence_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_composition_implicit_occurrence_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_composition_implicit_occurrence_mutant PRIVATE
    LAPLACE_TEST_COMPOSITION_IMPLICIT_OCCURRENCE=1)
target_compile_options(laplace_composition_implicit_occurrence_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_executable(laplace_composition_implicit_occurrence_mutation_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_tests.cpp")
target_include_directories(laplace_composition_implicit_occurrence_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(laplace_composition_implicit_occurrence_mutation_probe PRIVATE
    laplace_composition_implicit_occurrence_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(laplace_composition_implicit_occurrence_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME composition.mutation-implicit-occurrence-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_composition_implicit_occurrence_mutation_probe>"
        "-DFILTER=CompositionWorkingSet.CanonicalCompositionRequiresExplicitOccurrenceEmission"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_gtest_failure.cmake")
set_tests_properties(
    composition.mutation-implicit-occurrence-detected PROPERTIES
    LABELS "implementation;composition;identity;occurrence;evidence;mutation")

add_executable(laplace_composition_resource_accounting_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_resource_accounting_tests.cpp")
target_include_directories(laplace_composition_resource_accounting_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(laplace_composition_resource_accounting_tests PRIVATE
    Laplace::Composition
    GTest::gtest_main)
target_compile_options(laplace_composition_resource_accounting_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME composition.resource-accounting-canonical-structure
    COMMAND "$<TARGET_FILE:laplace_composition_resource_accounting_tests>"
        "--gtest_filter=CompositionResourceAccounting.CanonicalReuseBoundsPhysicalWorkAndSeparatesOccurrences")
set_tests_properties(
    composition.resource-accounting-canonical-structure PROPERTIES
    LABELS "implementation;composition;identity;physicality;occurrence;resource;evidence")

add_library(laplace_composition_request_count_memory_mutant STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/composition.cpp")
target_include_directories(laplace_composition_request_count_memory_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_composition_request_count_memory_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_composition_request_count_memory_mutant PRIVATE
    LAPLACE_TEST_COMPOSITION_REQUEST_COUNT_MEMORY=1)
target_compile_options(laplace_composition_request_count_memory_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_executable(laplace_composition_request_count_memory_mutation_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_resource_accounting_tests.cpp")
target_include_directories(laplace_composition_request_count_memory_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(laplace_composition_request_count_memory_mutation_probe PRIVATE
    laplace_composition_request_count_memory_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(laplace_composition_request_count_memory_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME composition.mutation-request-count-memory-law-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_composition_request_count_memory_mutation_probe>"
        "-DFILTER=CompositionResourceAccounting.CanonicalReuseBoundsPhysicalWorkAndSeparatesOccurrences"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_gtest_failure.cmake")
set_tests_properties(
    composition.mutation-request-count-memory-law-detected PROPERTIES
    LABELS "implementation;composition;identity;physicality;occurrence;resource;evidence;mutation")

include("${CMAKE_CURRENT_SOURCE_DIR}/tests/source_structural_witness.cmake")
