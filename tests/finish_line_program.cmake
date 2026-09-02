add_test(
    NAME finish-line.program-contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/finish_line_program_tests.py")
set_tests_properties(
    finish-line.program-contract-and-mutation PROPERTIES
    LABELS "contract;requirements;program;governance;finish-line;mutation")

add_test(
    NAME finish-line.branch-estate-closure-and-mutation
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/branch_estate_closure_tests.py")
set_tests_properties(
    finish-line.branch-estate-closure-and-mutation PROPERTIES
    LABELS "contract;program;governance;finish-line;branch-estate;mutation")
