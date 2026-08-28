function(laplace_configure_reference_topology_contract contract_path output_path)
    file(READ "${contract_path}" contract)
    string(JSON schema GET "${contract}" schema)
    string(JSON version GET "${contract}" version)
    string(JSON algorithm GET "${contract}" digest_algorithm)
    foreach(domain IN ITEMS identity occurrence input output receipt)
        string(JSON ${domain}_domain GET "${contract}" ${domain}_domain)
    endforeach()
    foreach(flag IN ITEMS endpoint present_declaration retired_declaration)
        string(JSON flag_${flag} GET "${contract}" rule_flags ${flag})
    endforeach()
    foreach(disposition IN ITEMS present retired unresolved)
        string(JSON disposition_${disposition} GET
            "${contract}" dispositions ${disposition})
    endforeach()
    if(NOT schema STREQUAL "laplace.reference-topology/v1" OR
       NOT version EQUAL 1 OR NOT algorithm STREQUAL "BLAKE3-256" OR
       NOT flag_endpoint EQUAL 1 OR
       NOT flag_present_declaration EQUAL 2 OR
       NOT flag_retired_declaration EQUAL 4 OR
       NOT disposition_present EQUAL 1 OR
       NOT disposition_retired EQUAL 2 OR
       NOT disposition_unresolved EQUAL 6)
        message(FATAL_ERROR "Unsupported reference-topology contract")
    endif()
    set(LAPLACE_REFERENCE_TOPOLOGY_VERSION "${version}")
    set(LAPLACE_REFERENCE_TOPOLOGY_IDENTITY_DOMAIN "${identity_domain}")
    set(LAPLACE_REFERENCE_TOPOLOGY_OCCURRENCE_DOMAIN "${occurrence_domain}")
    set(LAPLACE_REFERENCE_TOPOLOGY_INPUT_DOMAIN "${input_domain}")
    set(LAPLACE_REFERENCE_TOPOLOGY_OUTPUT_DOMAIN "${output_domain}")
    set(LAPLACE_REFERENCE_TOPOLOGY_RECEIPT_DOMAIN "${receipt_domain}")
    foreach(flag IN ITEMS endpoint present_declaration retired_declaration)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_REFERENCE_RULE_${symbol} "${flag_${flag}}")
    endforeach()
    foreach(disposition IN ITEMS present retired unresolved)
        string(TOUPPER "${disposition}" symbol)
        set(LAPLACE_REFERENCE_DISPOSITION_${symbol}
            "${disposition_${disposition}}")
    endforeach()
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/reference_topology.h.in"
        "${output_path}" @ONLY)
endfunction()
