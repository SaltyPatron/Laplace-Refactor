include_guard(GLOBAL)

function(laplace_configure_blake3_provider)
    # Upstream BLAKE3 1.8.7 selects the hand-written AMD64 assembly route for
    # GNU/Clang/MSVC. IntelLLVM accepts the x86 intrinsic sources and flags, but
    # upstream does not currently classify that compiler when defining them.
    # Keep the exception here instead of patching or copying upstream source.
    if(CMAKE_C_COMPILER_ID STREQUAL "IntelLLVM")
        if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(amd64|AMD64|x86_64)$")
            message(FATAL_ERROR
                "Laplace has no verified BLAKE3 IntelLLVM SIMD provider for ${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        set(BLAKE3_CFLAGS_SSE2 "-msse2" CACHE STRING "BLAKE3 SSE2 flags" FORCE)
        set(BLAKE3_CFLAGS_SSE4.1 "-msse4.1" CACHE STRING "BLAKE3 SSE4.1 flags" FORCE)
        set(BLAKE3_CFLAGS_AVX2 "-mavx2" CACHE STRING "BLAKE3 AVX2 flags" FORCE)
        set(BLAKE3_CFLAGS_AVX512 "-mavx512f -mavx512vl" CACHE STRING "BLAKE3 AVX-512 flags" FORCE)
        set(BLAKE3_SIMD_TYPE "x86-intrinsics" CACHE STRING
            "Verified BLAKE3 SIMD implementation" FORCE)
        message(STATUS "Laplace BLAKE3 provider: IntelLLVM x86 intrinsics")
    else()
        # Do not override upstream's compiler/architecture selection for toolchains
        # it explicitly supports.
        unset(BLAKE3_SIMD_TYPE CACHE)
        message(STATUS "Laplace BLAKE3 provider: verified upstream automatic selection")
    endif()
endfunction()
