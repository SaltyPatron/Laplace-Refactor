function(laplace_configure_query_search_contract contract_path output_path)
    file(READ "${contract_path}" contract)
    string(JSON schema GET "${contract}" schema)
    string(JSON version GET "${contract}" version)
    string(JSON algorithm GET "${contract}" digest_algorithm)
    string(JSON cost_component_count GET "${contract}" cost_component_count)
    foreach(domain IN ITEMS program state transition path input output receipt)
        string(JSON domain_${domain} GET "${contract}" domains ${domain})
    endforeach()
    foreach(flag IN ITEMS astar heuristic_admissible heuristic_consistent allow_reopen boundary_complete request_optimal require_set_oriented)
        string(JSON flag_${flag} GET "${contract}" flags ${flag})
    endforeach()
    string(JSON state_terminal GET "${contract}" state_flags terminal)
    foreach(disposition IN ITEMS complete upper_bound unsupported denied exhausted contradicted no_admissible_path incomplete_boundary partial unknown)
        string(JSON disposition_${disposition} GET "${contract}" dispositions ${disposition})
    endforeach()
    foreach(flag IN ITEMS frontier_exhausted optimal_certified resource_exhausted boundary_complete)
        string(JSON receipt_${flag} GET "${contract}" receipt_flags ${flag})
    endforeach()
    if(NOT schema STREQUAL "laplace.query-search/v1" OR
       NOT version EQUAL 1 OR NOT algorithm STREQUAL "BLAKE3-256" OR
       NOT cost_component_count EQUAL 8 OR
       NOT flag_astar EQUAL 1 OR NOT flag_heuristic_admissible EQUAL 2 OR
       NOT flag_heuristic_consistent EQUAL 4 OR NOT flag_allow_reopen EQUAL 8 OR
       NOT flag_boundary_complete EQUAL 16 OR NOT flag_request_optimal EQUAL 32 OR
       NOT flag_require_set_oriented EQUAL 64 OR NOT state_terminal EQUAL 1 OR
       NOT disposition_complete EQUAL 1 OR NOT disposition_upper_bound EQUAL 2 OR
       NOT disposition_unsupported EQUAL 3 OR NOT disposition_denied EQUAL 4 OR
       NOT disposition_exhausted EQUAL 5 OR NOT disposition_contradicted EQUAL 6 OR
       NOT disposition_no_admissible_path EQUAL 7 OR
       NOT disposition_incomplete_boundary EQUAL 8 OR NOT disposition_partial EQUAL 9 OR
       NOT disposition_unknown EQUAL 10 OR NOT receipt_frontier_exhausted EQUAL 1 OR
       NOT receipt_optimal_certified EQUAL 2 OR NOT receipt_resource_exhausted EQUAL 4 OR
       NOT receipt_boundary_complete EQUAL 8)
        message(FATAL_ERROR "Unsupported query-search contract")
    endif()
    set(LAPLACE_QUERY_SEARCH_VERSION "${version}")
    set(LAPLACE_QUERY_SEARCH_COST_COMPONENT_COUNT "${cost_component_count}")
    foreach(domain IN ITEMS program state transition path input output receipt)
        string(TOUPPER "${domain}" symbol)
        set(LAPLACE_QUERY_SEARCH_DOMAIN_${symbol} "${domain_${domain}}")
    endforeach()
    foreach(flag IN ITEMS astar heuristic_admissible heuristic_consistent allow_reopen boundary_complete request_optimal require_set_oriented)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_QUERY_SEARCH_FLAG_${symbol} "${flag_${flag}}")
    endforeach()
    set(LAPLACE_QUERY_SEARCH_STATE_TERMINAL "${state_terminal}")
    foreach(disposition IN ITEMS complete upper_bound unsupported denied exhausted contradicted no_admissible_path incomplete_boundary partial unknown)
        string(TOUPPER "${disposition}" symbol)
        set(LAPLACE_QUERY_SEARCH_DISPOSITION_${symbol} "${disposition_${disposition}}")
    endforeach()
    foreach(flag IN ITEMS frontier_exhausted optimal_certified resource_exhausted boundary_complete)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_QUERY_SEARCH_RECEIPT_${symbol} "${receipt_${flag}}")
    endforeach()
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/query_search.h.in"
        "${output_path}" @ONLY)
endfunction()
