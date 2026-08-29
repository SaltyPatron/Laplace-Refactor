include(GoogleTest)

add_executable(laplace_cognition_solver_tests
    "${PROJECT_SOURCE_DIR}/tests/cognition_solver_tests.cpp")
target_link_libraries(laplace_cognition_solver_tests PRIVATE
    Laplace::CognitionSolver GTest::gtest_main)
target_compile_options(laplace_cognition_solver_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_cognition_solver_tests PROPERTIES
    LABELS "implementation;cognition;solver;operator;matrix-free;residual;receipt")

function(laplace_add_cognition_solver_mutation suffix definition test_name filter)
    set(library "laplace_cognition_solver_${suffix}_mutant")
    set(probe "laplace_cognition_solver_${suffix}_mutation_probe")
    add_library(${library} STATIC
        "${PROJECT_SOURCE_DIR}/engine/src/cognition_solver.cpp")
    target_include_directories(${library} PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${library} PRIVATE Laplace::CognitionOperator BLAKE3::blake3)
    target_compile_definitions(${library} PRIVATE "${definition}=1")
    target_compile_options(${library} PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
    add_executable(${probe}
        "${PROJECT_SOURCE_DIR}/tests/cognition_solver_tests.cpp")
    target_include_directories(${probe} PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${probe} PRIVATE ${library} Laplace::CognitionOperator GTest::gtest_main)
    target_compile_options(${probe} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME ${test_name}
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:${probe}>"
            "-DFILTER=${filter}"
            -P "${PROJECT_SOURCE_DIR}/tests/expect_gtest_failure.cmake")
    set_tests_properties(${test_name} PROPERTIES
        LABELS "implementation;cognition;solver;mutation")
endfunction()

laplace_add_cognition_solver_mutation(
    residual_as_precision LAPLACE_TEST_COGNITION_SOLVER_RESIDUAL_AS_PRECISION
    cognition-solver.mutation-residual-as-precision-detected
    CognitionSolver.ConvergesMatrixFreeWithoutRewritingEvidencePrecision)
laplace_add_cognition_solver_mutation(
    ignore_regularization LAPLACE_TEST_COGNITION_SOLVER_IGNORE_REGULARIZATION
    cognition-solver.mutation-regularization-omission-detected
    CognitionSolver.NumericalRegularizationIsSolverStateNotEvidenceWeight)
