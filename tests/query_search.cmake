include(GoogleTest)

add_executable(laplace_query_search_tests
    "${PROJECT_SOURCE_DIR}/tests/query_search_tests.cpp")
target_link_libraries(laplace_query_search_tests PRIVATE
    Laplace::QuerySearch GTest::gtest_main)
target_compile_options(laplace_query_search_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_query_search_tests PROPERTIES
    LABELS "implementation;query;cognition;search;astar;answerability;receipt")

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
