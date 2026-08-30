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
