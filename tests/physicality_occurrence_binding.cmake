add_executable(laplace_physicality_occurrence_binding_tests physicality_occurrence_binding_tests.cpp)
target_link_libraries(laplace_physicality_occurrence_binding_tests PRIVATE
    Laplace::Composition GTest::gtest_main)
target_compile_options(laplace_physicality_occurrence_binding_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_physicality_occurrence_binding_tests PROPERTIES
    LABELS "implementation;physicality;occurrence;provenance;limits")

add_library(laplace_physicality_occurrence_selection_mutant STATIC
    ../engine/src/physicality_occurrence_binding.cpp)
target_include_directories(laplace_physicality_occurrence_selection_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include" "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_physicality_occurrence_selection_mutant PRIVATE
    Laplace::Composition BLAKE3::blake3)
target_compile_definitions(laplace_physicality_occurrence_selection_mutant PRIVATE
    LAPLACE_TEST_OCCURRENCE_IGNORE_SELECTION=1)
target_compile_options(laplace_physicality_occurrence_selection_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_physicality_occurrence_selection_mutation_probe physicality_occurrence_binding_tests.cpp)
target_link_libraries(laplace_physicality_occurrence_selection_mutation_probe PRIVATE
    laplace_physicality_occurrence_selection_mutant Laplace::Composition GTest::gtest_main)
target_compile_options(laplace_physicality_occurrence_selection_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME physicality-occurrence.mutation-selection-loss-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_physicality_occurrence_selection_mutation_probe>"
        "-DFILTER=PhysicalityOccurrenceBinding.OneStoredRunRetainsTwoActualSelectedPhysicalities"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(physicality-occurrence.mutation-selection-loss-detected PROPERTIES
    LABELS "implementation;physicality;occurrence;mutation")
