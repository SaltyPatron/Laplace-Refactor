                for (std::size_t candidate_index = 0U;
                     candidate_index < candidate_count;
                     ++candidate_index) {
                    const auto& candidate = candidates[candidate_index];
                    if (!CandidateValid(*request, candidate, batch_size)) {
                        return LAPLACE_COGNITION_COUPLING_PROVIDER_CONTRACT;
                    }
                    const auto& source = frontier[
                        offset + static_cast<std::size_t>(
                            candidate.source_state_index)];
                    const std::uint32_t depth = source.state.depth + 1U;
                    if (!RecordCandidate(
                            *request,
                            source,
                            candidate,
                            depth,
                            &aggregates,
                            &witnessed,
                            &owned->witnesses)) {
                        return LAPLACE_COGNITION_COUPLING_OVERFLOW;
                    }
                    if (depth >= request->maximum_depth) continue;

                    const auto visit_key = std::make_pair(
                        source.seed_index,
                        Key(candidate.target_entity_id));
                    if (!visited.insert(visit_key).second) continue;
                    if (receipt->generated_frontier_state_count >=
                        request->maximum_frontier_states) {
                        resource_limited = true;
                        break;
                    }

                    FrontierNode target{};
                    target.entity_id = candidate.target_entity_id;
                    target.seed_index = source.seed_index;
                    target.path_relation_mask =
                        source.path_relation_mask | candidate.relation_family;
                    target.path_source_layer_mask =
                        source.path_source_layer_mask | candidate.source_layer;
                    if (!Add(
                            source.accumulated_cost,
                            1U,
                            &target.accumulated_cost)) {
                        return LAPLACE_COGNITION_COUPLING_OVERFLOW;
                    }
                    const auto context = NextContext(source, candidate);
                    if (!MakeState(
                            *request,
                            binding.binding_fingerprint,
                            request->seeds[static_cast<std::size_t>(
                                source.seed_index)],
                            source.seed_index,
                            target.entity_id,
                            depth,
                            context,
                            &target.state)) {
                        return LAPLACE_COGNITION_COUPLING_INVALID_ARGUMENT;
                    }
                    next.push_back(target);
                    ++receipt->generated_frontier_state_count;
                }
                offset += batch_size;
            }
            frontier.swap(next);
        }
