void FinalizeAggregate(
    const laplace_cognition_coupling_request& request,
    Aggregate* aggregate) {
    aggregate->response.seed_support_count =
        static_cast<std::uint64_t>(aggregate->seeds.size());
    aggregate->response.independent_evidence_root_count =
        static_cast<std::uint64_t>(aggregate->evidence_roots.size());
    if (aggregate->response.semantic_witness_count != 0U) {
        aggregate->response.flags |=
            LAPLACE_COGNITION_COUPLING_RESPONSE_SEMANTIC;
    }
    if (aggregate->seeds.size() > 1U) {
        aggregate->response.flags |=
            LAPLACE_COGNITION_COUPLING_RESPONSE_MULTI_SEED;
    }
    if (!aggregate->evidence_roots.empty()) {
        aggregate->response.flags |=
            LAPLACE_COGNITION_COUPLING_RESPONSE_EVIDENCE;
    }
    if (aggregate->response.standing_witness_count != 0U) {
        aggregate->response.flags |=
            LAPLACE_COGNITION_COUPLING_RESPONSE_STANDING;
    }
