include(GoogleTest)

# Two mutation targets in tests/CMakeLists intentionally recompile the canonical
# isa.c directly instead of linking Laplace::Engine. Source properties from the
# engine directory are directory-scoped, so bind the same generated AST handler
# to that exact source in the tests directory. The mutation-specific handler
# branch fails AST closed and therefore does not import a second AST engine.
set_property(SOURCE "${PROJECT_SOURCE_DIR}/engine/src/isa.c"
    DIRECTORY "${PROJECT_SOURCE_DIR}/tests"
    APPEND PROPERTY COMPILE_OPTIONS
    "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,IntelLLVM>:-include;${PROJECT_SOURCE_DIR}/engine/src/isa_universal_ast_handler.h>")

add_executable(laplace_decomposition_orchestration_tests
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_orchestration_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_leaf_redispatch_tests.cpp")
target_link_libraries(laplace_decomposition_orchestration_tests PRIVATE
    Laplace::Decomposition
    GTest::gtest_main)
target_compile_options(laplace_decomposition_orchestration_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_decomposition_orchestration_tests
    PROPERTIES LABELS "implementation;decomposition;recipe;recursive;grammar")

add_executable(laplace_decomposition_structured_provider_tests
    "${CMAKE_CURRENT_LIST_DIR}/decomposition_structured_provider_tests.cpp")
target_link_libraries(laplace_decomposition_structured_provider_tests PRIVATE
    Laplace::Decomposition
    GTest::gtest_main)
target_compile_options(laplace_decomposition_structured_provider_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_decomposition_structured_provider_tests
    PROPERTIES LABELS "implementation;decomposition;recipe;grammar;ast;structure;witness")

add_executable(laplace_universal_ast_recipe_tests
    "${CMAKE_CURRENT_LIST_DIR}/universal_ast_recipe_tests.cpp")
target_link_libraries(laplace_universal_ast_recipe_tests PRIVATE
    Laplace::UniversalAst
    GTest::gtest_main)
target_compile_options(laplace_universal_ast_recipe_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_universal_ast_recipe_tests
    PROPERTIES LABELS "implementation;decomposition;grammar;recipe;ast;identity;highway")

add_executable(laplace_universal_ast_isa_tests
    "${CMAKE_CURRENT_LIST_DIR}/universal_ast_isa_tests.cpp")
target_include_directories(laplace_universal_ast_isa_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(laplace_universal_ast_isa_tests PRIVATE
    Laplace::UniversalAst
    Laplace::Isa
    GTest::gtest_main)
target_compile_options(laplace_universal_ast_isa_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_universal_ast_isa_tests
    PROPERTIES LABELS "implementation;decomposition;grammar;recipe;ast;isa;replay;receipt")

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

add_executable(laplace_tabular_recursive_merge_tests
    "${CMAKE_CURRENT_LIST_DIR}/tabular_recursive_merge_tests.cpp")
target_include_directories(laplace_tabular_recursive_merge_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/include"
    "${CMAKE_CURRENT_LIST_DIR}/../engine/src"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_tabular_recursive_merge_tests PRIVATE
    GTest::gtest_main)
target_compile_options(laplace_tabular_recursive_merge_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_tabular_recursive_merge_tests
    PROPERTIES LABELS "implementation;source-profile;decomposition;identity;ast;recursive")

add_executable(laplace_tabular_recursive_merge_mutation_probe
    "${CMAKE_CURRENT_LIST_DIR}/tabular_recursive_merge_tests.cpp")
target_include_directories(laplace_tabular_recursive_merge_mutation_probe PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/include"
    "${CMAKE_CURRENT_LIST_DIR}/../engine/src"
    "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(laplace_tabular_recursive_merge_mutation_probe PRIVATE
    LAPLACE_TEST_TABULAR_RECURSIVE_DROP_CANONICAL_PLAN=1)
target_link_libraries(laplace_tabular_recursive_merge_mutation_probe PRIVATE
    GTest::gtest_main)
target_compile_options(laplace_tabular_recursive_merge_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME decomposition.mutation-recursive-canonical-merge-drop-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_tabular_recursive_merge_mutation_probe>"
        "-DFILTER=TabularRecursiveMerge.InsertsCanonicalPlanBeforeFinalSourceRootWithGlobalReferences"
        -P "${CMAKE_CURRENT_LIST_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    decomposition.mutation-recursive-canonical-merge-drop-detected PROPERTIES
    LABELS "implementation;source-profile;decomposition;identity;ast;recursive;mutation")

add_executable(laplace_tabular_recursive_witness_mutation_probe
    "${CMAKE_CURRENT_LIST_DIR}/tabular_recursive_merge_tests.cpp")
target_include_directories(laplace_tabular_recursive_witness_mutation_probe PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/include"
    "${CMAKE_CURRENT_LIST_DIR}/../engine/src"
    "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(laplace_tabular_recursive_witness_mutation_probe PRIVATE
    LAPLACE_TEST_TABULAR_RECURSIVE_DROP_WITNESS_BINDINGS=1)
target_link_libraries(laplace_tabular_recursive_witness_mutation_probe PRIVATE
    GTest::gtest_main)
target_compile_options(laplace_tabular_recursive_witness_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME decomposition.mutation-recursive-witness-binding-drop-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_tabular_recursive_witness_mutation_probe>"
        "-DFILTER=TabularRecursiveMerge.RetainsWitnessMetadataBoundToCanonicalContent"
        -P "${CMAKE_CURRENT_LIST_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    decomposition.mutation-recursive-witness-binding-drop-detected PROPERTIES
    LABELS "implementation;source-profile;decomposition;identity;witness;ast;recursive;mutation")

add_executable(laplace_tabular_fixed_width_media_tests
    "${CMAKE_CURRENT_LIST_DIR}/tabular_fixed_width_media_tests.cpp")
target_include_directories(laplace_tabular_fixed_width_media_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/src")
target_link_libraries(laplace_tabular_fixed_width_media_tests PRIVATE
    Laplace::TabularSource
    Laplace::UnicodeRoot
    GTest::gtest_main)
target_compile_options(laplace_tabular_fixed_width_media_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_tabular_fixed_width_media_tests
    PROPERTIES
        LABELS "implementation;source-profile;decomposition;fixed-width;media-type;recursive"
        ENVIRONMENT "LAPLACE_UNICODE_SOURCE_ROOT=${LAPLACE_UNICODE_SOURCE_ROOT}")
