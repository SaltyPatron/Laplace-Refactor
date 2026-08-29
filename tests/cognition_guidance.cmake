include(GoogleTest)

add_executable(laplace_cognition_guidance_tests
    "${PROJECT_SOURCE_DIR}/tests/cognition_guidance_tests.cpp")
target_link_libraries(laplace_cognition_guidance_tests PRIVATE
    Laplace::CognitionGuidance GTest::gtest_main)
target_compile_options(laplace_cognition_guidance_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_cognition_guidance_tests PROPERTIES
    LABELS "implementation;cognition;guidance;query;evidence;fold;projection;decision;receipt")

function(laplace_add_cognition_guidance_mutation suffix definition test_name filter)
    set(library "laplace_cognition_guidance_${suffix}_mutant")
    set(probe "laplace_cognition_guidance_${suffix}_mutation_probe")
    add_library(${library} STATIC
        "${PROJECT_SOURCE_DIR}/engine/src/cognition_guidance.cpp")
    target_include_directories(${library} PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${library} PRIVATE BLAKE3::blake3)
    target_compile_definitions(${library} PRIVATE "${definition}=1")
    target_compile_options(${library} PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
    add_executable(${probe}
        "${PROJECT_SOURCE_DIR}/tests/cognition_guidance_tests.cpp")
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
        LABELS "implementation;cognition;guidance;mutation")
endfunction()

laplace_add_cognition_guidance_mutation(
    kind_collapse LAPLACE_TEST_COGNITION_GUIDANCE_COLLAPSE_KIND
    cognition-guidance.mutation-kind-collapse-detected
    GuidanceState.RetainsTypedObligationsInCanonicalIdentity)
laplace_add_cognition_guidance_mutation(
    nonempty_complete LAPLACE_TEST_COGNITION_GUIDANCE_NONEMPTY_COMPLETE
    cognition-guidance.mutation-nonempty-completion-detected
    GuidanceState.RequiresSemanticCompletion)
laplace_add_cognition_guidance_mutation(
    raw_novelty LAPLACE_TEST_COGNITION_GUIDANCE_RAW_NOVELTY
    cognition-guidance.mutation-raw-novelty-diversion-detected
    GuidanceScheduler.RanksRelevantReductionBeforeRawNovelty)
