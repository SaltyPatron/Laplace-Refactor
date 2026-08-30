function(laplace_configure_isa_contract contract_path output_path)
    file(READ "${contract_path}" contract_json)

    string(JSON contract_schema GET "${contract_json}" schema)
    string(JSON major GET "${contract_json}" version major)
    string(JSON minor GET "${contract_json}" version minor)
    string(JSON context_required GET "${contract_json}" execution_context required)
    string(JSON context_framework_major GET
        "${contract_json}" execution_context framework_major)
    string(JSON context_program_binding GET
        "${contract_json}" execution_context bound_to_program_fingerprint)
    string(JSON context_receipt_binding GET
        "${contract_json}" execution_context bound_to_receipt)
    string(JSON value_u32 GET "${contract_json}" value_types u32_vector)
    string(JSON value_id128 GET "${contract_json}" value_types id128_vector)
    string(JSON value_trajectory GET
        "${contract_json}" value_types composition_trajectory_vector)
    string(JSON value_occurrence GET
        "${contract_json}" value_types composition_occurrence_vector)
    string(JSON value_highway_key GET
        "${contract_json}" value_types highway_key_vector)
    string(JSON value_highway_coordinate GET
        "${contract_json}" value_types highway_coordinate_vector)
    string(JSON value_highway_registry_receipt GET
        "${contract_json}" value_types highway_registry_receipt_vector)
    string(JSON value_evidence_lineage_record GET
        "${contract_json}" value_types evidence_lineage_record_vector)
    string(JSON value_evidence_root_record GET
        "${contract_json}" value_types evidence_root_record_vector)
    string(JSON value_evidence_testimony_record GET
        "${contract_json}" value_types evidence_testimony_record_vector)
    string(JSON value_evidence_testimony_receipt GET
        "${contract_json}" value_types evidence_testimony_receipt_vector)
    string(JSON value_source_profile_manifest GET
        "${contract_json}" value_types source_profile_manifest_vector)
    string(JSON value_source_profile_receipt GET
        "${contract_json}" value_types source_profile_receipt_vector)
    string(JSON value_world_admission_record GET
        "${contract_json}" value_types world_admission_record_vector)
    string(JSON value_world_admission_receipt GET
        "${contract_json}" value_types world_admission_receipt_vector)
    string(JSON value_reference_candidate GET
        "${contract_json}" value_types reference_candidate_vector)
    string(JSON value_reference_record GET
        "${contract_json}" value_types reference_record_vector)
    string(JSON value_reference_mapping_candidate GET
        "${contract_json}" value_types reference_mapping_candidate_vector)
    string(JSON value_reference_mapping_record GET
        "${contract_json}" value_types reference_mapping_record_vector)
    string(JSON opcode_identity_codepoint GET
        "${contract_json}" opcodes identity_codepoint_batch)
    string(JSON opcode_trajectory_decode GET
        "${contract_json}" opcodes trajectory_composition_decode_batch)
    string(JSON opcode_highway_coordinate GET
        "${contract_json}" opcodes highway_coordinate_calculate_batch)
    string(JSON opcode_highway_registry_materialize GET
        "${contract_json}" opcodes highway_registry_materialize_batch)
    string(JSON opcode_evidence_record_lineage GET
        "${contract_json}" opcodes evidence_record_lineage_batch)
    string(JSON opcode_evidence_record_testimony GET
        "${contract_json}" opcodes evidence_record_testimony_batch)
    string(JSON opcode_source_profile_validate GET
        "${contract_json}" opcodes source_profile_validate_batch)
    string(JSON opcode_world_admission_close GET
        "${contract_json}" opcodes world_admission_close_batch)
    string(JSON opcode_reference_topology_resolve GET
        "${contract_json}" opcodes reference_topology_resolve_batch)
    string(JSON opcode_reference_mapping_resolve GET
        "${contract_json}" opcodes reference_mapping_resolve_batch)
    string(JSON opcode_cognition_solve_packet GET
        "${contract_json}" opcodes cognition_solve_packet)
    string(JSON instruction_version_identity_codepoint GET
        "${contract_json}" instruction_versions identity_codepoint_batch)
    string(JSON instruction_version_trajectory_decode GET
        "${contract_json}" instruction_versions trajectory_composition_decode_batch)
    string(JSON instruction_version_highway_coordinate GET
        "${contract_json}" instruction_versions highway_coordinate_calculate_batch)
    string(JSON instruction_version_highway_registry_materialize GET
        "${contract_json}" instruction_versions highway_registry_materialize_batch)
    string(JSON instruction_version_evidence_record_lineage GET
        "${contract_json}" instruction_versions evidence_record_lineage_batch)
    string(JSON instruction_version_evidence_record_testimony GET
        "${contract_json}" instruction_versions evidence_record_testimony_batch)
    string(JSON instruction_version_source_profile_validate GET
        "${contract_json}" instruction_versions source_profile_validate_batch)
    string(JSON instruction_version_world_admission_close GET
        "${contract_json}" instruction_versions world_admission_close_batch)
    string(JSON instruction_version_reference_topology_resolve GET
        "${contract_json}" instruction_versions reference_topology_resolve_batch)
    string(JSON instruction_version_reference_mapping_resolve GET
        "${contract_json}" instruction_versions reference_mapping_resolve_batch)
    string(JSON instruction_version_cognition_solve_packet GET
        "${contract_json}" instruction_versions cognition_solve_packet)
    string(JSON introduced_minor_identity GET
        "${contract_json}" introduced_minor identity_codepoint_batch)
    string(JSON introduced_minor_trajectory_decode GET
        "${contract_json}" introduced_minor trajectory_composition_decode_batch)
    string(JSON introduced_minor_highway_coordinate GET
        "${contract_json}" introduced_minor highway_coordinate_calculate_batch)
    string(JSON introduced_minor_highway_registry_materialize GET
        "${contract_json}" introduced_minor highway_registry_materialize_batch)
    string(JSON introduced_minor_evidence_record_lineage GET
        "${contract_json}" introduced_minor evidence_record_lineage_batch)
    string(JSON introduced_minor_evidence_record_testimony GET
        "${contract_json}" introduced_minor evidence_record_testimony_batch)
    string(JSON introduced_minor_source_profile_validate GET
        "${contract_json}" introduced_minor source_profile_validate_batch)
    string(JSON introduced_minor_world_admission_close GET
        "${contract_json}" introduced_minor world_admission_close_batch)
    string(JSON introduced_minor_reference_topology_resolve GET
        "${contract_json}" introduced_minor reference_topology_resolve_batch)
    string(JSON introduced_minor_reference_mapping_resolve GET
        "${contract_json}" introduced_minor reference_mapping_resolve_batch)
    string(JSON introduced_minor_cognition_solve_packet GET
        "${contract_json}" introduced_minor cognition_solve_packet)
    string(JSON receipt_algorithm GET "${contract_json}" receipt digest_algorithm)
    string(JSON receipt_bytes GET "${contract_json}" receipt digest_bytes)
    string(JSON receipt_detail_full GET "${contract_json}" receipt detail_full)
    string(JSON program_flags GET "${contract_json}" known_program_flags)
    string(JSON instruction_flags GET "${contract_json}" known_instruction_flags)
    string(JSON value_flags GET "${contract_json}" known_value_flags)
    string(JSON operation_count LENGTH "${contract_json}" operation_contracts)

    if(NOT contract_schema STREQUAL "laplace.isa-contract/v1")
        message(FATAL_ERROR "Unsupported ISA contract schema: ${contract_schema}")
    endif()
    if(NOT major EQUAL 1 OR NOT minor EQUAL 11)
        message(FATAL_ERROR "Current ISA version must remain 1.11")
    endif()
    if(NOT context_required OR NOT context_framework_major EQUAL 1
       OR NOT context_program_binding OR NOT context_receipt_binding)
        message(FATAL_ERROR "ISA execution context binding contract changed")
    endif()
    if(NOT value_u32 EQUAL 1 OR NOT value_id128 EQUAL 2
       OR NOT value_trajectory EQUAL 3 OR NOT value_occurrence EQUAL 4
       OR NOT value_highway_key EQUAL 5 OR NOT value_highway_coordinate EQUAL 6
       OR NOT value_highway_registry_receipt EQUAL 7
       OR NOT value_evidence_lineage_record EQUAL 8
       OR NOT value_evidence_root_record EQUAL 9
       OR NOT value_evidence_testimony_record EQUAL 10
       OR NOT value_evidence_testimony_receipt EQUAL 11
       OR NOT value_source_profile_manifest EQUAL 12
       OR NOT value_source_profile_receipt EQUAL 13
       OR NOT value_world_admission_record EQUAL 14
       OR NOT value_world_admission_receipt EQUAL 15
       OR NOT value_reference_candidate EQUAL 16
       OR NOT value_reference_record EQUAL 17
       OR NOT value_reference_mapping_candidate EQUAL 18
       OR NOT value_reference_mapping_record EQUAL 19)
        message(FATAL_ERROR "ISA value type assignments changed")
    endif()
    if(NOT opcode_identity_codepoint EQUAL 131073
       OR NOT opcode_trajectory_decode EQUAL 196609
       OR NOT opcode_highway_coordinate EQUAL 262145
       OR NOT opcode_highway_registry_materialize EQUAL 262146
       OR NOT opcode_evidence_record_lineage EQUAL 327681
       OR NOT opcode_evidence_record_testimony EQUAL 327682
       OR NOT opcode_source_profile_validate EQUAL 393217
       OR NOT opcode_world_admission_close EQUAL 393218
       OR NOT opcode_reference_topology_resolve EQUAL 393219
       OR NOT opcode_reference_mapping_resolve EQUAL 393220
       OR NOT opcode_cognition_solve_packet EQUAL 458753
       OR NOT instruction_version_identity_codepoint EQUAL 1
       OR NOT instruction_version_trajectory_decode EQUAL 1
       OR NOT instruction_version_highway_coordinate EQUAL 1
       OR NOT instruction_version_highway_registry_materialize EQUAL 1
       OR NOT instruction_version_evidence_record_lineage EQUAL 1
       OR NOT instruction_version_evidence_record_testimony EQUAL 1
       OR NOT instruction_version_source_profile_validate EQUAL 1
       OR NOT instruction_version_world_admission_close EQUAL 1
       OR NOT instruction_version_reference_topology_resolve EQUAL 1
       OR NOT instruction_version_reference_mapping_resolve EQUAL 1
       OR NOT instruction_version_cognition_solve_packet EQUAL 1
       OR NOT introduced_minor_identity EQUAL 0
       OR NOT introduced_minor_trajectory_decode EQUAL 1
       OR NOT introduced_minor_highway_coordinate EQUAL 3
       OR NOT introduced_minor_highway_registry_materialize EQUAL 4
       OR NOT introduced_minor_evidence_record_lineage EQUAL 5
       OR NOT introduced_minor_evidence_record_testimony EQUAL 6
       OR NOT introduced_minor_source_profile_validate EQUAL 7
       OR NOT introduced_minor_world_admission_close EQUAL 8
       OR NOT introduced_minor_reference_topology_resolve EQUAL 9
       OR NOT introduced_minor_reference_mapping_resolve EQUAL 10
       OR NOT introduced_minor_cognition_solve_packet EQUAL 11)
        message(FATAL_ERROR "ISA opcode assignment changed")
    endif()
    if(NOT receipt_algorithm STREQUAL "BLAKE3-256" OR NOT receipt_bytes EQUAL 32)
        message(FATAL_ERROR "ISA receipt digest contract changed")
    endif()
    if(NOT receipt_detail_full EQUAL 1)
        message(FATAL_ERROR "ISA receipt detail assignment changed")
    endif()
    if(NOT program_flags EQUAL 0 OR NOT instruction_flags EQUAL 0 OR NOT value_flags EQUAL 0)
        message(FATAL_ERROR "Initial ISA flags must remain empty")
    endif()
    if(operation_count LESS 1)
        message(FATAL_ERROR "ISA operation registry cannot be empty")
    endif()

    math(EXPR operation_last "${operation_count} - 1")
    set(remaining_operations "")
    foreach(index RANGE 0 ${operation_last})
        string(JSON operation_name MEMBER
            "${contract_json}" operation_contracts ${index})
        list(APPEND remaining_operations "${operation_name}")
    endforeach()
    set(operation_registry "")
    set(previous_opcode 0)
    set(emitted_count 0)
    while(remaining_operations)
        set(operation_name "")
        set(operation_opcode 4294967296)
        foreach(candidate IN LISTS remaining_operations)
            string(JSON candidate_opcode GET "${contract_json}" opcodes ${candidate})
            if(candidate_opcode LESS operation_opcode)
                set(operation_name "${candidate}")
                set(operation_opcode "${candidate_opcode}")
            elseif(candidate_opcode EQUAL operation_opcode)
                message(FATAL_ERROR "ISA operation registry contains a duplicate opcode")
            endif()
        endforeach()
        list(REMOVE_ITEM remaining_operations "${operation_name}")
        string(JSON operation_module GET
            "${contract_json}" operation_contracts ${operation_name} module)
        string(JSON operation_input GET
            "${contract_json}" operation_contracts ${operation_name} input_type)
        string(JSON operation_output GET
            "${contract_json}" operation_contracts ${operation_name} output_type)
        string(JSON operation_version GET
            "${contract_json}" instruction_versions ${operation_name})
        string(JSON operation_minor GET
            "${contract_json}" introduced_minor ${operation_name})
        string(JSON operation_module_id GET
            "${contract_json}" modules ${operation_module})
        string(JSON operation_input_id GET
            "${contract_json}" value_types ${operation_input})
        string(JSON operation_output_id GET
            "${contract_json}" value_types ${operation_output})
        if(operation_opcode LESS_EQUAL previous_opcode)
            message(FATAL_ERROR "ISA operation registry sort failed")
        endif()
        set(previous_opcode "${operation_opcode}")
        string(TOUPPER "${operation_name}" operation_symbol)
        string(APPEND operation_registry
            "    X(${operation_symbol}, ${operation_name}, UINT32_C(${operation_opcode}), UINT16_C(${operation_version}), UINT16_C(${operation_minor}), UINT32_C(${operation_input_id}), UINT32_C(${operation_output_id}), UINT32_C(${operation_module_id}))")
        math(EXPR emitted_count "${emitted_count} + 1")
        if(NOT emitted_count EQUAL operation_count)
            string(APPEND operation_registry " \\\n")
        endif()
    endwhile()

    set(LAPLACE_ISA_MAJOR "${major}")
    set(LAPLACE_ISA_MINOR "${minor}")
    set(LAPLACE_ISA_VALUE_U32_VECTOR "${value_u32}")
    set(LAPLACE_ISA_VALUE_ID128_VECTOR "${value_id128}")
    set(LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR "${value_trajectory}")
    set(LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR "${value_occurrence}")
    set(LAPLACE_ISA_VALUE_HIGHWAY_KEY_VECTOR "${value_highway_key}")
    set(LAPLACE_ISA_VALUE_HIGHWAY_COORDINATE_VECTOR "${value_highway_coordinate}")
    set(LAPLACE_ISA_VALUE_HIGHWAY_REGISTRY_RECEIPT_VECTOR
        "${value_highway_registry_receipt}")
    set(LAPLACE_ISA_VALUE_EVIDENCE_LINEAGE_RECORD_VECTOR
        "${value_evidence_lineage_record}")
    set(LAPLACE_ISA_VALUE_EVIDENCE_ROOT_RECORD_VECTOR
        "${value_evidence_root_record}")
    set(LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECORD_VECTOR
        "${value_evidence_testimony_record}")
    set(LAPLACE_ISA_VALUE_EVIDENCE_TESTIMONY_RECEIPT_VECTOR
        "${value_evidence_testimony_receipt}")
    set(LAPLACE_ISA_VALUE_SOURCE_PROFILE_MANIFEST_VECTOR
        "${value_source_profile_manifest}")
    set(LAPLACE_ISA_VALUE_SOURCE_PROFILE_RECEIPT_VECTOR
        "${value_source_profile_receipt}")
    set(LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECORD_VECTOR
        "${value_world_admission_record}")
    set(LAPLACE_ISA_VALUE_WORLD_ADMISSION_RECEIPT_VECTOR
        "${value_world_admission_receipt}")
    set(LAPLACE_ISA_VALUE_REFERENCE_CANDIDATE_VECTOR
        "${value_reference_candidate}")
    set(LAPLACE_ISA_VALUE_REFERENCE_RECORD_VECTOR
        "${value_reference_record}")
    set(LAPLACE_ISA_VALUE_REFERENCE_MAPPING_CANDIDATE_VECTOR
        "${value_reference_mapping_candidate}")
    set(LAPLACE_ISA_VALUE_REFERENCE_MAPPING_RECORD_VECTOR
        "${value_reference_mapping_record}")
    set(LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH "${opcode_identity_codepoint}")
    set(LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH
        "${opcode_trajectory_decode}")
    set(LAPLACE_ISA_OPCODE_HIGHWAY_COORDINATE_CALCULATE_BATCH
        "${opcode_highway_coordinate}")
    set(LAPLACE_ISA_OPCODE_HIGHWAY_REGISTRY_MATERIALIZE_BATCH
        "${opcode_highway_registry_materialize}")
    set(LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_LINEAGE_BATCH
        "${opcode_evidence_record_lineage}")
    set(LAPLACE_ISA_OPCODE_EVIDENCE_RECORD_TESTIMONY_BATCH
        "${opcode_evidence_record_testimony}")
    set(LAPLACE_ISA_OPCODE_SOURCE_PROFILE_VALIDATE_BATCH
        "${opcode_source_profile_validate}")
    set(LAPLACE_ISA_OPCODE_WORLD_ADMISSION_CLOSE_BATCH
        "${opcode_world_admission_close}")
    set(LAPLACE_ISA_OPCODE_REFERENCE_TOPOLOGY_RESOLVE_BATCH
        "${opcode_reference_topology_resolve}")
    set(LAPLACE_ISA_OPCODE_REFERENCE_MAPPING_RESOLVE_BATCH
        "${opcode_reference_mapping_resolve}")
    set(LAPLACE_ISA_OPCODE_COGNITION_SOLVE_PACKET
        "${opcode_cognition_solve_packet}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH
        "${instruction_version_identity_codepoint}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH
        "${instruction_version_trajectory_decode}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_COORDINATE_CALCULATE_BATCH
        "${instruction_version_highway_coordinate}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_REGISTRY_MATERIALIZE_BATCH
        "${instruction_version_highway_registry_materialize}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_LINEAGE_BATCH
        "${instruction_version_evidence_record_lineage}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_RECORD_TESTIMONY_BATCH
        "${instruction_version_evidence_record_testimony}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_SOURCE_PROFILE_VALIDATE_BATCH
        "${instruction_version_source_profile_validate}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_WORLD_ADMISSION_CLOSE_BATCH
        "${instruction_version_world_admission_close}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_REFERENCE_TOPOLOGY_RESOLVE_BATCH
        "${instruction_version_reference_topology_resolve}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_REFERENCE_MAPPING_RESOLVE_BATCH
        "${instruction_version_reference_mapping_resolve}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_COGNITION_SOLVE_PACKET
        "${instruction_version_cognition_solve_packet}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_IDENTITY_CODEPOINT_BATCH
        "${introduced_minor_identity}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_TRAJECTORY_COMPOSITION_DECODE_BATCH
        "${introduced_minor_trajectory_decode}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_HIGHWAY_COORDINATE_CALCULATE_BATCH
        "${introduced_minor_highway_coordinate}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_HIGHWAY_REGISTRY_MATERIALIZE_BATCH
        "${introduced_minor_highway_registry_materialize}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_EVIDENCE_RECORD_LINEAGE_BATCH
        "${introduced_minor_evidence_record_lineage}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_EVIDENCE_RECORD_TESTIMONY_BATCH
        "${introduced_minor_evidence_record_testimony}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_SOURCE_PROFILE_VALIDATE_BATCH
        "${introduced_minor_source_profile_validate}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_WORLD_ADMISSION_CLOSE_BATCH
        "${introduced_minor_world_admission_close}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_REFERENCE_TOPOLOGY_RESOLVE_BATCH
        "${introduced_minor_reference_topology_resolve}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_REFERENCE_MAPPING_RESOLVE_BATCH
        "${introduced_minor_reference_mapping_resolve}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_COGNITION_SOLVE_PACKET
        "${introduced_minor_cognition_solve_packet}")
    set(LAPLACE_ISA_RECEIPT_DIGEST_BYTES "${receipt_bytes}")
    set(LAPLACE_ISA_RECEIPT_DETAIL_FULL "${receipt_detail_full}")
    set(LAPLACE_ISA_KNOWN_PROGRAM_FLAGS "${program_flags}")
    set(LAPLACE_ISA_KNOWN_INSTRUCTION_FLAGS "${instruction_flags}")
    set(LAPLACE_ISA_KNOWN_VALUE_FLAGS "${value_flags}")
    set(LAPLACE_ISA_OPERATION_COUNT "${operation_count}")
    set(LAPLACE_ISA_OPERATION_REGISTRY "${operation_registry}")
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/isa.h.in"
        "${output_path}"
        @ONLY)
    set(LAPLACE_ISA_MAJOR "${major}" PARENT_SCOPE)
    set(LAPLACE_ISA_MINOR "${minor}" PARENT_SCOPE)
endfunction()
