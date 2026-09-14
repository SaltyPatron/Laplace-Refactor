TEST(CognitionPromptOrientation,
     DerivesGoalAndRelationFamiliesFromWholePromptBackReaction) {
    const std::string prompt = "BA";
    auto context = laplace_test_context(7U);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024U * 1024U;
    StructureFixture structure{};
    AtomFixture atoms{};
    PresenceFixture presence{};
    auto structure_provider = StructureProvider(&structure);
    auto atom_provider = AtomProvider(&atoms);
    auto presence_provider = PresenceProvider(&presence);
    auto prompt_input = PromptInput(prompt, &context, &structure_provider);

    OrientationAdmissionOwner admission{};
    ASSERT_EQ(
        laplace_cognition_prompt_admission_create(
            &prompt_input,
            &atom_provider,
            &presence_provider,
            &admission.value),
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK);
    ASSERT_NE(admission.value, nullptr);
    laplace_cognition_prompt_admission_view admission_view{};
    ASSERT_EQ(
        laplace_cognition_prompt_admission_view_get(
            admission.value,
            &admission_view),
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK);

    OrientationSemanticFixture semantic{};
    semantic.a = Codepoint(static_cast<std::uint32_t>('A'));
    semantic.b = Codepoint(static_cast<std::uint32_t>('B'));
    semantic.preferred = Codepoint(static_cast<std::uint32_t>('C'));
    semantic.distractor = Codepoint(static_cast<std::uint32_t>('D'));
    auto semantic_provider = OrientationSemanticProvider(&semantic);

    OrientationRealizationFixture realization{semantic.preferred, 0U};
    auto realization_provider = OrientationRealizationProvider(&realization);
    OrientationMaterializationFixture materialization{};
    ASSERT_EQ(
        laplace_identity_codepoint_witness(
            static_cast<std::uint32_t>('C'),
            &materialization.node.entity_id,
            &materialization.node.identity_witness),
        LAPLACE_IDENTITY_OK);
    materialization.node.node_receipt_id = Digest(0xF0U);
    materialization.node.logical_count = 1U;
    materialization.node.atom = static_cast<std::uint32_t>('C');
    materialization.node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
    auto materialization_provider =
        OrientationMaterializationProvider(&materialization);
    const auto request = UnboundConversationPolicy();

    std::array<std::uint8_t, 64> output{};
    std::array<std::uint8_t, LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES> frame{};
    std::size_t output_bytes = 0U;
    std::size_t frame_bytes = 0U;
    laplace_cognition_prompt_conversation_result result{};
    ASSERT_EQ(
        laplace_cognition_prompt_conversation_execute(
            admission.value,
            &request,
            nullptr,
            0U,
            &semantic_provider,
            1U,
            &realization_provider,
            &materialization_provider,
            output.data(),
            output.size(),
            &output_bytes,
            frame.data(),
            frame.size(),
            &frame_bytes,
            &result),
        LAPLACE_COGNITION_PROMPT_CONVERSATION_OK);

    ASSERT_EQ(output_bytes, 1U);
    EXPECT_EQ(output[0], static_cast<std::uint8_t>('C'));
    EXPECT_EQ(frame_bytes, frame.size());
    EXPECT_TRUE(SameId(
        result.conversation.cognition_request.goal_entity_id,
        semantic.preferred));
    EXPECT_FALSE(SameId(
        result.conversation.cognition_request.goal_entity_id,
        semantic.distractor));
    EXPECT_NE(
        result.conversation.cognition_request.flags &
            LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT,
        0U);
    EXPECT_EQ(
        result.conversation.cognition_request.flags &
            LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS,
        0U);
    EXPECT_NE(
        result.conversation.cognition_request.relation_mask &
            LAPLACE_OBSERVATION_QUERY_CONSTITUENT,
        0U);
    EXPECT_NE(
        result.conversation.cognition_request.relation_mask &
            LAPLACE_OBSERVATION_QUERY_SEMANTIC,
        0U);
    EXPECT_FALSE(SameDigest(
        result.conversation.cognition_request.context_fingerprint,
        admission_view.turn.context_fingerprint));
    EXPECT_GT(semantic.calls, 1U);
    EXPECT_EQ(realization.calls, 1U);
    EXPECT_EQ(materialization.calls, 1U);
}

}  // namespace
