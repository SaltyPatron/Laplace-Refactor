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
    string(JSON generation_prepared GET "${contract_json}" generation_dispositions prepared)
    string(JSON generation_reserved GET "${contract_json}" generation_dispositions reserved)
    string(JSON generation_activated GET "${contract_json}" generation_dispositions activated)
    string(JSON generation_rejected GET "${contract_json}" generation_dispositions rejected)
    string(JSON generation_aborted GET "${contract_json}" generation_dispositions aborted)

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
       OR NOT module_major EQUAL 1 OR NOT module_minor EQUAL 1
       OR NOT generation_prepared EQUAL 1 OR NOT generation_reserved EQUAL 2
       OR NOT generation_activated EQUAL 3 OR NOT generation_rejected EQUAL 4
       OR NOT generation_aborted EQUAL 5)
        message(FATAL_ERROR "Perfcache encoding contract changed")
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
    set(LAPLACE_PERFCACHE_GENERATION_PREPARED "${generation_prepared}")
    set(LAPLACE_PERFCACHE_GENERATION_RESERVED "${generation_reserved}")
    set(LAPLACE_PERFCACHE_GENERATION_ACTIVATED "${generation_activated}")
    set(LAPLACE_PERFCACHE_GENERATION_REJECTED "${generation_rejected}")
    set(LAPLACE_PERFCACHE_GENERATION_ABORTED "${generation_aborted}")
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/perfcache.h.in"
        "${output_path}"
        @ONLY)
endfunction()
