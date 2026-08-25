function(laplace_configure_identity_contract contract_path output_path)
    file(READ "${contract_path}" contract_json)

    string(JSON contract_schema GET "${contract_json}" schema)
    string(JSON identity_algorithm GET "${contract_json}" identity algorithm)
    string(JSON identity_bytes GET "${contract_json}" identity bytes)
    string(JSON composite_domain GET "${contract_json}" identity composite_domain_byte)
    string(JSON single_collapses GET "${contract_json}" identity single_child_collapses)
    string(JSON empty_is_error GET "${contract_json}" identity empty_composition_is_error)
    string(JSON unicode_minimum GET "${contract_json}" unicode_position minimum)
    string(JSON unicode_maximum GET "${contract_json}" unicode_position maximum)
    string(JSON unicode_encoding GET "${contract_json}" unicode_position encoding)
    string(JSON unicode_layout GET "${contract_json}" unicode_position layout)
    string(JSON unicode_scalar_equivalence GET
        "${contract_json}" unicode_position scalar_equivalence)
    string(JSON unicode_surrogate_semantics GET
        "${contract_json}" unicode_position surrogate_semantics)
    string(JSON includes_surrogates GET "${contract_json}" unicode_position includes_surrogate_positions)
    string(JSON vector_u0032 GET "${contract_json}" vectors codepoint_u0032)
    string(JSON vector_u0035 GET "${contract_json}" vectors codepoint_u0035)
    string(JSON vector_ud800 GET "${contract_json}" vectors codepoint_ud800)
    string(JSON vector_u0041_witness GET "${contract_json}" vectors codepoint_u0041_witness)
    string(JSON vector_255 GET "${contract_json}" vectors sequence_2_5_5)

    if(NOT contract_schema STREQUAL "laplace.identity-contract/v1")
        message(FATAL_ERROR "Unsupported identity contract schema: ${contract_schema}")
    endif()
    if(NOT identity_algorithm STREQUAL "BLAKE3" OR NOT identity_bytes EQUAL 16)
        message(FATAL_ERROR "Laplace identity must be BLAKE3-128")
    endif()
    if(NOT composite_domain EQUAL 1)
        message(FATAL_ERROR "Composite domain byte must remain 0x01")
    endif()
    if(NOT single_collapses OR NOT empty_is_error)
        message(FATAL_ERROR "Composite collapse and empty-state contracts changed")
    endif()
    if(NOT unicode_minimum EQUAL 0 OR NOT unicode_maximum EQUAL 1114111)
        message(FATAL_ERROR "Unicode position universe must contain 1,114,112 positions")
    endif()
    if(NOT unicode_encoding STREQUAL "Laplace-Unicode-Position-Encoding-v1"
       OR NOT unicode_layout STREQUAL "UTF-8-compatible-variable-width-bit-pattern"
       OR NOT unicode_scalar_equivalence STREQUAL "byte-identical-to-standard-UTF-8"
       OR NOT unicode_surrogate_semantics STREQUAL "position-address-only-not-UTF-8-text"
       OR NOT includes_surrogates)
        message(FATAL_ERROR "Unicode position encoding contract changed")
    endif()

    set(LAPLACE_IDENTITY_BYTES "${identity_bytes}")
    set(LAPLACE_COMPOSITE_DOMAIN_BYTE "${composite_domain}")
    set(LAPLACE_UNICODE_POSITION_MAXIMUM "${unicode_maximum}")
    math(EXPR LAPLACE_UNICODE_POSITION_COUNT "${unicode_maximum} + 1")
    set(LAPLACE_VECTOR_U0032_HEX "${vector_u0032}")
    set(LAPLACE_VECTOR_U0035_HEX "${vector_u0035}")
    set(LAPLACE_VECTOR_UD800_HEX "${vector_ud800}")
    set(LAPLACE_VECTOR_U0041_WITNESS_HEX "${vector_u0041_witness}")
    set(LAPLACE_VECTOR_255_HEX "${vector_255}")
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/identity.h.in"
        "${output_path}"
        @ONLY)
endfunction()
