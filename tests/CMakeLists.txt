add_executable(laplace_identity_tests
    identity_tests.cpp)
target_link_libraries(laplace_identity_tests
    PRIVATE
        Laplace::Identity
        GTest::gtest_main)
target_compile_options(laplace_identity_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

include(GoogleTest)
gtest_discover_tests(laplace_identity_tests
    PROPERTIES LABELS "implementation;identity")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME unicode-root.contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/unicode_root_contract_tests.py")
set_tests_properties(unicode-root.contract-and-mutation PROPERTIES
    LABELS "contracts;unicode;identity;geometry;mutation")

add_test(
    NAME postgresql-build.contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/postgresql_build_tests.py")
set_tests_properties(postgresql-build.contract-and-mutation PROPERTIES
    LABELS "build;package;postgresql;mutation")

add_test(
    NAME postgresql-cluster.activation-contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/postgresql_cluster_tests.py")
set_tests_properties(postgresql-cluster.activation-contract PROPERTIES
    LABELS "activation;package;postgresql;security;topology;mutation"
    RUN_SERIAL TRUE)

add_executable(laplace_dependency_math_tests
    dependency_math_tests.cpp)
target_include_directories(laplace_dependency_math_tests SYSTEM PRIVATE
    "${LAPLACE_EIGEN_SOURCE}"
    "${LAPLACE_SPECTRA_SOURCE}/include")
target_link_libraries(laplace_dependency_math_tests PRIVATE GTest::gtest_main)
target_compile_options(laplace_dependency_math_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_dependency_math_tests
    PROPERTIES LABELS "implementation;package;dependencies;numeric")

add_executable(laplace_dependency_math_mutation_probe
    dependency_math_tests.cpp)
target_include_directories(laplace_dependency_math_mutation_probe SYSTEM PRIVATE
    "${LAPLACE_EIGEN_SOURCE}"
    "${LAPLACE_SPECTRA_SOURCE}/include")
target_link_libraries(laplace_dependency_math_mutation_probe PRIVATE GTest::gtest_main)
target_compile_definitions(laplace_dependency_math_mutation_probe
    PRIVATE LAPLACE_TEST_WRONG_EIGENVALUE=1)
target_compile_options(laplace_dependency_math_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME dependencies.mutation-spectral-provider-result-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_dependency_math_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_dependency_math_failure.cmake")
set_tests_properties(dependencies.mutation-spectral-provider-result-detected PROPERTIES
    LABELS "package;dependencies;numeric;mutation")

add_executable(laplace_isa_tests
    isa_tests.cpp)
target_link_libraries(laplace_isa_tests
    PRIVATE
        Laplace::Isa
        GTest::gtest_main)
target_compile_options(laplace_isa_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_isa_tests
    PROPERTIES LABELS "implementation;isa")

if(LAPLACE_ENABLE_DOTNET_BINDINGS)
    set(laplace_dotnet_sanitizer_preload "")
    if(CMAKE_C_FLAGS MATCHES "-fsanitize=[^ ]*address")
        execute_process(
            COMMAND "${CMAKE_C_COMPILER}" -print-file-name=libasan.so
            OUTPUT_VARIABLE laplace_dotnet_sanitizer_preload
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE laplace_dotnet_sanitizer_result)
        if(NOT laplace_dotnet_sanitizer_result EQUAL 0
           OR NOT IS_ABSOLUTE "${laplace_dotnet_sanitizer_preload}"
           OR NOT EXISTS "${laplace_dotnet_sanitizer_preload}")
            message(FATAL_ERROR
                "Cannot locate the address-sanitizer runtime required by the .NET P/Invoke test")
        endif()
    endif()

    add_executable(laplace_dotnet_native_expectations
        dotnet/native_expectations.cpp)
    target_link_libraries(laplace_dotnet_native_expectations PRIVATE Laplace::Isa)
    target_compile_options(laplace_dotnet_native_expectations PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    set(laplace_dotnet_test_project
        "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/Laplace.Managed.Tests.csproj")
    set(laplace_dotnet_test_dll
        "${LAPLACE_DOTNET_MANAGED_ROOT}/Laplace.Managed.Tests/bin/Release/net10.0/Laplace.Managed.Tests.dll")
    add_custom_command(
        OUTPUT "${laplace_dotnet_test_dll}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "DOTNET_CLI_TELEMETRY_OPTOUT=1"
            "DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1"
            "NUGET_PACKAGES=${LAPLACE_DOTNET_MANAGED_ROOT}/packages"
            "${LAPLACE_DOTNET_EXECUTABLE}" build "${laplace_dotnet_test_project}"
            --configuration Release
            --nologo
            "-p:GeneratedIsaSource=${LAPLACE_DOTNET_GENERATED_SOURCE}"
            "-p:LaplaceManagedBuildRoot=${LAPLACE_DOTNET_MANAGED_ROOT}"
            "-p:RestoreConfigFile=${LAPLACE_DOTNET_NUGET_CONFIG}"
        DEPENDS
            laplace_dotnet_bindings
            "${CMAKE_SOURCE_DIR}/Directory.Build.props"
            "${laplace_dotnet_test_project}"
            "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/Program.cs"
        COMMENT "Building dependency-free managed/native ISA parity tests"
        VERBATIM)
    add_custom_target(laplace_dotnet_tests_build ALL
        DEPENDS "${laplace_dotnet_test_dll}")
    add_dependencies(laplace_dotnet_tests_build
        laplace_dotnet_bindings
        laplace_dotnet_native_expectations)

    add_test(
        NAME dotnet.generated-isa-contract
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/generator_contract_tests.py"
            "${CMAKE_SOURCE_DIR}/tools/bindings/generate-dotnet.py"
            "${CMAKE_SOURCE_DIR}/contracts/isa.json")
    set_tests_properties(dotnet.generated-isa-contract PROPERTIES
        LABELS "implementation;dotnet;isa;bindings;mutation")

    add_test(
        NAME dotnet.native-isa-parity
        COMMAND "${CMAKE_COMMAND}"
            "-DNATIVE_FIXTURE=$<TARGET_FILE:laplace_dotnet_native_expectations>"
            "-DFIXTURE_PATH=${CMAKE_CURRENT_BINARY_DIR}/dotnet/native-expectations.bin"
            "-DDOTNET=${LAPLACE_DOTNET_EXECUTABLE}"
            "-DMANAGED_TEST_DLL=${laplace_dotnet_test_dll}"
            "-DNATIVE_LIBRARY_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DSANITIZER_PRELOAD=${laplace_dotnet_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/run_native_parity.cmake")
    set_tests_properties(dotnet.native-isa-parity PROPERTIES
        LABELS "implementation;dotnet;isa;bindings;receipt;contract")
endif()

add_executable(laplace_trajectory_tests
    trajectory_tests.cpp)
target_link_libraries(laplace_trajectory_tests
    PRIVATE
        Laplace::Trajectory
        GTest::gtest_main)
target_compile_options(laplace_trajectory_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_trajectory_tests
    PROPERTIES LABELS "implementation;trajectory;structure")

add_executable(laplace_persistence_tests
    persistence_tests.cpp)
target_link_libraries(laplace_persistence_tests
    PRIVATE
        Laplace::Persistence
        GTest::gtest_main)
target_compile_options(laplace_persistence_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_persistence_tests
    PROPERTIES LABELS "implementation;persistence;identity;physicality;trajectory")

foreach(laplace_zero_mutant IN ITEMS identity digest)
    add_library(laplace_persistence_zero_${laplace_zero_mutant}_mutant STATIC
        ../engine/src/persistence.c)
    target_include_directories(laplace_persistence_zero_${laplace_zero_mutant}_mutant
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
            "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_persistence_zero_${laplace_zero_mutant}_mutant
        PUBLIC Laplace::Trajectory
        PRIVATE BLAKE3::blake3)
    target_compile_options(laplace_persistence_zero_${laplace_zero_mutant}_mutant PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
endforeach()
target_compile_definitions(laplace_persistence_zero_identity_mutant
    PRIVATE LAPLACE_TEST_REJECT_ZERO_IDENTITY_FIELDS=1)
target_compile_definitions(laplace_persistence_zero_digest_mutant
    PRIVATE LAPLACE_TEST_REJECT_ZERO_DIGEST_FIELDS=1)

foreach(laplace_zero_mutant IN ITEMS identity digest)
    add_executable(laplace_persistence_zero_${laplace_zero_mutant}_mutation_probe
        persistence_zero_probe.cpp)
    target_link_libraries(laplace_persistence_zero_${laplace_zero_mutant}_mutation_probe
        PRIVATE laplace_persistence_zero_${laplace_zero_mutant}_mutant)
    target_compile_options(laplace_persistence_zero_${laplace_zero_mutant}_mutation_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME persistence.mutation-zero-${laplace_zero_mutant}-sentinel-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:laplace_persistence_zero_${laplace_zero_mutant}_mutation_probe>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_persistence_zero_failure.cmake")
    set_tests_properties(
        persistence.mutation-zero-${laplace_zero_mutant}-sentinel-detected
        PROPERTIES LABELS "persistence;identity;physicality;occurrence;mutation")
endforeach()

add_executable(laplace_execution_tests
    execution_tests.cpp)
target_link_libraries(laplace_execution_tests
    PRIVATE
        Laplace::Execution
        GTest::gtest_main)
target_compile_options(laplace_execution_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_execution_tests
    PROPERTIES LABELS "implementation;execution;topology;resources")

add_executable(laplace_framework_tests
    framework_tests.cpp)
target_link_libraries(laplace_framework_tests
    PRIVATE
        Laplace::Framework
        GTest::gtest_main)
target_compile_options(laplace_framework_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_framework_tests
    PROPERTIES LABELS "implementation;framework;registry;batch;sink;receipt")

add_executable(laplace_framework_stream_probe
    framework_stream_probe.cpp)
target_link_libraries(laplace_framework_stream_probe PRIVATE Laplace::Framework)
target_compile_options(laplace_framework_stream_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME framework.canonical-stream-binding-contract
    COMMAND laplace_framework_stream_probe)
set_tests_properties(framework.canonical-stream-binding-contract PROPERTIES
    LABELS "implementation;framework;batch;sink;receipt;contract")

add_library(laplace_framework_stream_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_stream_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_stream_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_stream_mutant
    PRIVATE LAPLACE_TEST_OMIT_STREAM_BINDING_FROM_RECEIPT=1)
target_compile_options(laplace_framework_stream_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_stream_mutation_probe
    framework_stream_probe.cpp)
target_link_libraries(laplace_framework_stream_mutation_probe
    PRIVATE laplace_framework_stream_mutant)
target_compile_options(laplace_framework_stream_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-stream-binding-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_stream_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_stream_failure.cmake")
set_tests_properties(framework.mutation-stream-binding-omission-detected PROPERTIES
    LABELS "framework;batch;sink;receipt;mutation")

add_executable(laplace_framework_producer_probe
    framework_producer_probe.cpp)
target_link_libraries(laplace_framework_producer_probe PRIVATE Laplace::Framework)
target_compile_options(laplace_framework_producer_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME framework.producer-cancellation-contract
    COMMAND laplace_framework_producer_probe)
set_tests_properties(framework.producer-cancellation-contract PROPERTIES
    LABELS "implementation;framework;producer;batch;sink;receipt;replay;contract")

add_library(laplace_framework_producer_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_producer_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_producer_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_producer_mutant
    PRIVATE LAPLACE_TEST_IGNORE_PRODUCER_CANCELLATION=1)
target_compile_options(laplace_framework_producer_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_producer_mutation_probe
    framework_producer_probe.cpp)
target_link_libraries(laplace_framework_producer_mutation_probe
    PRIVATE laplace_framework_producer_mutant)
target_compile_options(laplace_framework_producer_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-producer-cancellation-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_producer_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_producer_failure.cmake")
set_tests_properties(
    framework.mutation-producer-cancellation-omission-detected PROPERTIES
    LABELS "framework;producer;batch;sink;receipt;replay;mutation")

add_library(laplace_framework_replay_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_replay_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_replay_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_replay_mutant
    PRIVATE LAPLACE_TEST_OMIT_REPLAY_PREFIX_ASSERTION=1)
target_compile_options(laplace_framework_replay_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_replay_mutation_probe
    framework_producer_probe.cpp)
target_link_libraries(laplace_framework_replay_mutation_probe
    PRIVATE laplace_framework_replay_mutant)
target_compile_options(laplace_framework_replay_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-replay-prefix-assertion-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_replay_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_producer_failure.cmake")
set_tests_properties(
    framework.mutation-replay-prefix-assertion-omission-detected PROPERTIES
    LABELS "framework;producer;batch;sink;receipt;replay;mutation")

add_executable(laplace_framework_activation_probe
    framework_activation_probe.cpp)
target_link_libraries(laplace_framework_activation_probe PRIVATE Laplace::Framework)
target_compile_options(laplace_framework_activation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME framework.activation-epoch-cas-contract
    COMMAND laplace_framework_activation_probe)
set_tests_properties(framework.activation-epoch-cas-contract PROPERTIES
    LABELS "implementation;framework;activation;authority;receipt;contract")

add_library(laplace_framework_activation_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_activation_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_activation_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_activation_mutant
    PRIVATE LAPLACE_TEST_ACTIVATE_WITHOUT_EXPECTED_EPOCH=1)
target_compile_options(laplace_framework_activation_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_activation_mutation_probe
    framework_activation_probe.cpp)
target_link_libraries(laplace_framework_activation_mutation_probe
    PRIVATE laplace_framework_activation_mutant)
target_compile_options(laplace_framework_activation_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-stale-epoch-activation-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_activation_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_activation_failure.cmake")
set_tests_properties(framework.mutation-stale-epoch-activation-detected PROPERTIES
    LABELS "framework;activation;authority;receipt;mutation")

add_test(
    NAME build.repository-output-rejected
    COMMAND "${CMAKE_COMMAND}"
        "-DMODULE=${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LaplaceOutputBoundary.cmake"
        "-DPROBE=${CMAKE_CURRENT_SOURCE_DIR}/output_boundary_invalid.cmake"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_output_boundary_failure.cmake")
set_tests_properties(build.repository-output-rejected PROPERTIES
    LABELS "build;output;contract")

add_executable(laplace_execution_grant_probe
    execution_grant_probe.cpp)
target_link_libraries(laplace_execution_grant_probe PRIVATE Laplace::Execution)
target_compile_options(laplace_execution_grant_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME execution.resource-conservation-contract COMMAND laplace_execution_grant_probe)
set_tests_properties(execution.resource-conservation-contract PROPERTIES
    LABELS "implementation;execution;resources;contract")

add_library(laplace_execution_resource_mutant STATIC
    ../engine/src/execution.cpp)
target_include_directories(laplace_execution_resource_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(laplace_execution_resource_mutant
    PRIVATE LAPLACE_TEST_DUPLICATE_PARENT_GRANT=1)
target_link_libraries(laplace_execution_resource_mutant PRIVATE BLAKE3::blake3)
target_compile_options(laplace_execution_resource_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

add_executable(laplace_execution_resource_mutation_probe
    execution_grant_probe.cpp)
target_link_libraries(laplace_execution_resource_mutation_probe
    PRIVATE laplace_execution_resource_mutant)
target_compile_options(laplace_execution_resource_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME execution.mutation-resource-duplication-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_execution_resource_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_execution_failure.cmake")
set_tests_properties(execution.mutation-resource-duplication-detected PROPERTIES
    LABELS "execution;resources;mutation")

add_library(laplace_execution_result_mutant STATIC
    ../engine/src/execution.cpp)
target_include_directories(laplace_execution_result_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_execution_result_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_execution_result_mutant
    PRIVATE LAPLACE_TEST_SKIP_CHUNK_RESULT_VALIDATION=1)
target_compile_options(laplace_execution_result_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

add_executable(laplace_execution_result_mutation_probe
    execution_result_probe.cpp)
target_link_libraries(laplace_execution_result_mutation_probe
    PRIVATE laplace_execution_result_mutant)
target_compile_options(laplace_execution_result_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME execution.mutation-missing-chunk-result-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_execution_result_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_execution_result_failure.cmake")
set_tests_properties(execution.mutation-missing-chunk-result-detected PROPERTIES
    LABELS "execution;resources;receipt;mutation")

add_executable(laplace_isa_preflight_probe
    isa_preflight_probe.cpp)
target_link_libraries(laplace_isa_preflight_probe PRIVATE Laplace::Isa)
target_compile_options(laplace_isa_preflight_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME isa.preflight-atomicity-contract COMMAND laplace_isa_preflight_probe)
set_tests_properties(isa.preflight-atomicity-contract PROPERTIES
    LABELS "implementation;isa;contract")

add_library(laplace_isa_preflight_mutant STATIC
    ../engine/src/identity.c
    ../engine/src/framework.c
    ../engine/src/isa.c
    ../engine/src/trajectory.c)
target_include_directories(laplace_isa_preflight_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_isa_preflight_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_isa_preflight_mutant
    PRIVATE LAPLACE_TEST_VALIDATE_FIRST_INSTRUCTION_ONLY=1)
target_compile_options(laplace_isa_preflight_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_isa_preflight_mutation_probe
    isa_preflight_probe.cpp)
target_link_libraries(laplace_isa_preflight_mutation_probe
    PRIVATE laplace_isa_preflight_mutant)
target_compile_options(laplace_isa_preflight_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME isa.mutation-partial-preflight-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_isa_preflight_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_isa_preflight_failure.cmake")
set_tests_properties(isa.mutation-partial-preflight-detected PROPERTIES
    LABELS "isa;mutation")

add_executable(laplace_isa_registry_probe
    isa_registry_probe.cpp)
target_link_libraries(laplace_isa_registry_probe PRIVATE Laplace::Isa)
target_compile_options(laplace_isa_registry_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME isa.generated-registry-dispatch-contract
    COMMAND laplace_isa_registry_probe)
set_tests_properties(isa.generated-registry-dispatch-contract PROPERTIES
    LABELS "implementation;isa;framework;registry;contract")

add_library(laplace_isa_registry_mutant STATIC
    ../engine/src/identity.c
    ../engine/src/framework.c
    ../engine/src/isa.c
    ../engine/src/trajectory.c)
target_include_directories(laplace_isa_registry_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_isa_registry_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_isa_registry_mutant
    PRIVATE LAPLACE_TEST_DISPATCH_FIRST_OPERATION_ONLY=1)
target_compile_options(laplace_isa_registry_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_isa_registry_mutation_probe
    isa_registry_probe.cpp)
target_link_libraries(laplace_isa_registry_mutation_probe
    PRIVATE laplace_isa_registry_mutant)
target_compile_options(laplace_isa_registry_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME isa.mutation-private-dispatch-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_isa_registry_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_isa_registry_failure.cmake")
set_tests_properties(isa.mutation-private-dispatch-detected PROPERTIES
    LABELS "isa;framework;registry;mutation")

add_executable(laplace_isa_context_probe
    isa_context_probe.cpp)
target_link_libraries(laplace_isa_context_probe PRIVATE Laplace::Isa)
target_compile_options(laplace_isa_context_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME isa.execution-context-binding-contract
    COMMAND laplace_isa_context_probe)
set_tests_properties(isa.execution-context-binding-contract PROPERTIES
    LABELS "implementation;isa;framework;context;receipt;contract")

add_library(laplace_isa_context_mutant STATIC
    ../engine/src/identity.c
    ../engine/src/framework.c
    ../engine/src/isa.c
    ../engine/src/trajectory.c)
target_include_directories(laplace_isa_context_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_isa_context_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_isa_context_mutant
    PRIVATE LAPLACE_TEST_OMIT_CONTEXT_FROM_ISA_RECEIPT=1)
target_compile_options(laplace_isa_context_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_isa_context_mutation_probe
    isa_context_probe.cpp)
target_link_libraries(laplace_isa_context_mutation_probe
    PRIVATE laplace_isa_context_mutant)
target_compile_options(laplace_isa_context_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME isa.mutation-context-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_isa_context_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_isa_context_failure.cmake")
set_tests_properties(isa.mutation-context-omission-detected PROPERTIES
    LABELS "isa;framework;context;receipt;mutation")

add_executable(laplace_identity_contract_probe
    identity_contract_probe.cpp)
target_link_libraries(laplace_identity_contract_probe PRIVATE Laplace::Identity)
target_compile_options(laplace_identity_contract_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME identity.contract-vectors COMMAND laplace_identity_contract_probe)
set_tests_properties(identity.contract-vectors PROPERTIES LABELS "implementation;identity;contract")

add_library(laplace_identity_domain_mutant STATIC
    ../engine/src/identity.c)
target_include_directories(laplace_identity_domain_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_identity_domain_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_identity_domain_mutant
    PRIVATE LAPLACE_TEST_MUTATE_COMPOSITE_DOMAIN=1)
target_compile_options(laplace_identity_domain_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_identity_domain_mutation_probe
    identity_contract_probe.cpp)
target_link_libraries(laplace_identity_domain_mutation_probe
    PRIVATE laplace_identity_domain_mutant)
target_compile_options(laplace_identity_domain_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME identity.mutation-domain-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_identity_domain_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_contract_failure.cmake")
set_tests_properties(identity.mutation-domain-detected PROPERTIES
    LABELS "identity;mutation")

add_executable(laplace_unicode_position_probe
    unicode_position_probe.cpp)
target_link_libraries(laplace_unicode_position_probe PRIVATE Laplace::Identity)
target_compile_options(laplace_unicode_position_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME identity.unicode-surrogate-position COMMAND laplace_unicode_position_probe)
set_tests_properties(identity.unicode-surrogate-position PROPERTIES
    LABELS "implementation;identity;unicode")

add_library(laplace_identity_surrogate_mutant STATIC
    ../engine/src/identity.c)
target_include_directories(laplace_identity_surrogate_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_identity_surrogate_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_identity_surrogate_mutant
    PRIVATE LAPLACE_TEST_REJECT_SURROGATE_POSITIONS=1)
target_compile_options(laplace_identity_surrogate_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_unicode_surrogate_mutation_probe
    unicode_position_probe.cpp)
target_link_libraries(laplace_unicode_surrogate_mutation_probe
    PRIVATE laplace_identity_surrogate_mutant)
target_compile_options(laplace_unicode_surrogate_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME identity.mutation-surrogate-exclusion-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_surrogate_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_unicode_failure.cmake")
set_tests_properties(identity.mutation-surrogate-exclusion-detected PROPERTIES
    LABELS "identity;unicode;mutation")

add_executable(laplace_geometry_tests
    geometry_tests.cpp)
target_link_libraries(laplace_geometry_tests
    PRIVATE
        Laplace::Geometry
        GTest::gtest_main)
target_compile_options(laplace_geometry_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
gtest_discover_tests(laplace_geometry_tests
    PROPERTIES LABELS "implementation;geometry")

add_executable(laplace_geometry_contract_probe
    geometry_contract_probe.cpp)
target_link_libraries(laplace_geometry_contract_probe PRIVATE Laplace::Geometry)
target_compile_options(laplace_geometry_contract_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
add_test(NAME geometry.arithmetic-centroid-contract COMMAND laplace_geometry_contract_probe)
set_tests_properties(geometry.arithmetic-centroid-contract PROPERTIES
    LABELS "implementation;geometry;contract")

add_library(laplace_geometry_normalization_mutant STATIC
    ../engine/src/geometry.c)
target_include_directories(laplace_geometry_normalization_mutant
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include")
target_link_libraries(laplace_geometry_normalization_mutant PUBLIC m)
target_compile_definitions(laplace_geometry_normalization_mutant
    PRIVATE LAPLACE_TEST_NORMALIZE_CENTROID=1)
target_compile_options(laplace_geometry_normalization_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes;-ffp-contract=off>)
add_executable(laplace_geometry_normalization_mutation_probe
    geometry_contract_probe.cpp)
target_link_libraries(laplace_geometry_normalization_mutation_probe
    PRIVATE laplace_geometry_normalization_mutant)
target_compile_options(laplace_geometry_normalization_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
add_test(
    NAME geometry.mutation-sphere-normalization-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_geometry_normalization_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_geometry_failure.cmake")
set_tests_properties(geometry.mutation-sphere-normalization-detected PROPERTIES
    LABELS "geometry;mutation")

add_executable(laplace_perfcache_tests
    perfcache_tests.cpp)
target_link_libraries(laplace_perfcache_tests
    PRIVATE
        Laplace::Perfcache
        BLAKE3::blake3
        GTest::gtest_main)
target_compile_options(laplace_perfcache_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_perfcache_tests
    PROPERTIES LABELS "implementation;perfcache")

add_executable(laplace_perfcache_contract_probe
    perfcache_contract_probe.cpp)
target_link_libraries(laplace_perfcache_contract_probe PRIVATE Laplace::Perfcache)
target_compile_options(laplace_perfcache_contract_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME perfcache.sorted-unique-contract COMMAND laplace_perfcache_contract_probe)
set_tests_properties(perfcache.sorted-unique-contract PROPERTIES
    LABELS "implementation;perfcache;contract")

add_library(laplace_perfcache_sorted_mutant STATIC
    ../engine/src/perfcache.c)
target_include_directories(laplace_perfcache_sorted_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_perfcache_sorted_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_perfcache_sorted_mutant
    PRIVATE LAPLACE_TEST_SKIP_SORTED_UNIQUE_VALIDATION=1)
target_compile_options(laplace_perfcache_sorted_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_perfcache_sorted_mutation_probe
    perfcache_contract_probe.cpp)
target_link_libraries(laplace_perfcache_sorted_mutation_probe
    PRIVATE laplace_perfcache_sorted_mutant)
target_compile_options(laplace_perfcache_sorted_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME perfcache.mutation-sorted-unique-bypass-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_perfcache_sorted_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_perfcache_failure.cmake")
set_tests_properties(perfcache.mutation-sorted-unique-bypass-detected PROPERTIES
    LABELS "perfcache;mutation")

if(UNIX)
    add_executable(laplace_perfcache_file_tests
        perfcache_file_tests.cpp)
    target_link_libraries(laplace_perfcache_file_tests
        PRIVATE
            Laplace::Perfcache
            GTest::gtest_main)
    target_compile_options(laplace_perfcache_file_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    gtest_discover_tests(laplace_perfcache_file_tests
        PROPERTIES LABELS "implementation;perfcache;file")
endif()

if(LAPLACE_POSTGRES_ENABLED)
    find_program(LAPLACE_BASH_EXECUTABLE bash REQUIRED)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/spi_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/spi_contract.sql"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/persistence_concurrency_call.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/persistence_concurrency_call.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/persistence_concurrency_verify.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/persistence_concurrency_verify.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/persistence_blind_conflict_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/persistence_blind_conflict_mutation.sql"
        COPYONLY)
    add_executable(laplace_postgres_native_receipt_probe
        postgres/native_receipt_probe.cpp)
    target_link_libraries(laplace_postgres_native_receipt_probe PRIVATE
        Laplace::Isa
        Laplace::Persistence)
    target_compile_options(laplace_postgres_native_receipt_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    add_test(
        NAME postgres.spi-isa-contract
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            contract
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/spi_contract.sql")
    set_tests_properties(postgres.spi-isa-contract PROPERTIES
        LABELS "implementation;postgresql;spi;isa;receipt"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-receipt-collision-detected
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            mutation
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/spi_receipt_mutation.sql")
    set_tests_properties(postgres.mutation-receipt-collision-detected PROPERTIES
        LABELS "postgresql;spi;receipt;mutation"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-blind-persistence-conflict-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_blind_conflict_mutant>"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/persistence_blind_conflict_mutation.sql"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_persistence_mutation_failure.cmake")
    set_tests_properties(postgres.mutation-blind-persistence-conflict-detected PROPERTIES
        LABELS "postgresql;spi;persistence;identity;mutation"
        RUN_SERIAL TRUE)
endif()

add_test(
    NAME package.native-consumer-contract
    COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
        "-DSOURCE_DIRECTORY=${CMAKE_SOURCE_DIR}"
        "-DINSTALL_DIRECTORY=${CMAKE_BINARY_DIR}/package-test/install"
        "-DCONSUMER_BUILD_DIRECTORY=${CMAKE_BINARY_DIR}/package-test/consumer"
        "-DCONSUMER_C_FLAGS=${CMAKE_C_FLAGS}"
        "-DCONSUMER_LINK_FLAGS=${CMAKE_EXE_LINKER_FLAGS}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_installed_consumer.cmake")
set_tests_properties(package.native-consumer-contract PROPERTIES
    LABELS "implementation;package;consumer"
    RUN_SERIAL TRUE)

if(UNIX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME requirements.traceability-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/requirements_trace_tests.py")
    set_tests_properties(requirements.traceability-contract PROPERTIES
        LABELS "implementation;requirements;evidence")

    add_test(
        NAME dependencies.intent-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/dependency_intent_tests.py")
    set_tests_properties(dependencies.intent-contract PROPERTIES
        LABELS "implementation;package;dependencies;evidence")

    add_test(
        NAME dependencies.installed-provider-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/installed_provider_lock_tests.py")
    set_tests_properties(dependencies.installed-provider-contract PROPERTIES
        LABELS "implementation;package;dependencies;oneapi;mutation")

    find_program(LAPLACE_BASH_EXECUTABLE bash REQUIRED)
    add_test(
        NAME package.elf-dependency-closure
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/elf_closure_test.sh"
            "${CMAKE_SOURCE_DIR}/tools/dependencies/elf-closure.py"
            "${CMAKE_BINARY_DIR}/elf-closure-test")
    set_tests_properties(package.elf-dependency-closure PROPERTIES
        LABELS "implementation;package;dependencies"
        RUN_SERIAL TRUE)

    add_test(
        NAME package.release-asset-integrity
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/release_assets_test.sh"
            "${CMAKE_SOURCE_DIR}/tools/dependencies/release-assets.py"
            "${CMAKE_BINARY_DIR}/release-assets-test")
    set_tests_properties(package.release-asset-integrity PROPERTIES
        LABELS "implementation;package;dependencies"
        RUN_SERIAL TRUE)
endif()
