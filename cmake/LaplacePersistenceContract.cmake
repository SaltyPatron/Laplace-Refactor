function(laplace_configure_persistence_contract contract_path output_path)
    file(READ "${contract_path}" contract_json)
    string(JSON schema GET "${contract_json}" schema)
    string(JSON byte_order GET "${contract_json}" frame byte_order)
    string(JSON batch_boundary GET "${contract_json}" frame batch_boundary)
    string(JSON digest_algorithm GET "${contract_json}" identities digest_algorithm)
    if(NOT schema STREQUAL "laplace.persistence-contract/v1"
       OR NOT byte_order STREQUAL "little-endian"
       OR NOT batch_boundary STREQUAL "between-complete-frames"
       OR NOT digest_algorithm STREQUAL "BLAKE3-256")
        message(FATAL_ERROR "Persistence contract v1 has an unsupported framing or digest law")
    endif()

    string(JSON LAPLACE_PERSISTENCE_STREAM_RECORD_TYPE GET "${contract_json}" stream_record_type)
    string(JSON LAPLACE_PERSISTENCE_FRAME_VERSION GET "${contract_json}" frame version)
    string(JSON LAPLACE_PERSISTENCE_FRAME_HEADER_BYTES GET "${contract_json}" frame header_bytes)
    string(JSON LAPLACE_PERSISTENCE_RECORD_ENTITY GET "${contract_json}" record_kinds entity)
    string(JSON LAPLACE_PERSISTENCE_RECORD_PHYSICALITY GET "${contract_json}" record_kinds physicality)
    string(JSON LAPLACE_PERSISTENCE_RECORD_TRAJECTORY_VERTEX GET "${contract_json}" record_kinds trajectory_vertex)
    string(JSON LAPLACE_PERSISTENCE_RECORD_OBSERVED_OCCURRENCE GET "${contract_json}" record_kinds observed_occurrence)
    string(JSON LAPLACE_PERSISTENCE_ENTITY_PAYLOAD_BYTES GET "${contract_json}" payload_bytes entity)
    string(JSON LAPLACE_PERSISTENCE_PHYSICALITY_PAYLOAD_BYTES GET "${contract_json}" payload_bytes physicality)
    string(JSON LAPLACE_PERSISTENCE_TRAJECTORY_VERTEX_PAYLOAD_BYTES GET "${contract_json}" payload_bytes trajectory_vertex)
    string(JSON LAPLACE_PERSISTENCE_OBSERVED_OCCURRENCE_PAYLOAD_BYTES GET "${contract_json}" payload_bytes observed_occurrence)
    string(JSON LAPLACE_PERSISTENCE_CONTENT_ID_BYTES GET "${contract_json}" identities content_bytes)
    string(JSON LAPLACE_PERSISTENCE_CONTENT_WITNESS_BYTES GET "${contract_json}" identities content_witness_bytes)
    string(JSON LAPLACE_PERSISTENCE_RECORD_ID_BYTES GET "${contract_json}" identities record_bytes)
    string(JSON LAPLACE_PERSISTENCE_PHYSICALITY_DOMAIN GET "${contract_json}" identities physicality_domain)
    string(JSON LAPLACE_PERSISTENCE_TRAJECTORY_DOMAIN GET "${contract_json}" identities trajectory_domain)
    string(JSON LAPLACE_PERSISTENCE_OCCURRENCE_DOMAIN GET "${contract_json}" identities occurrence_domain)
    string(JSON LAPLACE_PERSISTENCE_OCCURRENCE_HAS_PHYSICALITY GET "${contract_json}" occurrence_flags has_physicality)
    string(JSON LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION GET "${contract_json}" semantic_enums physicality_type composition)
    string(JSON LAPLACE_PERSISTENCE_PHYSICALITY_ATOMIC_POINT GET "${contract_json}" semantic_enums physicality_type atomic_point)
    string(JSON LAPLACE_PERSISTENCE_VERTEX_NONE GET "${contract_json}" semantic_enums vertex_class none)
    string(JSON LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER GET "${contract_json}" semantic_enums vertex_class trajectory_carrier)
    string(JSON LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION GET "${contract_json}" semantic_enums structural_form ordered_composition)
    string(JSON LAPLACE_PERSISTENCE_STRUCTURAL_ATOMIC_POINT GET "${contract_json}" semantic_enums structural_form atomic_point)
    string(JSON LAPLACE_PERSISTENCE_PHYSICALITY_FLAGS_NONE GET "${contract_json}" semantic_enums physicality_flags none)
    string(JSON LAPLACE_PERSISTENCE_PG_PLAN_FINGERPRINT_DOMAIN GET
        "${contract_json}" bindings postgresql plan_fingerprint_domain)
    string(JSON LAPLACE_PERSISTENCE_PG_PLAN_MANIFEST GET
        "${contract_json}" bindings postgresql plan_manifest)
    string(JSON LAPLACE_PERSISTENCE_PG_STREAM_BYTE_MULTIPLIER GET
        "${contract_json}" bindings postgresql working_memory_estimate stream_byte_multiplier)
    string(JSON LAPLACE_PERSISTENCE_PG_PER_RECORD_OVERHEAD_BYTES GET
        "${contract_json}" bindings postgresql working_memory_estimate per_record_overhead_bytes)
    foreach(plan_name IN ITEMS
        REFERENCE_PREFLIGHT ENTITY_INSERT ENTITY_VERIFY
        PHYSICALITY_INSERT PHYSICALITY_VERIFY TRAJECTORY_INSERT TRAJECTORY_VERIFY
        OCCURRENCE_INSERT OCCURRENCE_VERIFY RECEIPT_INSERT RECEIPT_VERIFY)
        string(TOLOWER "${plan_name}" plan_key)
        string(JSON LAPLACE_PERSISTENCE_PG_PLAN_${plan_name} GET
            "${contract_json}" bindings postgresql plan_sequence "${plan_key}")
    endforeach()
    set(LAPLACE_PERSISTENCE_PG_PLAN_COUNT 11)

    if(NOT LAPLACE_PERSISTENCE_FRAME_HEADER_BYTES EQUAL 8
       OR NOT LAPLACE_PERSISTENCE_CONTENT_ID_BYTES EQUAL 16
       OR NOT LAPLACE_PERSISTENCE_CONTENT_WITNESS_BYTES EQUAL 32
       OR NOT LAPLACE_PERSISTENCE_RECORD_ID_BYTES EQUAL 32
       OR LAPLACE_PERSISTENCE_PG_STREAM_BYTE_MULTIPLIER LESS 1
       OR LAPLACE_PERSISTENCE_PG_PER_RECORD_OVERHEAD_BYTES LESS 1)
        message(FATAL_ERROR "Persistence contract v1 changes a fixed ABI width")
    endif()
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/persistence.h.in"
        "${output_path}" @ONLY)
endfunction()
