# Preserve the established engine target graph, then add the generic
# decomposition-to-composition lowering layer as one coherent product slice.
include("${CMAKE_CURRENT_LIST_DIR}/CMakeLists.legacy.cmake")

target_sources(laplace_engine PRIVATE
    src/decomposition_composition.cpp)

add_library(Laplace::DecompositionComposition ALIAS laplace_engine)
