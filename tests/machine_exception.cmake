include(GoogleTest)

add_executable(laplace_machine_exception_tests
    "${CMAKE_CURRENT_LIST_DIR}/machine_exception_tests.cpp")
target_link_libraries(laplace_machine_exception_tests PRIVATE
    Laplace::MachineException
    GTest::gtest_main)
target_compile_options(laplace_machine_exception_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_machine_exception_tests PROPERTIES
    LABELS "implementation;machine;exception;why-not")

add_library(laplace_machine_exception_hardware_collapse_mutant STATIC
    "${CMAKE_CURRENT_LIST_DIR}/../engine/src/machine_exception.c")
target_include_directories(laplace_machine_exception_hardware_collapse_mutant PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(laplace_machine_exception_hardware_collapse_mutant PRIVATE
    LAPLACE_TEST_MACHINE_EXCEPTION_COLLAPSE_HARDWARE_TO_UNKNOWN=1)
target_compile_options(laplace_machine_exception_hardware_collapse_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_machine_exception_hardware_collapse_mutation_probe
    "${CMAKE_CURRENT_LIST_DIR}/machine_exception_tests.cpp")
target_include_directories(
    laplace_machine_exception_hardware_collapse_mutation_probe PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
    laplace_machine_exception_hardware_collapse_mutation_probe PRIVATE
    laplace_machine_exception_hardware_collapse_mutant
    GTest::gtest_main)
target_compile_options(
    laplace_machine_exception_hardware_collapse_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME machine-exception.mutation-hardware-to-unknown-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_machine_exception_hardware_collapse_mutation_probe>"
        "-DFILTER=MachineExceptionRegistry.GeneratedRegistryPreservesDistinctMachineConditions"
        -P "${CMAKE_CURRENT_LIST_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    machine-exception.mutation-hardware-to-unknown-detected PROPERTIES
    LABELS "implementation;machine;exception;mutation")
