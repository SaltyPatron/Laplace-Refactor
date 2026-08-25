if(NOT DEFINED NATIVE_FIXTURE OR NOT DEFINED FIXTURE_PATH
   OR NOT DEFINED DOTNET OR NOT DEFINED MANAGED_TEST_DLL
   OR NOT DEFINED NATIVE_LIBRARY_DIRECTORY)
    message(FATAL_ERROR "native/.NET parity runner arguments are incomplete")
endif()

execute_process(
    COMMAND "${NATIVE_FIXTURE}" "${FIXTURE_PATH}"
    RESULT_VARIABLE fixture_result)
if(NOT fixture_result EQUAL 0)
    message(FATAL_ERROR "direct-native expectation fixture failed: ${fixture_result}")
endif()

set(ENV{LD_LIBRARY_PATH} "${NATIVE_LIBRARY_DIRECTORY}:$ENV{LD_LIBRARY_PATH}")
if(DEFINED SANITIZER_PRELOAD AND NOT SANITIZER_PRELOAD STREQUAL "")
    set(ENV{LD_PRELOAD} "${SANITIZER_PRELOAD}:$ENV{LD_PRELOAD}")
    # CoreCLR intentionally retains process-lifetime allocations. Keep address and
    # undefined-behavior instrumentation active for the native library without
    # treating the managed host's shutdown allocations as Laplace native leaks.
    set(ENV{ASAN_OPTIONS}
        "detect_leaks=0:strict_string_checks=1:check_initialization_order=1")
endif()
set(ENV{DOTNET_CLI_TELEMETRY_OPTOUT} "1")
set(ENV{DOTNET_SKIP_FIRST_TIME_EXPERIENCE} "1")
execute_process(
    COMMAND "${DOTNET}" "${MANAGED_TEST_DLL}" "${FIXTURE_PATH}"
    RESULT_VARIABLE managed_result)
if(NOT managed_result EQUAL 0)
    message(FATAL_ERROR "managed/native parity test failed: ${managed_result}")
endif()
