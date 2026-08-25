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
    string(JSON opcode_identity_codepoint GET
        "${contract_json}" opcodes identity_codepoint_batch)
    string(JSON opcode_trajectory_decode GET
        "${contract_json}" opcodes trajectory_composition_decode_batch)
    string(JSON instruction_version_identity_codepoint GET
        "${contract_json}" instruction_versions identity_codepoint_batch)
    string(JSON instruction_version_trajectory_decode GET
        "${contract_json}" instruction_versions trajectory_composition_decode_batch)
    string(JSON introduced_minor_identity GET
        "${contract_json}" introduced_minor identity_codepoint_batch)
    string(JSON introduced_minor_trajectory_decode GET
        "${contract_json}" introduced_minor trajectory_composition_decode_batch)
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
    if(NOT major EQUAL 1 OR NOT minor EQUAL 2)
        message(FATAL_ERROR "Current ISA version must remain 1.2")
    endif()
    if(NOT context_required OR NOT context_framework_major EQUAL 1
       OR NOT context_program_binding OR NOT context_receipt_binding)
        message(FATAL_ERROR "ISA execution context binding contract changed")
    endif()
    if(NOT value_u32 EQUAL 1 OR NOT value_id128 EQUAL 2
       OR NOT value_trajectory EQUAL 3 OR NOT value_occurrence EQUAL 4)
        message(FATAL_ERROR "ISA value type assignments changed")
    endif()
    if(NOT opcode_identity_codepoint EQUAL 131073
       OR NOT opcode_trajectory_decode EQUAL 196609
       OR NOT instruction_version_identity_codepoint EQUAL 1
       OR NOT instruction_version_trajectory_decode EQUAL 1
       OR NOT introduced_minor_identity EQUAL 0
       OR NOT introduced_minor_trajectory_decode EQUAL 1)
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
    set(operation_registry "")
    set(previous_opcode 0)
    foreach(index RANGE 0 ${operation_last})
        string(JSON operation_name MEMBER "${contract_json}" operation_contracts ${index})
        string(JSON operation_module GET
            "${contract_json}" operation_contracts ${operation_name} module)
        string(JSON operation_input GET
            "${contract_json}" operation_contracts ${operation_name} input_type)
        string(JSON operation_output GET
            "${contract_json}" operation_contracts ${operation_name} output_type)
        string(JSON operation_opcode GET "${contract_json}" opcodes ${operation_name})
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
            message(FATAL_ERROR "ISA operation registry must be ordered by unique opcode")
        endif()
        set(previous_opcode "${operation_opcode}")
        string(TOUPPER "${operation_name}" operation_symbol)
        string(APPEND operation_registry
            "    X(${operation_symbol}, ${operation_name}, UINT32_C(${operation_opcode}), UINT16_C(${operation_version}), UINT16_C(${operation_minor}), UINT32_C(${operation_input_id}), UINT32_C(${operation_output_id}), UINT32_C(${operation_module_id}))")
        if(NOT index EQUAL operation_last)
            string(APPEND operation_registry " \\\n")
        endif()
    endforeach()

    set(LAPLACE_ISA_MAJOR "${major}")
    set(LAPLACE_ISA_MINOR "${minor}")
    set(LAPLACE_ISA_VALUE_U32_VECTOR "${value_u32}")
    set(LAPLACE_ISA_VALUE_ID128_VECTOR "${value_id128}")
    set(LAPLACE_ISA_VALUE_COMPOSITION_TRAJECTORY_VECTOR "${value_trajectory}")
    set(LAPLACE_ISA_VALUE_COMPOSITION_OCCURRENCE_VECTOR "${value_occurrence}")
    set(LAPLACE_ISA_OPCODE_IDENTITY_CODEPOINT_BATCH "${opcode_identity_codepoint}")
    set(LAPLACE_ISA_OPCODE_TRAJECTORY_COMPOSITION_DECODE_BATCH
        "${opcode_trajectory_decode}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_IDENTITY_CODEPOINT_BATCH
        "${instruction_version_identity_codepoint}")
    set(LAPLACE_ISA_INSTRUCTION_VERSION_TRAJECTORY_COMPOSITION_DECODE_BATCH
        "${instruction_version_trajectory_decode}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_IDENTITY_CODEPOINT_BATCH
        "${introduced_minor_identity}")
    set(LAPLACE_ISA_INTRODUCED_MINOR_TRAJECTORY_COMPOSITION_DECODE_BATCH
        "${introduced_minor_trajectory_decode}")
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
endfunction()
