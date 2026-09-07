include(GoogleTest)

add_executable(laplace_query_search_tests
    "${PROJECT_SOURCE_DIR}/tests/query_search_tests.cpp")
target_link_libraries(laplace_query_search_tests PRIVATE
    Laplace::QuerySearch GTest::gtest_main)
target_compile_options(laplace_query_search_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_query_search_tests PROPERTIES
    LABELS "implementation;query;cognition;search;astar;answerability;receipt")

add_executable(laplace_observation_query_tests
    "${PROJECT_SOURCE_DIR}/tests/observation_query_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_observation_request_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_observation_request_policy_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_observation_dynamic_request_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_observation_external_provider_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_semantic_act_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_discourse_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_discourse_frame_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_turn_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_realization_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_materialization_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_output_serialization_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_conversation_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_prompt_admission_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_prompt_structural_provider_tests.cpp"
    "${PROJECT_SOURCE_DIR}/tests/cognition_prompt_conversation_tests.cpp")
target_link_libraries(laplace_observation_query_tests PRIVATE
    Laplace::QuerySearch GTest::gtest_main)
target_compile_options(laplace_observation_query_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_observation_query_tests PROPERTIES
    LABELS "implementation;query;cognition;observation;prompt;admission;trunk;structural-fallback;physicality;trajectory;request;forward-pass;semantic-act;discourse;turn;realization;materialization;conversation;unicode;language;why-not;persistence;receipt")

function(laplace_add_query_search_mutation suffix definition test_name filter)
    set(library "laplace_query_search_${suffix}_mutant")
    set(probe "laplace_query_search_${suffix}_mutation_probe")
    add_library(${library} STATIC "${PROJECT_SOURCE_DIR}/engine/src/query_search.cpp")
    target_include_directories(${library} PRIVATE
        "${PROJECT_SOURCE_DIR}/engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${library} PRIVATE BLAKE3::blake3)
    target_compile_definitions(${library} PRIVATE "${definition}=1")
    target_compile_options(${library} PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
    add_executable(${probe} "${PROJECT_SOURCE_DIR}/tests/query_search_tests.cpp")
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
        LABELS "implementation;query;cognition;search;mutation")
endfunction()

laplace_add_query_search_mutation(
    g_only_priority LAPLACE_TEST_QUERY_SEARCH_IGNORE_HEURISTIC
    query-search.mutation-g-only-priority-detected
    QuerySearch.UsesDeclaredAStarPriority)
laplace_add_query_search_mutation(
    anchor_only_dominance LAPLACE_TEST_QUERY_SEARCH_ANCHOR_ONLY_DOMINANCE
    query-search.mutation-anchor-only-dominance-detected
    QuerySearch.PreservesDepthFeasibleDominance)
laplace_add_query_search_mutation(
    known_path_optimum LAPLACE_TEST_QUERY_SEARCH_PROMOTE_UPPER_BOUND
    query-search.mutation-known-path-optimum-detected
    QuerySearch.DoesNotPromoteKnownPathToOptimalAcrossIncompleteBoundary)
laplace_add_query_search_mutation(
    path_count_omission LAPLACE_TEST_QUERY_SEARCH_IGNORE_REQUESTED_PATH_COUNT
    query-search.mutation-path-count-omission-detected
    QuerySearch.ExecutesRequestedPathMultiplicity)

add_executable(laplace_cognition_output_encoding_test
    "${PROJECT_SOURCE_DIR}/tests/cognition_output_encoding_test.c")
target_compile_options(laplace_cognition_output_encoding_test PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion>)
add_test(NAME cognition-output.encoding-octet-exhaustive
    COMMAND laplace_cognition_output_encoding_test)
set_tests_properties(cognition-output.encoding-octet-exhaustive PROPERTIES
    LABELS "implementation;cognition;materialization;serialization;unicode")

add_executable(laplace_cognition_output_encoding_mutant
    "${PROJECT_SOURCE_DIR}/tests/cognition_output_encoding_test.c")
target_compile_definitions(laplace_cognition_output_encoding_mutant PRIVATE
    LAPLACE_TEST_OUTPUT_TRUNCATE=1)
add_test(NAME cognition-output.mutation-truncation-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_cognition_output_encoding_mutant>"
        -P "${PROJECT_SOURCE_DIR}/tests/expect_octet_mutation.cmake")
set_tests_properties(cognition-output.mutation-truncation-detected PROPERTIES
    LABELS "implementation;cognition;materialization;serialization;mutation")
