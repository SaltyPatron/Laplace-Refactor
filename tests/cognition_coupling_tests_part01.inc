struct CouplingResultOwner final {
    ~CouplingResultOwner() {
        laplace_cognition_coupling_result_destroy(&value);
    }
    laplace_cognition_coupling_result* value{};
};

laplace_cognition_coupling_request CouplingRequest(
    const laplace_cognition_coupling_seed* seeds,
    const std::size_t seed_count) {
    laplace_cognition_coupling_request request{};
    request.seeds = seeds;
    request.seed_count = seed_count;
    request.world_id = Digest(0x11U);
    request.time_fingerprint = Digest(0x12U);
    request.context_fingerprint = Digest(0x13U);
    request.evidence_boundary = Digest(0x14U);
    request.evidence_epoch = Digest(0x15U);
    request.authority_id = Digest(0x16U);
    request.maximum_witnesses = 32U;
    request.maximum_frontier_states = 32U;
    request.maximum_provider_calls = 8U;
    request.maximum_memory_bytes = UINT64_C(1048576);
    request.maximum_depth = 2U;
    request.frontier_batch_width = 8U;
    request.relation_mask =
        LAPLACE_OBSERVATION_QUERY_COOCCUR |
        LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    request.version = LAPLACE_COGNITION_COUPLING_VERSION;
    return request;
}

bool ReadResponseFor(
    const laplace_cognition_coupling_result* const result,
    const laplace_id128& entity,
    laplace_cognition_coupling_response* const response) {
    const std::size_t count = laplace_cognition_coupling_response_count(result);
    for (std::size_t index = 0U; index < count; ++index) {
        laplace_cognition_coupling_response current{};
        if (laplace_cognition_coupling_response_read(
                result,
                index,
                &current) != LAPLACE_COGNITION_COUPLING_OK) {
            return false;
        }
        if (SameId(current.entity_id, entity)) {
            *response = current;
            return true;
        }
    }
    return false;
}

TEST(CognitionCoupling,
     PreservesMultiOccurrenceTypedBackReactionAndAlternativePaths) {
    CouplingBackend backend{};
    backend.seed_entity = Id(0x21U);
    backend.shared_target = Id(0x22U);
    backend.propagated_target = Id(0x23U);
    auto provider = CouplingProvider(&backend);

    std::array<laplace_cognition_coupling_seed, 2> seeds{};
    seeds[0].entity_id = backend.seed_entity;
    seeds[0].occurrence_id = Digest(0x24U);
    seeds[0].logical_ordinal = 1U;
    seeds[0].flags = LAPLACE_COGNITION_COUPLING_SEED_PRIMARY;
    seeds[1].entity_id = backend.seed_entity;
    seeds[1].occurrence_id = Digest(0x25U);
    seeds[1].logical_ordinal = 2U;
    const auto request = CouplingRequest(seeds.data(), seeds.size());

    CouplingResultOwner owner{};
    laplace_cognition_coupling_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_coupling_execute(
            &request,
            &provider,
            &owner.value,
            &receipt),
        LAPLACE_COGNITION_COUPLING_OK);
    ASSERT_NE(owner.value, nullptr);
    EXPECT_EQ(receipt.disposition, LAPLACE_COGNITION_COUPLING_COMPLETE);
    EXPECT_EQ(receipt.response_count, 2U);
    EXPECT_EQ(receipt.witness_count, 8U);
    EXPECT_EQ(receipt.provider_call_count, 2U);

    laplace_cognition_coupling_response shared{};
    ASSERT_TRUE(ReadResponseFor(owner.value, backend.shared_target, &shared));
    EXPECT_EQ(shared.seed_support_count, 2U);
    EXPECT_EQ(shared.witness_count, 4U);
    EXPECT_EQ(shared.semantic_witness_count, 2U);
    EXPECT_EQ(shared.physicality_witness_count, 2U);
    EXPECT_EQ(shared.testimony_witness_count, 2U);
    EXPECT_EQ(shared.independent_evidence_root_count, 1U);
    EXPECT_NE(
        shared.flags & LAPLACE_COGNITION_COUPLING_RESPONSE_MULTI_SEED,
        0U);
    EXPECT_NE(
        shared.flags & LAPLACE_COGNITION_COUPLING_RESPONSE_SEMANTIC,
        0U);

    laplace_cognition_coupling_response propagated{};
    ASSERT_TRUE(ReadResponseFor(
        owner.value,
        backend.propagated_target,
        &propagated));
    EXPECT_EQ(propagated.seed_support_count, 2U);
    EXPECT_EQ(propagated.witness_count, 4U);
    EXPECT_EQ(propagated.calculation_witness_count, 4U);
    EXPECT_NE(
        propagated.relation_family_mask & LAPLACE_OBSERVATION_QUERY_COOCCUR,
        0U);
    EXPECT_NE(
        propagated.relation_family_mask & LAPLACE_OBSERVATION_QUERY_SEMANTIC,
        0U);
}
