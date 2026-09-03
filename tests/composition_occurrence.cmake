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

add_library(laplace_composition_empty_effect_mutant STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/composition.cpp")
target_include_directories(laplace_composition_empty_effect_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_composition_empty_effect_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_composition_empty_effect_mutant PRIVATE
    LAPLACE_TEST_COMPOSITION_REJECT_EMPTY_EFFECT=1)
target_compile_options(laplace_composition_empty_effect_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_executable(laplace_composition_empty_effect_mutation_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_tests.cpp")
target_include_directories(laplace_composition_empty_effect_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(laplace_composition_empty_effect_mutation_probe PRIVATE
    laplace_composition_empty_effect_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(laplace_composition_empty_effect_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME composition.mutation-empty-effect-rejection-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_composition_empty_effect_mutation_probe>"
        "-DFILTER=CompositionWorkingSet.ExactStateWithoutOccurrenceIsAReceiptedNoOp"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_gtest_failure.cmake")
set_tests_properties(
    composition.mutation-empty-effect-rejection-detected PROPERTIES
    LABELS "implementation;composition;presence;persistence;receipt;mutation")

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

add_library(laplace_composition_duplicate_calculation_mutant STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/composition.cpp")
target_include_directories(laplace_composition_duplicate_calculation_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_composition_duplicate_calculation_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_composition_duplicate_calculation_mutant PRIVATE
    LAPLACE_TEST_COMPOSITION_DUPLICATE_CALCULATION=1)
target_compile_options(laplace_composition_duplicate_calculation_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_executable(laplace_composition_duplicate_calculation_mutation_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_resource_accounting_tests.cpp")
target_include_directories(laplace_composition_duplicate_calculation_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(laplace_composition_duplicate_calculation_mutation_probe PRIVATE
    laplace_composition_duplicate_calculation_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(laplace_composition_duplicate_calculation_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME composition.mutation-duplicate-semantic-calculation-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_composition_duplicate_calculation_mutation_probe>"
        "-DFILTER=CompositionResourceAccounting.CanonicalReuseBoundsPhysicalWorkAndSeparatesOccurrences"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_gtest_failure.cmake")
set_tests_properties(
    composition.mutation-duplicate-semantic-calculation-detected PROPERTIES
    LABELS "implementation;composition;execution;resource;billing;mutation")

# #177: a source/file is not the scheduling atom. The clean composition engine
# carries exact prior-result dependencies, so derive dependency frontiers
# independently of read/batch/worker/provider choices.
add_executable(laplace_composition_frontier_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_frontier_tests.cpp")
target_link_libraries(laplace_composition_frontier_tests PRIVATE
    Laplace::Composition
    GTest::gtest_main)
target_compile_options(laplace_composition_frontier_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME composition.dependency-frontier-plan
    COMMAND "$<TARGET_FILE:laplace_composition_frontier_tests>")
set_tests_properties(
    composition.dependency-frontier-plan PROPERTIES
    LABELS "implementation;composition;execution;working-set;resource;determinism")

add_executable(laplace_composition_frontier_mutation_probe
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_frontier_tests.cpp")
target_link_libraries(laplace_composition_frontier_mutation_probe PRIVATE
    Laplace::Composition
    GTest::gtest_main)
target_compile_definitions(laplace_composition_frontier_mutation_probe PRIVATE
    LAPLACE_TEST_COMPOSITION_FRONTIER_FLATTEN_DEPENDENCIES=1)
target_compile_options(laplace_composition_frontier_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME composition.mutation-flattened-frontier-dependencies-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_composition_frontier_mutation_probe>"
        "-DFILTER=CompositionFrontierPlan.DeepChainHasOneRequestPerDependencyFrontier"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/expect_gtest_failure.cmake")
set_tests_properties(
    composition.mutation-flattened-frontier-dependencies-detected PROPERTIES
    LABELS "implementation;composition;execution;working-set;determinism;mutation")

include("${CMAKE_CURRENT_SOURCE_DIR}/tests/source_structural_witness.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/tests/machine_exception.cmake")
