include(GoogleTest)

add_executable(laplace_decomposition_orchestration_tests
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_orchestration_tests.cpp")
target_link_libraries(laplace_decomposition_orchestration_tests PRIVATE
    Laplace::Decomposition
    GTest::gtest_main)
target_compile_options(laplace_decomposition_orchestration_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_decomposition_orchestration_tests
    PROPERTIES LABELS "implementation;decomposition;recipe;recursive;grammar")

add_library(laplace_decomposition_witness_identity_mutant STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../engine/src/decomposition_composition.cpp")
target_include_directories(laplace_decomposition_witness_identity_mutant PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_decomposition_witness_identity_mutant PRIVATE
    Laplace::Engine
    BLAKE3::blake3)
target_compile_definitions(laplace_decomposition_witness_identity_mutant PRIVATE
    LAPLACE_TEST_DECOMPOSITION_COMPOSITION_COUPLE_WITNESS=1)
target_compile_options(laplace_decomposition_witness_identity_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_executable(laplace_decomposition_witness_identity_mutation_probe
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_orchestration_tests.cpp")
target_link_libraries(laplace_decomposition_witness_identity_mutation_probe PRIVATE
    laplace_decomposition_witness_identity_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(laplace_decomposition_witness_identity_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME decomposition.mutation-witness-metadata-identity-coupling-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_decomposition_witness_identity_mutation_probe>"
        "-DFILTER=DecompositionComposition.CanonicalRootExcludesTraceWitnessMetadata"
        -P "${CMAKE_CURRENT_LIST_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    decomposition.mutation-witness-metadata-identity-coupling-detected PROPERTIES
    LABELS "implementation;decomposition;identity;witness;mutation")

add_executable(laplace_decomposition_witness_binding_tests
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_witness_binding_tests.cpp")
target_link_libraries(laplace_decomposition_witness_binding_tests PRIVATE
    Laplace::Decomposition
    GTest::gtest_main)
target_compile_options(laplace_decomposition_witness_binding_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_decomposition_witness_binding_tests
    PROPERTIES LABELS "implementation;decomposition;identity;witness;ast;reuse")

add_library(laplace_decomposition_span_remint_mutant STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../engine/src/decomposition_composition.cpp")
target_include_directories(laplace_decomposition_span_remint_mutant PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_decomposition_span_remint_mutant PRIVATE
    Laplace::Engine
    BLAKE3::blake3)
target_compile_definitions(laplace_decomposition_span_remint_mutant PRIVATE
    LAPLACE_TEST_DECOMPOSITION_COMPOSITION_REMINT_SPAN=1)
target_compile_options(laplace_decomposition_span_remint_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_executable(laplace_decomposition_span_remint_mutation_probe
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_witness_binding_tests.cpp")
target_link_libraries(laplace_decomposition_span_remint_mutation_probe PRIVATE
    laplace_decomposition_span_remint_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(laplace_decomposition_span_remint_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME decomposition.mutation-equal-span-remint-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_decomposition_span_remint_mutation_probe>"
        "-DFILTER=DecompositionWitnessBinding.EqualSpanContentReusesCanonicalReferenceAcrossOffsetsAndWitnessMetadata"
        -P "${CMAKE_CURRENT_LIST_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    decomposition.mutation-equal-span-remint-detected PROPERTIES
    LABELS "implementation;decomposition;identity;witness;ast;reuse;mutation")

add_executable(laplace_decomposition_atom_reuse_tests
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_atom_reuse_tests.cpp")
target_link_libraries(laplace_decomposition_atom_reuse_tests PRIVATE
    Laplace::Decomposition
    GTest::gtest_main)
target_compile_options(laplace_decomposition_atom_reuse_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_decomposition_atom_reuse_tests
    PROPERTIES LABELS "implementation;decomposition;identity;tier0;reuse")

add_library(laplace_decomposition_atom_rematerialization_mutant STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../engine/src/decomposition_composition.cpp")
target_include_directories(laplace_decomposition_atom_rematerialization_mutant PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_decomposition_atom_rematerialization_mutant PRIVATE
    Laplace::Engine
    BLAKE3::blake3)
target_compile_definitions(laplace_decomposition_atom_rematerialization_mutant PRIVATE
    LAPLACE_TEST_DECOMPOSITION_COMPOSITION_REMATERIALIZE_ATOM=1)
target_compile_options(laplace_decomposition_atom_rematerialization_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_executable(laplace_decomposition_atom_rematerialization_mutation_probe
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_atom_reuse_tests.cpp")
target_link_libraries(laplace_decomposition_atom_rematerialization_mutation_probe PRIVATE
    laplace_decomposition_atom_rematerialization_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(laplace_decomposition_atom_rematerialization_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME decomposition.mutation-tier0-rematerialization-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_decomposition_atom_rematerialization_mutation_probe>"
        "-DFILTER=DecompositionComposition.SingleCodepointRootReusesTierZeroReference"
        -P "${CMAKE_CURRENT_LIST_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    decomposition.mutation-tier0-rematerialization-detected PROPERTIES
    LABELS "implementation;decomposition;identity;tier0;reuse;mutation")