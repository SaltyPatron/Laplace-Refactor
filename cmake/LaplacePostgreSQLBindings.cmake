function(laplace_configure_postgresql_bindings
    contract_path persistence_contract_path header_template sql_template control_template
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
    string(JSON persistence_vertex_trajectory_carrier GET
        "${persistence_json}" semantic_enums vertex_class trajectory_carrier)
    string(JSON persistence_structural_ordered_composition GET
        "${persistence_json}" semantic_enums structural_form ordered_composition)
    string(JSON persistence_physicality_flags_none GET
        "${persistence_json}" semantic_enums physicality_flags none)
    string(JSON persistence_occurrence_has_physicality GET
        "${persistence_json}" occurrence_flags has_physicality)
    string(JSON persistence_plan_count LENGTH
        "${persistence_json}" bindings postgresql plan_sequence)

    if(NOT contract_schema STREQUAL "laplace.isa-contract/v1"
       OR NOT persistence_schema STREQUAL "laplace.persistence-contract/v1")
        message(FATAL_ERROR "PostgreSQL bindings require the ISA v1 contract")
    endif()
    foreach(identifier
        pg_schema pg_module
        identity_calculate_sql identity_calculate_symbol
        identity_execute_sql identity_execute_symbol
        trajectory_calculate_sql trajectory_calculate_symbol
        trajectory_execute_sql trajectory_execute_symbol
        persistence_deposit_sql persistence_deposit_symbol)
        if(NOT "${${identifier}}" MATCHES "^[a-z][a-z0-9_]*$")
            message(FATAL_ERROR "Invalid PostgreSQL binding identifier: ${identifier}")
        endif()
    endforeach()
    if(NOT carrier_encoding STREQUAL "four-little-endian-binary64-bit-patterns")
        message(FATAL_ERROR "The PostgreSQL trajectory carrier encoding is not declared")
    endif()

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
    set(LAPLACE_PG_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER "${persistence_vertex_trajectory_carrier}")
    set(LAPLACE_PG_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION "${persistence_structural_ordered_composition}")
    set(LAPLACE_PG_PERSISTENCE_PHYSICALITY_FLAGS_NONE "${persistence_physicality_flags_none}")
    set(LAPLACE_PG_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY "${persistence_occurrence_has_physicality}")
    set(LAPLACE_PG_PERSISTENCE_PLAN_COUNT "${persistence_plan_count}")
    set(LAPLACE_EXTENSION_VERSION "${PROJECT_VERSION}")

    foreach(output_path "${header_output}" "${sql_output}" "${control_output}")
        get_filename_component(output_directory "${output_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${output_directory}")
    endforeach()
    configure_file("${header_template}" "${header_output}" @ONLY)
    configure_file("${sql_template}" "${sql_output}" @ONLY)
    configure_file("${control_template}" "${control_output}" @ONLY)
endfunction()
