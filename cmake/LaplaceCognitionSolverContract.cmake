function(laplace_configure_cognition_solver_contract contract_path output_path)
    file(READ "${contract_path}" contract)
    string(JSON schema GET "${contract}" schema)
    string(JSON version GET "${contract}" version)
    string(JSON algorithm GET "${contract}" digest_algorithm)
    foreach(domain IN ITEMS program input iteration output receipt)
        string(JSON domain_${domain} GET "${contract}" domains ${domain})
    endforeach()
    string(JSON method_cg GET "${contract}" methods conjugate_gradient)
    foreach(disposition IN ITEMS converged initially_satisfied iteration_limit singular_direction numeric_failure)
        string(JSON disposition_${disposition} GET "${contract}" dispositions ${disposition})
    endforeach()
    foreach(flag IN ITEMS require_psd_operator record_iteration_receipts)
        string(JSON flag_${flag} GET "${contract}" flags ${flag})
    endforeach()
    if(NOT schema STREQUAL "laplace.cognition-solver/v1" OR
       NOT version EQUAL 1 OR NOT algorithm STREQUAL "BLAKE3-256" OR
       NOT method_cg EQUAL 1 OR NOT disposition_converged EQUAL 1 OR
       NOT disposition_initially_satisfied EQUAL 2 OR
       NOT disposition_iteration_limit EQUAL 3 OR
       NOT disposition_singular_direction EQUAL 4 OR
       NOT disposition_numeric_failure EQUAL 5 OR
       NOT flag_require_psd_operator EQUAL 1 OR
       NOT flag_record_iteration_receipts EQUAL 2)
        message(FATAL_ERROR "Unsupported cognition-solver contract")
    endif()
    set(LAPLACE_COGNITION_SOLVER_VERSION "${version}")
    foreach(domain IN ITEMS program input iteration output receipt)
        string(TOUPPER "${domain}" symbol)
        set(LAPLACE_COGNITION_SOLVER_DOMAIN_${symbol} "${domain_${domain}}")
    endforeach()
    set(LAPLACE_COGNITION_SOLVER_METHOD_CG "${method_cg}")
    foreach(disposition IN ITEMS converged initially_satisfied iteration_limit singular_direction numeric_failure)
        string(TOUPPER "${disposition}" symbol)
        set(LAPLACE_COGNITION_SOLVER_DISPOSITION_${symbol} "${disposition_${disposition}}")
    endforeach()
    foreach(flag IN ITEMS require_psd_operator record_iteration_receipts)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_COGNITION_SOLVER_FLAG_${symbol} "${flag_${flag}}")
    endforeach()
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cognition_solver.h.in"
        "${output_path}" @ONLY)
endfunction()
