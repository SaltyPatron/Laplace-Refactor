function(laplace_configure_perfcache_contract contract_path output_path)
    file(READ "${contract_path}" contract_json)

    string(JSON contract_schema GET "${contract_json}" schema)
    string(JSON format_version GET "${contract_json}" format_version)
    string(JSON header_bytes GET "${contract_json}" header_bytes)
    string(JSON digest_bytes GET "${contract_json}" digest_bytes)
    string(JSON byte_order GET "${contract_json}" byte_order)
    string(JSON digest_algorithm GET "${contract_json}" digest_algorithm)
    string(JSON identity_digest_fields GET
        "${contract_json}" identity_and_digest_fields)
    string(JSON sorted_unique_access GET "${contract_json}" access_laws sorted_unique_fixed)
    string(JSON dense_u32_access GET "${contract_json}" access_laws dense_u32_zero_based)
    string(JSON module_defined_access GET "${contract_json}" access_laws module_defined)
    string(JSON activation_id_bytes GET "${contract_json}" artifact_epoch activation_id_bytes)
    string(JSON framework_epoch_bytes GET "${contract_json}" artifact_epoch framework_fingerprint_bytes)
    string(JSON module_contract_bytes GET "${contract_json}" module_contract_fingerprint bytes)
    string(JSON module_contract_algorithm GET "${contract_json}" module_contract_fingerprint algorithm)
    string(JSON provider_major GET "${contract_json}" artifact_provider_abi major)
    string(JSON provider_minor GET "${contract_json}" artifact_provider_abi minor)
    string(JSON module_major GET "${contract_json}" module_abi major)
    string(JSON module_minor GET "${contract_json}" module_abi minor)
    string(JSON framework_probe_module_id GET
        "${contract_json}" modules framework_probe module_id)
    string(JSON framework_probe_key_schema_id GET
        "${contract_json}" modules framework_probe key_schema_id)
    string(JSON framework_probe_value_schema_id GET
        "${contract_json}" modules framework_probe value_schema_id)
    string(JSON framework_probe_contract_fingerprint GET
        "${contract_json}" modules framework_probe contract_fingerprint)
    string(JSON framework_probe_contract_preimage GET
        "${contract_json}" modules framework_probe contract_preimage)
    string(JSON framework_probe_access_law GET
        "${contract_json}" modules framework_probe access_law)
    string(JSON framework_probe_validation GET
        "${contract_json}" modules framework_probe validation)
    string(JSON framework_probe_key_bytes GET
        "${contract_json}" modules framework_probe key_bytes)
    string(JSON framework_probe_value_bytes GET
        "${contract_json}" modules framework_probe value_bytes)
    string(JSON framework_probe_required GET
        "${contract_json}" modules framework_probe required)
    string(JSON unicode_tier0_module_id GET
        "${contract_json}" modules unicode_tier0 module_id)
    string(JSON unicode_tier0_key_schema_id GET
        "${contract_json}" modules unicode_tier0 key_schema_id)
    string(JSON unicode_tier0_value_schema_id GET
        "${contract_json}" modules unicode_tier0 value_schema_id)
    string(JSON unicode_tier0_contract_fingerprint GET
        "${contract_json}" modules unicode_tier0 contract_fingerprint)
    string(JSON unicode_tier0_contract_preimage GET
        "${contract_json}" modules unicode_tier0 contract_preimage)
    string(JSON unicode_tier0_access_law GET
        "${contract_json}" modules unicode_tier0 access_law)
    string(JSON unicode_tier0_validation GET
        "${contract_json}" modules unicode_tier0 validation)
    string(JSON unicode_tier0_key_bytes GET
        "${contract_json}" modules unicode_tier0 key_bytes)
    string(JSON unicode_tier0_value_bytes GET
        "${contract_json}" modules unicode_tier0 value_bytes)
    string(JSON unicode_tier0_population GET
        "${contract_json}" modules unicode_tier0 population)
    string(JSON unicode_tier0_required GET
        "${contract_json}" modules unicode_tier0 required)
    string(JSON unicode_reverse_module_id GET
        "${contract_json}" modules unicode_identity_reverse module_id)
    string(JSON unicode_reverse_key_schema_id GET
        "${contract_json}" modules unicode_identity_reverse key_schema_id)
    string(JSON unicode_reverse_value_schema_id GET
        "${contract_json}" modules unicode_identity_reverse value_schema_id)
    string(JSON unicode_reverse_contract_fingerprint GET
        "${contract_json}" modules unicode_identity_reverse contract_fingerprint)
    string(JSON unicode_reverse_contract_preimage GET
        "${contract_json}" modules unicode_identity_reverse contract_preimage)
    string(JSON unicode_reverse_access_law GET
        "${contract_json}" modules unicode_identity_reverse access_law)
    string(JSON unicode_reverse_validation GET
        "${contract_json}" modules unicode_identity_reverse validation)
    string(JSON unicode_reverse_key_bytes GET
        "${contract_json}" modules unicode_identity_reverse key_bytes)
    string(JSON unicode_reverse_value_bytes GET
        "${contract_json}" modules unicode_identity_reverse value_bytes)
    string(JSON unicode_reverse_population GET
        "${contract_json}" modules unicode_identity_reverse population)
    string(JSON unicode_reverse_capacity GET
        "${contract_json}" modules unicode_identity_reverse capacity)
    string(JSON unicode_reverse_required GET
        "${contract_json}" modules unicode_identity_reverse required)
    string(LENGTH "${framework_probe_module_id}" framework_probe_module_id_length)
    string(LENGTH "${framework_probe_key_schema_id}" framework_probe_key_schema_id_length)
    string(LENGTH "${framework_probe_value_schema_id}" framework_probe_value_schema_id_length)
    string(LENGTH "${framework_probe_contract_fingerprint}"
        framework_probe_contract_fingerprint_length)
    string(LENGTH "${unicode_tier0_module_id}" unicode_tier0_module_id_length)
    string(LENGTH "${unicode_tier0_key_schema_id}" unicode_tier0_key_schema_id_length)
    string(LENGTH "${unicode_tier0_value_schema_id}" unicode_tier0_value_schema_id_length)
    string(LENGTH "${unicode_tier0_contract_fingerprint}"
        unicode_tier0_contract_fingerprint_length)
    string(LENGTH "${unicode_reverse_module_id}" unicode_reverse_module_id_length)
    string(LENGTH "${unicode_reverse_key_schema_id}"
        unicode_reverse_key_schema_id_length)
    string(LENGTH "${unicode_reverse_value_schema_id}"
        unicode_reverse_value_schema_id_length)
    string(LENGTH "${unicode_reverse_contract_fingerprint}"
        unicode_reverse_contract_fingerprint_length)
    string(JSON generation_prepared GET "${contract_json}" generation_dispositions prepared)
    string(JSON generation_reserved GET "${contract_json}" generation_dispositions reserved)
    string(JSON generation_activated GET "${contract_json}" generation_dispositions activated)
    string(JSON generation_rejected GET "${contract_json}" generation_dispositions rejected)
    string(JSON generation_aborted GET "${contract_json}" generation_dispositions aborted)
    string(JSON generation_materialized GET
        "${contract_json}" generation_dispositions materialized)
    string(JSON performance_schema GET
        "${contract_json}" hot_lookup_performance receipt_schema)
    string(JSON performance_state GET
        "${contract_json}" hot_lookup_performance state)
    string(JSON performance_timing_boundary GET
        "${contract_json}" hot_lookup_performance timing_boundary)
    string(JSON performance_verification GET
        "${contract_json}" hot_lookup_performance verification)
    string(JSON performance_sample_count GET
        "${contract_json}" hot_lookup_performance sample_count)
    string(JSON performance_warmup_count GET
        "${contract_json}" hot_lookup_performance warmup_batch_count)
    string(JSON performance_width_count LENGTH
        "${contract_json}" hot_lookup_performance batch_widths)
    foreach(performance_width_index RANGE 0 3)
        string(JSON performance_width_${performance_width_index} GET
            "${contract_json}" hot_lookup_performance batch_widths
            ${performance_width_index})
    endforeach()
    string(JSON performance_percentile_count LENGTH
        "${contract_json}" hot_lookup_performance required_percentiles)
    foreach(performance_percentile_index RANGE 0 2)
        string(JSON performance_percentile_${performance_percentile_index} GET
            "${contract_json}" hot_lookup_performance required_percentiles
            ${performance_percentile_index})
    endforeach()

    if(NOT contract_schema STREQUAL "laplace.perfcache-contract/v3")
        message(FATAL_ERROR "Unsupported perfcache contract schema: ${contract_schema}")
    endif()
    if(NOT format_version EQUAL 3 OR NOT header_bytes EQUAL 352)
        message(FATAL_ERROR "Perfcache format version or header width changed")
    endif()
    if(NOT digest_bytes EQUAL 32 OR NOT digest_algorithm STREQUAL "BLAKE3-256"
       OR NOT activation_id_bytes EQUAL 16 OR NOT framework_epoch_bytes EQUAL 32
       OR NOT module_contract_bytes EQUAL 32
       OR NOT module_contract_algorithm STREQUAL "BLAKE3-256")
        message(FATAL_ERROR "Perfcache digest contract changed")
    endif()
    if(NOT identity_digest_fields STREQUAL
       "structurally-mandatory-all-bit-patterns-valid")
        message(FATAL_ERROR "Perfcache identity/digest value contract changed")
    endif()
    if(NOT byte_order STREQUAL "little-endian"
       OR NOT sorted_unique_access EQUAL 1
       OR NOT dense_u32_access EQUAL 2
       OR NOT module_defined_access EQUAL 3
       OR NOT provider_major EQUAL 1 OR NOT provider_minor EQUAL 1
       OR NOT module_major EQUAL 2 OR NOT module_minor EQUAL 0
       OR NOT framework_probe_module_id_length EQUAL 32
       OR NOT framework_probe_module_id MATCHES "^[0-9a-f]+$"
       OR NOT framework_probe_key_schema_id_length EQUAL 32
       OR NOT framework_probe_key_schema_id MATCHES "^[0-9a-f]+$"
       OR NOT framework_probe_value_schema_id_length EQUAL 32
       OR NOT framework_probe_value_schema_id MATCHES "^[0-9a-f]+$"
       OR NOT framework_probe_contract_fingerprint_length EQUAL 64
       OR NOT framework_probe_contract_fingerprint MATCHES "^[0-9a-f]+$"
       OR NOT framework_probe_contract_preimage STREQUAL
          "laplace.perfcache.module.framework-probe/v1|key=u32-le|value=u64-le|access=dense-u32-zero-based|validation=record+whole-view|module-abi=2.0"
       OR NOT framework_probe_validation STREQUAL "record-and-whole-view"
       OR NOT framework_probe_access_law STREQUAL "dense_u32_zero_based"
       OR NOT framework_probe_key_bytes EQUAL 4
       OR NOT framework_probe_value_bytes EQUAL 8
       OR NOT framework_probe_required
       OR NOT generation_prepared EQUAL 1 OR NOT generation_reserved EQUAL 2
       OR NOT generation_activated EQUAL 3 OR NOT generation_rejected EQUAL 4
       OR NOT generation_aborted EQUAL 5
       OR NOT generation_materialized EQUAL 6)
        message(FATAL_ERROR "Perfcache encoding contract changed")
    endif()
    if(NOT performance_schema STREQUAL
           "laplace.perfcache-hot-lookup-receipt/v1"
       OR NOT performance_state STREQUAL
           "validated-prefaulted-materialized-generation-held-by-one-exact-epoch-pin"
       OR NOT performance_timing_boundary STREQUAL
           "one-native-typed-batch-accessor-call-only"
       OR NOT performance_verification STREQUAL
           "all-input-output-parity-and-result-fingerprinting-outside-timing-boundary"
       OR NOT performance_sample_count EQUAL 31
       OR NOT performance_warmup_count EQUAL 4
       OR NOT performance_width_count EQUAL 4
       OR NOT performance_width_0 EQUAL 1
       OR NOT performance_width_1 EQUAL 16
       OR NOT performance_width_2 EQUAL 256
       OR NOT performance_width_3 EQUAL 4096
       OR NOT performance_percentile_count EQUAL 3
       OR NOT performance_percentile_0 EQUAL 50
       OR NOT performance_percentile_1 EQUAL 95
       OR NOT performance_percentile_2 EQUAL 99)
        message(FATAL_ERROR "Perfcache hot-lookup performance contract changed")
    endif()
    if(NOT unicode_tier0_module_id_length EQUAL 32
       OR NOT unicode_tier0_module_id MATCHES "^[0-9a-f]+$"
       OR NOT unicode_tier0_key_schema_id_length EQUAL 32
       OR NOT unicode_tier0_key_schema_id MATCHES "^[0-9a-f]+$"
       OR NOT unicode_tier0_value_schema_id_length EQUAL 32
       OR NOT unicode_tier0_value_schema_id MATCHES "^[0-9a-f]+$"
       OR NOT unicode_tier0_contract_fingerprint_length EQUAL 64
       OR NOT unicode_tier0_contract_fingerprint MATCHES "^[0-9a-f]+$"
       OR NOT unicode_tier0_access_law STREQUAL "dense_u32_zero_based"
       OR NOT unicode_tier0_validation STREQUAL "record-and-whole-view"
       OR NOT unicode_tier0_key_bytes EQUAL 4
       OR NOT unicode_tier0_value_bytes EQUAL 152
       OR NOT unicode_tier0_population EQUAL 1114112
       OR NOT unicode_tier0_required)
        message(FATAL_ERROR
            "Unicode Tier-0 perfcache module shape changed: "
            "module=${unicode_tier0_module_id}/${unicode_tier0_module_id_length}; "
            "key=${unicode_tier0_key_schema_id}/${unicode_tier0_key_schema_id_length}; "
            "value=${unicode_tier0_value_schema_id}/${unicode_tier0_value_schema_id_length}; "
            "contract=${unicode_tier0_contract_fingerprint}/${unicode_tier0_contract_fingerprint_length}; "
            "access=${unicode_tier0_access_law}; key-bytes=${unicode_tier0_key_bytes}; "
            "value-bytes=${unicode_tier0_value_bytes}; population=${unicode_tier0_population}; "
            "required=${unicode_tier0_required}")
    endif()
    if(NOT unicode_tier0_contract_preimage STREQUAL
       "laplace.perfcache.module.unicode-tier0/v2|key=u32-le|value=record-offset-u64le,record-bytes-u32le,placement-rank-u32le,position-class-u8,lup-length-u8,reserved-u16,lup-bytes-4,content-id-16,identity-witness-32,coordinate-bits-32,hilbert-key-16,physicality-id-32|metadata=canonical-atom-record-stream-v2|access=dense-u32-zero-based|population=1114112|validation=record+whole-view|module-abi=2.0")
        message(FATAL_ERROR "Unicode Tier-0 perfcache module preimage changed")
    endif()
    if(NOT unicode_reverse_module_id_length EQUAL 32
       OR NOT unicode_reverse_module_id MATCHES "^[0-9a-f]+$"
       OR NOT unicode_reverse_key_schema_id_length EQUAL 32
       OR NOT unicode_reverse_key_schema_id MATCHES "^[0-9a-f]+$"
       OR NOT unicode_reverse_value_schema_id_length EQUAL 32
       OR NOT unicode_reverse_value_schema_id MATCHES "^[0-9a-f]+$"
       OR NOT unicode_reverse_contract_fingerprint_length EQUAL 64
       OR NOT unicode_reverse_contract_fingerprint MATCHES "^[0-9a-f]+$"
       OR NOT unicode_reverse_access_law STREQUAL "module_defined"
       OR NOT unicode_reverse_validation STREQUAL "record-and-whole-view"
       OR NOT unicode_reverse_key_bytes EQUAL 48
       OR NOT unicode_reverse_value_bytes EQUAL 8
       OR NOT unicode_reverse_capacity EQUAL 2097152
       OR NOT unicode_reverse_population EQUAL 1114112
       OR NOT unicode_reverse_required)
        message(FATAL_ERROR "Unicode reverse perfcache module shape changed")
    endif()
    if(NOT unicode_reverse_contract_preimage STREQUAL
       "laplace.perfcache.module.unicode-identity-reverse/v1|key=content-id-16,identity-witness-32|value=codepoint-position-u32le,occupied-u8,reserved-u24|access=module-defined|capacity=2097152|population=1114112|home=u64le-identity-witness-low64-mask|probe=linear-plus-one|empty=occupied-zero-all-other-slot-bits-zero|validation=record+whole-view|module-abi=2.0")
        message(FATAL_ERROR "Unicode reverse perfcache module preimage changed")
    endif()

    set(LAPLACE_PERFCACHE_FORMAT_VERSION "${format_version}")
    set(LAPLACE_PERFCACHE_HEADER_BYTES "${header_bytes}")
    set(LAPLACE_PERFCACHE_DIGEST_BYTES "${digest_bytes}")
    set(LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED "${sorted_unique_access}")
    set(LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED "${dense_u32_access}")
    set(LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED "${module_defined_access}")
    set(LAPLACE_PERFCACHE_IDENTITY_DIGEST_ALL_BIT_PATTERNS_VALID 1)
    set(LAPLACE_PERFCACHE_ARTIFACT_PROVIDER_ABI_MAJOR "${provider_major}")
    set(LAPLACE_PERFCACHE_ARTIFACT_PROVIDER_ABI_MINOR "${provider_minor}")
    set(LAPLACE_PERFCACHE_MODULE_ABI_MAJOR "${module_major}")
    set(LAPLACE_PERFCACHE_MODULE_ABI_MINOR "${module_minor}")
    set(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_MODULE_ID
        "${framework_probe_module_id}")
    set(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_KEY_SCHEMA_ID
        "${framework_probe_key_schema_id}")
    set(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_VALUE_SCHEMA_ID
        "${framework_probe_value_schema_id}")
    set(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_CONTRACT_FINGERPRINT
        "${framework_probe_contract_fingerprint}")
    set(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_KEY_BYTES
        "${framework_probe_key_bytes}")
    set(LAPLACE_PERFCACHE_FRAMEWORK_PROBE_VALUE_BYTES
        "${framework_probe_value_bytes}")
    set(LAPLACE_PERFCACHE_UNICODE_TIER0_MODULE_ID
        "${unicode_tier0_module_id}")
    set(LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_SCHEMA_ID
        "${unicode_tier0_key_schema_id}")
    set(LAPLACE_PERFCACHE_UNICODE_TIER0_VALUE_SCHEMA_ID
        "${unicode_tier0_value_schema_id}")
    set(LAPLACE_PERFCACHE_UNICODE_TIER0_CONTRACT_FINGERPRINT
        "${unicode_tier0_contract_fingerprint}")
    set(LAPLACE_PERFCACHE_UNICODE_TIER0_KEY_BYTES
        "${unicode_tier0_key_bytes}")
    set(LAPLACE_PERFCACHE_UNICODE_TIER0_VALUE_BYTES
        "${unicode_tier0_value_bytes}")
    set(LAPLACE_PERFCACHE_UNICODE_TIER0_POPULATION
        "${unicode_tier0_population}")
    set(LAPLACE_PERFCACHE_UNICODE_REVERSE_MODULE_ID
        "${unicode_reverse_module_id}")
    set(LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_SCHEMA_ID
        "${unicode_reverse_key_schema_id}")
    set(LAPLACE_PERFCACHE_UNICODE_REVERSE_VALUE_SCHEMA_ID
        "${unicode_reverse_value_schema_id}")
    set(LAPLACE_PERFCACHE_UNICODE_REVERSE_CONTRACT_FINGERPRINT
        "${unicode_reverse_contract_fingerprint}")
    set(LAPLACE_PERFCACHE_UNICODE_REVERSE_KEY_BYTES
        "${unicode_reverse_key_bytes}")
    set(LAPLACE_PERFCACHE_UNICODE_REVERSE_VALUE_BYTES
        "${unicode_reverse_value_bytes}")
    set(LAPLACE_PERFCACHE_UNICODE_REVERSE_POPULATION
        "${unicode_reverse_population}")
    set(LAPLACE_PERFCACHE_UNICODE_REVERSE_CAPACITY
        "${unicode_reverse_capacity}")
    set(LAPLACE_PERFCACHE_HOT_RECEIPT_SCHEMA "${performance_schema}")
    set(LAPLACE_PERFCACHE_HOT_SAMPLE_COUNT "${performance_sample_count}")
    set(LAPLACE_PERFCACHE_HOT_WARMUP_BATCH_COUNT
        "${performance_warmup_count}")
    set(LAPLACE_PERFCACHE_HOT_BATCH_WIDTH_0 "${performance_width_0}")
    set(LAPLACE_PERFCACHE_HOT_BATCH_WIDTH_1 "${performance_width_1}")
    set(LAPLACE_PERFCACHE_HOT_BATCH_WIDTH_2 "${performance_width_2}")
    set(LAPLACE_PERFCACHE_HOT_BATCH_WIDTH_3 "${performance_width_3}")
    set(LAPLACE_PERFCACHE_GENERATION_PREPARED "${generation_prepared}")
    set(LAPLACE_PERFCACHE_GENERATION_RESERVED "${generation_reserved}")
    set(LAPLACE_PERFCACHE_GENERATION_ACTIVATED "${generation_activated}")
    set(LAPLACE_PERFCACHE_GENERATION_REJECTED "${generation_rejected}")
    set(LAPLACE_PERFCACHE_GENERATION_ABORTED "${generation_aborted}")
    set(LAPLACE_PERFCACHE_GENERATION_MATERIALIZED "${generation_materialized}")
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/perfcache.h.in"
        "${output_path}"
        @ONLY)
endfunction()
