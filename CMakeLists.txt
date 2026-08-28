cmake_minimum_required(VERSION 3.22)

project(Laplace VERSION 1.0.0 LANGUAGES C CXX)

include(cmake/LaplaceOutputBoundary.cmake)
laplace_require_external_output(
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}")

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LAPLACE_BLAKE3_SOURCE "" CACHE PATH "Verified upstream BLAKE3 C source directory")
set(LAPLACE_GTEST_SOURCE "" CACHE PATH "Verified upstream GoogleTest source directory")
set(LAPLACE_EIGEN_SOURCE "" CACHE PATH "Verified upstream Eigen source directory")
set(LAPLACE_SPECTRA_SOURCE "" CACHE PATH "Verified upstream Spectra source directory")
set(LAPLACE_PG_CONFIG "" CACHE FILEPATH "PostgreSQL pg_config for the server extension profile")
set(LAPLACE_PG_PHYSICAL_ROOT "" CACHE PATH
    "Optional staging root used to resolve absolute logical paths reported by pg_config")
set(LAPLACE_UNICODE_SOURCE_ROOT
    "/vault/Data/UCD/Public/UCD/latest"
    CACHE PATH "Pinned Unicode source-data root used by the root producer and conformance tests")
option(LAPLACE_ENABLE_DOTNET_BINDINGS
    "Generate and build the .NET 10 typed ISA orchestration surface" OFF)
set(LAPLACE_DEPENDENCY_LOCK
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/lock.json"
    CACHE FILEPATH "Laplace dependency lock")
set(LAPLACE_INSTALLED_PROVIDER_LOCK
    "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/installed-lock.json"
    CACHE FILEPATH "Selected installed-provider byte lock")
option(LAPLACE_VERIFY_ONEAPI_INSTALLED_PROVIDER
    "Verify and enable the exact selected installed oneAPI execution provider"
    OFF)

include(cmake/LaplaceDependencyLock.cmake)
include(cmake/LaplaceOneApiProvider.cmake)
laplace_configure_oneapi_provider()

if(NOT IS_ABSOLUTE "${LAPLACE_BLAKE3_SOURCE}")
    message(FATAL_ERROR "LAPLACE_BLAKE3_SOURCE must be an absolute verified source path")
endif()
if(NOT EXISTS "${LAPLACE_BLAKE3_SOURCE}/blake3.c"
   OR NOT EXISTS "${LAPLACE_BLAKE3_SOURCE}/blake3.h"
   OR NOT EXISTS "${LAPLACE_BLAKE3_SOURCE}/CMakeLists.txt")
    message(FATAL_ERROR "LAPLACE_BLAKE3_SOURCE does not contain the upstream C distribution")
endif()
get_filename_component(LAPLACE_BLAKE3_ROOT "${LAPLACE_BLAKE3_SOURCE}" DIRECTORY)
laplace_verify_git_dependency(
    blake3 "${LAPLACE_BLAKE3_ROOT}" "${LAPLACE_DEPENDENCY_LOCK}")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static internal dependencies" FORCE)
include(cmake/LaplaceBlake3.cmake)
laplace_configure_blake3_provider()
add_subdirectory("${LAPLACE_BLAKE3_SOURCE}" "${CMAKE_BINARY_DIR}/dependencies/blake3" EXCLUDE_FROM_ALL)

include(cmake/LaplaceIdentityContract.cmake)
laplace_configure_identity_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/identity.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/identity.h")

include(cmake/LaplacePerfcacheContract.cmake)
laplace_configure_perfcache_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/perfcache.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/perfcache.h")

include(cmake/LaplaceTrajectoryContract.cmake)
laplace_configure_trajectory_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/trajectory.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/trajectory.h")

include(cmake/LaplaceIsaContract.cmake)
laplace_configure_isa_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/isa.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/isa.h")

include(cmake/LaplaceExecutionContract.cmake)
laplace_configure_execution_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/execution.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/execution.h")

include(cmake/LaplaceFrameworkContract.cmake)
laplace_configure_framework_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/framework.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/framework.h")

include(cmake/LaplacePersistenceContract.cmake)
laplace_configure_persistence_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/persistence.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/persistence.h")

include(cmake/LaplaceEvidenceLineageContract.cmake)
laplace_configure_evidence_lineage_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/evidence-lineage.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/evidence_lineage.h")

