function(laplace_configure_cognition_forward_pass_contract contract_path output_path)
    file(READ "${contract_path}" contract)
    string(JSON schema GET "${contract}" schema)
    string(JSON version GET "${contract}" version)
    string(JSON algorithm GET "${contract}" digest_algorithm)
    foreach(domain IN ITEMS program enumeration execution layer output receipt)
        string(JSON domain_${domain} GET "${contract}" domains ${domain})
    endforeach()
    foreach(disposition IN ITEMS complete layer_limit provider_limit resource_limit no_operation blocked provider_failure)
        string(JSON disposition_${disposition} GET "${contract}" dispositions ${disposition})
    endforeach()
    foreach(flag IN ITEMS require_state_progress require_projected_query_consumption require_receipted_execution)
        string(JSON flag_${flag} GET "${contract}" flags ${flag})
    endforeach()
    if(NOT schema STREQUAL "laplace.cognition-forward-pass/v1" OR
       NOT version EQUAL 1 OR NOT algorithm STREQUAL "BLAKE3-256" OR
       NOT disposition_complete EQUAL 1 OR NOT disposition_layer_limit EQUAL 2 OR
       NOT disposition_provider_limit EQUAL 3 OR NOT disposition_resource_limit EQUAL 4 OR
       NOT disposition_no_operation EQUAL 5 OR NOT disposition_blocked EQUAL 6 OR
       NOT disposition_provider_failure EQUAL 7 OR
       NOT flag_require_state_progress EQUAL 1 OR
       NOT flag_require_projected_query_consumption EQUAL 2 OR
       NOT flag_require_receipted_execution EQUAL 4)
        message(FATAL_ERROR "Unsupported cognition-forward-pass contract")
    endif()
    set(LAPLACE_COGNITION_FORWARD_VERSION "${version}")
    foreach(domain IN ITEMS program enumeration execution layer output receipt)
        string(TOUPPER "${domain}" symbol)
        set(LAPLACE_COGNITION_FORWARD_DOMAIN_${symbol} "${domain_${domain}}")
    endforeach()
    foreach(disposition IN ITEMS complete layer_limit provider_limit resource_limit no_operation blocked provider_failure)
        string(TOUPPER "${disposition}" symbol)
        set(LAPLACE_COGNITION_FORWARD_DISPOSITION_${symbol} "${disposition_${disposition}}")
    endforeach()
    foreach(flag IN ITEMS require_state_progress require_projected_query_consumption require_receipted_execution)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_COGNITION_FORWARD_FLAG_${symbol} "${flag_${flag}}")
    endforeach()
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cognition_forward_pass.h.in"
        "${output_path}" @ONLY)
endfunction()
