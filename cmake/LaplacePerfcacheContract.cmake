function(laplace_configure_perfcache_contract contract_path output_path)
    file(READ "${contract_path}" contract_json)

    string(JSON contract_schema GET "${contract_json}" schema)
    string(JSON format_version GET "${contract_json}" format_version)
    string(JSON header_bytes GET "${contract_json}" header_bytes)
    string(JSON digest_bytes GET "${contract_json}" digest_bytes)
    string(JSON byte_order GET "${contract_json}" byte_order)
    string(JSON digest_algorithm GET "${contract_json}" digest_algorithm)
    string(JSON sorted_unique_access GET "${contract_json}" access_laws sorted_unique_fixed)
    string(JSON dense_u32_access GET "${contract_json}" access_laws dense_u32_zero_based)
    string(JSON module_defined_access GET "${contract_json}" access_laws module_defined)

    if(NOT contract_schema STREQUAL "laplace.perfcache-contract/v2")
        message(FATAL_ERROR "Unsupported perfcache contract schema: ${contract_schema}")
    endif()
    if(NOT format_version EQUAL 2 OR NOT header_bytes EQUAL 256)
        message(FATAL_ERROR "Perfcache format version or header width changed")
    endif()
    if(NOT digest_bytes EQUAL 32 OR NOT digest_algorithm STREQUAL "BLAKE3-256")
        message(FATAL_ERROR "Perfcache digest contract changed")
    endif()
    if(NOT byte_order STREQUAL "little-endian"
       OR NOT sorted_unique_access EQUAL 1
       OR NOT dense_u32_access EQUAL 2
       OR NOT module_defined_access EQUAL 3)
        message(FATAL_ERROR "Perfcache encoding contract changed")
    endif()

    set(LAPLACE_PERFCACHE_FORMAT_VERSION "${format_version}")
    set(LAPLACE_PERFCACHE_HEADER_BYTES "${header_bytes}")
    set(LAPLACE_PERFCACHE_DIGEST_BYTES "${digest_bytes}")
    set(LAPLACE_PERFCACHE_ACCESS_SORTED_UNIQUE_FIXED "${sorted_unique_access}")
    set(LAPLACE_PERFCACHE_ACCESS_DENSE_U32_ZERO_BASED "${dense_u32_access}")
    set(LAPLACE_PERFCACHE_ACCESS_MODULE_DEFINED "${module_defined_access}")
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/perfcache.h.in"
        "${output_path}"
        @ONLY)
endfunction()
