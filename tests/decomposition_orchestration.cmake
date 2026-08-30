include(GoogleTest)

add_executable(laplace_decomposition_orchestration_tests
    decomposition_orchestration_tests.cpp)
target_link_libraries(laplace_decomposition_orchestration_tests PRIVATE
    Laplace::Decomposition
    GTest::gtest_main)
target_compile_options(laplace_decomposition_orchestration_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_decomposition_orchestration_tests
    PROPERTIES LABELS "implementation;decomposition;recipe;recursive;grammar")
