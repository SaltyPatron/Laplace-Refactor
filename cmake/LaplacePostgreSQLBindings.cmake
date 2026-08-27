function(laplace_configure_postgresql_bindings
    contract_path persistence_contract_path composition_contract_path unicode_postgresql_contract_path
    unicode_root_contract_path
    header_template sql_template control_template
    header_output sql_output control_output)
    file(READ "${contract_path}" contract_json)
    string(JSON contract_schema GET "${contract_json}" schema)
    string(JSON pg_schema GET "${contract_json}" bindings postgresql schema)
    string(JSON pg_module GET "${contract_json}" bindings postgresql module)
    string(JSON identity_calculate_sql GET
        "${contract_json}" bindings postgresql identity_codepoint_batch calculate_sql_name)
    string(JSON identity_calculate_symbol GET
        "${contract_json}" bindings postgresql identity_codepoint_batch calculate_c_symbol)
    string(JSON identity_execute_sql GET
        "${contract_json}" bindings postgresql identity_codepoint_batch execute_sql_name)
    string(JSON identity_execute_symbol GET
        "${contract_json}" bindings postgresql identity_codepoint_batch execute_c_symbol)
    string(JSON trajectory_calculate_sql GET
        "${contract_json}" bindings postgresql trajectory_composition_decode_batch calculate_sql_name)
    string(JSON trajectory_calculate_symbol GET
        "${contract_json}" bindings postgresql trajectory_composition_decode_batch calculate_c_symbol)
    string(JSON trajectory_execute_sql GET
        "${contract_json}" bindings postgresql trajectory_composition_decode_batch execute_sql_name)
    string(JSON trajectory_execute_symbol GET
        "${contract_json}" bindings postgresql trajectory_composition_decode_batch execute_c_symbol)
    string(JSON carrier_encoding GET
        "${contract_json}" bindings postgresql trajectory_composition_decode_batch carrier_encoding)
    file(READ "${persistence_contract_path}" persistence_json)
    string(JSON persistence_schema GET "${persistence_json}" schema)
    string(JSON persistence_deposit_sql GET
        "${persistence_json}" bindings postgresql deposit_sql_name)
    string(JSON persistence_deposit_symbol GET
        "${persistence_json}" bindings postgresql deposit_c_symbol)
    string(JSON persistence_stream_record_type GET
        "${persistence_json}" stream_record_type)
    string(JSON persistence_physicality_composition GET
        "${persistence_json}" semantic_enums physicality_type composition)
    string(JSON persistence_physicality_atomic_point GET
        "${persistence_json}" semantic_enums physicality_type atomic_point)
    string(JSON persistence_vertex_none GET
        "${persistence_json}" semantic_enums vertex_class none)
    string(JSON persistence_vertex_trajectory_carrier GET
        "${persistence_json}" semantic_enums vertex_class trajectory_carrier)
    string(JSON persistence_structural_ordered_composition GET
        "${persistence_json}" semantic_enums structural_form ordered_composition)
    string(JSON persistence_structural_atomic_point GET
        "${persistence_json}" semantic_enums structural_form atomic_point)
    string(JSON persistence_physicality_flags_none GET
        "${persistence_json}" semantic_enums physicality_flags none)
    string(JSON persistence_occurrence_has_physicality GET
        "${persistence_json}" occurrence_flags has_physicality)
    string(JSON persistence_plan_count LENGTH
        "${persistence_json}" bindings postgresql plan_sequence)
    file(READ "${composition_contract_path}" composition_json)
    string(JSON composition_schema GET "${composition_json}" schema)
    string(JSON composition_deposit_sql GET
        "${composition_json}" bindings postgresql deposit_sql_name)
    string(JSON composition_deposit_symbol GET
        "${composition_json}" bindings postgresql deposit_c_symbol)
    file(READ "${unicode_postgresql_contract_path}" unicode_postgresql_json)
    file(READ "${unicode_root_contract_path}" unicode_root_json)
    string(JSON unicode_postgresql_schema GET
        "${unicode_postgresql_json}" schema)
    string(JSON unicode_postgresql_contract_fingerprint GET
        "${unicode_postgresql_json}" contract_fingerprint value)
    string(JSON unicode_postgresql_sql GET
        "${unicode_postgresql_json}" binding sql_name)
    string(JSON unicode_postgresql_symbol GET
        "${unicode_postgresql_json}" binding c_symbol)
    string(JSON unicode_tier0_access_sql GET
        "${unicode_postgresql_json}" access_bindings unicode_tier0 sql_name)
    string(JSON unicode_tier0_access_symbol GET
        "${unicode_postgresql_json}" access_bindings unicode_tier0 c_symbol)
    string(JSON unicode_reverse_access_sql GET
        "${unicode_postgresql_json}" access_bindings unicode_identity_reverse sql_name)
    string(JSON unicode_reverse_access_symbol GET
        "${unicode_postgresql_json}" access_bindings unicode_identity_reverse c_symbol)
    string(JSON unicode_access_maximum_batch_items GET
        "${unicode_postgresql_json}" access_bindings maximum_batch_items)
    string(JSON unicode_postgresql_record_type GET
        "${unicode_postgresql_json}" stream record_type)
    string(JSON unicode_postgresql_stream_byte_multiplier GET
        "${unicode_postgresql_json}" stream working_memory_estimate
        stream_byte_multiplier)
    string(JSON unicode_postgresql_per_frame_overhead_bytes GET
        "${unicode_postgresql_json}" stream working_memory_estimate
        per_frame_overhead_bytes)
    string(JSON unicode_postgresql_plan_count LENGTH
        "${unicode_postgresql_json}" plans)
    string(JSON unicode_root_schema GET "${unicode_root_json}" schema)
    string(JSON unicode_root_record_type GET
        "${unicode_root_json}" record_type)
    string(JSON unicode_root_atom_population GET
        "${unicode_root_json}" frame_kinds 0 count)
    string(JSON unicode_root_ducet_population GET
        "${unicode_root_json}" frame_kinds 1 count)
    string(JSON unicode_root_manifest_count GET
        "${unicode_root_json}" frame_kinds 4 count)
    string(JSON unicode_root_manifest_bytes GET
        "${unicode_root_json}" payload_contracts root-manifest-v2 record_bytes)
    foreach(plan_name
        generation_insert generation_verify
        entity_insert entity_verify
        physicality_insert physicality_verify
        atom_insert atom_verify
        ducet_position_insert ducet_position_verify
        ducet_contraction_insert ducet_contraction_verify
        normalization_insert normalization_verify
        effect_verify
        deposit_receipt_insert deposit_receipt_verify)
        string(JSON unicode_postgresql_plan_${plan_name} GET
            "${unicode_postgresql_json}" plans ${plan_name})
    endforeach()

    if(NOT contract_schema STREQUAL "laplace.isa-contract/v1"
       OR NOT persistence_schema STREQUAL "laplace.persistence-contract/v1"
       OR NOT composition_schema STREQUAL "laplace.composition-contract/v1"
       OR NOT unicode_postgresql_schema STREQUAL
            "laplace.unicode-postgresql-contract/v1"
       OR NOT unicode_root_schema STREQUAL
            "laplace.unicode-root-stream-contract/v2")
        message(FATAL_ERROR "PostgreSQL bindings require the ISA v1 contract")
    endif()
    foreach(identifier
        pg_schema pg_module
        identity_calculate_sql identity_calculate_symbol
        identity_execute_sql identity_execute_symbol
        trajectory_calculate_sql trajectory_calculate_symbol
        trajectory_execute_sql trajectory_execute_symbol
        persistence_deposit_sql persistence_deposit_symbol
        composition_deposit_sql composition_deposit_symbol
        unicode_postgresql_sql unicode_postgresql_symbol
        unicode_tier0_access_sql unicode_tier0_access_symbol
        unicode_reverse_access_sql unicode_reverse_access_symbol)
        if(NOT "${${identifier}}" MATCHES "^[a-z][a-z0-9_]*$")
            message(FATAL_ERROR "Invalid PostgreSQL binding identifier: ${identifier}")
        endif()
    endforeach()
    string(LENGTH "${unicode_postgresql_contract_fingerprint}"
        unicode_postgresql_contract_fingerprint_length)
    if(NOT unicode_postgresql_contract_fingerprint_length EQUAL 64
       OR NOT unicode_postgresql_contract_fingerprint MATCHES "^[0-9a-f]+$")
        message(FATAL_ERROR
            "The Unicode PostgreSQL sink contract fingerprint is invalid")
    endif()
    if(NOT carrier_encoding STREQUAL "four-little-endian-binary64-bit-patterns")
        message(FATAL_ERROR "The PostgreSQL trajectory carrier encoding is not declared")
    endif()
    if(NOT unicode_postgresql_record_type EQUAL unicode_root_record_type
       OR NOT unicode_root_atom_population EQUAL unicode_root_ducet_population
       OR NOT unicode_root_manifest_count EQUAL 1)
        message(FATAL_ERROR
            "The Unicode PostgreSQL and canonical-root population contracts diverge")
    endif()
    math(EXPR unicode_root_minimum_frame_count
        "${unicode_root_atom_population} + ${unicode_root_ducet_population} + ${unicode_root_manifest_count}")

    set(LAPLACE_PG_SCHEMA "${pg_schema}")
    set(LAPLACE_PG_MODULE "${pg_module}")
    set(LAPLACE_PG_IDENTITY_CALCULATE_SQL "${identity_calculate_sql}")
    set(LAPLACE_PG_IDENTITY_CALCULATE_SYMBOL "${identity_calculate_symbol}")
    set(LAPLACE_PG_IDENTITY_EXECUTE_SQL "${identity_execute_sql}")
    set(LAPLACE_PG_IDENTITY_EXECUTE_SYMBOL "${identity_execute_symbol}")
    set(LAPLACE_PG_TRAJECTORY_CALCULATE_SQL "${trajectory_calculate_sql}")
    set(LAPLACE_PG_TRAJECTORY_CALCULATE_SYMBOL "${trajectory_calculate_symbol}")
    set(LAPLACE_PG_TRAJECTORY_EXECUTE_SQL "${trajectory_execute_sql}")
    set(LAPLACE_PG_TRAJECTORY_EXECUTE_SYMBOL "${trajectory_execute_symbol}")
    set(LAPLACE_PG_CARRIER_ENCODING "${carrier_encoding}")
    set(LAPLACE_PG_PERSISTENCE_DEPOSIT_SQL "${persistence_deposit_sql}")
    set(LAPLACE_PG_PERSISTENCE_DEPOSIT_SYMBOL "${persistence_deposit_symbol}")
    set(LAPLACE_PG_PERSISTENCE_STREAM_RECORD_TYPE "${persistence_stream_record_type}")
    set(LAPLACE_PG_PERSISTENCE_PHYSICALITY_COMPOSITION "${persistence_physicality_composition}")
    set(LAPLACE_PG_PERSISTENCE_PHYSICALITY_ATOMIC_POINT "${persistence_physicality_atomic_point}")
    set(LAPLACE_PG_PERSISTENCE_VERTEX_NONE "${persistence_vertex_none}")
    set(LAPLACE_PG_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER "${persistence_vertex_trajectory_carrier}")
    set(LAPLACE_PG_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION "${persistence_structural_ordered_composition}")
    set(LAPLACE_PG_PERSISTENCE_STRUCTURAL_ATOMIC_POINT "${persistence_structural_atomic_point}")
    set(LAPLACE_PG_PERSISTENCE_PHYSICALITY_FLAGS_NONE "${persistence_physicality_flags_none}")
    set(LAPLACE_PG_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY "${persistence_occurrence_has_physicality}")
    set(LAPLACE_PG_PERSISTENCE_PLAN_COUNT "${persistence_plan_count}")
    set(LAPLACE_PG_COMPOSITION_DEPOSIT_SQL "${composition_deposit_sql}")
    set(LAPLACE_PG_COMPOSITION_DEPOSIT_SYMBOL "${composition_deposit_symbol}")
    set(LAPLACE_PG_UNICODE_ROOT_SQL "${unicode_postgresql_sql}")
    set(LAPLACE_PG_UNICODE_ROOT_SYMBOL "${unicode_postgresql_symbol}")
    set(LAPLACE_PG_UNICODE_TIER0_ACCESS_SQL "${unicode_tier0_access_sql}")
    set(LAPLACE_PG_UNICODE_TIER0_ACCESS_SYMBOL "${unicode_tier0_access_symbol}")
    set(LAPLACE_PG_UNICODE_REVERSE_ACCESS_SQL "${unicode_reverse_access_sql}")
    set(LAPLACE_PG_UNICODE_REVERSE_ACCESS_SYMBOL "${unicode_reverse_access_symbol}")
    set(LAPLACE_PG_UNICODE_ACCESS_MAXIMUM_BATCH_ITEMS
        "${unicode_access_maximum_batch_items}")
    set(LAPLACE_PG_UNICODE_ROOT_RECORD_TYPE
        "${unicode_postgresql_record_type}")
    set(LAPLACE_PG_UNICODE_ROOT_CONTRACT_FINGERPRINT_HEX
        "${unicode_postgresql_contract_fingerprint}")
    set(LAPLACE_PG_UNICODE_ROOT_PLAN_COUNT
        "${unicode_postgresql_plan_count}")
    set(LAPLACE_PG_UNICODE_ROOT_STREAM_BYTE_MULTIPLIER
        "${unicode_postgresql_stream_byte_multiplier}")
    set(LAPLACE_PG_UNICODE_ROOT_PER_FRAME_OVERHEAD_BYTES
        "${unicode_postgresql_per_frame_overhead_bytes}")
    set(LAPLACE_PG_UNICODE_ROOT_POPULATION
        "${unicode_root_atom_population}")
    set(LAPLACE_PG_UNICODE_ROOT_POPULATION
        "${unicode_root_atom_population}" CACHE INTERNAL
        "Generated Unicode root population used by PostgreSQL acceptance" FORCE)
    math(EXPR LAPLACE_PG_UNICODE_ROOT_MAX_POSITION
        "${unicode_root_atom_population} - 1")
    set(LAPLACE_PG_UNICODE_ROOT_MINIMUM_FRAME_COUNT
        "${unicode_root_minimum_frame_count}")
    set(LAPLACE_PG_UNICODE_ROOT_MANIFEST_BYTES
        "${unicode_root_manifest_bytes}")
    foreach(plan_name
        generation_insert generation_verify
        entity_insert entity_verify
        physicality_insert physicality_verify
        atom_insert atom_verify
        ducet_position_insert ducet_position_verify
        ducet_contraction_insert ducet_contraction_verify
        normalization_insert normalization_verify
        effect_verify
        deposit_receipt_insert deposit_receipt_verify)
        string(TOUPPER "${plan_name}" plan_macro)
        set("LAPLACE_PG_UNICODE_ROOT_PLAN_${plan_macro}"
            "${unicode_postgresql_plan_${plan_name}}")
    endforeach()
    set(LAPLACE_EXTENSION_VERSION "${PROJECT_VERSION}")

    foreach(output_path "${header_output}" "${sql_output}" "${control_output}")
        get_filename_component(output_directory "${output_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${output_directory}")
    endforeach()
    configure_file("${header_template}" "${header_output}" @ONLY)
    configure_file("${sql_template}" "${sql_output}" @ONLY)
    configure_file("${control_template}" "${control_output}" @ONLY)
endfunction()
