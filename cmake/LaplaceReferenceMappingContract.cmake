function(laplace_configure_reference_mapping_contract contract_path output_path)
    file(READ "${contract_path}" contract)
    string(JSON schema GET "${contract}" schema)
    string(JSON version GET "${contract}" version)
    string(JSON algorithm GET "${contract}" digest_algorithm)
    foreach(domain IN ITEMS proposition occurrence record input output receipt)
        string(JSON ${domain}_domain GET "${contract}" ${domain}_domain)
    endforeach()
    foreach(flag IN ITEMS directed symmetric)
        string(JSON flag_${flag} GET "${contract}" flags ${flag})
    endforeach()
    foreach(disposition IN ITEMS resolved left_unresolved right_unresolved both_unresolved retired_endpoint)
        string(JSON disposition_${disposition} GET "${contract}" dispositions ${disposition})
    endforeach()
    if(NOT schema STREQUAL "laplace.reference-mapping/v1" OR
       NOT version EQUAL 1 OR NOT algorithm STREQUAL "BLAKE3-256" OR
       NOT flag_directed EQUAL 1 OR NOT flag_symmetric EQUAL 2 OR
       NOT disposition_resolved EQUAL 1 OR
       NOT disposition_left_unresolved EQUAL 2 OR
       NOT disposition_right_unresolved EQUAL 3 OR
       NOT disposition_both_unresolved EQUAL 4 OR
       NOT disposition_retired_endpoint EQUAL 5)
        message(FATAL_ERROR "Unsupported reference-mapping contract")
    endif()
    set(LAPLACE_REFERENCE_MAPPING_VERSION "${version}")
    foreach(domain IN ITEMS proposition occurrence record input output receipt)
        string(TOUPPER "${domain}" symbol)
        set(LAPLACE_REFERENCE_MAPPING_${symbol}_DOMAIN "${${domain}_domain}")
    endforeach()
    foreach(flag IN ITEMS directed symmetric)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_REFERENCE_MAPPING_FLAG_${symbol} "${flag_${flag}}")
    endforeach()
    foreach(disposition IN ITEMS resolved left_unresolved right_unresolved both_unresolved retired_endpoint)
        string(TOUPPER "${disposition}" symbol)
        set(LAPLACE_REFERENCE_MAPPING_DISPOSITION_${symbol} "${disposition_${disposition}}")
    endforeach()
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/reference_mapping.h.in"
        "${output_path}" @ONLY)
endfunction()
