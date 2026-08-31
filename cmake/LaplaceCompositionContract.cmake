function(laplace_configure_composition_contract contract_path output_path)
    file(READ "${contract_path}" contract_json)
    string(JSON schema GET "${contract_json}" schema)
    string(JSON LAPLACE_COMPOSITION_ABI_MAJOR GET "${contract_json}" abi major)
    string(JSON LAPLACE_COMPOSITION_ABI_MINOR GET "${contract_json}" abi minor)
    string(JSON LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY GET
        "${contract_json}" references known_entity)
    string(JSON LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT GET
        "${contract_json}" references prior_result)
    string(JSON topological_required GET
        "${contract_json}" references topological_order_required)
    string(JSON LAPLACE_COMPOSITION_TIER_MINIMUM GET
        "${contract_json}" tier minimum)
    string(JSON LAPLACE_COMPOSITION_TIER_MAXIMUM GET
        "${contract_json}" tier maximum)
    string(JSON tier_identity_component GET
        "${contract_json}" tier identity_component)
    string(JSON LAPLACE_COMPOSITION_MAXIMUM_RUN_PER_CARRIER GET
        "${contract_json}" trajectory maximum_run_per_carrier)
    string(JSON LAPLACE_COMPOSITION_STREAM_RECORD_TYPE GET
        "${contract_json}" producer record_type)
    string(JSON LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE GET
        "${contract_json}" occurrence_emission request_flag)
    string(JSON LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI GET
        "${contract_json}" presence provider_abi)
    string(JSON LAPLACE_COMPOSITION_PRESENCE_SEMANTIC_RECEIPT_DOMAIN GET
        "${contract_json}" presence semantic_receipt_domain)
    string(JSON LAPLACE_COMPOSITION_PRESENCE_EXECUTION_RECEIPT_DOMAIN GET
        "${contract_json}" presence execution_receipt_domain)
    string(JSON batch_boundary GET
        "${contract_json}" producer batch_boundary)

    if(NOT schema STREQUAL "laplace.composition-contract/v1"
       OR NOT LAPLACE_COMPOSITION_ABI_MAJOR EQUAL 1
       OR NOT topological_required
       OR tier_identity_component
       OR NOT LAPLACE_COMPOSITION_TIER_MINIMUM EQUAL 0
       OR NOT LAPLACE_COMPOSITION_TIER_MAXIMUM EQUAL 31
       OR NOT LAPLACE_COMPOSITION_MAXIMUM_RUN_PER_CARRIER EQUAL 65535
       OR NOT LAPLACE_COMPOSITION_REQUEST_EMIT_OCCURRENCE EQUAL 1
       OR NOT LAPLACE_COMPOSITION_PRESENCE_PROVIDER_ABI EQUAL 1
       OR NOT LAPLACE_COMPOSITION_PRESENCE_SEMANTIC_RECEIPT_DOMAIN STREQUAL
            "laplace-composition-presence-semantic-receipt-v1"
       OR NOT LAPLACE_COMPOSITION_PRESENCE_EXECUTION_RECEIPT_DOMAIN STREQUAL
            "laplace-composition-presence-execution-receipt-v1"
       OR NOT batch_boundary STREQUAL "between-complete-persistence-frames")
        message(FATAL_ERROR "Composition contract v1 changed a fixed semantic boundary")
    endif()

    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/composition.h.in"
        "${output_path}" @ONLY)
endfunction()
