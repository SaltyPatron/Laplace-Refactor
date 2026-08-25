add_library(laplace_engine SHARED
    src/identity.c
    src/geometry.c
    src/execution.cpp
    src/isa.c
    src/trajectory.c
    src/perfcache.c)
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

set_target_properties(laplace_engine PROPERTIES
    EXPORT_NAME Engine
    OUTPUT_NAME laplace_engine
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
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

install(TARGETS laplace_engine
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
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/laplace/contract")
