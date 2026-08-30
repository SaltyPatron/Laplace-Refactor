include_guard(GLOBAL)

function(laplace_configure_source_profile_contract contract_path output)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${contract_path}")
    file(READ "${contract_path}" contract)
    string(JSON schema GET "${contract}" schema)
    string(JSON version GET "${contract}" version)
    string(JSON digest GET "${contract}" identity digest)
    string(JSON profile_domain GET "${contract}" identity profile_domain)
    string(JSON input_domain GET "${contract}" identity input_domain)
    string(JSON output_domain GET "${contract}" identity output_domain)
    string(JSON receipt_domain GET "${contract}" identity receipt_domain)
    string(JSON coordinate_kind GET "${contract}" coordinate_kind)
    string(JSON denominator_count LENGTH "${contract}" denominators)
    string(JSON disposition_count LENGTH "${contract}" dispositions)
    string(JSON reconstruction_count LENGTH "${contract}" reconstruction_classes)
    string(JSON flags_none GET "${contract}" flags none)
    string(JSON epistemic_class_shift GET "${contract}" flags epistemic_class_shift)
    string(JSON epistemic_class_mask GET "${contract}" flags epistemic_class_mask)
    string(JSON foundational_seed GET "${contract}" flags foundational_seed)
    string(JSON observation GET "${contract}" flags observation)
    string(JSON derived GET "${contract}" flags derived)
    string(JSON model GET "${contract}" flags model)
    string(JSON maximum_class GET "${contract}" flags maximum_class)
    string(JSON evidence_source_type_shift GET "${contract}" flags evidence_source_type_shift)
    string(JSON evidence_source_type_mask GET "${contract}" flags evidence_source_type_mask)
    string(JSON evidence_standard GET "${contract}" flags evidence_source_types standard)
    string(JSON evidence_curated_dataset GET "${contract}" flags evidence_source_types curated_dataset)
    string(JSON evidence_corpus GET "${contract}" flags evidence_source_types corpus)
    string(JSON evidence_direct_observation GET "${contract}" flags evidence_source_types direct_observation)
    string(JSON evidence_calculation GET "${contract}" flags evidence_source_types calculation)
    string(JSON evidence_model GET "${contract}" flags evidence_source_types model)
    string(JSON evidence_self_assertion GET "${contract}" flags evidence_source_types self_assertion)
    string(JSON evidence_external_provider GET "${contract}" flags evidence_source_types external_provider)
    string(JSON maximum_evidence_source_type GET "${contract}" flags maximum_evidence_source_type)
    string(JSON known_mask GET "${contract}" flags known_mask)
    if(NOT schema STREQUAL "laplace.source-profile-execution-contract/v1" OR
       NOT version EQUAL 1 OR NOT digest STREQUAL "BLAKE3-256" OR
       NOT coordinate_kind STREQUAL "source-profile" OR
       NOT denominator_count EQUAL 17 OR NOT disposition_count EQUAL 11 OR
       NOT reconstruction_count EQUAL 3 OR NOT flags_none EQUAL 0 OR
       NOT epistemic_class_shift EQUAL 0 OR NOT epistemic_class_mask EQUAL 15 OR
       NOT foundational_seed EQUAL 1 OR NOT observation EQUAL 2 OR
       NOT derived EQUAL 3 OR NOT model EQUAL 4 OR NOT maximum_class EQUAL 4 OR
       NOT evidence_source_type_shift EQUAL 4 OR
       NOT evidence_source_type_mask EQUAL 240 OR
       NOT evidence_standard EQUAL 1 OR NOT evidence_curated_dataset EQUAL 2 OR
       NOT evidence_corpus EQUAL 3 OR NOT evidence_direct_observation EQUAL 4 OR
       NOT evidence_calculation EQUAL 5 OR NOT evidence_model EQUAL 6 OR
       NOT evidence_self_assertion EQUAL 7 OR NOT evidence_external_provider EQUAL 8 OR
       NOT maximum_evidence_source_type EQUAL 8 OR NOT known_mask EQUAL 255)
        message(FATAL_ERROR "Source-profile execution contract changed incompatibly")
    endif()
    set(LAPLACE_SOURCE_PROFILE_VERSION "${version}")
    set(LAPLACE_SOURCE_PROFILE_PROFILE_DOMAIN "${profile_domain}")
    set(LAPLACE_SOURCE_PROFILE_INPUT_DOMAIN "${input_domain}")
    set(LAPLACE_SOURCE_PROFILE_OUTPUT_DOMAIN "${output_domain}")
    set(LAPLACE_SOURCE_PROFILE_RECEIPT_DOMAIN "${receipt_domain}")
    set(LAPLACE_SOURCE_PROFILE_EPISTEMIC_CLASS_SHIFT "${epistemic_class_shift}")
    set(LAPLACE_SOURCE_PROFILE_EPISTEMIC_CLASS_MASK "${epistemic_class_mask}")
    set(LAPLACE_SOURCE_PROFILE_EPISTEMIC_FOUNDATIONAL_SEED "${foundational_seed}")
    set(LAPLACE_SOURCE_PROFILE_EPISTEMIC_OBSERVATION "${observation}")
    set(LAPLACE_SOURCE_PROFILE_EPISTEMIC_DERIVED "${derived}")
    set(LAPLACE_SOURCE_PROFILE_EPISTEMIC_MODEL "${model}")
    set(LAPLACE_SOURCE_PROFILE_EPISTEMIC_MAX "${maximum_class}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_SOURCE_TYPE_SHIFT "${evidence_source_type_shift}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_SOURCE_TYPE_MASK "${evidence_source_type_mask}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_STANDARD "${evidence_standard}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_CURATED_DATASET "${evidence_curated_dataset}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_CORPUS "${evidence_corpus}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_DIRECT_OBSERVATION "${evidence_direct_observation}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_CALCULATION "${evidence_calculation}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_MODEL "${evidence_model}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_SELF_ASSERTION "${evidence_self_assertion}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_EXTERNAL_PROVIDER "${evidence_external_provider}")
    set(LAPLACE_SOURCE_PROFILE_EVIDENCE_MAX "${maximum_evidence_source_type}")
    set(LAPLACE_SOURCE_PROFILE_FLAGS_KNOWN_MASK "${known_mask}")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/source_profile.h.in" "${output}" @ONLY)
endfunction()
