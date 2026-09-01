set(_laplace_collation_archive
    "${LAPLACE_UNICODE_SOURCE_ROOT}/uca/CollationTest.zip")
if(EXISTS "${_laplace_collation_archive}")
    file(SHA256 "${_laplace_collation_archive}" _laplace_collation_archive_sha256)
    if(NOT _laplace_collation_archive_sha256 STREQUAL
       "9ba92cb7627c2d09ba537dc2035d006f44f0cafb4e2198f147897fdd9d71bb10")
        message(FATAL_ERROR
            "Pinned Unicode CollationTest.zip does not match the UCA 17 contract")
    endif()
    set(_laplace_collation_extract_root
        "${CMAKE_CURRENT_BINARY_DIR}/unicode-collation-17")
    set(_laplace_collation_non_ignorable
        "${_laplace_collation_extract_root}/CollationTest/CollationTest_NON_IGNORABLE.txt")
    set(_laplace_collation_shifted
        "${_laplace_collation_extract_root}/CollationTest/CollationTest_SHIFTED.txt")
    set(_laplace_extract_collation TRUE)
    if(EXISTS "${_laplace_collation_non_ignorable}" AND
       EXISTS "${_laplace_collation_shifted}")
        file(SHA256 "${_laplace_collation_non_ignorable}"
            _laplace_collation_non_ignorable_sha256)
        file(SHA256 "${_laplace_collation_shifted}"
            _laplace_collation_shifted_sha256)
        if(_laplace_collation_non_ignorable_sha256 STREQUAL
               "6c43c92ea9c87fcefa94a2e362fb31fb0650d5c80ae420b46d9ccc02784a0c5f" AND
           _laplace_collation_shifted_sha256 STREQUAL
               "cbeecf871a3567e8229c869a1f7c36e777c077ee2be6470efe4fb17f01886d4b")
            set(_laplace_extract_collation FALSE)
        endif()
    endif()
    if(_laplace_extract_collation)
        file(REMOVE_RECURSE "${_laplace_collation_extract_root}")
        file(MAKE_DIRECTORY "${_laplace_collation_extract_root}")
        file(ARCHIVE_EXTRACT
            INPUT "${_laplace_collation_archive}"
            DESTINATION "${_laplace_collation_extract_root}")
        foreach(_laplace_suite IN ITEMS non_ignorable shifted)
            file(SHA256 "${_laplace_collation_${_laplace_suite}}"
                _laplace_collation_${_laplace_suite}_sha256)
        endforeach()
        if(NOT _laplace_collation_non_ignorable_sha256 STREQUAL
               "6c43c92ea9c87fcefa94a2e362fb31fb0650d5c80ae420b46d9ccc02784a0c5f" OR
           NOT _laplace_collation_shifted_sha256 STREQUAL
               "cbeecf871a3567e8229c869a1f7c36e777c077ee2be6470efe4fb17f01886d4b")
            message(FATAL_ERROR
                "Extracted Unicode 17 collation suites do not match their contracts")
        endif()
    endif()
    add_compile_definitions(
        LAPLACE_UNICODE_COLLATION_TEST_ROOT="${_laplace_collation_extract_root}")
endif()

add_executable(laplace_identity_tests
    identity_tests.cpp)
target_link_libraries(laplace_identity_tests
    PRIVATE
        Laplace::Identity
        GTest::gtest_main)
