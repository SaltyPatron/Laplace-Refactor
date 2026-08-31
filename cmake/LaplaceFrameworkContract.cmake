function(laplace_configure_framework_contract contract_path output_path)
    file(READ "${contract_path}" contract_json)

    string(JSON contract_schema GET "${contract_json}" schema)
    string(JSON major GET "${contract_json}" version major)
    string(JSON minor GET "${contract_json}" version minor)
    foreach(epoch IN ITEMS source identity geometry evidence firmware dependency database perfcache numeric package)
        string(JSON epoch_${epoch} GET "${contract_json}" epoch_slots ${epoch})
    endforeach()
    string(JSON context_bootstrap GET "${contract_json}" context_flags bootstrap)
    string(JSON context_read_only GET "${contract_json}" context_flags read_only)
    string(JSON batch_none GET "${contract_json}" canonical_batch_flags none)
    string(JSON stream_none GET "${contract_json}" canonical_stream_flags none)
    string(JSON effect_none GET "${contract_json}" effect_dispositions none)
    string(JSON effect_staged GET "${contract_json}" effect_dispositions staged_inert)
    string(JSON effect_admitted GET "${contract_json}" effect_dispositions activation_admitted)
    string(JSON effect_activated GET "${contract_json}" effect_dispositions activated)
    string(JSON sink_major GET "${contract_json}" sink_abi major)
    string(JSON sink_minor GET "${contract_json}" sink_abi minor)
    string(JSON activation_provider_major GET "${contract_json}" activation_provider_abi major)
    string(JSON activation_provider_minor GET "${contract_json}" activation_provider_abi minor)
    string(JSON producer_major GET "${contract_json}" producer_abi major)
    string(JSON producer_minor GET "${contract_json}" producer_abi minor)
    string(JSON producer_control_major GET "${contract_json}" producer_control_abi major)
    string(JSON producer_control_minor GET "${contract_json}" producer_control_abi minor)
    string(JSON producer_none GET "${contract_json}" producer_flags none)
    string(JSON producer_control_none GET "${contract_json}" producer_control_flags none)
    string(JSON replay_none GET "${contract_json}" replay_flags none)
    string(JSON activation_none GET "${contract_json}" activation_flags none)
    string(JSON digest_algorithm GET "${contract_json}" digest algorithm)
    string(JSON digest_bytes GET "${contract_json}" digest bytes)
    string(JSON digest_all_patterns GET "${contract_json}" digest all_bit_patterns_valid)
    string(JSON optional_presence GET "${contract_json}" digest optional_presence)
    string(JSON absent_epoch_payload GET "${contract_json}" digest absent_epoch_payload)

    string(JSON machine_exception_abi_major GET "${contract_json}" machine_exception abi major)
    string(JSON machine_exception_abi_minor GET "${contract_json}" machine_exception abi minor)
    foreach(kind IN ITEMS none trap interrupt fault cancellation terminal_disposition)
        string(JSON kind_${kind} GET "${contract_json}" machine_exception kinds ${kind})
    endforeach()
    foreach(capability IN ITEMS precise restartable retryable reroutable replayable compensatable terminal)
        string(JSON capability_${capability} GET "${contract_json}" machine_exception capability_flags ${capability})
    endforeach()
    foreach(recovery IN ITEMS terminate retry reroute_replay replay typed_limit)
        string(JSON recovery_${recovery} GET "${contract_json}" machine_exception recovery_dispositions ${recovery})
    endforeach()
    foreach(publication IN ITEMS none partial upper_bound)
        string(JSON publication_${publication} GET "${contract_json}" machine_exception publication_dispositions ${publication})
    endforeach()

    if(NOT contract_schema STREQUAL "laplace.framework-contract/v1")
        message(FATAL_ERROR "Unsupported framework contract schema: ${contract_schema}")
    endif()
    if(NOT major EQUAL 1 OR NOT minor EQUAL 6
       OR NOT sink_major EQUAL 1 OR NOT sink_minor EQUAL 0
       OR NOT activation_provider_major EQUAL 1
       OR NOT activation_provider_minor EQUAL 0
       OR NOT producer_major EQUAL 1 OR NOT producer_minor EQUAL 0
       OR NOT producer_control_major EQUAL 1
       OR NOT producer_control_minor EQUAL 0)
        message(FATAL_ERROR
            "Current framework ABI must remain 1.6 with provider ABIs at 1.0")
    endif()
    if(NOT machine_exception_abi_major EQUAL 1 OR
       NOT machine_exception_abi_minor EQUAL 0)
        message(FATAL_ERROR "Machine exception ABI must remain 1.0")
    endif()
    set(expected_epoch 0)
    foreach(epoch IN ITEMS source identity geometry evidence firmware dependency database perfcache numeric package)
        if(NOT epoch_${epoch} EQUAL expected_epoch)
            message(FATAL_ERROR "Framework epoch slot assignment changed: ${epoch}")
        endif()
        math(EXPR expected_epoch "${expected_epoch} + 1")
    endforeach()
    if(NOT context_bootstrap EQUAL 1 OR NOT context_read_only EQUAL 2
       OR NOT batch_none EQUAL 0 OR NOT stream_none EQUAL 0
       OR NOT effect_none EQUAL 0 OR NOT effect_staged EQUAL 1
       OR NOT effect_admitted EQUAL 2 OR NOT effect_activated EQUAL 3
       OR NOT activation_none EQUAL 0 OR NOT producer_none EQUAL 0
       OR NOT producer_control_none EQUAL 0 OR NOT replay_none EQUAL 0)
        message(FATAL_ERROR "Framework flag assignment changed")
    endif()
    if(NOT digest_algorithm STREQUAL "BLAKE3-256" OR NOT digest_bytes EQUAL 32)
        message(FATAL_ERROR "Framework digest contract changed")
    endif()
    if(NOT digest_all_patterns OR
       NOT optional_presence STREQUAL "typed-state-only" OR
       NOT absent_epoch_payload STREQUAL "canonical-all-zero")
        message(FATAL_ERROR "Framework digest presence contract changed")
    endif()

    if(NOT kind_none EQUAL 0 OR NOT kind_trap EQUAL 1 OR
       NOT kind_interrupt EQUAL 2 OR NOT kind_fault EQUAL 3 OR
       NOT kind_cancellation EQUAL 4 OR NOT kind_terminal_disposition EQUAL 5)
        message(FATAL_ERROR "Machine exception kind assignment changed")
    endif()
    if(NOT capability_precise EQUAL 1 OR NOT capability_restartable EQUAL 2 OR
       NOT capability_retryable EQUAL 4 OR NOT capability_reroutable EQUAL 8 OR
       NOT capability_replayable EQUAL 16 OR NOT capability_compensatable EQUAL 32 OR
       NOT capability_terminal EQUAL 64)
        message(FATAL_ERROR "Machine exception capability assignment changed")
    endif()
    if(NOT recovery_terminate EQUAL 0 OR NOT recovery_retry EQUAL 1 OR
       NOT recovery_reroute_replay EQUAL 2 OR NOT recovery_replay EQUAL 3 OR
       NOT recovery_typed_limit EQUAL 4)
        message(FATAL_ERROR "Machine exception recovery assignment changed")
    endif()
    if(NOT publication_none EQUAL 0 OR NOT publication_partial EQUAL 1 OR
       NOT publication_upper_bound EQUAL 2)
        message(FATAL_ERROR "Machine exception publication assignment changed")
    endif()

    set(machine_conditions
        none
        invalid_instruction
        invalid_operand
        implementation_fault
        durability_fault
        storage_fault
        hardware_fault
        consistency_fault
        network_fault
        provider_unavailable
        authority_denied
        cancelled
        deadline_exceeded
        resource_exhausted
        semantic_contradiction
        incomplete_boundary
        unsupported_operation
        partial_result
        known_upper_bound
        unknown)
    list(LENGTH machine_conditions machine_condition_count)
    set(machine_priorities "")
    set(expected_condition_code 0)
    foreach(condition IN LISTS machine_conditions)
        string(JSON condition_code GET "${contract_json}" machine_exception conditions ${condition} code)
        string(JSON condition_kind GET "${contract_json}" machine_exception conditions ${condition} kind)
        string(JSON condition_priority GET "${contract_json}" machine_exception conditions ${condition} priority)
        string(JSON condition_recovery GET "${contract_json}" machine_exception conditions ${condition} recovery)
        string(JSON condition_publication GET "${contract_json}" machine_exception conditions ${condition} publication)
        if(NOT condition_code EQUAL expected_condition_code)
            message(FATAL_ERROR "Machine exception condition code changed: ${condition}")
        endif()
        if(condition STREQUAL "none")
            if(NOT condition_priority EQUAL 65535)
                message(FATAL_ERROR "Machine exception none priority changed")
            endif()
        else()
            list(FIND machine_priorities "${condition_priority}" duplicate_priority)
            if(NOT duplicate_priority EQUAL -1)
                message(FATAL_ERROR "Machine exception priority repeated: ${condition_priority}")
            endif()
            list(APPEND machine_priorities "${condition_priority}")
        endif()

        set(kind_variable "kind_${condition_kind}")
        if(NOT DEFINED ${kind_variable})
            message(FATAL_ERROR "Unknown machine exception kind: ${condition_kind}")
        endif()
        set(condition_kind_value "${${kind_variable}}")

        set(condition_capabilities 0)
        string(JSON capability_count LENGTH "${contract_json}" machine_exception conditions ${condition} capabilities)
        if(capability_count GREATER 0)
            math(EXPR capability_last "${capability_count} - 1")
            foreach(capability_index RANGE 0 ${capability_last})
                string(JSON capability_name GET "${contract_json}" machine_exception conditions ${condition} capabilities ${capability_index})
                set(capability_variable "capability_${capability_name}")
                if(NOT DEFINED ${capability_variable})
                    message(FATAL_ERROR "Unknown machine exception capability: ${capability_name}")
                endif()
                math(EXPR condition_capabilities "${condition_capabilities} | ${${capability_variable}}")
            endforeach()
        endif()

        set(recovery_variable "recovery_${condition_recovery}")
        set(publication_variable "publication_${condition_publication}")
        if(NOT DEFINED ${recovery_variable} OR NOT DEFINED ${publication_variable})
            message(FATAL_ERROR "Unknown machine exception recovery or publication disposition: ${condition}")
        endif()

        string(TOUPPER "${condition}" condition_upper)
        set("LAPLACE_MACHINE_CONDITION_${condition_upper}" "${condition_code}")
        set("LAPLACE_MACHINE_KIND_${condition_upper}" "${condition_kind_value}")
        set("LAPLACE_MACHINE_PRIORITY_${condition_upper}" "${condition_priority}")
        set("LAPLACE_MACHINE_CAPABILITIES_${condition_upper}" "${condition_capabilities}")
        set("LAPLACE_MACHINE_RECOVERY_${condition_upper}" "${${recovery_variable}}")
        set("LAPLACE_MACHINE_PUBLICATION_${condition_upper}" "${${publication_variable}}")
        math(EXPR expected_condition_code "${expected_condition_code} + 1")
    endforeach()
    if(NOT machine_condition_count EQUAL 20 OR NOT expected_condition_code EQUAL 20)
        message(FATAL_ERROR "Machine exception condition registry cardinality changed")
    endif()

    set(LAPLACE_FRAMEWORK_MAJOR "${major}")
    set(LAPLACE_FRAMEWORK_MINOR "${minor}")
    string(TOUPPER "${epoch_source}" LAPLACE_FRAMEWORK_EPOCH_SOURCE)
    set(LAPLACE_FRAMEWORK_EPOCH_IDENTITY "${epoch_identity}")
    set(LAPLACE_FRAMEWORK_EPOCH_GEOMETRY "${epoch_geometry}")
    set(LAPLACE_FRAMEWORK_EPOCH_EVIDENCE "${epoch_evidence}")
    set(LAPLACE_FRAMEWORK_EPOCH_FIRMWARE "${epoch_firmware}")
    set(LAPLACE_FRAMEWORK_EPOCH_DEPENDENCY "${epoch_dependency}")
    set(LAPLACE_FRAMEWORK_EPOCH_DATABASE "${epoch_database}")
    set(LAPLACE_FRAMEWORK_EPOCH_PERFCACHE "${epoch_perfcache}")
    set(LAPLACE_FRAMEWORK_EPOCH_NUMERIC "${epoch_numeric}")
    set(LAPLACE_FRAMEWORK_EPOCH_PACKAGE "${epoch_package}")
    set(LAPLACE_FRAMEWORK_EPOCH_COUNT "${expected_epoch}")
    set(LAPLACE_FRAMEWORK_CONTEXT_BOOTSTRAP "${context_bootstrap}")
    set(LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY "${context_read_only}")
    math(EXPR known_context_flags "${context_bootstrap} | ${context_read_only}")
    set(LAPLACE_FRAMEWORK_KNOWN_CONTEXT_FLAGS "${known_context_flags}")
    set(LAPLACE_FRAMEWORK_KNOWN_BATCH_FLAGS "${batch_none}")
    set(LAPLACE_FRAMEWORK_KNOWN_STREAM_FLAGS "${stream_none}")
    set(LAPLACE_FRAMEWORK_EFFECT_NONE "${effect_none}")
    set(LAPLACE_FRAMEWORK_EFFECT_STAGED_INERT "${effect_staged}")
    set(LAPLACE_FRAMEWORK_EFFECT_ACTIVATION_ADMITTED "${effect_admitted}")
    set(LAPLACE_FRAMEWORK_EFFECT_ACTIVATED "${effect_activated}")
    set(LAPLACE_FRAMEWORK_SINK_ABI_MAJOR "${sink_major}")
    set(LAPLACE_FRAMEWORK_SINK_ABI_MINOR "${sink_minor}")
    set(LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR "${activation_provider_major}")
    set(LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR "${activation_provider_minor}")
    set(LAPLACE_FRAMEWORK_PRODUCER_ABI_MAJOR "${producer_major}")
    set(LAPLACE_FRAMEWORK_PRODUCER_ABI_MINOR "${producer_minor}")
    set(LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR "${producer_control_major}")
    set(LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR "${producer_control_minor}")
    set(LAPLACE_FRAMEWORK_KNOWN_PRODUCER_FLAGS "${producer_none}")
    set(LAPLACE_FRAMEWORK_KNOWN_PRODUCER_CONTROL_FLAGS "${producer_control_none}")
    set(LAPLACE_FRAMEWORK_KNOWN_REPLAY_FLAGS "${replay_none}")
    set(LAPLACE_FRAMEWORK_KNOWN_ACTIVATION_FLAGS "${activation_none}")
    set(LAPLACE_FRAMEWORK_DIGEST_BYTES "${digest_bytes}")
    set(LAPLACE_FRAMEWORK_DIGEST_ALL_BIT_PATTERNS_VALID 1)
    set(LAPLACE_FRAMEWORK_OPTIONAL_PRESENCE_TYPED_STATE_ONLY 1)
    set(LAPLACE_FRAMEWORK_ABSENT_EPOCH_PAYLOAD_CANONICAL_ZERO 1)

    set(LAPLACE_MACHINE_EXCEPTION_ABI_MAJOR "${machine_exception_abi_major}")
    set(LAPLACE_MACHINE_EXCEPTION_ABI_MINOR "${machine_exception_abi_minor}")
    set(LAPLACE_MACHINE_KIND_NONE "${kind_none}")
    set(LAPLACE_MACHINE_KIND_TRAP "${kind_trap}")
    set(LAPLACE_MACHINE_KIND_INTERRUPT "${kind_interrupt}")
    set(LAPLACE_MACHINE_KIND_FAULT "${kind_fault}")
    set(LAPLACE_MACHINE_KIND_CANCELLATION "${kind_cancellation}")
    set(LAPLACE_MACHINE_KIND_TERMINAL_DISPOSITION "${kind_terminal_disposition}")
    set(LAPLACE_MACHINE_CAPABILITY_PRECISE "${capability_precise}")
    set(LAPLACE_MACHINE_CAPABILITY_RESTARTABLE "${capability_restartable}")
    set(LAPLACE_MACHINE_CAPABILITY_RETRYABLE "${capability_retryable}")
    set(LAPLACE_MACHINE_CAPABILITY_REROUTABLE "${capability_reroutable}")
    set(LAPLACE_MACHINE_CAPABILITY_REPLAYABLE "${capability_replayable}")
    set(LAPLACE_MACHINE_CAPABILITY_COMPENSATABLE "${capability_compensatable}")
    set(LAPLACE_MACHINE_CAPABILITY_TERMINAL "${capability_terminal}")
    math(EXPR machine_capability_known_mask
        "${capability_precise} | ${capability_restartable} | ${capability_retryable} | ${capability_reroutable} | ${capability_replayable} | ${capability_compensatable} | ${capability_terminal}")
    set(LAPLACE_MACHINE_CAPABILITY_KNOWN_MASK "${machine_capability_known_mask}")
    set(LAPLACE_MACHINE_RECOVERY_TERMINATE "${recovery_terminate}")
    set(LAPLACE_MACHINE_RECOVERY_RETRY "${recovery_retry}")
    set(LAPLACE_MACHINE_RECOVERY_REROUTE_REPLAY "${recovery_reroute_replay}")
    set(LAPLACE_MACHINE_RECOVERY_REPLAY "${recovery_replay}")
    set(LAPLACE_MACHINE_RECOVERY_TYPED_LIMIT "${recovery_typed_limit}")
    set(LAPLACE_MACHINE_PUBLICATION_NONE "${publication_none}")
    set(LAPLACE_MACHINE_PUBLICATION_PARTIAL "${publication_partial}")
    set(LAPLACE_MACHINE_PUBLICATION_UPPER_BOUND "${publication_upper_bound}")
    set(LAPLACE_MACHINE_CONDITION_COUNT "${machine_condition_count}")

    set(LAPLACE_FRAMEWORK_MAJOR "${major}" PARENT_SCOPE)
    set(LAPLACE_FRAMEWORK_MINOR "${minor}" PARENT_SCOPE)
    set(LAPLACE_FRAMEWORK_CONTEXT_BOOTSTRAP "${context_bootstrap}" PARENT_SCOPE)
    set(LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY "${context_read_only}" PARENT_SCOPE)
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/framework.h.in"
        "${output_path}"
        @ONLY)
endfunction()
