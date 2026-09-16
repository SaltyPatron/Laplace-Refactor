struct OrientationRealizationFixture final {
    laplace_id128 content{};
    std::size_t calls{};
};

int EnumerateOrientationRealization(
    void* const opaque,
    const laplace_cognition_semantic_act* const semantic_act,
    const laplace_cognition_realization_request* const request,
    laplace_cognition_realization_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_realization_usage* const usage) {
    if (opaque == nullptr || semantic_act == nullptr || request == nullptr ||
        candidates == nullptr || candidate_capacity == 0U ||
        candidate_count == nullptr || usage == nullptr) {
        return 1;
    }
    auto& fixture = *static_cast<OrientationRealizationFixture*>(opaque);
    ++fixture.calls;
    if (!SameId(semantic_act->primary_answer.entity_id, fixture.content)) return 2;
    *candidate_count = 1U;
    *usage = laplace_cognition_realization_usage{};
    usage->provider_receipt_id = Digest(0xD0U);
    usage->rows_examined = 1U;
    usage->exact_whole_examined = 1U;
    usage->disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    auto& candidate = candidates[0];
    candidate = laplace_cognition_realization_candidate{};
    candidate.content_id = fixture.content;
    candidate.language_id = request->language_id;
    candidate.candidate_receipt_id = Digest(0xD1U);
    candidate.realization_recipe_id = Digest(0xD2U);
    candidate.obligation_fingerprint = Digest(0xD3U);
    candidate.preference_rank = 1U;
    candidate.reused_subtree_count = 1U;
    candidate.match_class =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
    candidate.flags =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    return 0;
}

laplace_cognition_realization_provider_v1 OrientationRealizationProvider(
    OrientationRealizationFixture* const fixture) {
    laplace_cognition_realization_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0xD4U);
    provider.maximum_candidate_records = 1U;
    provider.enumerate = EnumerateOrientationRealization;
    provider.abi_major = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR;
    return provider;
}

struct OrientationMaterializationFixture final {
    laplace_cognition_materialization_node node{};
    std::size_t calls{};
};

int ResolveOrientationNode(
    void* const opaque,
    const laplace_id128* const entity_id,
    laplace_cognition_materialization_node* const node) {
    if (opaque == nullptr || entity_id == nullptr || node == nullptr) return 1;
    auto& fixture = *static_cast<OrientationMaterializationFixture*>(opaque);
    ++fixture.calls;
    if (!SameId(*entity_id, fixture.node.entity_id)) return 2;
    *node = fixture.node;
    return 0;
}

int ReadOrientationTrajectory(
    void*,
    const laplace_cognition_materialization_node*,
    laplace_trajectory_carrier*,
    std::size_t,
    laplace_digest256*) {
    return 1;
}

laplace_cognition_materialization_provider_v1 OrientationMaterializationProvider(
    OrientationMaterializationFixture* const fixture) {
    laplace_cognition_materialization_provider_v1 provider{};
    provider.state = fixture;
    provider.provider_fingerprint = Digest(0xD5U);
    provider.resolve_node = ResolveOrientationNode;
    provider.read_trajectory = ReadOrientationTrajectory;
    provider.abi_major = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR;
    return provider;
}

struct OrientationAdmissionOwner final {
    laplace_cognition_prompt_admission* value{};
    ~OrientationAdmissionOwner() {
        laplace_cognition_prompt_admission_destroy(&value);
    }
};
