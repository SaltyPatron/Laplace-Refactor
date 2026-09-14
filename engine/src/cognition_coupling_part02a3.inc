                std::uint64_t maximum_candidates = 0U;
                if (!Multiply(
                        provider->maximum_candidate_records_per_expansion,
                        static_cast<std::uint64_t>(batch_size),
                        &maximum_candidates)) {
                    return LAPLACE_COGNITION_COUPLING_OVERFLOW;
                }
                const std::uint64_t witness_count =
                    static_cast<std::uint64_t>(owned->witnesses.size());
                if (witness_count >= request->maximum_witnesses) {
                    resource_limited = true;
                    break;
                }
                const std::uint64_t capacity64 = std::min(
                    maximum_candidates,
                    request->maximum_witnesses - witness_count);
                if (capacity64 == 0U ||
                    capacity64 > static_cast<std::uint64_t>(
                        std::numeric_limits<std::size_t>::max())) {
                    resource_limited = true;
                    break;
                }
                const std::size_t capacity =
                    static_cast<std::size_t>(capacity64);

                std::vector<laplace_id128> source_ids(batch_size);
                std::vector<laplace_query_search_state> states(batch_size);
                std::vector<std::uint64_t> costs(batch_size);
                std::vector<laplace_cognition_observation_candidate> candidates(
                    capacity);
                for (std::size_t index = 0U; index < batch_size; ++index) {
                    const auto& node = frontier[offset + index];
                    source_ids[index] = node.entity_id;
                    states[index] = node.state;
                    costs[index] = node.accumulated_cost;
                }

                std::size_t candidate_count = 0U;
                laplace_cognition_observation_candidate_usage usage{};
                const int provider_status = provider->enumerate_candidates(
                    provider->state,
                    &binding,
                    source_ids.data(),
                    states.data(),
                    costs.data(),
                    batch_size,
                    candidates.data(),
                    candidates.size(),
                    &candidate_count,
                    &usage);
                ++receipt->provider_call_count;
                if (provider_status != 0) {
                    return LAPLACE_COGNITION_COUPLING_PROVIDER_FAILURE;
                }
                if (candidate_count > candidates.size() || usage.flags != 0U ||
                    (usage.limiting_disposition != 0U && candidate_count != 0U) ||
                    !AddUsage(receipt, usage)) {
                    return LAPLACE_COGNITION_COUPLING_PROVIDER_CONTRACT;
                }
                if (usage.limiting_disposition != 0U) {
                    provider_limited = true;
                    break;
                }
