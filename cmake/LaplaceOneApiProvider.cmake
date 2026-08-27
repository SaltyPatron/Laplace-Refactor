include_guard(GLOBAL)

function(laplace_oneapi_import_shared target_name location include_root)
    if(TARGET "${target_name}")
        message(FATAL_ERROR "duplicate installed oneAPI target: ${target_name}")
    endif()
    add_library("${target_name}" SHARED IMPORTED GLOBAL)
    set_target_properties("${target_name}" PROPERTIES
        IMPORTED_LOCATION "${location}"
        INTERFACE_INCLUDE_DIRECTORIES "${include_root}")
endfunction()

function(laplace_configure_oneapi_provider)
    if(NOT LAPLACE_VERIFY_ONEAPI_INSTALLED_PROVIDER)
        return()
    endif()
    if(NOT IS_ABSOLUTE "${LAPLACE_INSTALLED_PROVIDER_LOCK}")
        message(FATAL_ERROR
            "LAPLACE_INSTALLED_PROVIDER_LOCK must be an absolute contract path")
    endif()

    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(selection_file
        "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/oneapi-provider.cmake")
    execute_process(
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/dependencies/verify-installed-lock.py"
            "${LAPLACE_INSTALLED_PROVIDER_LOCK}"
            --verify-files
            --cmake-output "${selection_file}"
        RESULT_VARIABLE verification_status
        OUTPUT_VARIABLE verification_output
        ERROR_VARIABLE verification_error)
    if(NOT verification_status EQUAL 0)
        message(FATAL_ERROR
            "Installed oneAPI provider verification failed:\n${verification_output}${verification_error}")
    endif()
    include("${selection_file}")

    foreach(required_variable IN ITEMS
            LAPLACE_ONEAPI_SELECTION_SHA256
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_VERSION
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INCLUDE_ROOT
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IMF_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_SVML_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INTLC_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IRC_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IRNG_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_OPENMP_RUNTIME
            LAPLACE_ONEAPI_ONETBB_VERSION
            LAPLACE_ONEAPI_ONETBB_INCLUDE_ROOT
            LAPLACE_ONEAPI_ONETBB_TBB_RUNTIME
            LAPLACE_ONEAPI_ONETBB_TBBMALLOC_RUNTIME
            LAPLACE_ONEAPI_ONETBB_TBBBIND_2_5_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_VERSION
            LAPLACE_ONEAPI_ONEMKL_INCLUDE_ROOT
            LAPLACE_ONEAPI_ONEMKL_MKL_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_CORE_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_LP64_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_SEQUENTIAL_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_TBB_THREAD_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_VML_AVX2_RUNTIME)
        if(NOT DEFINED "${required_variable}" OR "${${required_variable}}" STREQUAL "")
            message(FATAL_ERROR
                "Installed oneAPI selection omitted ${required_variable}")
        endif()
    endforeach()

    set(oneapi_runtime_directories "")
    foreach(runtime_variable IN ITEMS
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IMF_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_SVML_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INTLC_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IRC_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IRNG_RUNTIME
            LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_OPENMP_RUNTIME
            LAPLACE_ONEAPI_ONETBB_TBB_RUNTIME
            LAPLACE_ONEAPI_ONETBB_TBBMALLOC_RUNTIME
            LAPLACE_ONEAPI_ONETBB_TBBBIND_2_5_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_CORE_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_LP64_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_SEQUENTIAL_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_TBB_THREAD_RUNTIME
            LAPLACE_ONEAPI_ONEMKL_MKL_VML_AVX2_RUNTIME)
        get_filename_component(
            runtime_directory "${${runtime_variable}}" DIRECTORY)
        list(APPEND oneapi_runtime_directories "${runtime_directory}")
    endforeach()
    list(REMOVE_DUPLICATES oneapi_runtime_directories)
    set(LAPLACE_ONEAPI_RUNTIME_DIRECTORIES
        "${oneapi_runtime_directories}" PARENT_SCOPE)

    laplace_oneapi_import_shared(
        LaplaceOneApiIntelImf
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IMF_RUNTIME}"
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiIntelSvml
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_SVML_RUNTIME}"
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiIntelIntlc
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INTLC_RUNTIME}"
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiIntelIrc
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IRC_RUNTIME}"
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiIntelIrng
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_IRNG_RUNTIME}"
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiIntelOpenMPObject
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_OPENMP_RUNTIME}"
        "${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_INCLUDE_ROOT}")

    add_library(LaplaceOneApiIntelMathRuntime INTERFACE)
    target_link_libraries(LaplaceOneApiIntelMathRuntime INTERFACE
        LaplaceOneApiIntelImf
        LaplaceOneApiIntelSvml
        LaplaceOneApiIntelIntlc
        LaplaceOneApiIntelIrc
        LaplaceOneApiIntelIrng)
    add_library(LaplaceOneApi::IntelMathRuntime ALIAS LaplaceOneApiIntelMathRuntime)
    add_library(LaplaceOneApi::IntelOpenMP ALIAS LaplaceOneApiIntelOpenMPObject)

    laplace_oneapi_import_shared(
        LaplaceOneApiTbbObject
        "${LAPLACE_ONEAPI_ONETBB_TBB_RUNTIME}"
        "${LAPLACE_ONEAPI_ONETBB_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiTbbMallocObject
        "${LAPLACE_ONEAPI_ONETBB_TBBMALLOC_RUNTIME}"
        "${LAPLACE_ONEAPI_ONETBB_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiTbbBindObject
        "${LAPLACE_ONEAPI_ONETBB_TBBBIND_2_5_RUNTIME}"
        "${LAPLACE_ONEAPI_ONETBB_INCLUDE_ROOT}")
    add_library(LaplaceOneApi::TBB ALIAS LaplaceOneApiTbbObject)
    add_library(LaplaceOneApi::TBBMalloc ALIAS LaplaceOneApiTbbMallocObject)
    add_library(LaplaceOneApi::TBBBind ALIAS LaplaceOneApiTbbBindObject)

    laplace_oneapi_import_shared(
        LaplaceOneApiMklRuntimeObject
        "${LAPLACE_ONEAPI_ONEMKL_MKL_RUNTIME}"
        "${LAPLACE_ONEAPI_ONEMKL_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiMklCoreObject
        "${LAPLACE_ONEAPI_ONEMKL_MKL_CORE_RUNTIME}"
        "${LAPLACE_ONEAPI_ONEMKL_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiMklLp64Object
        "${LAPLACE_ONEAPI_ONEMKL_MKL_LP64_RUNTIME}"
        "${LAPLACE_ONEAPI_ONEMKL_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiMklSequentialObject
        "${LAPLACE_ONEAPI_ONEMKL_MKL_SEQUENTIAL_RUNTIME}"
        "${LAPLACE_ONEAPI_ONEMKL_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiMklTbbThreadObject
        "${LAPLACE_ONEAPI_ONEMKL_MKL_TBB_THREAD_RUNTIME}"
        "${LAPLACE_ONEAPI_ONEMKL_INCLUDE_ROOT}")
    laplace_oneapi_import_shared(
        LaplaceOneApiMklVmlAvx2Object
        "${LAPLACE_ONEAPI_ONEMKL_MKL_VML_AVX2_RUNTIME}"
        "${LAPLACE_ONEAPI_ONEMKL_INCLUDE_ROOT}")
    add_library(LaplaceOneApi::MKLRuntime ALIAS LaplaceOneApiMklRuntimeObject)
    add_library(LaplaceOneApi::MKLVmlAvx2 ALIAS LaplaceOneApiMklVmlAvx2Object)

    add_library(LaplaceOneApiMklSequential INTERFACE)
    target_link_libraries(LaplaceOneApiMklSequential INTERFACE
        LaplaceOneApiMklLp64Object
        LaplaceOneApiMklSequentialObject
        LaplaceOneApiMklCoreObject)
    add_library(LaplaceOneApi::MKLSequential ALIAS LaplaceOneApiMklSequential)

    add_library(LaplaceOneApiMklTbb INTERFACE)
    target_link_libraries(LaplaceOneApiMklTbb INTERFACE
        LaplaceOneApiMklLp64Object
        LaplaceOneApiMklTbbThreadObject
        LaplaceOneApiMklCoreObject
        LaplaceOneApiTbbObject)
    add_library(LaplaceOneApi::MKLTbb ALIAS LaplaceOneApiMklTbb)

    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/oneapi-provider.h.in"
        "${CMAKE_CURRENT_BINARY_DIR}/generated/laplace/contract/oneapi-provider.h"
        @ONLY)

    message(STATUS
        "Verified installed oneAPI selection ${LAPLACE_ONEAPI_SELECTION_SHA256}: "
        "Intel runtime ${LAPLACE_ONEAPI_INTEL_ONEAPI_RUNTIME_VERSION}, "
        "oneTBB ${LAPLACE_ONEAPI_ONETBB_VERSION}, "
        "oneMKL ${LAPLACE_ONEAPI_ONEMKL_VERSION}; provider targets selected")
endfunction()
