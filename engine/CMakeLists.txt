set(LAPLACE_TREE_SITTER_SOURCE "" CACHE PATH "Verified upstream Tree-sitter source directory")
if(NOT IS_ABSOLUTE "${LAPLACE_TREE_SITTER_SOURCE}")
    message(FATAL_ERROR "LAPLACE_TREE_SITTER_SOURCE must be an absolute verified source path")
endif()
if(NOT EXISTS "${LAPLACE_TREE_SITTER_SOURCE}/lib/include/tree_sitter/api.h"
   OR NOT EXISTS "${LAPLACE_TREE_SITTER_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "LAPLACE_TREE_SITTER_SOURCE does not contain the locked Tree-sitter runtime")
endif()
laplace_verify_git_dependency(
    tree-sitter "${LAPLACE_TREE_SITTER_SOURCE}" "${LAPLACE_DEPENDENCY_LOCK}")
set(TREE_SITTER_FEATURE_WASM OFF CACHE BOOL "Disable Tree-sitter WASM provider" FORCE)
add_subdirectory(
    "${LAPLACE_TREE_SITTER_SOURCE}"
    "${CMAKE_BINARY_DIR}/dependencies/tree-sitter"
    EXCLUDE_FROM_ALL)

add_library(laplace_engine SHARED
    src/identity.c
    src/geometry.c
    src/execution.cpp
    src/framework.c
    src/highway.c
    src/highway_ast.cpp
    src/spool.cpp
    src/isa.c
    src/persistence.c
    src/evidence_lineage.c
    src/evidence_testimony.c
    src/reference_topology.c
    src/reference_mapping.c
    src/source_profile.c
    src/tabular_source.cpp
    src/decomposition_composition.cpp
    src/world_admission.c
    src/composition.cpp
    src/trajectory.c
    src/unicode_root.c
    src/unicode_root_builder.cpp
    src/unicode_tier0_sink.cpp
    src/unicode_source.cpp
    src/unicode_properties.cpp
    src/unicode_ducet.cpp
    src/uax29.cpp
    src/text_decomposition.cpp
    src/decomposition.cpp
    src/decomposition_uax29.cpp
    src/decomposition_delimited.cpp
    src/decomposition_tree_sitter.cpp
    src/tree_sitter_grammar.cpp
    src/perfcache.c
    src/perfcache_modules.c
    src/perfcache_registry.cpp)
if(TARGET LaplaceOneApi::TBB AND TARGET LaplaceOneApi::MKLTbb)
    target_sources(laplace_engine PRIVATE src/execution_oneapi.cpp)
    target_link_libraries(laplace_engine PRIVATE
        LaplaceOneApi::TBB
        LaplaceOneApi::MKLTbb)
else()
    target_sources(laplace_engine PRIVATE src/execution_oneapi_stub.c)
endif()
if(UNIX)
    target_sources(laplace_engine PRIVATE src/perfcache_file_posix.c)
endif()

add_library(Laplace::Engine ALIAS laplace_engine)
add_library(Laplace::Identity ALIAS laplace_engine)
add_library(Laplace::Geometry ALIAS laplace_engine)
add_library(Laplace::Perfcache ALIAS laplace_engine)
add_library(Laplace::Isa ALIAS laplace_engine)
add_library(Laplace::Trajectory ALIAS laplace_engine)
add_library(Laplace::Execution ALIAS laplace_engine)
add_library(Laplace::Framework ALIAS laplace_engine)
add_library(Laplace::Persistence ALIAS laplace_engine)
add_library(Laplace::EvidenceLineage ALIAS laplace_engine)
add_library(Laplace::EvidenceTestimony ALIAS laplace_engine)
add_library(Laplace::ReferenceTopology ALIAS laplace_engine)
add_library(Laplace::ReferenceMapping ALIAS laplace_engine)
add_library(Laplace::SourceProfile ALIAS laplace_engine)
add_library(Laplace::TabularSource ALIAS laplace_engine)
add_library(Laplace::DecompositionComposition ALIAS laplace_engine)
add_library(Laplace::WorldAdmission ALIAS laplace_engine)
add_library(Laplace::Composition ALIAS laplace_engine)
add_library(Laplace::Highway ALIAS laplace_engine)
add_library(Laplace::UnicodeRoot ALIAS laplace_engine)
add_library(Laplace::Uax29 ALIAS laplace_engine)
add_library(Laplace::TextDecomposition ALIAS laplace_engine)
add_library(Laplace::Decomposition ALIAS laplace_engine)

if(TARGET LaplaceOneApi::MKLRuntime)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
        message(FATAL_ERROR
            "canonical Unicode numeric provider requires the selected IntelLLVM compiler")
    endif()
    add_library(laplace_unicode_numeric SHARED
        src/unicode_numeric_oneapi.cpp)
    target_include_directories(laplace_unicode_numeric
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_link_libraries(laplace_unicode_numeric PRIVATE
        BLAKE3::blake3
        LaplaceOneApi::MKLRuntime
        LaplaceOneApi::IntelMathRuntime)
    target_compile_options(laplace_unicode_numeric PRIVATE
        -O3 -march=haswell -fp-model=strict -fno-fast-math -ffp-contract=off
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
        -Wno-overriding-option)
else()
    add_library(laplace_unicode_numeric SHARED
        src/unicode_numeric_stub.c)
    target_include_directories(laplace_unicode_numeric
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_options(laplace_unicode_numeric PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
endif()
add_library(Laplace::UnicodeNumeric ALIAS laplace_unicode_numeric)
set_target_properties(laplace_unicode_numeric PROPERTIES
    EXPORT_NAME UnicodeNumeric
    OUTPUT_NAME laplace_unicode_numeric
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    INSTALL_RPATH "$ORIGIN"
    INSTALL_RPATH_USE_LINK_PATH FALSE
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

set_target_properties(laplace_engine PROPERTIES
    EXPORT_NAME Engine
    OUTPUT_NAME laplace_engine
    VERSION 2.0.0
    SOVERSION 2
    INSTALL_RPATH "$ORIGIN"
    INSTALL_RPATH_USE_LINK_PATH FALSE
    C_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(laplace_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(laplace_engine
    PRIVATE
        BLAKE3::blake3
        tree-sitter
        ${CMAKE_DL_LIBS}
        m)
target_compile_definitions(laplace_engine
    PRIVATE LAPLACE_ENGINE_BUILD=1)
target_compile_options(laplace_engine PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes;-ffp-contract=off>)
target_compile_options(laplace_engine PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
if(UNIX AND NOT APPLE)
    target_link_options(laplace_engine PRIVATE
        "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/laplace_engine.map")
endif()

install(TARGETS laplace_engine laplace_unicode_numeric
    EXPORT LaplaceTargets
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/identity.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/isa.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/perfcache.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/trajectory.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/execution.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/framework.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/persistence.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/evidence_lineage.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/evidence_testimony.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/reference_topology.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/reference_mapping.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/source_profile.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/tabular_source.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/world_admission.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/composition.h"
    "${CMAKE_BINARY_DIR}/generated/laplace/contract/highway.h"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/laplace/contract")
