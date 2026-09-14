    std::vector<FrontierNode> frontier;
    std::set<std::pair<std::uint64_t, IdKey>> visited;
    std::set<DigestKey> witnessed;
    std::map<IdKey, Aggregate> aggregates;
    try {
        frontier.reserve(request->seed_count);
        for (std::size_t index = 0U; index < request->seed_count; ++index) {
            FrontierNode node{};
            node.entity_id = request->seeds[index].entity_id;
            node.seed_index = static_cast<std::uint64_t>(index);
            const auto context = SeedContext(
                *request,
                request->seeds[index],
                node.seed_index);
            if (!MakeState(
                    *request,
                    binding.binding_fingerprint,
                    request->seeds[index],
                    node.seed_index,
                    node.entity_id,
                    0U,
                    context,
                    &node.state)) {
                return LAPLACE_COGNITION_COUPLING_INVALID_ARGUMENT;
            }
            frontier.push_back(node);
            visited.insert({node.seed_index, Key(node.entity_id)});
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_COUPLING_MEMORY_FAILURE;
    }
    receipt->generated_frontier_state_count =
        static_cast<std::uint64_t>(frontier.size());

    bool resource_limited = false;
    bool provider_limited = false;
    try {
        for (std::uint32_t layer = 0U;
             layer < request->maximum_depth && !frontier.empty() &&
             !resource_limited && !provider_limited;
             ++layer) {
            std::vector<FrontierNode> next;
            for (std::size_t offset = 0U;
                 offset < frontier.size() && !resource_limited && !provider_limited;) {
                if (receipt->provider_call_count >= request->maximum_provider_calls) {
                    resource_limited = true;
                    break;
                }
                const std::size_t batch_size = std::min<std::size_t>(
                    frontier.size() - offset,
                    static_cast<std::size_t>(request->frontier_batch_width));
