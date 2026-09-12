add_executable(laplace_composition_frontier_benchmark EXCLUDE_FROM_ALL
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/composition_frontier_benchmark.cpp")
target_include_directories(laplace_composition_frontier_benchmark PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(laplace_composition_frontier_benchmark PRIVATE
    Laplace::Composition)
target_compile_options(laplace_composition_frontier_benchmark PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