target_compile_options(laplace_identity_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

include(GoogleTest)
gtest_discover_tests(laplace_identity_tests
    PROPERTIES LABELS "implementation;identity")

add_executable(laplace_composition_tests
    composition_tests.cpp)
target_link_libraries(laplace_composition_tests PRIVATE
    Laplace::Composition GTest::gtest_main)
target_compile_options(laplace_composition_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_composition_tests PROPERTIES
    LABELS "implementation;composition;working-set;producer")

add_executable(laplace_tabular_source_tests
    tabular_source_tests.cpp)
target_link_libraries(laplace_tabular_source_tests PRIVATE
    Laplace::TabularSource Laplace::Composition GTest::gtest_main)
target_include_directories(laplace_tabular_source_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src")
target_compile_options(laplace_tabular_source_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_tabular_source_tests PROPERTIES
    LABELS "implementation;source-profile;grammar;recipe;ast;composition")

add_executable(laplace_iso_639_source_tests
    iso_639_source_tests.cpp)
target_link_libraries(laplace_iso_639_source_tests PRIVATE
    Laplace::TabularSource GTest::gtest_main)
target_include_directories(laplace_iso_639_source_tests PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_compile_options(laplace_iso_639_source_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_iso_639_source_tests PROPERTIES
    LABELS "implementation;source-profile;iso-639;grammar;recipe;ast;reconstruction"
    ENVIRONMENT "LAPLACE_ISO_639_SOURCE_ROOT=${LAPLACE_ISO_639_SOURCE_ROOT}")

add_executable(laplace_cili_source_tests
    cili_source_tests.cpp)
target_link_libraries(laplace_cili_source_tests PRIVATE
    Laplace::TabularSource GTest::gtest_main)
target_include_directories(laplace_cili_source_tests PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_compile_options(laplace_cili_source_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
target_compile_definitions(laplace_cili_source_tests PRIVATE
    LAPLACE_CILI_TEST_SOURCE_ROOT="${LAPLACE_CILI_SOURCE_ROOT}")
gtest_discover_tests(laplace_cili_source_tests PROPERTIES
    LABELS "implementation;source-profile;cili;grammar;recipe;ast;reconstruction;mapping"
    ENVIRONMENT "LAPLACE_CILI_SOURCE_ROOT=${LAPLACE_CILI_SOURCE_ROOT}")

add_library(laplace_tabular_parent_edge_mutant STATIC
    ../engine/src/tabular_source.cpp)
target_include_directories(laplace_tabular_parent_edge_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_tabular_parent_edge_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_tabular_parent_edge_mutant PRIVATE
    LAPLACE_TEST_TABULAR_OMIT_PARENT_EDGE=1)
target_compile_options(laplace_tabular_parent_edge_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_tabular_parent_edge_mutation_probe
    tabular_source_tests.cpp)
target_include_directories(laplace_tabular_parent_edge_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src")
target_link_libraries(laplace_tabular_parent_edge_mutation_probe PRIVATE
    laplace_tabular_parent_edge_mutant Laplace::Engine GTest::gtest_main)
target_compile_options(laplace_tabular_parent_edge_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME tabular-source.mutation-parent-edge-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_tabular_parent_edge_mutation_probe>"
        "-DFILTER=TabularSource.ArtifactGraphIdentityIncludesParentContainment"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    tabular-source.mutation-parent-edge-omission-detected PROPERTIES
    LABELS "implementation;source-profile;artifact-graph;mutation")

add_library(laplace_tabular_reconstruction_bypass_mutant STATIC
    ../engine/src/tabular_source.cpp)
target_include_directories(laplace_tabular_reconstruction_bypass_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_tabular_reconstruction_bypass_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_tabular_reconstruction_bypass_mutant PRIVATE
    LAPLACE_TEST_TABULAR_CORRUPT_PARSED_RECONSTRUCTION=1
    LAPLACE_TEST_TABULAR_RECOMPOSE_FROM_INPUT=1)
target_compile_options(laplace_tabular_reconstruction_bypass_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_tabular_reconstruction_bypass_mutation_probe
    tabular_source_tests.cpp)
target_include_directories(
    laplace_tabular_reconstruction_bypass_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src")
target_link_libraries(
    laplace_tabular_reconstruction_bypass_mutation_probe PRIVATE
    laplace_tabular_reconstruction_bypass_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(
    laplace_tabular_reconstruction_bypass_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME tabular-source.mutation-input-reconstruction-bypass-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_tabular_reconstruction_bypass_mutation_probe>"
        "-DFILTER=TabularSource.CompilesRawAndDelimitedArtifactsIntoOneExactAstPlan"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    tabular-source.mutation-input-reconstruction-bypass-detected PROPERTIES
    LABELS "implementation;source-profile;reconstruction;mutation")

add_library(laplace_tabular_reference_promotion_mutant STATIC
    ../engine/src/tabular_source.cpp)
target_include_directories(laplace_tabular_reference_promotion_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_tabular_reference_promotion_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_tabular_reference_promotion_mutant PRIVATE
    LAPLACE_TEST_TABULAR_PROMOTE_REFERENCES_RESOLVED=1)
target_compile_options(laplace_tabular_reference_promotion_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_tabular_reference_promotion_mutation_probe
    tabular_source_tests.cpp)
target_include_directories(
    laplace_tabular_reference_promotion_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src")
target_link_libraries(
    laplace_tabular_reference_promotion_mutation_probe PRIVATE
    laplace_tabular_reference_promotion_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(
    laplace_tabular_reference_promotion_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME tabular-source.mutation-structural-reference-promotion-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_tabular_reference_promotion_mutation_probe>"
        "-DFILTER=TabularSource.CompilesRawAndDelimitedArtifactsIntoOneExactAstPlan"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    tabular-source.mutation-structural-reference-promotion-detected PROPERTIES
    LABELS "implementation;source-profile;reference;highway;mutation")

add_library(laplace_tabular_field_claim_promotion_mutant STATIC
    ../engine/src/tabular_source.cpp)
target_include_directories(laplace_tabular_field_claim_promotion_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_tabular_field_claim_promotion_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_tabular_field_claim_promotion_mutant PRIVATE
    LAPLACE_TEST_TABULAR_PROMOTE_FIELDS_TO_CLAIMS=1)
target_compile_options(laplace_tabular_field_claim_promotion_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_tabular_field_claim_promotion_mutation_probe
    tabular_source_tests.cpp)
target_include_directories(
    laplace_tabular_field_claim_promotion_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/src")
target_link_libraries(
    laplace_tabular_field_claim_promotion_mutation_probe PRIVATE
    laplace_tabular_field_claim_promotion_mutant
    Laplace::Engine
    GTest::gtest_main)
target_compile_options(
    laplace_tabular_field_claim_promotion_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME tabular-source.mutation-field-claim-promotion-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_tabular_field_claim_promotion_mutation_probe>"
        "-DFILTER=TabularSource.CompilesRawAndDelimitedArtifactsIntoOneExactAstPlan"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    tabular-source.mutation-field-claim-promotion-detected PROPERTIES
    LABELS "implementation;source-profile;ast;evidence;mutation")

add_library(laplace_composition_dedup_mutant STATIC
    ../engine/src/composition.cpp)
target_include_directories(laplace_composition_dedup_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_composition_dedup_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_composition_dedup_mutant PRIVATE
    LAPLACE_TEST_COMPOSITION_DISABLE_WORKING_SET_DEDUP=1)
target_compile_options(laplace_composition_dedup_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_composition_dedup_mutation_probe
    composition_tests.cpp)
target_link_libraries(laplace_composition_dedup_mutation_probe PRIVATE
    laplace_composition_dedup_mutant Laplace::Engine GTest::gtest_main)
target_compile_options(laplace_composition_dedup_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME composition.mutation-whole-working-set-dedup-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_composition_dedup_mutation_probe>"
        "-DFILTER=CompositionWorkingSet.BuildsDeduplicatedTopologicalDagAndProducer"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    composition.mutation-whole-working-set-dedup-detected PROPERTIES
    LABELS "implementation;composition;working-set;producer;mutation")

add_library(laplace_composition_presence_mutant STATIC
    ../engine/src/composition.cpp)
target_include_directories(laplace_composition_presence_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_composition_presence_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_composition_presence_mutant PRIVATE
    LAPLACE_TEST_COMPOSITION_IGNORE_PRESENCE=1)
target_compile_options(laplace_composition_presence_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_composition_presence_mutation_probe
    composition_tests.cpp)
target_link_libraries(laplace_composition_presence_mutation_probe PRIVATE
    laplace_composition_presence_mutant Laplace::Engine GTest::gtest_main)
target_compile_options(laplace_composition_presence_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME composition.mutation-presence-filter-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_composition_presence_mutation_probe>"
        "-DFILTER=CompositionWorkingSet.PresenceFiltersExactStateBeforePublication"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    composition.mutation-presence-filter-omission-detected PROPERTIES
    LABELS "implementation;composition;presence;producer;mutation")

if(UNIX)
    add_executable(laplace_spool_tests
        spool_tests.cpp)
    target_link_libraries(laplace_spool_tests PRIVATE
        Laplace::Framework GTest::gtest_main)
    target_compile_options(laplace_spool_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    gtest_discover_tests(laplace_spool_tests
        PROPERTIES LABELS "implementation;framework;spool;producer")

    add_library(laplace_spool_ordinal_mutant STATIC
        ../engine/src/spool.cpp)
    target_include_directories(laplace_spool_ordinal_mutant PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_spool_ordinal_mutant PRIVATE
        Laplace::Framework BLAKE3::blake3)
    target_compile_definitions(laplace_spool_ordinal_mutant PRIVATE
        LAPLACE_TEST_SPOOL_SKIP_ORDINAL_VALIDATION=1)
    target_compile_options(laplace_spool_ordinal_mutant PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_executable(laplace_spool_ordinal_mutation_probe
        spool_tests.cpp)
    target_include_directories(laplace_spool_ordinal_mutation_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_spool_ordinal_mutation_probe PRIVATE
        laplace_spool_ordinal_mutant Laplace::Framework GTest::gtest_main)
    target_compile_options(laplace_spool_ordinal_mutation_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME spool.mutation-ordinal-validation-omission-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:laplace_spool_ordinal_mutation_probe>"
            "-DFILTER=CanonicalSpool.RejectsOrdinalAndTypeDefectsBeforeWritingAnyBytes"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
    set_tests_properties(
        spool.mutation-ordinal-validation-omission-detected PROPERTIES
        LABELS "implementation;framework;spool;producer;mutation")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME unicode-root.contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/unicode_root_contract_tests.py")
set_tests_properties(unicode-root.contract-and-mutation PROPERTIES
    LABELS "contracts;unicode;identity;geometry;mutation")

add_test(
    NAME unicode-source.generated-manifest-contract
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/unicode_source_generation_tests.py")
set_tests_properties(unicode-source.generated-manifest-contract PROPERTIES
    LABELS "contracts;unicode;source;generation;mutation")

add_test(
    NAME iso-639-source.profile-contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/iso_639_source_profile_tests.py")
set_tests_properties(iso-639-source.profile-contract-and-mutation PROPERTIES
    LABELS "contracts;source-profile;iso-639;artifact-graph;reconstruction;mutation"
    ENVIRONMENT "LAPLACE_ISO_639_SOURCE_ROOT=${LAPLACE_ISO_639_SOURCE_ROOT}")

add_test(
    NAME cili-source.profile-contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cili_source_profile_tests.py")
set_tests_properties(cili-source.profile-contract-and-mutation PROPERTIES
    LABELS "contracts;source-profile;cili;artifact-graph;reconstruction;mapping;mutation"
    ENVIRONMENT "LAPLACE_CILI_SOURCE_ROOT=${LAPLACE_CILI_SOURCE_ROOT}")

add_test(
    NAME tabular-source.acquisition-contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tabular_source_acquisition_tests.py")
set_tests_properties(tabular-source.acquisition-contract-and-mutation PROPERTIES
    LABELS "acquisition;contracts;source-profile;artifact-graph;reconstruction;mutation")

add_test(
    NAME highway.generated-mirror-and-append-only-contract
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/highway_generation_tests.py")
set_tests_properties(highway.generated-mirror-and-append-only-contract PROPERTIES
    LABELS "contracts;highway;generation;parity;mutation")

add_executable(laplace_highway_tests highway_tests.cpp)
target_link_libraries(laplace_highway_tests PRIVATE
    Laplace::Highway GTest::gtest_main)
target_compile_options(laplace_highway_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_highway_tests PROPERTIES
    LABELS "implementation;highway;coordinate;batch")

add_library(laplace_highway_release_scope_mutant STATIC
    ../engine/src/highway.c)
target_include_directories(laplace_highway_release_scope_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_highway_release_scope_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_highway_release_scope_mutant PRIVATE
    LAPLACE_TEST_HIGHWAY_OMIT_RELEASE_SCOPE=1)
target_compile_options(laplace_highway_release_scope_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_highway_release_scope_mutation_probe highway_tests.cpp)
target_include_directories(laplace_highway_release_scope_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_highway_release_scope_mutation_probe PRIVATE
    laplace_highway_release_scope_mutant Laplace::Framework GTest::gtest_main)
target_compile_options(laplace_highway_release_scope_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME highway.mutation-release-scope-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_highway_release_scope_mutation_probe>"
        "-DFILTER=HighwayCoordinate.ExactScopeAndVersionDetermineDistinctCoordinates"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(highway.mutation-release-scope-omission-detected PROPERTIES
    LABELS "implementation;highway;coordinate;mutation")

add_executable(laplace_evidence_lineage_tests evidence_lineage_tests.cpp)
target_link_libraries(laplace_evidence_lineage_tests PRIVATE
    Laplace::EvidenceLineage GTest::gtest_main)
target_compile_options(laplace_evidence_lineage_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_evidence_lineage_tests PROPERTIES
    LABELS "implementation;evidence;lineage;dependence;receipt")

add_executable(laplace_reference_topology_tests reference_topology_tests.cpp)
target_link_libraries(laplace_reference_topology_tests PRIVATE
    Laplace::ReferenceTopology GTest::gtest_main)
target_compile_options(laplace_reference_topology_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_reference_topology_tests PROPERTIES
    LABELS "implementation;reference-topology;highway;receipt")

add_executable(laplace_reference_mapping_tests reference_mapping_tests.cpp)
target_link_libraries(laplace_reference_mapping_tests PRIVATE
    Laplace::ReferenceMapping GTest::gtest_main)
target_compile_options(laplace_reference_mapping_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_reference_mapping_tests PROPERTIES
    LABELS "implementation;reference-mapping;reference-topology;highway;receipt")

foreach(mapping_mutation IN ITEMS
        promote_unresolved
        collapse_direction
        witness_in_proposition)
    string(TOUPPER "${mapping_mutation}" mapping_mutation_symbol)
    add_library(laplace_reference_mapping_${mapping_mutation}_mutant STATIC
        ../engine/src/reference_mapping.c)
    target_include_directories(
        laplace_reference_mapping_${mapping_mutation}_mutant PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(
        laplace_reference_mapping_${mapping_mutation}_mutant PRIVATE
        Laplace::Highway BLAKE3::blake3)
    target_compile_definitions(
        laplace_reference_mapping_${mapping_mutation}_mutant PRIVATE
        LAPLACE_TEST_REFERENCE_MAPPING_${mapping_mutation_symbol}=1)
    target_compile_options(
        laplace_reference_mapping_${mapping_mutation}_mutant PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
    add_executable(laplace_reference_mapping_${mapping_mutation}_mutation_probe
        reference_mapping_tests.cpp)
    target_include_directories(
        laplace_reference_mapping_${mapping_mutation}_mutation_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(
        laplace_reference_mapping_${mapping_mutation}_mutation_probe PRIVATE
        laplace_reference_mapping_${mapping_mutation}_mutant GTest::gtest_main)
    target_compile_definitions(
        laplace_reference_mapping_${mapping_mutation}_mutation_probe PRIVATE
        LAPLACE_TEST_REFERENCE_MAPPING_${mapping_mutation_symbol}=1)
    target_compile_options(
        laplace_reference_mapping_${mapping_mutation}_mutation_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
endforeach()

add_test(
    NAME reference-mapping.mutation-unresolved-promotion-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_reference_mapping_promote_unresolved_mutation_probe>"
        "-DFILTER=ReferenceMapping.KeepsUnresolvedAndRetiredEndpointsDistinct"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
add_test(
    NAME reference-mapping.mutation-direction-collapse-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_reference_mapping_collapse_direction_mutation_probe>"
        "-DFILTER=ReferenceMapping.PreservesDirectionAndCanonicalizesOnlySymmetricRelations"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
add_test(
    NAME reference-mapping.mutation-witness-identity-leak-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_reference_mapping_witness_in_proposition_mutation_probe>"
        "-DFILTER=ReferenceMapping.FoldsWitnessOccurrencesWithoutMultiplyingPropositions"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    reference-mapping.mutation-unresolved-promotion-detected
    reference-mapping.mutation-direction-collapse-detected
    reference-mapping.mutation-witness-identity-leak-detected
    PROPERTIES LABELS "implementation;reference-mapping;mutation")

add_library(laplace_reference_topology_unresolved_mutant STATIC
    ../engine/src/reference_topology.c)
target_include_directories(laplace_reference_topology_unresolved_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_reference_topology_unresolved_mutant PRIVATE
    Laplace::Highway BLAKE3::blake3)
target_compile_definitions(laplace_reference_topology_unresolved_mutant PRIVATE
    LAPLACE_TEST_REFERENCE_PROMOTE_UNRESOLVED=1)
target_compile_options(laplace_reference_topology_unresolved_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_reference_topology_unresolved_mutation_probe
    reference_topology_tests.cpp)
target_include_directories(laplace_reference_topology_unresolved_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_reference_topology_unresolved_mutation_probe PRIVATE
    laplace_reference_topology_unresolved_mutant GTest::gtest_main)
target_compile_definitions(laplace_reference_topology_unresolved_mutation_probe PRIVATE
    LAPLACE_TEST_REFERENCE_PROMOTE_UNRESOLVED=1)
target_compile_options(laplace_reference_topology_unresolved_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME reference-topology.mutation-unresolved-promotion-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_reference_topology_unresolved_mutation_probe>"
        "-DFILTER=ReferenceTopology.NamespaceRemainsInCoordinateScope"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    reference-topology.mutation-unresolved-promotion-detected PROPERTIES
    LABELS "implementation;reference-topology;mutation")

add_library(laplace_reference_topology_conflict_mutant STATIC
    ../engine/src/reference_topology.c)
target_include_directories(laplace_reference_topology_conflict_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_reference_topology_conflict_mutant PRIVATE
    Laplace::Highway BLAKE3::blake3)
target_compile_definitions(laplace_reference_topology_conflict_mutant PRIVATE
    LAPLACE_TEST_REFERENCE_IGNORE_DECLARATION_CONFLICT=1)
target_compile_options(laplace_reference_topology_conflict_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_reference_topology_conflict_mutation_probe
    reference_topology_tests.cpp)
target_include_directories(laplace_reference_topology_conflict_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_reference_topology_conflict_mutation_probe PRIVATE
    laplace_reference_topology_conflict_mutant GTest::gtest_main)
target_compile_definitions(laplace_reference_topology_conflict_mutation_probe PRIVATE
    LAPLACE_TEST_REFERENCE_IGNORE_DECLARATION_CONFLICT=1)
target_compile_options(laplace_reference_topology_conflict_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME reference-topology.mutation-declaration-conflict-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_reference_topology_conflict_mutation_probe>"
        "-DFILTER=ReferenceTopology.RejectsConflictAndDuplicateOccurrence"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    reference-topology.mutation-declaration-conflict-detected PROPERTIES
    LABELS "implementation;reference-topology;mutation")

add_library(laplace_reference_topology_field_identity_mutant STATIC
    ../engine/src/reference_topology.c)
target_include_directories(laplace_reference_topology_field_identity_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_reference_topology_field_identity_mutant PRIVATE
    Laplace::Highway BLAKE3::blake3)
target_compile_definitions(laplace_reference_topology_field_identity_mutant PRIVATE
    LAPLACE_TEST_REFERENCE_USE_FIELD_AS_LOCAL_IDENTIFIER=1)
target_compile_options(laplace_reference_topology_field_identity_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_reference_topology_field_identity_mutation_probe
    reference_topology_tests.cpp)
target_include_directories(laplace_reference_topology_field_identity_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_reference_topology_field_identity_mutation_probe PRIVATE
    laplace_reference_topology_field_identity_mutant GTest::gtest_main)
target_compile_definitions(laplace_reference_topology_field_identity_mutation_probe PRIVATE
    LAPLACE_TEST_REFERENCE_USE_FIELD_AS_LOCAL_IDENTIFIER=1)
target_compile_options(laplace_reference_topology_field_identity_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME reference-topology.mutation-field-identity-substitution-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_reference_topology_field_identity_mutation_probe>"
        "-DFILTER=ReferenceTopology.NamespaceRemainsInCoordinateScope"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    reference-topology.mutation-field-identity-substitution-detected PROPERTIES
    LABELS "implementation;reference-topology;identity;mutation")

add_executable(laplace_evidence_testimony_tests evidence_testimony_tests.cpp)
target_link_libraries(laplace_evidence_testimony_tests PRIVATE
    Laplace::Engine GTest::gtest_main)
target_compile_options(laplace_evidence_testimony_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_evidence_testimony_tests PROPERTIES
    LABELS "implementation;evidence;testimony;receipt")

add_executable(laplace_standing_calculation_tests standing_calculation_tests.cpp)
target_link_libraries(laplace_standing_calculation_tests PRIVATE
    Laplace::StandingCalculation GTest::gtest_main)
target_compile_options(laplace_standing_calculation_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_standing_calculation_tests PROPERTIES
    LABELS "implementation;standing;glicko2;evidence;receipt")

add_executable(laplace_stock_recipe_tests stock_recipe_tests.cpp)
target_link_libraries(laplace_stock_recipe_tests PRIVATE
    Laplace::StockRecipe GTest::gtest_main)
target_compile_options(laplace_stock_recipe_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_stock_recipe_tests PROPERTIES
    LABELS "implementation;recipe;source-profile;perfcache;catalog")

add_library(laplace_stock_recipe_parent_mutant STATIC
    ../engine/src/stock_recipe.c)
target_include_directories(laplace_stock_recipe_parent_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_stock_recipe_parent_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_stock_recipe_parent_mutant PRIVATE
    LAPLACE_TEST_STOCK_RECIPE_PARENT_BYPASS=1)
target_compile_options(laplace_stock_recipe_parent_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_stock_recipe_parent_mutation_probe stock_recipe_tests.cpp)
target_include_directories(laplace_stock_recipe_parent_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_stock_recipe_parent_mutation_probe PRIVATE
    laplace_stock_recipe_parent_mutant GTest::gtest_main)
target_compile_options(laplace_stock_recipe_parent_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME stock-recipe.mutation-parent-bypass-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_stock_recipe_parent_mutation_probe>"
        "-DFILTER=StockRecipe.RejectsOrphanAndCrossSourceHierarchy"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(stock-recipe.mutation-parent-bypass-detected PROPERTIES
    LABELS "implementation;recipe;source-profile;mutation")

add_library(laplace_standing_prior_reset_mutant STATIC
    ../engine/src/standing_calculation.c)
target_include_directories(laplace_standing_prior_reset_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_standing_prior_reset_mutant PRIVATE
    BLAKE3::blake3 m)
target_compile_definitions(laplace_standing_prior_reset_mutant PRIVATE
    LAPLACE_TEST_STANDING_RESET_PRIOR=1)
target_compile_options(laplace_standing_prior_reset_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes;-ffp-contract=off>)
add_executable(laplace_standing_prior_reset_mutation_probe
    standing_calculation_tests.cpp)
target_include_directories(laplace_standing_prior_reset_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_standing_prior_reset_mutation_probe PRIVATE
    laplace_standing_prior_reset_mutant GTest::gtest_main)
target_compile_options(laplace_standing_prior_reset_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME standing-calculation.mutation-prior-reset-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_standing_prior_reset_mutation_probe>"
        "-DFILTER=StandingCalculation.SuccessorConsumesPriorState"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(standing-calculation.mutation-prior-reset-detected PROPERTIES
    LABELS "implementation;standing;glicko2;mutation")

add_library(laplace_standing_neutral_opponent_mutant STATIC
    ../engine/src/standing_calculation.c)
target_include_directories(laplace_standing_neutral_opponent_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_standing_neutral_opponent_mutant PRIVATE
    BLAKE3::blake3 m)
target_compile_definitions(laplace_standing_neutral_opponent_mutant PRIVATE
    LAPLACE_TEST_STANDING_NEUTRAL_OPPONENT=1)
target_compile_options(laplace_standing_neutral_opponent_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes;-ffp-contract=off>)
add_executable(laplace_standing_neutral_opponent_mutation_probe
    standing_calculation_tests.cpp)
target_include_directories(laplace_standing_neutral_opponent_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_standing_neutral_opponent_mutation_probe PRIVATE
    laplace_standing_neutral_opponent_mutant GTest::gtest_main)
target_compile_options(laplace_standing_neutral_opponent_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME standing-calculation.mutation-neutral-opponent-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_standing_neutral_opponent_mutation_probe>"
        "-DFILTER=StandingCalculation.RealOpponentPriorChangesUpdate"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(standing-calculation.mutation-neutral-opponent-detected PROPERTIES
    LABELS "implementation;standing;glicko2;mutation")

add_library(laplace_standing_caller_mapping_mutant STATIC
    ../engine/src/standing_calculation.c)
target_include_directories(laplace_standing_caller_mapping_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_standing_caller_mapping_mutant PRIVATE
    BLAKE3::blake3 m)
target_compile_definitions(laplace_standing_caller_mapping_mutant PRIVATE
    LAPLACE_TEST_STANDING_ACCEPT_CALLER_MAPPING=1)
target_compile_options(laplace_standing_caller_mapping_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes;-ffp-contract=off>)
add_executable(laplace_standing_caller_mapping_mutation_probe
    standing_calculation_tests.cpp)
target_include_directories(laplace_standing_caller_mapping_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_standing_caller_mapping_mutation_probe PRIVATE
    laplace_standing_caller_mapping_mutant GTest::gtest_main)
target_compile_options(laplace_standing_caller_mapping_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME standing-calculation.mutation-caller-mapping-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_standing_caller_mapping_mutation_probe>"
        "-DFILTER=StandingCalculation.RejectsCallerInventedOutcomeMappingsAndScores"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    standing-calculation.mutation-caller-mapping-detected PROPERTIES
    LABELS "implementation;standing;recipe;identity;mutation")

add_library(laplace_evidence_testimony_profile_mutant STATIC
    ../engine/src/evidence_testimony.c)
target_include_directories(laplace_evidence_testimony_profile_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_evidence_testimony_profile_mutant PRIVATE
    BLAKE3::blake3)
target_compile_definitions(laplace_evidence_testimony_profile_mutant PRIVATE
    LAPLACE_TEST_TESTIMONY_PROFILE_HOMOGENEITY_BYPASS=1)
target_compile_options(laplace_evidence_testimony_profile_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_evidence_testimony_profile_mutation_probe
    evidence_testimony_tests.cpp)
target_include_directories(laplace_evidence_testimony_profile_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_evidence_testimony_profile_mutation_probe PRIVATE
    laplace_evidence_testimony_profile_mutant GTest::gtest_main)
target_compile_definitions(laplace_evidence_testimony_profile_mutation_probe PRIVATE
    LAPLACE_TEST_TESTIMONY_PROFILE_HOMOGENEITY_BYPASS=1)
target_compile_options(laplace_evidence_testimony_profile_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME evidence-testimony.mutation-profile-mixing-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_evidence_testimony_profile_mutation_probe>"
        "-DFILTER=EvidenceTestimony.RejectsProfileMixingWithinOneBatch"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    evidence-testimony.mutation-profile-mixing-detected PROPERTIES
    LABELS "implementation;evidence;testimony;mutation")

add_executable(laplace_source_profile_tests source_profile_tests.cpp)
target_link_libraries(laplace_source_profile_tests PRIVATE
    Laplace::SourceProfile GTest::gtest_main)
target_compile_options(laplace_source_profile_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_source_profile_tests PROPERTIES
    LABELS "implementation;source-profile;admission;receipt")

add_library(laplace_source_profile_boundary_mutant STATIC
    ../engine/src/source_profile.c)
target_include_directories(laplace_source_profile_boundary_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_source_profile_boundary_mutant PRIVATE
    Laplace::Engine BLAKE3::blake3)
target_compile_definitions(laplace_source_profile_boundary_mutant PRIVATE
    LAPLACE_TEST_SOURCE_PROFILE_BOUNDARY_BYPASS=1)
target_compile_options(laplace_source_profile_boundary_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_source_profile_boundary_mutation_probe
    source_profile_tests.cpp)
target_include_directories(laplace_source_profile_boundary_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_source_profile_boundary_mutation_probe PRIVATE
    laplace_source_profile_boundary_mutant GTest::gtest_main)
target_compile_definitions(laplace_source_profile_boundary_mutation_probe PRIVATE
    LAPLACE_TEST_SOURCE_PROFILE_BOUNDARY_BYPASS=1)
target_compile_options(laplace_source_profile_boundary_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME source-profile.mutation-selected-boundary-mixing-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_source_profile_boundary_mutation_probe>"
        "-DFILTER=SourceProfile.RejectsProfilesFromDifferentSelectedBoundaries"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    source-profile.mutation-selected-boundary-mixing-detected PROPERTIES
    LABELS "implementation;source-profile;admission;mutation")

add_executable(laplace_world_admission_tests world_admission_tests.cpp)
target_link_libraries(laplace_world_admission_tests PRIVATE
    Laplace::WorldAdmission GTest::gtest_main)
target_compile_options(laplace_world_admission_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_world_admission_tests PROPERTIES
    LABELS "implementation;world-admission;closure;receipt")

add_library(laplace_world_admission_lineage_mutant STATIC
    ../engine/src/world_admission.c)
target_include_directories(laplace_world_admission_lineage_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_world_admission_lineage_mutant PRIVATE
    BLAKE3::blake3)
target_compile_definitions(laplace_world_admission_lineage_mutant PRIVATE
    LAPLACE_TEST_WORLD_ADMISSION_LINEAGE_BINDING_BYPASS=1)
target_compile_options(laplace_world_admission_lineage_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_world_admission_lineage_mutation_probe
    world_admission_tests.cpp)
target_include_directories(laplace_world_admission_lineage_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_world_admission_lineage_mutation_probe PRIVATE
    laplace_world_admission_lineage_mutant GTest::gtest_main)
target_compile_definitions(laplace_world_admission_lineage_mutation_probe PRIVATE
    LAPLACE_TEST_WORLD_ADMISSION_LINEAGE_BINDING_BYPASS=1)
target_compile_options(laplace_world_admission_lineage_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME world-admission.mutation-lineage-binding-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_world_admission_lineage_mutation_probe>"
        "-DFILTER=WorldAdmission.RejectsTestimonyWithoutDurableLineageBinding"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    world-admission.mutation-lineage-binding-detected PROPERTIES
    LABELS "implementation;world-admission;lineage;mutation")

add_library(laplace_evidence_lineage_independence_mutant STATIC
    ../engine/src/evidence_lineage.c)
target_include_directories(laplace_evidence_lineage_independence_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_evidence_lineage_independence_mutant PRIVATE
    BLAKE3::blake3)
target_compile_definitions(laplace_evidence_lineage_independence_mutant PRIVATE
    LAPLACE_TEST_LINEAGE_COUNT_DESCENDANTS_AS_ROOTS=1)
target_compile_options(laplace_evidence_lineage_independence_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_evidence_lineage_independence_mutation_probe
    evidence_lineage_tests.cpp)
target_include_directories(laplace_evidence_lineage_independence_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_evidence_lineage_independence_mutation_probe PRIVATE
    laplace_evidence_lineage_independence_mutant GTest::gtest_main)
target_compile_options(laplace_evidence_lineage_independence_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME evidence-lineage.mutation-dependent-descendants-counted-as-roots-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_evidence_lineage_independence_mutation_probe>"
        "-DFILTER=EvidenceLineage.CopiesCollapseToOneRootWhileIndependentWitnessesRemain"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    evidence-lineage.mutation-dependent-descendants-counted-as-roots-detected PROPERTIES
    LABELS "implementation;evidence;lineage;dependence;mutation")

add_executable(laplace_unicode_root_tests
    unicode_root_tests.cpp)
target_link_libraries(laplace_unicode_root_tests PRIVATE
    Laplace::UnicodeRoot Laplace::Perfcache GTest::gtest_main)
target_compile_options(laplace_unicode_root_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_unicode_root_tests
    PROPERTIES LABELS "implementation;unicode;identity;geometry;perfcache")

if(TARGET LaplaceOneApi::MKLRuntime)
    add_executable(laplace_unicode_numeric_tests
        unicode_numeric_tests.cpp)
    target_link_libraries(laplace_unicode_numeric_tests PRIVATE
        Laplace::UnicodeNumeric GTest::gtest_main)
    target_compile_options(laplace_unicode_numeric_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    gtest_discover_tests(laplace_unicode_numeric_tests PROPERTIES
        LABELS "implementation;unicode;geometry;oneapi;numeric;receipt")

    add_library(laplace_unicode_numeric_axis_mutant STATIC
        ../engine/src/unicode_numeric_oneapi.cpp)
    target_include_directories(laplace_unicode_numeric_axis_mutant PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_unicode_numeric_axis_mutant PRIVATE
        BLAKE3::blake3
        LaplaceOneApi::MKLRuntime
        LaplaceOneApi::IntelMathRuntime)
    target_compile_definitions(laplace_unicode_numeric_axis_mutant PRIVATE
        LAPLACE_TEST_UNICODE_NUMERIC_SWAP_FIRST_AXES=1)
    target_compile_options(laplace_unicode_numeric_axis_mutant PRIVATE
        -O3 -march=haswell -fp-model=strict -fno-fast-math -ffp-contract=off
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wshadow
        -Wno-overriding-option)
    add_executable(laplace_unicode_numeric_axis_mutation_probe
        unicode_numeric_tests.cpp)
    target_include_directories(laplace_unicode_numeric_axis_mutation_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_unicode_numeric_axis_mutation_probe PRIVATE
        laplace_unicode_numeric_axis_mutant GTest::gtest_main)
    target_compile_options(laplace_unicode_numeric_axis_mutation_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME unicode-numeric.mutation-axis-order-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:laplace_unicode_numeric_axis_mutation_probe>"
            "-DFILTER=UnicodeNumericProvider.CalculatesBatchWithExactReceiptAndStableBits"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
    set_tests_properties(unicode-numeric.mutation-axis-order-detected PROPERTIES
        LABELS "implementation;unicode;geometry;oneapi;numeric;receipt;mutation")

    add_executable(laplace_unicode_root_builder_tests
        unicode_root_builder_tests.cpp)
    target_link_libraries(laplace_unicode_root_builder_tests PRIVATE
        Laplace::UnicodeRoot Laplace::UnicodeNumeric BLAKE3::blake3
        GTest::gtest_main)
    target_compile_options(laplace_unicode_root_builder_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    gtest_discover_tests(laplace_unicode_root_builder_tests
        PROPERTIES
            LABELS "implementation"
            ENVIRONMENT "LAPLACE_PERFCACHE_PERFORMANCE_RECEIPT=${CMAKE_BINARY_DIR}/receipts/unicode-perfcache-hot.json"
            FIXTURES_SETUP unicode_perfcache_performance_receipt)

    add_test(
        NAME perfcache.hot-lookup-receipt-contract-and-mutation
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/perfcache_performance_receipt_tests.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../contracts/perfcache.json"
            "${CMAKE_BINARY_DIR}/receipts/unicode-perfcache-hot.json")
    set_tests_properties(
        perfcache.hot-lookup-receipt-contract-and-mutation PROPERTIES
        FIXTURES_REQUIRED unicode_perfcache_performance_receipt
        LABELS "implementation;unicode;perfcache;performance;receipt;mutation")

    add_library(laplace_unicode_root_builder_rank_mutant STATIC
        ../engine/src/unicode_root_builder.cpp)
    target_include_directories(laplace_unicode_root_builder_rank_mutant PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_unicode_root_builder_rank_mutant PRIVATE
        Laplace::UnicodeRoot BLAKE3::blake3)
    target_compile_definitions(laplace_unicode_root_builder_rank_mutant PRIVATE
        LAPLACE_TEST_UNICODE_ROOT_BUILDER_USE_POSITION_AS_NUMERIC_RANK=1)
    target_compile_options(laplace_unicode_root_builder_rank_mutant PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_executable(laplace_unicode_root_builder_rank_mutation_probe
        unicode_root_builder_tests.cpp)
    target_link_libraries(
        laplace_unicode_root_builder_rank_mutation_probe PRIVATE
        laplace_unicode_root_builder_rank_mutant Laplace::UnicodeNumeric
        BLAKE3::blake3 GTest::gtest_main)
    target_compile_options(
        laplace_unicode_root_builder_rank_mutation_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME unicode-root-builder.mutation-placement-rank-bypass-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:laplace_unicode_root_builder_rank_mutation_probe>"
            "-DFILTER=UnicodeRootBuilder.BuildsOneCanonicalRootAndReplaysToSiblingSinks"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
    set_tests_properties(
        unicode-root-builder.mutation-placement-rank-bypass-detected PROPERTIES
        LABELS "implementation;unicode;root;producer;geometry;mutation")

    add_executable(laplace_unicode_tier0_sink_offset_mutation_probe
        unicode_root_builder_tests.cpp
        ../engine/src/unicode_tier0_sink.cpp)
    target_include_directories(
        laplace_unicode_tier0_sink_offset_mutation_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(
        laplace_unicode_tier0_sink_offset_mutation_probe PRIVATE
        LAPLACE_TEST_UNICODE_TIER0_SINK_WRONG_METADATA_OFFSET=1)
    target_link_libraries(
        laplace_unicode_tier0_sink_offset_mutation_probe PRIVATE
        Laplace::UnicodeRoot Laplace::UnicodeNumeric BLAKE3::blake3
        GTest::gtest_main)
    target_compile_options(
        laplace_unicode_tier0_sink_offset_mutation_probe PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME unicode-tier0-sink.mutation-metadata-offset-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:laplace_unicode_tier0_sink_offset_mutation_probe>"
            "-DFILTER=UnicodeRootBuilder.BuildsOneCanonicalRootAndReplaysToSiblingSinks"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
    set_tests_properties(
        unicode-tier0-sink.mutation-metadata-offset-detected PROPERTIES
        LABELS "implementation;unicode;root;perfcache;mutation")

    add_executable(laplace_unicode_tier0_sink_physicality_mutation_probe
        unicode_root_builder_tests.cpp
        ../engine/src/unicode_tier0_sink.cpp)
    target_include_directories(
        laplace_unicode_tier0_sink_physicality_mutation_probe PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(
        laplace_unicode_tier0_sink_physicality_mutation_probe PRIVATE
        LAPLACE_TEST_UNICODE_TIER0_SINK_WRONG_PHYSICALITY_ID=1)
    target_link_libraries(
        laplace_unicode_tier0_sink_physicality_mutation_probe PRIVATE
        Laplace::UnicodeRoot Laplace::UnicodeNumeric BLAKE3::blake3
        GTest::gtest_main)
    target_compile_options(
        laplace_unicode_tier0_sink_physicality_mutation_probe PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME unicode-tier0-sink.mutation-physicality-parity-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:laplace_unicode_tier0_sink_physicality_mutation_probe>"
            "-DFILTER=UnicodeRootBuilder.BuildsOneCanonicalRootAndReplaysToSiblingSinks"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
    set_tests_properties(
        unicode-tier0-sink.mutation-physicality-parity-detected PROPERTIES
        LABELS "implementation;unicode;root;identity;geometry;perfcache;mutation")
endif()

add_executable(laplace_unicode_source_tests
    unicode_source_tests.cpp)
target_include_directories(laplace_unicode_source_tests PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_source_tests PRIVATE
    Laplace::UnicodeRoot GTest::gtest_main)
target_compile_options(laplace_unicode_source_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_unicode_source_tests
    PROPERTIES LABELS "implementation;unicode;source;receipt")

add_library(laplace_unicode_core_bidi_mutant STATIC
    ../engine/src/unicode_properties.cpp)
target_include_directories(laplace_unicode_core_bidi_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_core_bidi_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_unicode_core_bidi_mutant PRIVATE
    LAPLACE_TEST_UNICODE_CORE_IGNORE_SPECIFIC_BIDI_DEFAULTS=1)
target_compile_options(laplace_unicode_core_bidi_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_unicode_core_bidi_mutation_probe
    unicode_source_tests.cpp)
target_include_directories(laplace_unicode_core_bidi_mutation_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_core_bidi_mutation_probe PRIVATE
    laplace_unicode_core_bidi_mutant Laplace::UnicodeRoot GTest::gtest_main)
target_compile_options(laplace_unicode_core_bidi_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-core.mutation-bidi-default-order-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_core_bidi_mutation_probe>"
        "-DFILTER=UnicodeCoreProperties.ClassifiesEveryPositionAndAppliesBidiDefaults"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(unicode-core.mutation-bidi-default-order-detected PROPERTIES
    LABELS "implementation;unicode;source;mutation"
    SKIP_REGULAR_EXPRESSION "LAPLACE_MUTATION_PREREQUISITE_UNAVAILABLE")

add_library(laplace_unicode_script_fallback_mutant STATIC
    ../engine/src/unicode_properties.cpp)
target_include_directories(laplace_unicode_script_fallback_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_script_fallback_mutant PRIVATE
    BLAKE3::blake3)
target_compile_definitions(laplace_unicode_script_fallback_mutant PRIVATE
    LAPLACE_TEST_UNICODE_SCRIPT_EXTENSIONS_EMPTY_FALLBACK=1)
target_compile_options(laplace_unicode_script_fallback_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_unicode_script_fallback_mutation_probe
    unicode_source_tests.cpp)
target_include_directories(laplace_unicode_script_fallback_mutation_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_script_fallback_mutation_probe PRIVATE
    laplace_unicode_script_fallback_mutant Laplace::UnicodeRoot GTest::gtest_main)
target_compile_options(laplace_unicode_script_fallback_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-core.mutation-script-extensions-fallback-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_script_fallback_mutation_probe>"
        "-DFILTER=UnicodeCoreProperties.ParsesCompleteSourceAndCanonicalizesPayloads"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(unicode-core.mutation-script-extensions-fallback-detected PROPERTIES
    LABELS "implementation;unicode;source;mutation"
    SKIP_REGULAR_EXPRESSION "LAPLACE_MUTATION_PREREQUISITE_UNAVAILABLE")

add_library(laplace_unicode_ducet_truncation_mutant STATIC
    ../engine/src/unicode_ducet.cpp)
target_include_directories(laplace_unicode_ducet_truncation_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_ducet_truncation_mutant PRIVATE
    BLAKE3::blake3)
target_compile_definitions(laplace_unicode_ducet_truncation_mutant PRIVATE
    LAPLACE_TEST_DUCET_TRUNCATE_EXPANSIONS=1)
target_compile_options(laplace_unicode_ducet_truncation_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_unicode_ducet_truncation_mutation_probe
    unicode_source_tests.cpp)
target_include_directories(laplace_unicode_ducet_truncation_mutation_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_ducet_truncation_mutation_probe PRIVATE
    laplace_unicode_ducet_truncation_mutant Laplace::UnicodeRoot GTest::gtest_main)
target_compile_options(laplace_unicode_ducet_truncation_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-ducet.mutation-expansion-truncation-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_ducet_truncation_mutation_probe>"
        "-DFILTER=UnicodeDucet.RetainsCompleteMappingsAndImplicitRanges"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-ducet.mutation-expansion-truncation-detected PROPERTIES
    LABELS "implementation;unicode;ducet;source;mutation"
    SKIP_REGULAR_EXPRESSION "LAPLACE_MUTATION_PREREQUISITE_UNAVAILABLE")

add_library(laplace_unicode_ducet_blocking_mutant STATIC
    ../engine/src/unicode_ducet.cpp)
target_include_directories(laplace_unicode_ducet_blocking_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_ducet_blocking_mutant PRIVATE
    BLAKE3::blake3)
target_compile_definitions(laplace_unicode_ducet_blocking_mutant PRIVATE
    LAPLACE_TEST_DUCET_SKIP_IGNORABLE_STARTER_BLOCKER=1)
target_compile_options(laplace_unicode_ducet_blocking_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_unicode_ducet_blocking_mutation_probe
    unicode_source_tests.cpp)
target_include_directories(laplace_unicode_ducet_blocking_mutation_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_ducet_blocking_mutation_probe PRIVATE
    laplace_unicode_ducet_blocking_mutant Laplace::UnicodeRoot GTest::gtest_main)
target_compile_options(laplace_unicode_ducet_blocking_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-ducet.mutation-starter-blocker-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_ducet_blocking_mutation_probe>"
        "-DFILTER=UnicodeDucet.HonorsLongestDiscontiguousAndStarterBlockingMatches"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-ducet.mutation-starter-blocker-detected PROPERTIES
    LABELS "implementation;unicode;ducet;source;mutation"
    SKIP_REGULAR_EXPRESSION "LAPLACE_MUTATION_PREREQUISITE_UNAVAILABLE")

add_library(laplace_unicode_placement_collapse_mutant STATIC
    ../engine/src/unicode_ducet.cpp)
target_include_directories(laplace_unicode_placement_collapse_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_placement_collapse_mutant PRIVATE
    BLAKE3::blake3)
target_compile_definitions(laplace_unicode_placement_collapse_mutant PRIVATE
    LAPLACE_TEST_DUCET_PLACEMENT_COLLAPSE_EQUIVALENT_RANKS=1)
target_compile_options(laplace_unicode_placement_collapse_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_executable(laplace_unicode_placement_collapse_mutation_probe
    unicode_source_tests.cpp)
target_include_directories(
    laplace_unicode_placement_collapse_mutation_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
    laplace_unicode_placement_collapse_mutation_probe PRIVATE
    laplace_unicode_placement_collapse_mutant Laplace::UnicodeRoot
    GTest::gtest_main)
target_compile_options(
    laplace_unicode_placement_collapse_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-ducet.mutation-placement-equivalence-collapse-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_placement_collapse_mutation_probe>"
        "-DFILTER=UnicodeDucet.TotalizesEveryPositionIntoUniquePlacementPermutation"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-ducet.mutation-placement-equivalence-collapse-detected PROPERTIES
    LABELS "implementation;unicode;ducet;placement;source;mutation"
    SKIP_REGULAR_EXPRESSION "LAPLACE_MUTATION_PREREQUISITE_UNAVAILABLE")

add_executable(laplace_unicode_source_fixture_tests
    unicode_source_fixture_tests.cpp
    ../engine/src/unicode_source.cpp)
target_include_directories(laplace_unicode_source_fixture_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_source_fixture_tests PRIVATE
    BLAKE3::blake3 GTest::gtest_main)
target_compile_definitions(laplace_unicode_source_fixture_tests PRIVATE
    LAPLACE_UNICODE_SOURCE_MANIFEST_HEADER="unicode_source_fixture_manifest.h")
target_compile_options(laplace_unicode_source_fixture_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_unicode_source_fixture_tests
    PROPERTIES LABELS "implementation;unicode;source;receipt")

add_executable(laplace_unicode_source_mutation_probe
    unicode_source_fixture_tests.cpp
    ../engine/src/unicode_source.cpp)
target_include_directories(laplace_unicode_source_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_source_mutation_probe PRIVATE
    BLAKE3::blake3 GTest::gtest_main)
target_compile_definitions(laplace_unicode_source_mutation_probe PRIVATE
    LAPLACE_UNICODE_SOURCE_MANIFEST_HEADER="unicode_source_fixture_manifest.h"
    LAPLACE_TEST_SKIP_UNICODE_SOURCE_DIGEST_VALIDATION=1)
target_compile_options(laplace_unicode_source_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-source.mutation-digest-bypass-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_source_mutation_probe>"
        "-DFILTER=UnicodeSourceFixture.RejectsOneByteChange"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(unicode-source.mutation-digest-bypass-detected PROPERTIES
    LABELS "implementation;unicode;source;receipt;mutation")

add_executable(laplace_unicode_root_mutation_probe
    unicode_root_tests.cpp
    ../engine/src/unicode_root.c)
target_include_directories(laplace_unicode_root_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_unicode_root_mutation_probe PRIVATE
    Laplace::Identity Laplace::Perfcache BLAKE3::blake3 GTest::gtest_main)
target_compile_definitions(laplace_unicode_root_mutation_probe PRIVATE
    LAPLACE_TEST_SKIP_UNICODE_ATOM_IDENTITY_VALIDATION=1
    LAPLACE_TEST_SKIP_UNICODE_FIELD_PAYLOAD_VALIDATION=1
    LAPLACE_TEST_UNICODE_ROOT_FRAME_SKIP_NESTED_VALIDATION=1
    LAPLACE_TEST_SKIP_UNICODE_STREAM_SECTION_ORDINAL_VALIDATION=1
    LAPLACE_TEST_SKIP_UNICODE_ATOM_PHYSICALITY_VALIDATION=1
    LAPLACE_TEST_UNICODE_HILBERT_REVERSE_AXIS_ORDER=1)
target_compile_options(laplace_unicode_root_mutation_probe PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-root.mutation-codec-and-hilbert-defects-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_root_mutation_probe>"
            "-DFILTER=UnicodeAtomCodec.RejectsIdentityAndFieldKindCorruption:UnicodeAtomPayload.RejectsNonCanonicalPayloadsAndAcceptsSortedSets:UnicodeRootFrameCodec.RejectsOrdinalOrNestedAtomCorruption:UnicodeRootStreamValidator.RejectsCrossBatchOrdinalGapAndTruncation:UnicodeRootStreamValidator.RejectsAtomPhysicalityOutsideBoundGeometryEpoch:UnicodeHilbert.MatchesAllContractOrientationVectors"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-root.mutation-codec-and-hilbert-defects-detected PROPERTIES
    LABELS "implementation;unicode;identity;geometry;mutation")

add_executable(laplace_unicode_atom_projection_mutation_probe
    unicode_root_tests.cpp
    ../engine/src/unicode_root.c)
target_include_directories(
    laplace_unicode_atom_projection_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
    laplace_unicode_atom_projection_mutation_probe PRIVATE
    Laplace::Identity Laplace::Perfcache BLAKE3::blake3 GTest::gtest_main)
target_compile_definitions(
    laplace_unicode_atom_projection_mutation_probe PRIVATE
    LAPLACE_TEST_UNICODE_PROJECT_REMINT_PHYSICALITY=1)
target_compile_options(
    laplace_unicode_atom_projection_mutation_probe PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-root.mutation-physicality-remint-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_atom_projection_mutation_probe>"
        "-DFILTER=UnicodeAtomPersistence.ProjectsEmittedIdentityAndRejectsPhysicalityReminting"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-root.mutation-physicality-remint-detected PROPERTIES
    LABELS "implementation;unicode;identity;physicality;persistence;mutation")

add_executable(laplace_unicode_placement_permutation_mutation_probe
    unicode_root_tests.cpp
    ../engine/src/unicode_root.c)
target_include_directories(
    laplace_unicode_placement_permutation_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
    laplace_unicode_placement_permutation_mutation_probe PRIVATE
    Laplace::Identity Laplace::Perfcache BLAKE3::blake3 GTest::gtest_main)
target_compile_definitions(
    laplace_unicode_placement_permutation_mutation_probe PRIVATE
    LAPLACE_TEST_SKIP_UNICODE_PLACEMENT_PERMUTATION_VALIDATION=1)
target_compile_options(
    laplace_unicode_placement_permutation_mutation_probe PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-root.mutation-placement-permutation-validation-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_placement_permutation_mutation_probe>"
        "-DFILTER=UnicodePlacementFingerprint.RejectsDuplicateRankBeforeCoordinateTableIdentity:UnicodeRootStreamValidator.RejectsDuplicateObservedPlacementRank"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-root.mutation-placement-permutation-validation-detected PROPERTIES
    LABELS "implementation;unicode;geometry;identity;mutation")

add_executable(laplace_unicode_physicality_recipe_version_mutation_probe
    unicode_root_tests.cpp
    ../engine/src/unicode_root.c)
target_include_directories(
    laplace_unicode_physicality_recipe_version_mutation_probe PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
    laplace_unicode_physicality_recipe_version_mutation_probe PRIVATE
    Laplace::Identity Laplace::Perfcache BLAKE3::blake3 GTest::gtest_main)
target_compile_definitions(
    laplace_unicode_physicality_recipe_version_mutation_probe PRIVATE
    LAPLACE_TEST_ALLOW_UNICODE_PHYSICALITY_RECIPE_VERSION=1)
target_compile_options(
    laplace_unicode_physicality_recipe_version_mutation_probe PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>
    $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-root.mutation-physicality-recipe-version-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_physicality_recipe_version_mutation_probe>"
        "-DFILTER=UnicodeRootManifestCodec.RejectsUnsupportedPhysicalityRecipeVersion"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-root.mutation-physicality-recipe-version-detected PROPERTIES
    LABELS "implementation;unicode;physicality;identity;mutation")

add_library(laplace_unicode_tier0_partial_view_mutant STATIC
    ../engine/src/perfcache_modules.c)
target_include_directories(laplace_unicode_tier0_partial_view_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(laplace_unicode_tier0_partial_view_mutant PRIVATE
    LAPLACE_TEST_ALLOW_UNICODE_TIER0_PARTIAL_VIEW=1)
target_compile_options(laplace_unicode_tier0_partial_view_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_unicode_tier0_partial_view_mutation_probe
    unicode_root_tests.cpp)
target_include_directories(
    laplace_unicode_tier0_partial_view_mutation_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
    laplace_unicode_tier0_partial_view_mutation_probe PRIVATE
    laplace_unicode_tier0_partial_view_mutant
    Laplace::UnicodeRoot GTest::gtest_main)
target_compile_options(
    laplace_unicode_tier0_partial_view_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-tier0.mutation-partial-population-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_tier0_partial_view_mutation_probe>"
        "-DFILTER=UnicodeTier0Module.WholeViewRejectsPartialPopulation"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-tier0.mutation-partial-population-detected PROPERTIES
    LABELS "implementation;unicode;perfcache;mutation")

add_library(laplace_unicode_reverse_sorted_mutant STATIC
    ../engine/src/perfcache_modules.c)
target_include_directories(laplace_unicode_reverse_sorted_mutant PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
    "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(laplace_unicode_reverse_sorted_mutant PRIVATE
    LAPLACE_TEST_UNICODE_REVERSE_SORTED_SUBSTITUTION=1)
target_compile_options(laplace_unicode_reverse_sorted_mutant PRIVATE
    $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_unicode_reverse_sorted_mutation_probe
    unicode_root_tests.cpp)
target_include_directories(
    laplace_unicode_reverse_sorted_mutation_probe PRIVATE
    "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(
    laplace_unicode_reverse_sorted_mutation_probe PRIVATE
    laplace_unicode_reverse_sorted_mutant
    Laplace::UnicodeRoot GTest::gtest_main)
target_compile_options(
    laplace_unicode_reverse_sorted_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME unicode-reverse.mutation-sorted-generic-substitution-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_reverse_sorted_mutation_probe>"
        "-DFILTER=UnicodeIdentityReverseModule.ProbesCollisionChainsAndPreservesTypedEmpty"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    unicode-reverse.mutation-sorted-generic-substitution-detected PROPERTIES
    LABELS "implementation;unicode;perfcache;mutation")

add_test(
    NAME toolchain-build.contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/toolchain_build_tests.py")
set_tests_properties(toolchain-build.contract-and-mutation PROPERTIES
    LABELS "build;package;toolchain;dependencies;mutation")

add_test(
    NAME postgresql-build.contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/postgresql_build_tests.py")
set_tests_properties(postgresql-build.contract-and-mutation PROPERTIES
    LABELS "build;package;postgresql;mutation")

add_test(
    NAME postgresql-host-provider.contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/postgresql_host_provider_tests.py")
set_tests_properties(postgresql-host-provider.contract-and-mutation PROPERTIES
    LABELS "build;package;postgresql;host-provider;mutation")

add_test(
    NAME postgresql-package-publication.contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/postgresql_package_publication_tests.py")
set_tests_properties(postgresql-package-publication.contract-and-mutation PROPERTIES
    LABELS "build;package;postgresql;publication;delivery;mutation")

add_test(
    NAME postgresql-cluster.activation-contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/postgresql_cluster_tests.py")
set_tests_properties(postgresql-cluster.activation-contract PROPERTIES
    LABELS "activation;package;postgresql;security;topology;mutation"
    RUN_SERIAL TRUE)

add_test(
    NAME postgresql-host.inventory-and-selection-contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/postgresql_host_tests.py")
set_tests_properties(postgresql-host.inventory-and-selection-contract PROPERTIES
    LABELS "activation;installation;postgresql;discovery;topology;routing;mutation"
    RUN_SERIAL TRUE)

add_test(
    NAME unicode-product.activation-contract
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/unicode_product_activation_tests.py"
        "$<TARGET_FILE:laplace_unicode_activation_identify>")
set_tests_properties(unicode-product.activation-contract PROPERTIES
    LABELS "activation;product;unicode;postgresql;perfcache;receipt;mutation"
    RUN_SERIAL TRUE)

add_test(
    NAME highway-product.activation-contract
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/highway_product_activation_tests.py")
set_tests_properties(highway-product.activation-contract PROPERTIES
    LABELS "activation;product;highway;postgresql;receipt;mutation"
    RUN_SERIAL TRUE)

add_test(
    NAME product-activation.gateway-contract
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/product_activation_gateway_tests.py")
set_tests_properties(product-activation.gateway-contract PROPERTIES
    LABELS "activation;delivery;product;postgresql;unicode;highway;security;mutation"
    RUN_SERIAL TRUE)

add_test(
    NAME product-host.convergence-contract
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/product_host_tests.py")
set_tests_properties(product-host.convergence-contract PROPERTIES
    LABELS "activation;delivery;product;host;installation;repair;security;mutation"
    RUN_SERIAL TRUE)

add_test(
    NAME product-distribution.standalone-installer-contract
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/product_distribution_tests.py")
set_tests_properties(product-distribution.standalone-installer-contract PROPERTIES
    LABELS "delivery;product;installation;distribution;package;mutation"
    RUN_SERIAL TRUE)

add_test(
    NAME product-package.contract-and-mutation
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/product_package_tests.py")
set_tests_properties(product-package.contract-and-mutation PROPERTIES
    LABELS "build;package;product;postgresql;activation;mutation")

add_test(
    NAME dependencies.runtime-build-graph-contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/runtime_build_graph_tests.py")
set_tests_properties(dependencies.runtime-build-graph-contract PROPERTIES
    LABELS "build;package;postgresql;dependencies;mutation")

add_test(
    NAME dependencies.locked-git-acquisition-contract
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/locked_git_acquisition_tests.py")
set_tests_properties(dependencies.locked-git-acquisition-contract PROPERTIES
    LABELS "acquisition;dependencies;mutation")

add_executable(laplace_dependency_math_tests
    dependency_math_tests.cpp)
target_include_directories(laplace_dependency_math_tests SYSTEM PRIVATE
    "${LAPLACE_EIGEN_SOURCE}"
    "${LAPLACE_SPECTRA_SOURCE}/include")
target_link_libraries(laplace_dependency_math_tests PRIVATE GTest::gtest_main)
target_compile_options(laplace_dependency_math_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_dependency_math_tests
    PROPERTIES LABELS "implementation;package;dependencies;numeric")

add_executable(laplace_dependency_math_mutation_probe
    dependency_math_tests.cpp)
target_include_directories(laplace_dependency_math_mutation_probe SYSTEM PRIVATE
    "${LAPLACE_EIGEN_SOURCE}"
    "${LAPLACE_SPECTRA_SOURCE}/include")
target_link_libraries(laplace_dependency_math_mutation_probe PRIVATE GTest::gtest_main)
target_compile_definitions(laplace_dependency_math_mutation_probe
    PRIVATE LAPLACE_TEST_WRONG_EIGENVALUE=1)
target_compile_options(laplace_dependency_math_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME dependencies.mutation-spectral-provider-result-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_dependency_math_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_dependency_math_failure.cmake")
set_tests_properties(dependencies.mutation-spectral-provider-result-detected PROPERTIES
    LABELS "package;dependencies;numeric;mutation")

add_executable(laplace_isa_tests
    isa_tests.cpp)
target_link_libraries(laplace_isa_tests
    PRIVATE
        Laplace::Isa
        GTest::gtest_main)
target_compile_options(laplace_isa_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_isa_tests
    PROPERTIES LABELS "implementation;isa")

set(laplace_sanitizer_preload "")
if(CMAKE_C_FLAGS MATCHES "-fsanitize=[^ ]*address")
    execute_process(
        COMMAND "${CMAKE_C_COMPILER}" -print-file-name=libasan.so
        OUTPUT_VARIABLE laplace_sanitizer_preload
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE laplace_sanitizer_result)
    if(NOT laplace_sanitizer_result EQUAL 0
       OR NOT IS_ABSOLUTE "${laplace_sanitizer_preload}"
       OR NOT EXISTS "${laplace_sanitizer_preload}")
        message(FATAL_ERROR
            "Cannot locate the address-sanitizer runtime required by process-hosted native tests")
    endif()
endif()

if(LAPLACE_ENABLE_DOTNET_BINDINGS)

    add_executable(laplace_dotnet_native_expectations
        dotnet/native_expectations.cpp)
    target_link_libraries(laplace_dotnet_native_expectations PRIVATE Laplace::Isa)
    target_compile_options(laplace_dotnet_native_expectations PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    set(laplace_dotnet_test_project
        "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/Laplace.Managed.Tests.csproj")
    set(laplace_dotnet_test_dll
        "${LAPLACE_DOTNET_MANAGED_ROOT}/Laplace.Managed.Tests/bin/Release/net10.0/Laplace.Managed.Tests.dll")
    add_custom_command(
        OUTPUT "${laplace_dotnet_test_dll}"
        COMMAND "${CMAKE_COMMAND}" -E env
            "DOTNET_CLI_TELEMETRY_OPTOUT=1"
            "DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1"
            "NUGET_PACKAGES=${LAPLACE_DOTNET_MANAGED_ROOT}/packages"
            "${LAPLACE_DOTNET_EXECUTABLE}" build "${laplace_dotnet_test_project}"
            --configuration Release
            --nologo
            "-p:GeneratedIsaSource=${LAPLACE_DOTNET_GENERATED_SOURCE}"
            "-p:GeneratedHighwaySource=${LAPLACE_DOTNET_HIGHWAY_SOURCE}"
            "-p:LaplaceManagedBuildRoot=${LAPLACE_DOTNET_MANAGED_ROOT}"
            "-p:RestoreConfigFile=${LAPLACE_DOTNET_NUGET_CONFIG}"
        DEPENDS
            laplace_dotnet_bindings
            "${CMAKE_SOURCE_DIR}/Directory.Build.props"
            "${CMAKE_SOURCE_DIR}/managed/Laplace.Managed/Abi.cs"
            "${laplace_dotnet_test_project}"
            "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/Program.cs"
        COMMENT "Building dependency-free managed/native ISA parity tests"
        VERBATIM)
    add_custom_target(laplace_dotnet_tests_build ALL
        DEPENDS "${laplace_dotnet_test_dll}")
    add_dependencies(laplace_dotnet_tests_build
        laplace_dotnet_bindings
        laplace_dotnet_native_expectations)

    add_test(
        NAME dotnet.generated-isa-contract
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/generator_contract_tests.py"
            "${CMAKE_SOURCE_DIR}/tools/bindings/generate-dotnet.py"
            "${CMAKE_SOURCE_DIR}/contracts/isa.json")
    set_tests_properties(dotnet.generated-isa-contract PROPERTIES
        LABELS "implementation;dotnet;isa;bindings;mutation")

    add_test(
        NAME dotnet.native-isa-parity
        COMMAND "${CMAKE_COMMAND}"
            "-DNATIVE_FIXTURE=$<TARGET_FILE:laplace_dotnet_native_expectations>"
            "-DFIXTURE_PATH=${CMAKE_CURRENT_BINARY_DIR}/dotnet/native-expectations.bin"
            "-DDOTNET=${LAPLACE_DOTNET_EXECUTABLE}"
            "-DMANAGED_TEST_DLL=${laplace_dotnet_test_dll}"
            "-DNATIVE_LIBRARY_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/dotnet/run_native_parity.cmake")
    set_tests_properties(dotnet.native-isa-parity PROPERTIES
        LABELS "implementation;dotnet;isa;bindings;receipt;contract")
endif()

add_executable(laplace_trajectory_tests
    trajectory_tests.cpp)
target_link_libraries(laplace_trajectory_tests
    PRIVATE
        Laplace::Trajectory
        GTest::gtest_main)
target_compile_options(laplace_trajectory_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_trajectory_tests
    PROPERTIES LABELS "implementation;trajectory;structure")

add_executable(laplace_persistence_tests
    persistence_tests.cpp)
target_link_libraries(laplace_persistence_tests
    PRIVATE
        Laplace::Persistence
        GTest::gtest_main)
target_compile_options(laplace_persistence_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_persistence_tests
    PROPERTIES LABELS "implementation;persistence;identity;physicality;trajectory")

add_library(laplace_persistence_family_subset_mutant STATIC
    ../engine/src/persistence.c)
target_include_directories(laplace_persistence_family_subset_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_persistence_family_subset_mutant
    PUBLIC Laplace::Trajectory
    PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_persistence_family_subset_mutant
    PRIVATE LAPLACE_TEST_PERSISTENCE_REQUIRE_ALL_FAMILIES=1)
target_compile_options(laplace_persistence_family_subset_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_persistence_family_subset_mutation_probe
    persistence_tests.cpp)
target_link_libraries(laplace_persistence_family_subset_mutation_probe
    PRIVATE laplace_persistence_family_subset_mutant GTest::gtest_main)
target_compile_options(laplace_persistence_family_subset_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME persistence.mutation-record-family-subset-rejected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_persistence_family_subset_mutation_probe>"
        "-DFILTER=PersistenceContract.LegalRecordFamiliesDepositIndependently"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    persistence.mutation-record-family-subset-rejected PROPERTIES
    LABELS "persistence;occurrence;mutation")

add_library(laplace_persistence_atomic_point_mutant STATIC
    ../engine/src/persistence.c)
target_include_directories(laplace_persistence_atomic_point_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_persistence_atomic_point_mutant
    PUBLIC Laplace::Trajectory
    PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_persistence_atomic_point_mutant
    PRIVATE LAPLACE_TEST_ACCEPT_ACTIVE_ATOMIC_TRAJECTORY_PAYLOAD=1)
target_compile_options(laplace_persistence_atomic_point_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_persistence_atomic_point_mutation_probe
    persistence_tests.cpp)
target_link_libraries(laplace_persistence_atomic_point_mutation_probe
    PRIVATE laplace_persistence_atomic_point_mutant GTest::gtest_main)
target_compile_options(laplace_persistence_atomic_point_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME persistence.mutation-active-atomic-trajectory-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_persistence_atomic_point_mutation_probe>"
        "-DFILTER=PersistenceContract.AtomicPointHasCanonicalInactiveTrajectoryState"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    persistence.mutation-active-atomic-trajectory-detected PROPERTIES
    LABELS "persistence;identity;physicality;mutation")

foreach(laplace_zero_mutant IN ITEMS identity digest)
    add_library(laplace_persistence_zero_${laplace_zero_mutant}_mutant STATIC
        ../engine/src/persistence.c)
    target_include_directories(laplace_persistence_zero_${laplace_zero_mutant}_mutant
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
            "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_persistence_zero_${laplace_zero_mutant}_mutant
        PUBLIC Laplace::Trajectory
        PRIVATE BLAKE3::blake3)
    target_compile_options(laplace_persistence_zero_${laplace_zero_mutant}_mutant PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
endforeach()
target_compile_definitions(laplace_persistence_zero_identity_mutant
    PRIVATE LAPLACE_TEST_REJECT_ZERO_IDENTITY_FIELDS=1)
target_compile_definitions(laplace_persistence_zero_digest_mutant
    PRIVATE LAPLACE_TEST_REJECT_ZERO_DIGEST_FIELDS=1)

foreach(laplace_zero_mutant IN ITEMS identity digest)
    add_executable(laplace_persistence_zero_${laplace_zero_mutant}_mutation_probe
        persistence_zero_probe.cpp)
    target_link_libraries(laplace_persistence_zero_${laplace_zero_mutant}_mutation_probe
        PRIVATE laplace_persistence_zero_${laplace_zero_mutant}_mutant)
    target_compile_options(laplace_persistence_zero_${laplace_zero_mutant}_mutation_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME persistence.mutation-zero-${laplace_zero_mutant}-sentinel-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:laplace_persistence_zero_${laplace_zero_mutant}_mutation_probe>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_persistence_zero_failure.cmake")
    set_tests_properties(
        persistence.mutation-zero-${laplace_zero_mutant}-sentinel-detected
        PROPERTIES LABELS "persistence;identity;physicality;occurrence;mutation")
endforeach()

add_executable(laplace_execution_tests
    execution_tests.cpp)
target_link_libraries(laplace_execution_tests
    PRIVATE
        Laplace::Execution
        GTest::gtest_main)
target_compile_options(laplace_execution_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_execution_tests
    PROPERTIES LABELS "implementation;execution;topology;resources")

if(TARGET LaplaceOneApi::TBB AND TARGET LaplaceOneApi::MKLTbb)
    add_executable(laplace_execution_oneapi_tests
        execution_oneapi_tests.cpp)
    target_link_libraries(laplace_execution_oneapi_tests PRIVATE
        Laplace::Execution
        LaplaceOneApi::TBB
        LaplaceOneApi::MKLTbb
        GTest::gtest_main)
    target_compile_options(laplace_execution_oneapi_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    gtest_discover_tests(laplace_execution_oneapi_tests PROPERTIES
        LABELS "implementation;execution;topology;resources;oneapi;numeric")

    add_library(laplace_execution_oneapi_grant_mutant STATIC
        ../engine/src/execution_oneapi.cpp)
    target_include_directories(laplace_execution_oneapi_grant_mutant PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_execution_oneapi_grant_mutant PRIVATE
        BLAKE3::blake3
        LaplaceOneApi::TBB
        LaplaceOneApi::MKLTbb)
    target_compile_definitions(laplace_execution_oneapi_grant_mutant PRIVATE
        LAPLACE_TEST_IGNORE_MKL_THREAD_GRANT=1)
    target_compile_options(laplace_execution_oneapi_grant_mutant PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

    add_executable(laplace_execution_oneapi_grant_mutation_probe
        execution_oneapi_probe.cpp)
    target_link_libraries(laplace_execution_oneapi_grant_mutation_probe PRIVATE
        laplace_execution_oneapi_grant_mutant
        Laplace::Execution
        LaplaceOneApi::TBB
        LaplaceOneApi::MKLTbb)
    target_compile_options(laplace_execution_oneapi_grant_mutation_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME execution.mutation-oneapi-mkl-grant-omission-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:laplace_execution_oneapi_grant_mutation_probe>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_execution_result_failure.cmake")
    set_tests_properties(
        execution.mutation-oneapi-mkl-grant-omission-detected PROPERTIES
        LABELS "execution;resources;oneapi;numeric;mutation")
endif()

add_executable(laplace_framework_tests
    framework_tests.cpp)
target_link_libraries(laplace_framework_tests
    PRIVATE
        Laplace::Framework
        GTest::gtest_main)
target_compile_options(laplace_framework_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_framework_tests
    PROPERTIES LABELS "implementation;framework;registry;batch;sink;receipt")

add_executable(laplace_framework_zero_value_probe
    framework_zero_value_probe.cpp)
target_link_libraries(laplace_framework_zero_value_probe PRIVATE Laplace::Framework)
target_compile_options(laplace_framework_zero_value_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME framework.zero-bit-pattern-values-contract
    COMMAND laplace_framework_zero_value_probe)
set_tests_properties(framework.zero-bit-pattern-values-contract PROPERTIES
    LABELS "implementation;framework;context;batch;sink;producer;replay;activation;receipt;contract")

function(laplace_add_framework_zero_mutant family definition)
    set(library "laplace_framework_zero_${family}_mutant")
    set(probe "laplace_framework_zero_${family}_mutation_probe")
    add_library(${library} STATIC ../engine/src/framework.c)
    target_include_directories(${library}
        PUBLIC
            "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
            "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(${library} PRIVATE BLAKE3::blake3)
    target_compile_definitions(${library} PRIVATE "${definition}=1")
    target_compile_options(${library} PRIVATE
        $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
    add_executable(${probe} framework_zero_value_probe.cpp)
    target_link_libraries(${probe} PRIVATE ${library})
    target_compile_options(${probe} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME "framework.mutation-zero-${family}-sentinel-detected"
        COMMAND "${CMAKE_COMMAND}"
            "-DPROBE=$<TARGET_FILE:${probe}>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_zero_sentinel_failure.cmake")
    set_tests_properties(
        "framework.mutation-zero-${family}-sentinel-detected" PROPERTIES
        LABELS "framework;context;batch;sink;producer;replay;activation;receipt;mutation")
endfunction()

laplace_add_framework_zero_mutant(
    context LAPLACE_TEST_REJECT_ZERO_CONTEXT_VALUES)
laplace_add_framework_zero_mutant(
    stream LAPLACE_TEST_REJECT_ZERO_STREAM_VALUES)
laplace_add_framework_zero_mutant(
    sink LAPLACE_TEST_REJECT_ZERO_SINK_VALUES)
laplace_add_framework_zero_mutant(
    producer LAPLACE_TEST_REJECT_ZERO_PRODUCER_VALUES)
laplace_add_framework_zero_mutant(
    activation LAPLACE_TEST_REJECT_ZERO_ACTIVATION_VALUES)

add_executable(laplace_framework_stream_probe
    framework_stream_probe.cpp)
target_link_libraries(laplace_framework_stream_probe PRIVATE Laplace::Framework)
target_compile_options(laplace_framework_stream_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME framework.canonical-stream-binding-contract
    COMMAND laplace_framework_stream_probe)
set_tests_properties(framework.canonical-stream-binding-contract PROPERTIES
    LABELS "implementation;framework;batch;sink;receipt;contract")

add_library(laplace_framework_stream_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_stream_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_stream_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_stream_mutant
    PRIVATE LAPLACE_TEST_OMIT_STREAM_BINDING_FROM_RECEIPT=1)
target_compile_options(laplace_framework_stream_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_stream_mutation_probe
    framework_stream_probe.cpp)
target_link_libraries(laplace_framework_stream_mutation_probe
    PRIVATE laplace_framework_stream_mutant)
target_compile_options(laplace_framework_stream_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-stream-binding-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_stream_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_stream_failure.cmake")
set_tests_properties(framework.mutation-stream-binding-omission-detected PROPERTIES
    LABELS "framework;batch;sink;receipt;mutation")

add_executable(laplace_framework_sink_artifact_probe
    framework_sink_artifact_probe.cpp)
target_link_libraries(laplace_framework_sink_artifact_probe
    PRIVATE Laplace::Framework)
target_compile_options(laplace_framework_sink_artifact_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME framework.ordered-sink-artifact-output-contract
    COMMAND laplace_framework_sink_artifact_probe)
set_tests_properties(framework.ordered-sink-artifact-output-contract PROPERTIES
    LABELS "implementation;framework;batch;sink;receipt;contract")

add_library(laplace_framework_sink_artifact_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_sink_artifact_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_sink_artifact_mutant
    PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_sink_artifact_mutant
    PRIVATE LAPLACE_TEST_OMIT_ORDERED_SINK_ARTIFACT_OUTPUT=1)
target_compile_options(laplace_framework_sink_artifact_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_sink_artifact_mutation_probe
    framework_sink_artifact_probe.cpp)
target_link_libraries(laplace_framework_sink_artifact_mutation_probe
    PRIVATE laplace_framework_sink_artifact_mutant)
target_compile_options(laplace_framework_sink_artifact_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-ordered-sink-artifact-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_sink_artifact_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_sink_artifact_failure.cmake")
set_tests_properties(
    framework.mutation-ordered-sink-artifact-omission-detected PROPERTIES
    LABELS "framework;batch;sink;receipt;mutation")

add_executable(laplace_framework_producer_probe
    framework_producer_probe.cpp)
target_link_libraries(laplace_framework_producer_probe PRIVATE Laplace::Framework)
target_compile_options(laplace_framework_producer_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME framework.producer-cancellation-contract
    COMMAND laplace_framework_producer_probe)
set_tests_properties(framework.producer-cancellation-contract PROPERTIES
    LABELS "implementation;framework;producer;batch;sink;receipt;replay;contract")

add_library(laplace_framework_producer_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_producer_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_producer_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_producer_mutant
    PRIVATE LAPLACE_TEST_IGNORE_PRODUCER_CANCELLATION=1)
target_compile_options(laplace_framework_producer_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_producer_mutation_probe
    framework_producer_probe.cpp)
target_link_libraries(laplace_framework_producer_mutation_probe
    PRIVATE laplace_framework_producer_mutant)
target_compile_options(laplace_framework_producer_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-producer-cancellation-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_producer_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_producer_failure.cmake")
set_tests_properties(
    framework.mutation-producer-cancellation-omission-detected PROPERTIES
    LABELS "framework;producer;batch;sink;receipt;replay;mutation")

add_library(laplace_framework_replay_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_replay_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_replay_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_replay_mutant
    PRIVATE LAPLACE_TEST_OMIT_REPLAY_PREFIX_ASSERTION=1)
target_compile_options(laplace_framework_replay_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_replay_mutation_probe
    framework_producer_probe.cpp)
target_link_libraries(laplace_framework_replay_mutation_probe
    PRIVATE laplace_framework_replay_mutant)
target_compile_options(laplace_framework_replay_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-replay-prefix-assertion-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_replay_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_producer_failure.cmake")
set_tests_properties(
    framework.mutation-replay-prefix-assertion-omission-detected PROPERTIES
    LABELS "framework;producer;batch;sink;receipt;replay;mutation")

add_executable(laplace_framework_activation_probe
    framework_activation_probe.cpp)
target_link_libraries(laplace_framework_activation_probe PRIVATE Laplace::Framework)
target_compile_options(laplace_framework_activation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME framework.activation-epoch-cas-contract
    COMMAND laplace_framework_activation_probe)
set_tests_properties(framework.activation-epoch-cas-contract PROPERTIES
    LABELS "implementation;framework;activation;authority;receipt;contract")

add_library(laplace_framework_activation_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_activation_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_activation_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_activation_mutant
    PRIVATE LAPLACE_TEST_ACTIVATE_WITHOUT_EXPECTED_EPOCH=1)
target_compile_options(laplace_framework_activation_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_activation_mutation_probe
    framework_activation_probe.cpp)
target_link_libraries(laplace_framework_activation_mutation_probe
    PRIVATE laplace_framework_activation_mutant)
target_compile_options(laplace_framework_activation_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-stale-epoch-activation-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_activation_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_activation_failure.cmake")
set_tests_properties(framework.mutation-stale-epoch-activation-detected PROPERTIES
    LABELS "framework;activation;authority;receipt;mutation")

add_library(laplace_framework_eager_activation_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_eager_activation_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_eager_activation_mutant
    PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_eager_activation_mutant
    PRIVATE LAPLACE_TEST_COMMIT_DURING_ACTIVATION_ADMISSION=1)
target_compile_options(laplace_framework_eager_activation_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_eager_activation_mutation_probe
    framework_activation_probe.cpp)
target_link_libraries(laplace_framework_eager_activation_mutation_probe
    PRIVATE laplace_framework_eager_activation_mutant)
target_compile_options(laplace_framework_eager_activation_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-eager-activation-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_eager_activation_mutation_probe>"
        "-DEXPECTED_ERROR=framework-activation-admission-not-inert\n"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_activation_failure.cmake")
set_tests_properties(framework.mutation-eager-activation-detected PROPERTIES
    LABELS "framework;activation;transaction;receipt;mutation")

add_library(laplace_framework_cross_context_mutant STATIC
    ../engine/src/framework.c)
target_include_directories(laplace_framework_cross_context_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_framework_cross_context_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_framework_cross_context_mutant
    PRIVATE LAPLACE_TEST_SKIP_STAGED_RECEIPT_CONTEXT_BINDING=1)
target_compile_options(laplace_framework_cross_context_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_framework_cross_context_mutation_probe
    framework_activation_probe.cpp)
target_link_libraries(laplace_framework_cross_context_mutation_probe
    PRIVATE laplace_framework_cross_context_mutant)
target_compile_options(laplace_framework_cross_context_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME framework.mutation-cross-context-stage-admission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_framework_cross_context_mutation_probe>"
        "-DEXPECTED_ERROR=framework-activation-cross-context\n"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_framework_activation_failure.cmake")
set_tests_properties(
    framework.mutation-cross-context-stage-admission-detected PROPERTIES
    LABELS "framework;activation;context;authority;receipt;mutation")

add_test(
    NAME build.repository-output-rejected
    COMMAND "${CMAKE_COMMAND}"
        "-DMODULE=${CMAKE_CURRENT_SOURCE_DIR}/../cmake/LaplaceOutputBoundary.cmake"
        "-DPROBE=${CMAKE_CURRENT_SOURCE_DIR}/output_boundary_invalid.cmake"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_output_boundary_failure.cmake")
set_tests_properties(build.repository-output-rejected PROPERTIES
    LABELS "build;output;contract")

add_executable(laplace_execution_grant_probe
    execution_grant_probe.cpp)
target_link_libraries(laplace_execution_grant_probe PRIVATE Laplace::Execution)
target_compile_options(laplace_execution_grant_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME execution.resource-conservation-contract COMMAND laplace_execution_grant_probe)
set_tests_properties(execution.resource-conservation-contract PROPERTIES
    LABELS "implementation;execution;resources;contract")

add_library(laplace_execution_resource_mutant STATIC
    ../engine/src/execution.cpp)
target_include_directories(laplace_execution_resource_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(laplace_execution_resource_mutant
    PRIVATE LAPLACE_TEST_DUPLICATE_PARENT_GRANT=1)
target_link_libraries(laplace_execution_resource_mutant PRIVATE BLAKE3::blake3)
target_compile_options(laplace_execution_resource_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

add_executable(laplace_execution_resource_mutation_probe
    execution_grant_probe.cpp)
target_link_libraries(laplace_execution_resource_mutation_probe
    PRIVATE laplace_execution_resource_mutant)
target_compile_options(laplace_execution_resource_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME execution.mutation-resource-duplication-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_execution_resource_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_execution_failure.cmake")
set_tests_properties(execution.mutation-resource-duplication-detected PROPERTIES
    LABELS "execution;resources;mutation")

add_library(laplace_execution_result_mutant STATIC
    ../engine/src/execution.cpp)
target_include_directories(laplace_execution_result_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_execution_result_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_execution_result_mutant
    PRIVATE LAPLACE_TEST_SKIP_CHUNK_RESULT_VALIDATION=1)
target_compile_options(laplace_execution_result_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

add_executable(laplace_execution_result_mutation_probe
    execution_result_probe.cpp)
target_link_libraries(laplace_execution_result_mutation_probe
    PRIVATE laplace_execution_result_mutant)
target_compile_options(laplace_execution_result_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME execution.mutation-missing-chunk-result-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_execution_result_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_execution_result_failure.cmake")
set_tests_properties(execution.mutation-missing-chunk-result-detected PROPERTIES
    LABELS "execution;resources;receipt;mutation")

add_library(laplace_execution_zero_result_mutant STATIC
    ../engine/src/execution.cpp)
target_include_directories(laplace_execution_zero_result_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_execution_zero_result_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_execution_zero_result_mutant
    PRIVATE LAPLACE_TEST_INFER_COMPLETION_FROM_RESULT_BYTES=1)
target_compile_options(laplace_execution_zero_result_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

add_executable(laplace_execution_zero_result_mutation_probe
    execution_zero_result_probe.cpp)
target_link_libraries(laplace_execution_zero_result_mutation_probe
    PRIVATE laplace_execution_zero_result_mutant)
target_compile_options(laplace_execution_zero_result_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME execution.mutation-result-bytes-as-completion-state-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_execution_zero_result_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_execution_result_failure.cmake")
set_tests_properties(
    execution.mutation-result-bytes-as-completion-state-detected PROPERTIES
    LABELS "execution;resources;receipt;mutation")

add_library(laplace_execution_zero_provider_mutant STATIC
    ../engine/src/execution.cpp)
target_include_directories(laplace_execution_zero_provider_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_execution_zero_provider_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_execution_zero_provider_mutant
    PRIVATE LAPLACE_TEST_ZERO_PROVIDER_FINGERPRINT_IS_ABSENT=1)
target_compile_options(laplace_execution_zero_provider_mutant PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)

add_executable(laplace_execution_zero_provider_mutation_probe
    execution_zero_provider_probe.cpp)
target_link_libraries(laplace_execution_zero_provider_mutation_probe
    PRIVATE laplace_execution_zero_provider_mutant)
target_compile_options(laplace_execution_zero_provider_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME execution.mutation-zero-provider-fingerprint-sentinel-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_execution_zero_provider_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_execution_result_failure.cmake")
set_tests_properties(
    execution.mutation-zero-provider-fingerprint-sentinel-detected PROPERTIES
    LABELS "execution;resources;receipt;mutation")

add_executable(laplace_isa_preflight_probe
    isa_preflight_probe.cpp)
target_link_libraries(laplace_isa_preflight_probe PRIVATE Laplace::Isa)
target_compile_options(laplace_isa_preflight_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME isa.preflight-atomicity-contract COMMAND laplace_isa_preflight_probe)
set_tests_properties(isa.preflight-atomicity-contract PROPERTIES
    LABELS "implementation;isa;contract")

add_library(laplace_isa_preflight_mutant STATIC
    ../engine/src/evidence_lineage.c
    ../engine/src/evidence_testimony.c
    ../engine/src/standing_calculation.c
    ../engine/src/reference_topology.c
    ../engine/src/reference_mapping.c
    ../engine/src/source_profile.c
    ../engine/src/stock_recipe.c
    ../engine/src/world_admission.c
    ../engine/src/identity.c
    ../engine/src/framework.c
    ../engine/src/highway.c
    ../engine/src/isa.c
    ../engine/src/trajectory.c)
target_include_directories(laplace_isa_preflight_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_isa_preflight_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_isa_preflight_mutant
    PRIVATE LAPLACE_TEST_VALIDATE_FIRST_INSTRUCTION_ONLY=1)
target_compile_options(laplace_isa_preflight_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_isa_preflight_mutation_probe
    isa_preflight_probe.cpp)
target_link_libraries(laplace_isa_preflight_mutation_probe
    PRIVATE laplace_isa_preflight_mutant)
target_compile_options(laplace_isa_preflight_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME isa.mutation-partial-preflight-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_isa_preflight_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_isa_preflight_failure.cmake")
set_tests_properties(isa.mutation-partial-preflight-detected PROPERTIES
    LABELS "isa;mutation")

add_library(laplace_isa_standing_order_mutant STATIC
    ../engine/src/evidence_lineage.c
    ../engine/src/evidence_testimony.c
    ../engine/src/standing_calculation.c
    ../engine/src/reference_topology.c
    ../engine/src/reference_mapping.c
    ../engine/src/source_profile.c
    ../engine/src/stock_recipe.c
    ../engine/src/world_admission.c
    ../engine/src/identity.c
    ../engine/src/framework.c
    ../engine/src/highway.c
    ../engine/src/isa.c
    ../engine/src/trajectory.c)
target_include_directories(laplace_isa_standing_order_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_isa_standing_order_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_isa_standing_order_mutant
    PRIVATE LAPLACE_TEST_STANDING_INPUT_ORDER_RECEIPT=1)
target_compile_options(laplace_isa_standing_order_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_isa_standing_order_mutation_probe
    isa_tests.cpp)
target_compile_definitions(laplace_isa_standing_order_mutation_probe
    PRIVATE LAPLACE_TEST_STANDING_ISOLATED_MUTANT=1)
target_link_libraries(laplace_isa_standing_order_mutation_probe
    PRIVATE laplace_isa_standing_order_mutant GTest::gtest_main)
target_compile_options(laplace_isa_standing_order_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME isa.mutation-standing-input-order-receipt-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_isa_standing_order_mutation_probe>"
        "-DFILTER=IsaExecution.StandingMatchesCanonicalNativeOperationAndReceipt"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
set_tests_properties(
    isa.mutation-standing-input-order-receipt-detected PROPERTIES
    LABELS "isa;standing;receipt;mutation")

add_executable(laplace_isa_registry_probe
    isa_registry_probe.cpp)
target_link_libraries(laplace_isa_registry_probe PRIVATE Laplace::Isa)
target_compile_options(laplace_isa_registry_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME isa.generated-registry-dispatch-contract
    COMMAND laplace_isa_registry_probe)
set_tests_properties(isa.generated-registry-dispatch-contract PROPERTIES
    LABELS "implementation;isa;framework;registry;contract")

add_library(laplace_isa_registry_mutant STATIC
    ../engine/src/evidence_lineage.c
    ../engine/src/evidence_testimony.c
    ../engine/src/standing_calculation.c
    ../engine/src/reference_topology.c
    ../engine/src/reference_mapping.c
    ../engine/src/source_profile.c
    ../engine/src/stock_recipe.c
    ../engine/src/world_admission.c
    ../engine/src/identity.c
    ../engine/src/framework.c
    ../engine/src/highway.c
    ../engine/src/isa.c
    ../engine/src/trajectory.c)
target_include_directories(laplace_isa_registry_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_isa_registry_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_isa_registry_mutant
    PRIVATE LAPLACE_TEST_DISPATCH_FIRST_OPERATION_ONLY=1)
target_compile_options(laplace_isa_registry_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_isa_registry_mutation_probe
    isa_registry_probe.cpp)
target_link_libraries(laplace_isa_registry_mutation_probe
    PRIVATE laplace_isa_registry_mutant)
target_compile_options(laplace_isa_registry_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME isa.mutation-private-dispatch-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_isa_registry_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_isa_registry_failure.cmake")
set_tests_properties(isa.mutation-private-dispatch-detected PROPERTIES
    LABELS "isa;framework;registry;mutation")

add_executable(laplace_isa_context_probe
    isa_context_probe.cpp)
target_link_libraries(laplace_isa_context_probe PRIVATE Laplace::Isa)
target_compile_options(laplace_isa_context_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME isa.execution-context-binding-contract
    COMMAND laplace_isa_context_probe)
set_tests_properties(isa.execution-context-binding-contract PROPERTIES
    LABELS "implementation;isa;framework;context;receipt;contract")

add_library(laplace_isa_context_mutant STATIC
    ../engine/src/evidence_lineage.c
    ../engine/src/evidence_testimony.c
    ../engine/src/standing_calculation.c
    ../engine/src/reference_topology.c
    ../engine/src/reference_mapping.c
    ../engine/src/source_profile.c
    ../engine/src/stock_recipe.c
    ../engine/src/world_admission.c
    ../engine/src/identity.c
    ../engine/src/framework.c
    ../engine/src/highway.c
    ../engine/src/isa.c
    ../engine/src/trajectory.c)
target_include_directories(laplace_isa_context_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_isa_context_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_isa_context_mutant
    PRIVATE LAPLACE_TEST_OMIT_CONTEXT_FROM_ISA_RECEIPT=1)
target_compile_options(laplace_isa_context_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_isa_context_mutation_probe
    isa_context_probe.cpp)
target_link_libraries(laplace_isa_context_mutation_probe
    PRIVATE laplace_isa_context_mutant)
target_compile_options(laplace_isa_context_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME isa.mutation-context-omission-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_isa_context_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_isa_context_failure.cmake")
set_tests_properties(isa.mutation-context-omission-detected PROPERTIES
    LABELS "isa;framework;context;receipt;mutation")

add_executable(laplace_identity_contract_probe
    identity_contract_probe.cpp)
target_link_libraries(laplace_identity_contract_probe PRIVATE Laplace::Identity)
target_compile_options(laplace_identity_contract_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME identity.contract-vectors COMMAND laplace_identity_contract_probe)
set_tests_properties(identity.contract-vectors PROPERTIES LABELS "implementation;identity;contract")

add_library(laplace_identity_domain_mutant STATIC
    ../engine/src/identity.c)
target_include_directories(laplace_identity_domain_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_identity_domain_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_identity_domain_mutant
    PRIVATE LAPLACE_TEST_MUTATE_COMPOSITE_DOMAIN=1)
target_compile_options(laplace_identity_domain_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_identity_domain_mutation_probe
    identity_contract_probe.cpp)
target_link_libraries(laplace_identity_domain_mutation_probe
    PRIVATE laplace_identity_domain_mutant)
target_compile_options(laplace_identity_domain_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

add_test(
    NAME identity.mutation-domain-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_identity_domain_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_contract_failure.cmake")
set_tests_properties(identity.mutation-domain-detected PROPERTIES
    LABELS "identity;mutation")

add_executable(laplace_unicode_position_probe
    unicode_position_probe.cpp)
target_link_libraries(laplace_unicode_position_probe PRIVATE Laplace::Identity)
target_compile_options(laplace_unicode_position_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME identity.unicode-surrogate-position COMMAND laplace_unicode_position_probe)
set_tests_properties(identity.unicode-surrogate-position PROPERTIES
    LABELS "implementation;identity;unicode")

add_library(laplace_identity_surrogate_mutant STATIC
    ../engine/src/identity.c)
target_include_directories(laplace_identity_surrogate_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_identity_surrogate_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_identity_surrogate_mutant
    PRIVATE LAPLACE_TEST_REJECT_SURROGATE_POSITIONS=1)
target_compile_options(laplace_identity_surrogate_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)

add_executable(laplace_unicode_surrogate_mutation_probe
    unicode_position_probe.cpp)
target_link_libraries(laplace_unicode_surrogate_mutation_probe
    PRIVATE laplace_identity_surrogate_mutant)
target_compile_options(laplace_unicode_surrogate_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME identity.mutation-surrogate-exclusion-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_unicode_surrogate_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_unicode_failure.cmake")
set_tests_properties(identity.mutation-surrogate-exclusion-detected PROPERTIES
    LABELS "identity;unicode;mutation")

add_executable(laplace_geometry_tests
    geometry_tests.cpp)
target_link_libraries(laplace_geometry_tests
    PRIVATE
        Laplace::Geometry
        GTest::gtest_main)
target_compile_options(laplace_geometry_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
gtest_discover_tests(laplace_geometry_tests
    PROPERTIES LABELS "implementation;geometry")

add_executable(laplace_geometry_contract_probe
    geometry_contract_probe.cpp)
target_link_libraries(laplace_geometry_contract_probe PRIVATE Laplace::Geometry)
target_compile_options(laplace_geometry_contract_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
add_test(NAME geometry.arithmetic-centroid-contract COMMAND laplace_geometry_contract_probe)
set_tests_properties(geometry.arithmetic-centroid-contract PROPERTIES
    LABELS "implementation;geometry;contract")

add_library(laplace_geometry_normalization_mutant STATIC
    ../engine/src/geometry.c)
target_include_directories(laplace_geometry_normalization_mutant
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include")
target_link_libraries(laplace_geometry_normalization_mutant PUBLIC m)
target_compile_definitions(laplace_geometry_normalization_mutant
    PRIVATE LAPLACE_TEST_NORMALIZE_CENTROID=1)
target_compile_options(laplace_geometry_normalization_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes;-ffp-contract=off>)
add_executable(laplace_geometry_normalization_mutation_probe
    geometry_contract_probe.cpp)
target_link_libraries(laplace_geometry_normalization_mutation_probe
    PRIVATE laplace_geometry_normalization_mutant)
target_compile_options(laplace_geometry_normalization_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-ffp-contract=off>)
add_test(
    NAME geometry.mutation-sphere-normalization-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_geometry_normalization_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_geometry_failure.cmake")
set_tests_properties(geometry.mutation-sphere-normalization-detected PROPERTIES
    LABELS "geometry;mutation")

add_executable(laplace_perfcache_tests
    perfcache_tests.cpp)
target_link_libraries(laplace_perfcache_tests
    PRIVATE
        Laplace::Perfcache
        BLAKE3::blake3
        GTest::gtest_main)
target_compile_options(laplace_perfcache_tests PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
gtest_discover_tests(laplace_perfcache_tests
    PROPERTIES LABELS "implementation;perfcache")

add_executable(laplace_perfcache_zero_value_probe
    perfcache_zero_value_probe.cpp)
target_link_libraries(laplace_perfcache_zero_value_probe PRIVATE Laplace::Perfcache)
target_compile_options(laplace_perfcache_zero_value_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME perfcache.zero-bit-pattern-values-contract
    COMMAND laplace_perfcache_zero_value_probe)
set_tests_properties(perfcache.zero-bit-pattern-values-contract PROPERTIES
    LABELS "implementation;perfcache;identity;receipt;contract")

add_library(laplace_perfcache_zero_value_mutant STATIC
    ../engine/src/perfcache.c)
target_include_directories(laplace_perfcache_zero_value_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_perfcache_zero_value_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_perfcache_zero_value_mutant
    PRIVATE LAPLACE_TEST_REJECT_ZERO_PERFCACHE_VALUES=1)
target_compile_options(laplace_perfcache_zero_value_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_perfcache_zero_value_mutation_probe
    perfcache_zero_value_probe.cpp)
target_link_libraries(laplace_perfcache_zero_value_mutation_probe
    PRIVATE laplace_perfcache_zero_value_mutant)
target_compile_options(laplace_perfcache_zero_value_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME perfcache.mutation-zero-value-sentinel-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_perfcache_zero_value_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_zero_sentinel_failure.cmake")
set_tests_properties(perfcache.mutation-zero-value-sentinel-detected PROPERTIES
    LABELS "perfcache;identity;receipt;mutation")

add_executable(laplace_perfcache_contract_probe
    perfcache_contract_probe.cpp)
target_link_libraries(laplace_perfcache_contract_probe PRIVATE Laplace::Perfcache)
target_compile_options(laplace_perfcache_contract_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(NAME perfcache.sorted-unique-contract COMMAND laplace_perfcache_contract_probe)
set_tests_properties(perfcache.sorted-unique-contract PROPERTIES
    LABELS "implementation;perfcache;contract")

add_library(laplace_perfcache_sorted_mutant STATIC
    ../engine/src/perfcache.c)
target_include_directories(laplace_perfcache_sorted_mutant
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
        "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(laplace_perfcache_sorted_mutant PRIVATE BLAKE3::blake3)
target_compile_definitions(laplace_perfcache_sorted_mutant
    PRIVATE LAPLACE_TEST_SKIP_SORTED_UNIQUE_VALIDATION=1)
target_compile_options(laplace_perfcache_sorted_mutant PRIVATE
    $<$<C_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>)
add_executable(laplace_perfcache_sorted_mutation_probe
    perfcache_contract_probe.cpp)
target_link_libraries(laplace_perfcache_sorted_mutation_probe
    PRIVATE laplace_perfcache_sorted_mutant)
target_compile_options(laplace_perfcache_sorted_mutation_probe PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
add_test(
    NAME perfcache.mutation-sorted-unique-bypass-detected
    COMMAND "${CMAKE_COMMAND}"
        "-DPROBE=$<TARGET_FILE:laplace_perfcache_sorted_mutation_probe>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_perfcache_failure.cmake")
set_tests_properties(perfcache.mutation-sorted-unique-bypass-detected PROPERTIES
    LABELS "perfcache;mutation")

if(UNIX)
    add_executable(laplace_perfcache_file_tests
        perfcache_file_tests.cpp)
    target_link_libraries(laplace_perfcache_file_tests
        PRIVATE
            Laplace::Perfcache
            GTest::gtest_main)
    target_compile_options(laplace_perfcache_file_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    gtest_discover_tests(laplace_perfcache_file_tests
        PROPERTIES LABELS "implementation;perfcache;file")

    add_executable(laplace_perfcache_file_write_corruption_mutant
        perfcache_file_tests.cpp
        ../engine/src/perfcache_file_posix.c)
    target_include_directories(laplace_perfcache_file_write_corruption_mutant
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
            "${CMAKE_BINARY_DIR}/generated")
    target_compile_definitions(laplace_perfcache_file_write_corruption_mutant
        PRIVATE LAPLACE_TEST_CORRUPT_PERFCACHE_TEMPORARY_WRITE=1)
    target_link_libraries(laplace_perfcache_file_write_corruption_mutant
        PRIVATE
            Laplace::Perfcache
            BLAKE3::blake3
            GTest::gtest_main)
    target_compile_options(laplace_perfcache_file_write_corruption_mutant PRIVATE
        $<$<COMPILE_LANG_AND_ID:C,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow;-Wstrict-prototypes>
        $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    add_test(
        NAME perfcache.file-write-corruption-rejected
        COMMAND laplace_perfcache_file_write_corruption_mutant
            --gtest_filter=PerfcacheFile.WrittenTemporaryInodeIsValidatedBeforeImmutableLink)
    set_tests_properties(
        perfcache.file-write-corruption-rejected PROPERTIES
        LABELS "perfcache;file;lifecycle;deliberate-defect")

    add_executable(laplace_perfcache_registry_tests
        perfcache_registry_tests.cpp)
    target_link_libraries(laplace_perfcache_registry_tests
        PRIVATE
            Laplace::Perfcache
            GTest::gtest_main)
    target_compile_options(laplace_perfcache_registry_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
    gtest_discover_tests(laplace_perfcache_registry_tests
        PROPERTIES LABELS "implementation;perfcache;registry;activation")

    function(laplace_add_perfcache_registry_mutant name definition filter)
        set(target "laplace_perfcache_registry_${name}_mutant")
        add_executable(${target}
            perfcache_registry_tests.cpp
            ../engine/src/perfcache_registry.cpp)
        target_include_directories(${target}
            PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/../engine/include"
                "${CMAKE_BINARY_DIR}/generated")
        target_compile_definitions(${target} PRIVATE "${definition}=1")
        target_link_libraries(${target}
            PRIVATE
                Laplace::Perfcache
                BLAKE3::blake3
                GTest::gtest_main)
        target_compile_options(${target} PRIVATE
            $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)
        add_test(
            NAME "perfcache.mutation-${name}-detected"
            COMMAND "${CMAKE_COMMAND}"
                "-DPROBE=$<TARGET_FILE:${target}>"
                "-DFILTER=${filter}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_gtest_failure.cmake")
        set_tests_properties("perfcache.mutation-${name}-detected" PROPERTIES
            LABELS "perfcache;registry;lifecycle;mutation")
    endfunction()

    laplace_add_perfcache_registry_mutant(
        prefault-omission
        LAPLACE_TEST_SKIP_PERFCACHE_PREFAULT
        PerfcacheRegistry.NullProviderHandleStillPrefaultsAndClosesExactlyOnce)
    laplace_add_perfcache_registry_mutant(
        pinned-generation-close
        LAPLACE_TEST_CLOSE_PINNED_PERFCACHE_GENERATION
        PerfcacheRegistry.PinnedReadersDrainOnlyAfterCollection)
    laplace_add_perfcache_registry_mutant(
        dependency-cycle-bypass
        LAPLACE_TEST_SKIP_PERFCACHE_DEPENDENCY_CYCLE_VALIDATION
        PerfcacheRegistry.DependencyGraphAcceptsAcyclicAndRejectsCycles)
    laplace_add_perfcache_registry_mutant(
        fixed-access-law-override
        LAPLACE_TEST_OVERRIDE_FIXED_PERFCACHE_ACCESS_LAW
        PerfcacheRegistry.PrepareReserveCommitIsInactiveUntilCommit)
    laplace_add_perfcache_registry_mutant(
        duplicate-prepared-owner
        LAPLACE_TEST_RETAIN_PREPARED_OWNERSHIP
        PerfcacheRegistry.PreparedGenerationHasOneActivationOwner)
    laplace_add_perfcache_registry_mutant(
        encoded-manifest-digest-bypass
        LAPLACE_TEST_SKIP_ENCODED_MANIFEST_DIGEST
        PerfcacheRegistry.CanonicalManifestReconstructsARealGeneration)
    laplace_add_perfcache_registry_mutant(
        materialization-forges-activation
        LAPLACE_TEST_MATERIALIZE_PERFCACHE_AS_ACTIVE
        PerfcacheRegistry.DurableGenerationMaterializesWithoutForgingActivation)
    laplace_add_perfcache_registry_mutant(
        sink-aggregate-bypass
        LAPLACE_TEST_SKIP_PERFCACHE_SINK_AGGREGATE_VALIDATION
        PerfcacheRegistry.MultiSinkReceiptBindsEverySiblingAndSelectedArtifact)
    laplace_add_perfcache_registry_mutant(
        module-view-validation-bypass
        LAPLACE_TEST_SKIP_PERFCACHE_MODULE_VIEW_VALIDATION
        PerfcacheRegistry.ColdOpenRequiresRegisteredWholeViewSemantics)
endif()

if(LAPLACE_POSTGRES_ENABLED)
    find_program(LAPLACE_BASH_EXECUTABLE bash REQUIRED)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/spi_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/spi_contract.sql"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/composition_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/composition_contract.sql"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/world_admission_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/world_admission_contract.sql"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/standing_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/standing_contract.sql"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/stock_recipe_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/stock_recipe_contract.sql"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/standing_replay_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/standing_replay_mutation.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/standing_admission_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/standing_admission_mutation.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/standing_dependence_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/standing_dependence_mutation.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/unicode_root_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/unicode_root_contract.sql"
        @ONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/persistence_concurrency_call.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/persistence_concurrency_call.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/persistence_concurrency_verify.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/persistence_concurrency_verify.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/persistence_blind_conflict_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/persistence_blind_conflict_mutation.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/evidence_root_replay_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/evidence_root_replay_mutation.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/evidence_testimony_replay_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/evidence_testimony_replay_mutation.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/source_profile_replay_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/source_profile_replay_mutation.sql"
        COPYONLY)
    foreach(composition_mutation_sql
        composition_presence_order_mutation.sql
        composition_presence_blind_mutation.sql
        composition_presence_per_row_mutation.sql
        composition_presence_partial_mutation.sql
        composition_presence_semantic_drift_mutation.sql
        composition_replay_receipt_mutation.sql)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/${composition_mutation_sql}"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/${composition_mutation_sql}"
            COPYONLY)
    endforeach()
    foreach(perfcache_sql
        perfcache_epoch_contract.sql
        perfcache_cold_lookup.sql
        perfcache_concurrency_hold.sql
        perfcache_concurrency_activate.sql
        perfcache_concurrency_verify.sql
        perfcache_prepare_rejected.sql
        perfcache_recreate.sql
        perfcache_competing_activate.sql
        perfcache_competing_verify.sql
        perfcache_epoch_mutation.sql)
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/${perfcache_sql}"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/${perfcache_sql}"
            COPYONLY)
    endforeach()
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/unicode_access_epoch_mutation.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/unicode_access_epoch_mutation.sql"
        COPYONLY)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/source_admission_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/source_admission_contract.sql")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/iso_639_source_admission_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/iso_639_source_admission_contract.sql")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/postgres/cili_source_admission_contract.sql"
        "${CMAKE_CURRENT_BINARY_DIR}/postgres/cili_source_admission_contract.sql")
    add_executable(laplace_postgres_native_receipt_probe
        postgres/native_receipt_probe.cpp)
    target_link_libraries(laplace_postgres_native_receipt_probe PRIVATE
        Laplace::Composition
        Laplace::Isa
        Laplace::Persistence
        Laplace::ReferenceMapping)
    target_compile_options(laplace_postgres_native_receipt_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    add_executable(laplace_postgres_standing_probe
        postgres/standing_probe.cpp)
    target_link_libraries(laplace_postgres_standing_probe PRIVATE
        Laplace::StandingCalculation Laplace::Isa)
    target_compile_options(laplace_postgres_standing_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    add_executable(laplace_postgres_stock_recipe_probe
        postgres/stock_recipe_probe.cpp)
    target_link_libraries(laplace_postgres_stock_recipe_probe PRIVATE
        Laplace::StockRecipe Laplace::Isa)
    target_compile_options(laplace_postgres_stock_recipe_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    add_executable(laplace_postgres_iso_639_profile_probe
        postgres/iso_639_profile_probe.cpp)
    target_include_directories(laplace_postgres_iso_639_profile_probe PRIVATE
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_postgres_iso_639_profile_probe PRIVATE
        Laplace::TabularSource)
    target_compile_options(laplace_postgres_iso_639_profile_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    add_executable(laplace_postgres_cili_profile_probe
        postgres/cili_profile_probe.cpp)
    target_include_directories(laplace_postgres_cili_profile_probe PRIVATE
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(laplace_postgres_cili_profile_probe PRIVATE
        Laplace::TabularSource)
    target_compile_options(laplace_postgres_cili_profile_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    find_library(LAPLACE_POSTGRES_CLIENT_LIBRARY NAMES pq REQUIRED)
    add_executable(laplace_postgres_composition_measurement_client
        postgres/composition_measurement_client.cpp)
    target_include_directories(
        laplace_postgres_composition_measurement_client SYSTEM PRIVATE
        "${LAPLACE_POSTGRES_INCLUDE_CLIENT}")
    target_include_directories(
        laplace_postgres_composition_measurement_client PRIVATE
        "${CMAKE_BINARY_DIR}/generated")
    target_link_libraries(
        laplace_postgres_composition_measurement_client PRIVATE
        "${LAPLACE_POSTGRES_CLIENT_LIBRARY}")
    target_compile_options(
        laplace_postgres_composition_measurement_client PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    add_executable(laplace_postgres_perfcache_manifest_probe
        postgres/perfcache_manifest_probe.cpp)
    target_link_libraries(laplace_postgres_perfcache_manifest_probe PRIVATE
        Laplace::Perfcache Laplace::Framework)
    target_compile_options(laplace_postgres_perfcache_manifest_probe PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wshadow>)

    add_test(
        NAME postgres.spi-isa-contract
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            contract
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/spi_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.spi-isa-contract PROPERTIES
        LABELS "implementation;postgresql;spi;isa;receipt"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.composition-working-set-contract
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            contract
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/composition_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.composition-working-set-contract PROPERTIES
        LABELS "implementation;postgresql;composition;presence;persistence;receipt"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.world-admission-whole-route-contract
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            contract
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/world_admission_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.world-admission-whole-route-contract PROPERTIES
        LABELS "implementation;postgresql;world-admission;composition;evidence;testimony;receipt;cohesion"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.standing-persistent-whole-route-contract
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            standing
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_standing_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/standing_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.standing-persistent-whole-route-contract PROPERTIES
        LABELS "implementation;postgresql;standing;glicko2;isa;persistence;replay;receipt"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.stock-recipe-persistent-catalog-contract
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            stock-catalog
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_stock_recipe_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/stock_recipe_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(
        postgres.stock-recipe-persistent-catalog-contract PROPERTIES
        LABELS "implementation;postgresql;recipe;perfcache;isa;persistence;replay;receipt"
        RUN_SERIAL TRUE
        ENVIRONMENT
            "RUNNER_TEMP=${CMAKE_CURRENT_BINARY_DIR}/postgres;LAPLACE_POSTGRES_TEST_PORT=55439")

    add_test(
        NAME postgres.mutation-standing-replay-conflict-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_standing_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_standing_replay_mutant>"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/standing_replay_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_standing_mutation_failure.cmake")
    set_tests_properties(postgres.mutation-standing-replay-conflict-detected PROPERTIES
        LABELS "postgresql;standing;persistence;replay;mutation"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-standing-admission-bypass-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_standing_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_standing_admission_mutant>"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/standing_admission_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_standing_admission_mutation_failure.cmake")
    set_tests_properties(postgres.mutation-standing-admission-bypass-detected PROPERTIES
        LABELS "postgresql;standing;authority;evidence-epoch;persistence;mutation"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-standing-dependence-history-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_standing_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/standing_dependence_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_standing_dependence_mutation_failure.cmake")
    set_tests_properties(
        postgres.mutation-standing-dependence-history-detected PROPERTIES
        LABELS "postgresql;standing;dependence;persistence;mutation"
        RUN_SERIAL TRUE)

    set(composition_measurement_receipt
        "${CMAKE_BINARY_DIR}/receipts/composition-whole-boundary.json")
    add_test(
        NAME postgres.composition-whole-boundary-measurement
        COMMAND "${CMAKE_COMMAND}" -E env
            "LAPLACE_COMPOSITION_MEASUREMENT_SAMPLES=5"
            "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            composition-measurement
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "$<TARGET_FILE:laplace_postgres_composition_measurement_client>"
            "${composition_measurement_receipt}"
            "${laplace_sanitizer_preload}")
    set_tests_properties(
        postgres.composition-whole-boundary-measurement PROPERTIES
        LABELS "implementation;postgresql;composition;presence;persistence;receipt;performance"
        FIXTURES_SETUP composition-whole-boundary-receipt
        RUN_SERIAL TRUE
        TIMEOUT 900)
    add_test(
        NAME postgres.composition-whole-boundary-receipt-contract
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/composition_performance_receipt_tests.py"
            "${composition_measurement_receipt}")
    set_tests_properties(
        postgres.composition-whole-boundary-receipt-contract PROPERTIES
        LABELS "contract;postgresql;composition;receipt;performance;mutation"
        FIXTURES_REQUIRED composition-whole-boundary-receipt
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.unicode-root-persistence-contract
        COMMAND "${CMAKE_COMMAND}" -E env
            "LAPLACE_UNICODE_SOURCE_ROOT=${LAPLACE_UNICODE_SOURCE_ROOT}"
            "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            unicode-root
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/unicode_root_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.unicode-root-persistence-contract PROPERTIES
        LABELS "implementation;postgresql;spi;unicode;root;persistence;perfcache;receipt"
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
        TIMEOUT 1800)

    add_test(
        NAME postgres.source-admission-whole-route-contract
        COMMAND "${CMAKE_COMMAND}" -E env
            "LAPLACE_UNICODE_SOURCE_ROOT=${LAPLACE_UNICODE_SOURCE_ROOT}"
            "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            source-admission
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/source_admission_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.source-admission-whole-route-contract PROPERTIES
        LABELS "implementation;postgresql;unicode;source-profile;grammar;composition;evidence;testimony;world-admission;replay;rollback;receipt;cohesion"
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
        TIMEOUT 1800)

    add_test(
        NAME postgres.iso-639-source-admission-whole-route-contract
        COMMAND "${CMAKE_COMMAND}" -E env
            "LAPLACE_UNICODE_SOURCE_ROOT=${LAPLACE_UNICODE_SOURCE_ROOT}"
            "LAPLACE_ISO_639_SOURCE_ROOT=${LAPLACE_ISO_639_SOURCE_ROOT}"
            "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            iso-639-admission
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_iso_639_profile_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/iso_639_source_admission_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(
        postgres.iso-639-source-admission-whole-route-contract PROPERTIES
        LABELS "implementation;postgresql;unicode;source-profile;iso-639;grammar;composition;evidence;testimony;world-admission;replay;rollback;receipt;cohesion"
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
        TIMEOUT 1800)

    add_test(
        NAME postgres.cili-source-admission-whole-route-contract
        COMMAND "${CMAKE_COMMAND}" -E env
            "LAPLACE_UNICODE_SOURCE_ROOT=${LAPLACE_UNICODE_SOURCE_ROOT}"
            "LAPLACE_CILI_SOURCE_ROOT=${LAPLACE_CILI_SOURCE_ROOT}"
            "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            cili-admission
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_cili_profile_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_BINARY_DIR}/postgres/cili_source_admission_contract.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(
        postgres.cili-source-admission-whole-route-contract PROPERTIES
        LABELS "implementation;postgresql;unicode;source-profile;cili;grammar;composition;evidence;testimony;reference-topology;reference-mapping;world-admission;replay;rollback;receipt;cohesion"
        RUN_SERIAL TRUE
        SKIP_RETURN_CODE 77
        TIMEOUT 3600)

    add_test(
        NAME postgres.mutation-receipt-collision-detected
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            mutation
            "${LAPLACE_POSTGRES_BINDIR}"
            "$<TARGET_FILE_DIR:laplace_pg>/share"
            "$<TARGET_FILE_DIR:laplace_pg>"
            "$<TARGET_FILE_DIR:laplace_engine>"
            "$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "${CMAKE_CURRENT_SOURCE_DIR}/postgres/spi_receipt_mutation.sql"
            "${laplace_sanitizer_preload}")
    set_tests_properties(postgres.mutation-receipt-collision-detected PROPERTIES
        LABELS "postgresql;spi;receipt;mutation"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-blind-persistence-conflict-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_blind_conflict_mutant>"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/persistence_blind_conflict_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_persistence_mutation_failure.cmake")
    set_tests_properties(postgres.mutation-blind-persistence-conflict-detected PROPERTIES
        LABELS "postgresql;spi;persistence;identity;mutation"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-evidence-root-replay-conflict-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_evidence_root_replay_mutant>"
            "-DMUTANT_NAME=postgres.mutation-evidence-root-replay-conflict-detected"
            "-DEXPECTED_PATTERN=evidence root-replay mutant accepted conflicting durable projection"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/evidence_root_replay_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_mutation_failure.cmake")
    set_tests_properties(
        postgres.mutation-evidence-root-replay-conflict-detected PROPERTIES
        LABELS "postgresql;evidence;lineage;dependence;receipt;mutation"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-evidence-testimony-replay-conflict-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_evidence_testimony_replay_mutant>"
            "-DMUTANT_NAME=postgres.mutation-evidence-testimony-replay-conflict-detected"
            "-DEXPECTED_PATTERN=evidence testimony replay mutant accepted conflicting durable testimony"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/evidence_testimony_replay_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_mutation_failure.cmake")
    set_tests_properties(
        postgres.mutation-evidence-testimony-replay-conflict-detected PROPERTIES
        LABELS "postgresql;evidence;testimony;receipt;mutation"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-source-profile-replay-conflict-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_source_profile_replay_mutant>"
            "-DMUTANT_NAME=postgres.mutation-source-profile-replay-conflict-detected"
            "-DEXPECTED_PATTERN=source-profile replay mutant accepted conflicting durable manifest"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/source_profile_replay_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_mutation_failure.cmake")
    set_tests_properties(
        postgres.mutation-source-profile-replay-conflict-detected PROPERTIES
        LABELS "postgresql;admission;source-profile;receipt;mutation"
        RUN_SERIAL TRUE)

    function(laplace_add_postgresql_composition_mutation
        test_name mutant_target sql_file expected_pattern)
        add_test(
            NAME ${test_name}
            COMMAND "${CMAKE_COMMAND}"
                "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
                "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
                "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
                "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
                "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
                "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
                "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
                "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
                "-DMUTANT_MODULE=$<TARGET_FILE:${mutant_target}>"
                "-DMUTANT_NAME=${test_name}"
                "-DEXPECTED_PATTERN=${expected_pattern}"
                "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/${sql_file}"
                "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_mutation_failure.cmake")
        set_tests_properties(${test_name} PROPERTIES
            LABELS "postgresql;composition;presence;persistence;receipt;mutation"
            RUN_SERIAL TRUE)
    endfunction()

    laplace_add_postgresql_composition_mutation(
        postgres.mutation-composition-presence-order-detected
        laplace_pg_composition_presence_order_mutant
        composition_presence_order_mutation.sql
        "Laplace presence provider result order or value is invalid")
    laplace_add_postgresql_composition_mutation(
        postgres.mutation-composition-presence-blind-detected
        laplace_pg_composition_presence_blind_mutant
        composition_presence_blind_mutation.sql
        "Laplace persistence producer execution diverged from preflight")
    laplace_add_postgresql_composition_mutation(
        postgres.mutation-composition-presence-per-row-detected
        laplace_pg_composition_presence_per_row_mutant
        composition_presence_per_row_mutation.sql
        "status=14")
    laplace_add_postgresql_composition_mutation(
        postgres.mutation-composition-presence-partial-detected
        laplace_pg_composition_presence_partial_mutant
        composition_presence_partial_mutation.sql
        "Laplace presence provider returned a partial set")
    laplace_add_postgresql_composition_mutation(
        postgres.mutation-composition-presence-semantic-drift-detected
        laplace_pg_composition_presence_semantic_drift_mutant
        composition_presence_semantic_drift_mutation.sql
        "Laplace composition execution receipt conflicts with durable state")
    laplace_add_postgresql_composition_mutation(
        postgres.mutation-composition-replay-receipt-collision-detected
        laplace_pg_composition_replay_receipt_mutant
        composition_replay_receipt_mutation.sql
        "composition replay-receipt mutant accepted colliding durable fields")

    add_test(
        NAME postgres.mutation-perfcache-expected-epoch-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_perfcache_epoch_mutant>"
            "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/perfcache_epoch_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_perfcache_mutation_failure.cmake")
    set_tests_properties(postgres.mutation-perfcache-expected-epoch-detected PROPERTIES
        LABELS "postgresql;perfcache;epoch;mutation"
        RUN_SERIAL TRUE)

    add_test(
        NAME postgres.mutation-unicode-plan-substitution-detected
        COMMAND "${CMAKE_COMMAND}"
            "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
            "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
            "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
            "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
            "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
            "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
            "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
            "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
            "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_plan_manifest_mutant>"
            "-DSQL_FILE=${CMAKE_CURRENT_SOURCE_DIR}/postgres/unicode_plan_manifest_mutation.sql"
            "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_plan_manifest_mutation_failure.cmake")
    set_tests_properties(
        postgres.mutation-unicode-plan-substitution-detected PROPERTIES
        LABELS "postgresql;unicode;persistence;receipt;mutation"
        RUN_SERIAL TRUE)

    if(LAPLACE_VERIFY_ONEAPI_INSTALLED_PROVIDER)
        add_test(
            NAME postgres.mutation-unicode-access-expected-epoch-detected
            COMMAND "${CMAKE_COMMAND}"
                "-DBASH=${LAPLACE_BASH_EXECUTABLE}"
                "-DRUNNER=${CMAKE_CURRENT_SOURCE_DIR}/postgres/run_spi_test.sh"
                "-DPG_BINDIR=${LAPLACE_POSTGRES_BINDIR}"
                "-DCONTROL_ROOT=$<TARGET_FILE_DIR:laplace_pg>/share"
                "-DMODULE_DIRECTORY=$<TARGET_FILE_DIR:laplace_pg>"
                "-DENGINE_DIRECTORY=$<TARGET_FILE_DIR:laplace_engine>"
                "-DNATIVE_PROBE=$<TARGET_FILE:laplace_postgres_native_receipt_probe>"
                "-DPERFCACHE_PROBE=$<TARGET_FILE:laplace_postgres_perfcache_manifest_probe>"
                "-DMUTANT_MODULE=$<TARGET_FILE:laplace_pg_unicode_access_epoch_mutant>"
                "-DUNICODE_SOURCE_ROOT=${LAPLACE_UNICODE_SOURCE_ROOT}"
                "-DSQL_FILE=${CMAKE_CURRENT_BINARY_DIR}/postgres/unicode_access_epoch_mutation.sql"
                "-DSANITIZER_PRELOAD=${laplace_sanitizer_preload}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_postgres_unicode_access_mutation_failure.cmake")
        set_tests_properties(
            postgres.mutation-unicode-access-expected-epoch-detected PROPERTIES
            LABELS "postgresql;unicode;perfcache;epoch;mutation"
            RUN_SERIAL TRUE
            TIMEOUT 1800)
    endif()
endif()

set(laplace_consumer_link_flags "${CMAKE_EXE_LINKER_FLAGS}")
set(laplace_consumer_runtime_path "")
if(LAPLACE_ONEAPI_RUNTIME_DIRECTORIES)
    foreach(runtime_directory IN LISTS LAPLACE_ONEAPI_RUNTIME_DIRECTORIES)
        string(APPEND laplace_consumer_link_flags
            " -Wl,-rpath-link,${runtime_directory}")
    endforeach()
    list(JOIN LAPLACE_ONEAPI_RUNTIME_DIRECTORIES ":"
        laplace_consumer_runtime_path)
endif()
add_test(
    NAME package.native-consumer-contract
    COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
        "-DSOURCE_DIRECTORY=${CMAKE_SOURCE_DIR}"
        "-DINSTALL_DIRECTORY=${CMAKE_BINARY_DIR}/package-test/install"
        "-DCONSUMER_BUILD_DIRECTORY=${CMAKE_BINARY_DIR}/package-test/consumer"
        "-DCONSUMER_C_FLAGS=${CMAKE_C_FLAGS}"
        "-DCONSUMER_LINK_FLAGS=${laplace_consumer_link_flags}"
        "-DCONSUMER_RUNTIME_PATH=${laplace_consumer_runtime_path}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/run_installed_consumer.cmake")
set_tests_properties(package.native-consumer-contract PROPERTIES
    LABELS "implementation;package;consumer"
    RUN_SERIAL TRUE)

if(UNIX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_test(
        NAME requirements.traceability-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/requirements_trace_tests.py")
    set_tests_properties(requirements.traceability-contract PROPERTIES
        LABELS "contract;requirements;evidence")

    add_test(
        NAME program.invention-authority
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/program_authority_tests.py")
    set_tests_properties(program.invention-authority PROPERTIES
        LABELS "contract;requirements;program;authority;mutation")

    add_test(
        NAME dependencies.intent-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/dependency_intent_tests.py")
    set_tests_properties(dependencies.intent-contract PROPERTIES
        LABELS "implementation;package;dependencies;evidence")

    add_test(
        NAME dependencies.installed-provider-contract
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/installed_provider_lock_tests.py")
    set_tests_properties(dependencies.installed-provider-contract PROPERTIES
        LABELS "implementation;package;dependencies;oneapi;mutation")

    find_program(LAPLACE_BASH_EXECUTABLE bash REQUIRED)
    add_test(
        NAME package.elf-dependency-closure
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/elf_closure_test.sh"
            "${CMAKE_SOURCE_DIR}/tools/dependencies/elf-closure.py"
            "${CMAKE_BINARY_DIR}/elf-closure-test")
    set_tests_properties(package.elf-dependency-closure PROPERTIES
        LABELS "implementation;package;dependencies"
        RUN_SERIAL TRUE)

    add_test(
        NAME package.release-asset-integrity
        COMMAND "${LAPLACE_BASH_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/release_assets_test.sh"
            "${CMAKE_SOURCE_DIR}/tools/dependencies/release-assets.py"
            "${CMAKE_BINARY_DIR}/release-assets-test")
    set_tests_properties(package.release-asset-integrity PROPERTIES
        LABELS "implementation;package;dependencies"
        RUN_SERIAL TRUE)
endif()
