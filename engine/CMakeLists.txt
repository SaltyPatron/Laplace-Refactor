add_library(laplace_engine SHARED
    src/identity.c
    src/geometry.c
    src/execution.cpp
    src/framework.c
    src/spool.cpp
    src/isa.c
    src/persistence.c
    src/trajectory.c
    src/unicode_root.c
    src/unicode_root_builder.cpp
    src/unicode_tier0_sink.cpp
    src/unicode_source.cpp
    src/unicode_properties.cpp
    src/unicode_ducet.cpp
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
add_library(Laplace::UnicodeRoot ALIAS laplace_engine)

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
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON)

set_target_properties(laplace_engine PROPERTIES
    EXPORT_NAME Engine
    OUTPUT_NAME laplace_engine
    VERSION 2.0.0
    SOVERSION 2
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
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/laplace/contract")
