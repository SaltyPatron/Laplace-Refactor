add_executable(laplace_content_reference_view_tests content_reference_view_tests.cpp)
target_link_libraries(laplace_content_reference_view_tests PRIVATE Laplace::Composition GTest::gtest_main)
target_compile_options(laplace_content_reference_view_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_content_reference_view_tests PROPERTIES
    LABELS "implementation;physicality;identity;materialization;limits")

function(laplace_add_content_reference_mutant name definition filter)
    set(target "laplace_content_reference_${name}_mutant")
    add_library(${target} STATIC ../engine/src/content_reference_view.cpp)
    target_include_directories(${target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include" "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${target} PRIVATE Laplace::Composition BLAKE3::blake3)
    target_compile_definitions(${target} PRIVATE "${definition}=1")
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_executable(${target}_probe content_reference_view_tests.cpp)
    target_link_libraries(${target}_probe PRIVATE ${target} Laplace::Composition GTest::gtest_main)
    target_compile_options(${target}_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(NAME "content-reference.mutation-${name}-detected"
        COMMAND "${CMAKE_COMMAND}" "-DPROBE=$<TARGET_FILE:${target}_probe>"
            "-DFILTER=ContentReferenceView.${filter}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
    set_tests_properties("content-reference.mutation-${name}-detected" PROPERTIES
        LABELS "implementation;physicality;identity;mutation")
endfunction()
laplace_add_content_reference_mutant(full-witness-loss
    LAPLACE_TEST_CONTENT_REFERENCE_IGNORE_RESULT_WITNESS CalculatedResultTailWitnessCorruptionCannotPass)
laplace_add_content_reference_mutant(form-authentication-loss
    LAPLACE_TEST_CONTENT_REFERENCE_SKIP_FORM_AUTHENTICATION EveryFormAndFullWitnessAreValidated)
laplace_add_content_reference_mutant(singleton-content-cycle
    LAPLACE_TEST_CONTENT_REFERENCE_SINGLETON_AS_TOPOLOGY SingletonFormRequiresIndependentClosureWithoutCreatingCycle)
