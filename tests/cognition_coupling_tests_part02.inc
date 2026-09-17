TEST(CognitionCoupling, RetainsTypedWitnessChannelsWithoutScalarFlattening) {
    CouplingBackend backend{};
    backend.seed_entity = Id(0x31U);
    backend.shared_target = Id(0x32U);
    backend.propagated_target = Id(0x33U);
    auto provider = CouplingProvider(&backend);

    laplace_cognition_coupling_seed seed{};
    seed.entity_id = backend.seed_entity;
    seed.occurrence_id = Digest(0x34U);
    seed.flags = LAPLACE_COGNITION_COUPLING_SEED_PRIMARY;
    const auto request = CouplingRequest(&seed, 1U);

    CouplingResultOwner owner{};
    laplace_cognition_coupling_receipt receipt{};
    ASSERT_EQ(
        laplace_cognition_coupling_execute(
            &request,
            &provider,
            &owner.value,
            &receipt),
        LAPLACE_COGNITION_COUPLING_OK);

    std::size_t testimony_count = 0U;
    std::size_t physicality_count = 0U;
    std::size_t calculation_count = 0U;
    const std::size_t witness_count =
        laplace_cognition_coupling_witness_count(owner.value);
    for (std::size_t index = 0U; index < witness_count; ++index) {
        laplace_cognition_coupling_witness witness{};
        ASSERT_EQ(
            laplace_cognition_coupling_witness_read(
                owner.value,
                index,
                &witness),
            LAPLACE_COGNITION_COUPLING_OK);
        if (witness.source_layer == LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY) {
            ++testimony_count;
            EXPECT_EQ(witness.evidence_uncertainty_numerator, 2U);
            EXPECT_EQ(witness.evidence_uncertainty_denominator, 10U);
            EXPECT_NE(
                witness.flags &
                    LAPLACE_COGNITION_OBSERVATION_CANDIDATE_EVIDENCE_UNCERTAINTY_PRESENT,
                0U);
        } else if (
            witness.source_layer ==
            LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY) {
            ++physicality_count;
            EXPECT_EQ(witness.evidence_uncertainty_denominator, 0U);
        } else if (
            witness.source_layer ==
            LAPLACE_OBSERVATION_QUERY_SOURCE_CALCULATION) {
            ++calculation_count;
        }
    }
    EXPECT_EQ(testimony_count, 1U);
    EXPECT_EQ(physicality_count, 1U);
    EXPECT_EQ(calculation_count, 2U);
}

TEST(CognitionCoupling, RejectsASeedSetThatCannotFitTheDeclaredFrontier) {
    CouplingBackend backend{};
    backend.seed_entity = Id(0x41U);
    backend.shared_target = Id(0x42U);
    backend.propagated_target = Id(0x43U);
    auto provider = CouplingProvider(&backend);

    std::array<laplace_cognition_coupling_seed, 2> seeds{};
    for (std::size_t index = 0U; index < seeds.size(); ++index) {
        seeds[index].entity_id = backend.seed_entity;
        seeds[index].occurrence_id =
            Digest(static_cast<std::uint8_t>(0x44U + index));
    }
    auto request = CouplingRequest(seeds.data(), seeds.size());
    request.maximum_frontier_states = 1U;

    laplace_cognition_coupling_result* result = nullptr;
    laplace_cognition_coupling_receipt receipt{};
    EXPECT_EQ(
        laplace_cognition_coupling_execute(
            &request,
            &provider,
            &result,
            &receipt),
        LAPLACE_COGNITION_COUPLING_EXHAUSTED);
    EXPECT_EQ(result, nullptr);
    EXPECT_EQ(backend.calls, 0U);
}

}  // namespace
