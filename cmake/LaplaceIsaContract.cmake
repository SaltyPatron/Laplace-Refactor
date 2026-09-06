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
    string(JSON receipt_algorithm GET "${contract_json}" receipt digest_algorithm)
    string(JSON receipt_bytes GET "${contract_json}" receipt digest_bytes)
    string(JSON receipt_detail_full GET "${contract_json}" receipt detail_full)
    string(JSON program_flags GET "${contract_json}" known_program_flags)
    string(JSON instruction_flags GET "${contract_json}" known_instruction_flags)
    string(JSON value_flags GET "${contract_json}" known_value_flags)

    if(NOT contract_schema STREQUAL "laplace.isa-contract/v1")
        message(FATAL_ERROR "Unsupported ISA contract schema: ${contract_schema}")
    endif()
    if(NOT major EQUAL 1 OR NOT minor EQUAL 14)
        message(FATAL_ERROR "Current ISA version must be 1.14")
    endif()
    if(NOT context_required OR NOT context_framework_major EQUAL 1
       OR NOT context_program_binding OR NOT context_receipt_binding)
        message(FATAL_ERROR "ISA execution context binding contract changed")
    endif()
    if(NOT receipt_algorithm STREQUAL "BLAKE3-256" OR NOT receipt_bytes EQUAL 32
       OR NOT receipt_detail_full EQUAL 1)
        message(FATAL_ERROR "ISA receipt contract changed")
    endif()
    if(NOT program_flags EQUAL 0 OR NOT instruction_flags EQUAL 0
       OR NOT value_flags EQUAL 0)
        message(FATAL_ERROR "ISA flags must remain explicitly closed")
    endif()

    # Value type numbers are ABI. Keep the validation data-driven so adding a
    # typed operation does not require another handwritten parser branch.
    set(expected_value_types
        "u32_vector|1"
        "id128_vector|2"
        "composition_trajectory_vector|3"
        "composition_occurrence_vector|4"
        "highway_key_vector|5"
        "highway_coordinate_vector|6"
        "highway_registry_receipt_vector|7"
        "evidence_lineage_record_vector|8"
        "evidence_root_record_vector|9"
        "evidence_testimony_record_vector|10"
        "evidence_testimony_receipt_vector|11"
        "source_profile_manifest_vector|12"
        "source_profile_receipt_vector|13"
        "world_admission_record_vector|14"
        "world_admission_receipt_vector|15"
        "reference_candidate_vector|16"
        "reference_record_vector|17"
        "reference_mapping_candidate_vector|18"
        "reference_mapping_record_vector|19"
        "standing_period_input_vector|20"
        "standing_period_result_vector|21"
        "stock_catalog_item_vector|22"
        "stock_catalog_receipt_vector|23")
    foreach(spec IN LISTS expected_value_types)
        string(REPLACE "|" ";" parts "${spec}")
        list(GET parts 0 value_name)
        list(GET parts 1 expected_id)
        string(JSON actual_id GET "${contract_json}" value_types ${value_name})
        if(NOT actual_id EQUAL expected_id)
            message(FATAL_ERROR
                "ISA value type ${value_name} changed: expected ${expected_id}, got ${actual_id}")
        endif()
    endforeach()

    set(expected_modules
        "identity|2"
        "trajectory|3"
        "highway|4"
        "evidence|5"
        "admission|6"
        "cognition|7"
        "ast|8")
    foreach(spec IN LISTS expected_modules)
        string(REPLACE "|" ";" parts "${spec}")
        list(GET parts 0 module_name)
        list(GET parts 1 expected_id)
        string(JSON actual_id GET "${contract_json}" modules ${module_name})
        if(NOT actual_id EQUAL expected_id)
            message(FATAL_ERROR
                "ISA module ${module_name} changed: expected ${expected_id}, got ${actual_id}")
        endif()
    endforeach()

    # name | opcode | instruction version | introduced minor
    set(expected_operations
        "identity_codepoint_batch|131073|1|0"
        "trajectory_composition_decode_batch|196609|1|1"
        "highway_coordinate_calculate_batch|262145|1|3"
        "highway_registry_materialize_batch|262146|1|4"
        "evidence_record_lineage_batch|327681|1|5"
        "evidence_record_testimony_batch|327682|1|6"
        "evidence_calculate_standing_batch|327683|1|12"
        "source_profile_validate_batch|393217|1|7"
        "world_admission_close_batch|393218|1|8"
        "reference_topology_resolve_batch|393219|1|9"
        "reference_mapping_resolve_batch|393220|1|10"
        "stock_recipe_compile_catalog_batch|393221|1|13"
        "cognition_solve_packet|458753|1|11"
        "universal_ast_apply_packet|524289|1|14")
    foreach(spec IN LISTS expected_operations)
        string(REPLACE "|" ";" parts "${spec}")
        list(GET parts 0 operation_name)
        list(GET parts 1 expected_opcode)
        list(GET parts 2 expected_version)
        list(GET parts 3 expected_minor)
        string(JSON actual_opcode GET "${contract_json}" opcodes ${operation_name})
        string(JSON actual_version GET
            "${contract_json}" instruction_versions ${operation_name})
        string(JSON actual_minor GET
            "${contract_json}" introduced_minor ${operation_name})
        if(NOT actual_opcode EQUAL expected_opcode
           OR NOT actual_version EQUAL expected_version
           OR NOT actual_minor EQUAL expected_minor)
            message(FATAL_ERROR
                "ISA operation ${operation_name} ABI assignment changed")
        endif()
    endforeach()

    string(JSON operation_count LENGTH "${contract_json}" operation_contracts)
    list(LENGTH expected_operations expected_operation_count)
    if(NOT operation_count EQUAL expected_operation_count)
        message(FATAL_ERROR
            "ISA operation registry count ${operation_count} does not match accepted ABI ${expected_operation_count}")
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