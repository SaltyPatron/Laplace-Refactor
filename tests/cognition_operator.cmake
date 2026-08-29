include(GoogleTest)

add_executable(laplace_cognition_operator_tests
    "${PROJECT_SOURCE_DIR}/tests/cognition_operator_tests.cpp")
target_link_libraries(laplace_cognition_operator_tests PRIVATE
    Laplace::CognitionOperator GTest::gtest_main)
target_compile_options(laplace_cognition_operator_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_cognition_operator_tests PROPERTIES
    LABELS "implementation;cognition;operator;incidence;relation-plane;evidence;matrix-free;receipt")

function(laplace_add_cognition_operator_mutation suffix definition test_name filter)
    set(library "laplace_cognition_operator_${suffix}_mutant")
    set(probe "laplace_cognition_operator_${suffix}_mutation_probe")
    add_library(${library} STATIC
        "${PROJECT_SOURCE_DIR}/engine/src/cognition_operator.cpp")
    target_include_directories(${library} PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${library} PRIVATE BLAKE3::blake3)
    target_compile_definitions(${library} PRIVATE "${definition}=1")
    target_compile_options(${library} PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
    add_executable(${probe}
        "${PROJECT_SOURCE_DIR}/tests/cognition_operator_tests.cpp")
    target_include_directories(${probe} PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${probe} PRIVATE ${library} GTest::gtest_main)
    target_compile_options(${probe} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME ${test_name}
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:${probe}>"
            "-DFILTER=${filter}"
            -P "${PROJECT_SOURCE_DIR}/tests/expect_gtest_failure.cmake")
    set_tests_properties(${test_name} PROPERTIES
        LABELS "implementation;cognition;operator;mutation")
endfunction()

laplace_add_cognition_operator_mutation(
    relation_plane_flatten LAPLACE_TEST_COGNITION_OPERATOR_FLATTEN_RELATION_PLANE
    cognition-operator.mutation-relation-plane-flatten-detected
    CognitionOperator.RelationPlaneTypeErasureChangesIdentity)
laplace_add_cognition_operator_mutation(
    dependent_amplification LAPLACE_TEST_COGNITION_OPERATOR_AMPLIFY_DEPENDENT_EVIDENCE
    cognition-operator.mutation-dependent-evidence-amplification-detected
    CognitionOperator.CollapsesDependentEvidenceRootCopies)
