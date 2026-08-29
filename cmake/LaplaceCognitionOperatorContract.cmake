function(laplace_configure_cognition_operator_contract contract_path output_path)
    file(READ "${contract_path}" contract)
    string(JSON schema GET "${contract}" schema)
    string(JSON version GET "${contract}" version)
    string(JSON algorithm GET "${contract}" digest_algorithm)
    foreach(domain IN ITEMS program field constraint operator application materialization)
        string(JSON domain_${domain} GET "${contract}" domains ${domain})
    endforeach()
    foreach(source IN ITEMS physicality testimony derived)
        string(JSON source_${source} GET "${contract}" constraint_sources ${source})
    endforeach()
    foreach(direction IN ITEMS source_to_target target_to_source symmetric)
        string(JSON direction_${direction} GET "${contract}" directions ${direction})
    endforeach()
    foreach(transport IN ITEMS identity signed affine ordinal containment semantic temporal custom)
        string(JSON transport_${transport} GET "${contract}" transport_kinds ${transport})
    endforeach()
    foreach(flag IN ITEMS container role observed derived)
        string(JSON field_flag_${flag} GET "${contract}" field_flags ${flag})
    endforeach()
    foreach(flag IN ITEMS has_target independent_root)
        string(JSON constraint_flag_${flag} GET "${contract}" constraint_flags ${flag})
    endforeach()
    foreach(flag IN ITEMS require_positive_semidefinite_precision require_relation_plane_separation require_matrix_free_materialized_parity)
        string(JSON program_flag_${flag} GET "${contract}" program_flags ${flag})
    endforeach()
    if(NOT schema STREQUAL "laplace.cognition-operator/v1" OR
       NOT version EQUAL 1 OR NOT algorithm STREQUAL "BLAKE3-256" OR
       NOT source_physicality EQUAL 1 OR NOT source_testimony EQUAL 2 OR
       NOT source_derived EQUAL 3 OR
       NOT direction_source_to_target EQUAL 1 OR NOT direction_target_to_source EQUAL 2 OR
       NOT direction_symmetric EQUAL 3 OR
       NOT transport_identity EQUAL 1 OR NOT transport_signed EQUAL 2 OR
       NOT transport_affine EQUAL 3 OR NOT transport_ordinal EQUAL 4 OR
       NOT transport_containment EQUAL 5 OR NOT transport_semantic EQUAL 6 OR
       NOT transport_temporal EQUAL 7 OR NOT transport_custom EQUAL 8 OR
       NOT field_flag_container EQUAL 1 OR NOT field_flag_role EQUAL 2 OR
       NOT field_flag_observed EQUAL 4 OR NOT field_flag_derived EQUAL 8 OR
       NOT constraint_flag_has_target EQUAL 1 OR
       NOT constraint_flag_independent_root EQUAL 2 OR
       NOT program_flag_require_positive_semidefinite_precision EQUAL 1 OR
       NOT program_flag_require_relation_plane_separation EQUAL 2 OR
       NOT program_flag_require_matrix_free_materialized_parity EQUAL 4)
        message(FATAL_ERROR "Unsupported cognition-operator contract")
    endif()
    set(LAPLACE_COGNITION_OPERATOR_VERSION "${version}")
    foreach(domain IN ITEMS program field constraint operator application materialization)
        string(TOUPPER "${domain}" symbol)
        set(LAPLACE_COGNITION_OPERATOR_DOMAIN_${symbol} "${domain_${domain}}")
    endforeach()
    foreach(source IN ITEMS physicality testimony derived)
        string(TOUPPER "${source}" symbol)
        set(LAPLACE_COGNITION_OPERATOR_SOURCE_${symbol} "${source_${source}}")
    endforeach()
    foreach(direction IN ITEMS source_to_target target_to_source symmetric)
        string(TOUPPER "${direction}" symbol)
        set(LAPLACE_COGNITION_OPERATOR_DIRECTION_${symbol} "${direction_${direction}}")
    endforeach()
    foreach(transport IN ITEMS identity signed affine ordinal containment semantic temporal custom)
        string(TOUPPER "${transport}" symbol)
        set(LAPLACE_COGNITION_OPERATOR_TRANSPORT_${symbol} "${transport_${transport}}")
    endforeach()
    foreach(flag IN ITEMS container role observed derived)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_COGNITION_OPERATOR_FIELD_${symbol} "${field_flag_${flag}}")
    endforeach()
    foreach(flag IN ITEMS has_target independent_root)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_COGNITION_OPERATOR_CONSTRAINT_${symbol} "${constraint_flag_${flag}}")
    endforeach()
    foreach(flag IN ITEMS require_positive_semidefinite_precision require_relation_plane_separation require_matrix_free_materialized_parity)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_COGNITION_OPERATOR_PROGRAM_${symbol} "${program_flag_${flag}}")
    endforeach()
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cognition_operator.h.in"
        "${output_path}" @ONLY)
endfunction()