include(cmake/LaplaceCompositionContract.cmake)
laplace_configure_composition_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/composition.json"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/composition.h")

include(cmake/LaplaceHighwayContract.cmake)
laplace_configure_highway_contract(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts/highway.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/contracts/generate-highway.py"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")

include(cmake/LaplaceUnicodeSource.cmake)
laplace_configure_unicode_source(
    "${CMAKE_CURRENT_SOURCE_DIR}/contracts"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/contracts/generate-unicode-source.py"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/unicode-source-manifest.h")

add_subdirectory(engine)

if(LAPLACE_ENABLE_DOTNET_BINDINGS)
    include(cmake/LaplaceDotnetBindings.cmake)
    laplace_configure_dotnet_bindings(
        "${CMAKE_CURRENT_SOURCE_DIR}/contracts/isa.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/bindings/generate-dotnet.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/managed/Laplace.Managed/Laplace.Managed.csproj"
        "${LAPLACE_HIGHWAY_GENERATED_CSHARP}")
endif()

if(LAPLACE_PG_CONFIG)
    include(cmake/LaplacePostgreSQL.cmake)
    laplace_discover_postgresql("${LAPLACE_PG_CONFIG}")
    add_subdirectory(postgres/extension)
endif()

add_subdirectory(tools)

if(BUILD_TESTING)
    if(NOT IS_ABSOLUTE "${LAPLACE_GTEST_SOURCE}")
        message(FATAL_ERROR "LAPLACE_GTEST_SOURCE must be an absolute verified source path when BUILD_TESTING=ON")
    endif()
    if(NOT EXISTS "${LAPLACE_GTEST_SOURCE}/CMakeLists.txt")
        message(FATAL_ERROR "LAPLACE_GTEST_SOURCE does not contain GoogleTest")
    endif()
    laplace_verify_git_dependency(
        googletest "${LAPLACE_GTEST_SOURCE}" "${LAPLACE_DEPENDENCY_LOCK}")
    foreach(math_dependency IN ITEMS EIGEN SPECTRA)
        if(NOT IS_ABSOLUTE "${LAPLACE_${math_dependency}_SOURCE}")
            message(FATAL_ERROR
                "LAPLACE_${math_dependency}_SOURCE must be an absolute verified source path when BUILD_TESTING=ON")
        endif()
    endforeach()
    if(NOT EXISTS "${LAPLACE_EIGEN_SOURCE}/Eigen/Core")
        message(FATAL_ERROR "LAPLACE_EIGEN_SOURCE does not contain Eigen")
    endif()
    if(NOT EXISTS "${LAPLACE_SPECTRA_SOURCE}/include/Spectra/SymEigsSolver.h")
        message(FATAL_ERROR "LAPLACE_SPECTRA_SOURCE does not contain Spectra")
    endif()
    laplace_verify_git_dependency(
        eigen "${LAPLACE_EIGEN_SOURCE}" "${LAPLACE_DEPENDENCY_LOCK}")
    laplace_verify_git_dependency(
        spectra "${LAPLACE_SPECTRA_SOURCE}" "${LAPLACE_DEPENDENCY_LOCK}")
    set(INSTALL_GTEST OFF CACHE BOOL "Do not install internal test dependency" FORCE)
    add_subdirectory("${LAPLACE_GTEST_SOURCE}" "${CMAKE_BINARY_DIR}/dependencies/googletest" EXCLUDE_FROM_ALL)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
        include(cmake/LaplaceDependencyCompiler.cmake)
        foreach(google_test_target IN ITEMS gtest gtest_main)
            laplace_remove_dependency_compile_flag(
                "${google_test_target}" "-Winline"
                "IntelLLVM 2026.1 rejects GoogleTest 1.18.0's Clang-family strict flag")
        endforeach()
        laplace_add_dependency_compile_option(
            gtest "-Wno-deprecated-declarations"
            "IntelLLVM reports a libstdc++ stable_sort implementation detail used while compiling GoogleTest 1.18.0")
    endif()
    add_subdirectory(tests)
endif()

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LaplaceConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/LaplaceConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Laplace")
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/LaplaceConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)
install(EXPORT LaplaceTargets
    FILE LaplaceTargets.cmake
    NAMESPACE Laplace::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Laplace")
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/LaplaceConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/LaplaceConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Laplace")
