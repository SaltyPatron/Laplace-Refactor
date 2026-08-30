set(_laplace_source_structural_witness_contract
    "${CMAKE_CURRENT_LIST_DIR}/postgres/source_structural_witness_contract.sql")
set(_laplace_source_admission_generated_contract
    "${CMAKE_BINARY_DIR}/tests/postgres/source_admission_contract.sql")
if(NOT EXISTS "${_laplace_source_admission_generated_contract}")
    message(FATAL_ERROR
        "generated source-admission contract is unavailable for structural witness proof")
endif()
file(READ "${_laplace_source_structural_witness_contract}"
    _laplace_source_structural_witness_contract_sql)
file(APPEND "${_laplace_source_admission_generated_contract}"
    "\n${_laplace_source_structural_witness_contract_sql}\n")
