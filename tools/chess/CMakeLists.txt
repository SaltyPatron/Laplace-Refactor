cmake_minimum_required(VERSION 3.22)
get_filename_component(LAPLACE_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${LAPLACE_REPOSITORY_ROOT}/cmake/LaplaceOutputBoundary.cmake")
laplace_require_external_output("${LAPLACE_REPOSITORY_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}")
project(LaplaceCuteChessRulesQualification LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "This qualification carrier currently observes the Linux loader")
endif()
foreach(required LAPLACE_CUTECHESS_SOURCE LAPLACE_CUTECHESS_LIBRARY
                 LAPLACE_QT_PREFIX LAPLACE_QT_VERSION LAPLACE_RULES_OUTPUT_ROOT
                 LAPLACE_NATIVE_BUILD_ROOT LAPLACE_NATIVE_ENGINE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name an exact selected provider input")
    endif()
endforeach()
if(NOT EXISTS "${LAPLACE_CUTECHESS_SOURCE}/projects/lib/src/board/standardboard.h"
   OR NOT EXISTS "${LAPLACE_CUTECHESS_LIBRARY}")
    message(FATAL_ERROR "Verified CuteChess source/library input is missing")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_BUILD_RPATH "${LAPLACE_QT_PREFIX}/lib")
find_package(Qt6 "${LAPLACE_QT_VERSION}" EXACT REQUIRED COMPONENTS Core Core5Compat)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

add_library(laplace_cutechess_upstream STATIC IMPORTED)
set_target_properties(laplace_cutechess_upstream PROPERTIES
    IMPORTED_LOCATION "${LAPLACE_CUTECHESS_LIBRARY}"
    INTERFACE_COMPILE_DEFINITIONS "LIB_EXPORT="
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${LAPLACE_CUTECHESS_SOURCE}/projects/lib/src/board"
    INTERFACE_INCLUDE_DIRECTORIES "${LAPLACE_CUTECHESS_SOURCE}/projects/lib/src/board"
    INTERFACE_LINK_LIBRARIES "Qt6::Core;Qt6::Core5Compat")

foreach(target laplace_cutechess_rules_provider laplace_cutechess_rules_provider_noncanonical_defect)
    add_executable(${target} cutechess_rules_provider.cpp)
    target_link_libraries(${target} PRIVATE laplace_cutechess_upstream)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
endforeach()
target_compile_definitions(laplace_cutechess_rules_provider_noncanonical_defect
    PRIVATE LAPLACE_TEST_ACCEPT_NONCANONICAL_SAN=1)

include(CTest)
add_test(NAME chess.native-rules-provider-contract
    COMMAND "${Python3_EXECUTABLE}" "${LAPLACE_REPOSITORY_ROOT}/tests/cutechess_rules_provider_tests.py"
        --provider "$<TARGET_FILE:laplace_cutechess_rules_provider>"
        --defect-provider "$<TARGET_FILE:laplace_cutechess_rules_provider_noncanonical_defect>"
        --build-receipt "${LAPLACE_RULES_OUTPUT_ROOT}/build-receipt.json"
        --output "${LAPLACE_RULES_OUTPUT_ROOT}/native-qualification.json")
set_tests_properties(chess.native-rules-provider-contract PROPERTIES
    LABELS "implementation;chess;physical-provider;legality;mutation"
    TIMEOUT 180)

if(NOT EXISTS "${LAPLACE_NATIVE_BUILD_ROOT}/generated/laplace/contract/composition.h"
   OR NOT EXISTS "${LAPLACE_NATIVE_ENGINE}")
    message(FATAL_ERROR "An actual common native build and generated public contracts are required")
endif()
add_library(laplace_native_engine SHARED IMPORTED)
set_target_properties(laplace_native_engine PROPERTIES
    IMPORTED_LOCATION "${LAPLACE_NATIVE_ENGINE}"
    INTERFACE_INCLUDE_DIRECTORIES "${LAPLACE_REPOSITORY_ROOT}/engine/include;${LAPLACE_NATIVE_BUILD_ROOT}/generated")
add_library(laplace_cutechess_trace_adapter STATIC cutechess_trace_adapter.cpp)
target_link_libraries(laplace_cutechess_trace_adapter PUBLIC laplace_cutechess_upstream laplace_native_engine)
target_include_directories(laplace_cutechess_trace_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(laplace_cutechess_trace_adapter PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_cutechess_trace_adapter_tests
    "${LAPLACE_REPOSITORY_ROOT}/tests/cutechess_trace_adapter_tests.cpp")
target_link_libraries(laplace_cutechess_trace_adapter_tests PRIVATE laplace_cutechess_trace_adapter)
target_compile_options(laplace_cutechess_trace_adapter_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME chess.native-trace-adapter-contract
    COMMAND "${CMAKE_COMMAND}" -E env
        "LD_LIBRARY_PATH=${LAPLACE_QT_PREFIX}/lib:$<TARGET_FILE_DIR:laplace_native_engine>:$ENV{LD_LIBRARY_PATH}"
        "$<TARGET_FILE:laplace_cutechess_trace_adapter_tests>"
        "${LAPLACE_RULES_OUTPUT_ROOT}/native-adapter-qualification.json"
        "${LAPLACE_NATIVE_ENGINE}" "${LAPLACE_QT_PREFIX}" "${LAPLACE_QT_VERSION}")
set_tests_properties(chess.native-trace-adapter-contract PROPERTIES
    LABELS "implementation;chess;physical-provider;composition" TIMEOUT 60)

option(LAPLACE_CHESS_POSTGRES "Build the selected native PostgreSQL LINE admission carrier" OFF)
if(LAPLACE_CHESS_POSTGRES)
    include("${CMAKE_CURRENT_SOURCE_DIR}/ChessPostgreSQL.cmake")
endif()
