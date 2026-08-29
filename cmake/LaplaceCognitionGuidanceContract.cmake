function(laplace_configure_cognition_guidance_contract contract_path output_path)
    file(READ "${contract_path}" contract)
    string(JSON schema GET "${contract}" schema)
    string(JSON version GET "${contract}" version)
    string(JSON algorithm GET "${contract}" digest_algorithm)
    foreach(domain IN ITEMS state obligation projection fold operation decision transition)
        string(JSON domain_${domain} GET "${contract}" domains ${domain})
    endforeach()
    foreach(disposition IN ITEMS open satisfied unsupported denied exhausted contradicted unknown)
        string(JSON obligation_${disposition} GET "${contract}" obligation_dispositions ${disposition})
    endforeach()
    foreach(flag IN ITEMS required allow_typed_unresolved)
        string(JSON obligation_flag_${flag} GET "${contract}" obligation_flags ${flag})
    endforeach()
    foreach(kind IN ITEMS structural_calculation indexed_search research experiment clarification explicit_unresolved answer correct execute)
        string(JSON operation_${kind} GET "${contract}" operation_kinds ${kind})
    endforeach()
    foreach(flag IN ITEMS requires_effect_authority information_seeking terminal_act)
        string(JSON operation_flag_${flag} GET "${contract}" operation_flags ${flag})
    endforeach()
    foreach(value IN ITEMS incomplete complete blocked)
        string(JSON completion_${value} GET "${contract}" completion ${value})
    endforeach()
    if(NOT schema STREQUAL "laplace.cognition-guidance/v1" OR
       NOT version EQUAL 1 OR NOT algorithm STREQUAL "BLAKE3-256" OR
       NOT obligation_open EQUAL 1 OR NOT obligation_satisfied EQUAL 2 OR
       NOT obligation_unsupported EQUAL 3 OR NOT obligation_denied EQUAL 4 OR
       NOT obligation_exhausted EQUAL 5 OR NOT obligation_contradicted EQUAL 6 OR
       NOT obligation_unknown EQUAL 7 OR NOT obligation_flag_required EQUAL 1 OR
       NOT obligation_flag_allow_typed_unresolved EQUAL 2 OR
       NOT operation_structural_calculation EQUAL 1 OR NOT operation_indexed_search EQUAL 2 OR
       NOT operation_research EQUAL 3 OR NOT operation_experiment EQUAL 4 OR
       NOT operation_clarification EQUAL 5 OR NOT operation_explicit_unresolved EQUAL 6 OR
       NOT operation_answer EQUAL 7 OR NOT operation_correct EQUAL 8 OR
       NOT operation_execute EQUAL 9 OR
       NOT operation_flag_requires_effect_authority EQUAL 1 OR
       NOT operation_flag_information_seeking EQUAL 2 OR
       NOT operation_flag_terminal_act EQUAL 4 OR
       NOT completion_incomplete EQUAL 1 OR NOT completion_complete EQUAL 2 OR
       NOT completion_blocked EQUAL 3)
        message(FATAL_ERROR "Unsupported cognition-guidance contract")
    endif()
    set(LAPLACE_COGNITION_GUIDANCE_VERSION "${version}")
    foreach(domain IN ITEMS state obligation projection fold operation decision transition)
        string(TOUPPER "${domain}" symbol)
        set(LAPLACE_COGNITION_GUIDANCE_DOMAIN_${symbol} "${domain_${domain}}")
    endforeach()
    foreach(disposition IN ITEMS open satisfied unsupported denied exhausted contradicted unknown)
        string(TOUPPER "${disposition}" symbol)
        set(LAPLACE_COGNITION_OBLIGATION_${symbol} "${obligation_${disposition}}")
    endforeach()
    foreach(flag IN ITEMS required allow_typed_unresolved)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_COGNITION_OBLIGATION_FLAG_${symbol} "${obligation_flag_${flag}}")
    endforeach()
    foreach(kind IN ITEMS structural_calculation indexed_search research experiment clarification explicit_unresolved answer correct execute)
        string(TOUPPER "${kind}" symbol)
        set(LAPLACE_COGNITION_OPERATION_${symbol} "${operation_${kind}}")
    endforeach()
    foreach(flag IN ITEMS requires_effect_authority information_seeking terminal_act)
        string(TOUPPER "${flag}" symbol)
        set(LAPLACE_COGNITION_OPERATION_FLAG_${symbol} "${operation_flag_${flag}}")
    endforeach()
    foreach(value IN ITEMS incomplete complete blocked)
        string(TOUPPER "${value}" symbol)
        set(LAPLACE_COGNITION_COMPLETION_${symbol} "${completion_${value}}")
    endforeach()
    get_filename_component(output_directory "${output_path}" DIRECTORY)
    file(MAKE_DIRECTORY "${output_directory}")
    configure_file(
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/cognition_guidance.h.in"
        "${output_path}" @ONLY)
endfunction()
