namespace laplace_observation_query_detail {

const laplace_observation_query_binding* FindBinding(
    const laplace_observation_query_index& index,
    const laplace_digest256& fingerprint) {
    const auto found = index.bindings.find(Key(fingerprint));
    return found == index.bindings.end() ? nullptr : &found->second;
}

bool ResolveCoordinate(
    const laplace_observation_query_index& index,
    const laplace_digest256& coordinate,
    laplace_id128* const entity_id) {
    const auto found = index.coordinate_to_entity.find(Key(coordinate));
    if (found == index.coordinate_to_entity.end() || entity_id == nullptr) {
        return false;
    }
    *entity_id = IdFromKey(found->second);
    return true;
}

bool ChargeWork(
    const laplace_observation_query_index& index,
    std::uint64_t* const work_records,
    bool* const limited) {
    if (*work_records >= index.summary.maximum_candidate_records_per_expansion) {
        *limited = true;
        return false;
    }
    ++*work_records;
    return true;
}

bool AppendCandidate(
    const laplace_observation_query_index& index,
    std::vector<Candidate>* const candidates,
    const Candidate& candidate,
    bool* const limited) {
    if (candidates->size() >=
        index.summary.maximum_candidate_records_per_expansion) {
        *limited = true;
        return false;
    }
    candidates->push_back(candidate);
    return true;
}

bool ImmediatePrecedes(
    const Container& container,
    const std::size_t left_index,
    const std::size_t right_index) {
    int precedes = 0;
    if (laplace_trajectory_occurrence_precedes(
            container.occurrences.data(), container.occurrences.size(),
            left_index, right_index, &precedes) != LAPLACE_TRAJECTORY_OK ||
        precedes == 0) {
        return false;
    }
    const auto& left = container.occurrences[left_index];
    const auto& right = container.occurrences[right_index];
    if (left.run_length == 0U ||
        left.logical_ordinal >
            std::numeric_limits<std::uint64_t>::max() -
                static_cast<std::uint64_t>(left.run_length)) {
        return false;
    }
    return left.logical_ordinal + static_cast<std::uint64_t>(left.run_length) ==
        right.logical_ordinal;
}

bool GenerateCandidates(
    const laplace_observation_query_index& index,
    const laplace_observation_query_binding& binding,
    const laplace_id128& entity_id,
    std::vector<Candidate>* const candidates,
    std::uint64_t* const rows_examined,
    std::uint64_t* const index_plan_count,
    bool* const limited) {
    candidates->clear();
    *rows_examined = 0U;
    *index_plan_count = 0U;
    *limited = false;
    const auto entity_key = Key(entity_id);
    const auto occurrence_locations = index.occurrences_by_entity.find(entity_key);
    const auto enclosing_containers = index.containers_by_entity.find(entity_key);

    const auto charge = [&]() -> bool {
        return ChargeWork(index, rows_examined, limited);
    };
    const auto append = [&](const Candidate& candidate) -> bool {
        return AppendCandidate(index, candidates, candidate, limited);
    };

    if ((binding.relation_mask & LAPLACE_OBSERVATION_QUERY_CONTAINER) != 0U) {
        ++*index_plan_count;
        if (occurrence_locations != index.occurrences_by_entity.end()) {
            for (const auto& location : occurrence_locations->second) {
                if (!charge()) break;
                const auto& container = index.containers[location.container_index];
                const auto& source = container.occurrences[location.occurrence_index];
                Candidate candidate{};
                candidate.target_entity_id = container.physicality.entity_id;
                candidate.physicality_id = container.physicality.physicality_id;
                candidate.source_logical_ordinal = source.logical_ordinal;
                candidate.target_logical_ordinal = 0U;
                candidate.multiplicity = source.run_length;
                candidate.relation = LAPLACE_OBSERVATION_QUERY_CONTAINER;
                if (!append(candidate)) break;
            }
        }
    }

    if (!*limited &&
        (binding.relation_mask & LAPLACE_OBSERVATION_QUERY_CONSTITUENT) != 0U) {
        ++*index_plan_count;
        if (enclosing_containers != index.containers_by_entity.end()) {
            for (const auto container_index : enclosing_containers->second) {
                const auto& container = index.containers[container_index];
                for (const auto& target : container.occurrences) {
                    if (!charge()) break;
                    Candidate candidate{};
                    candidate.target_entity_id = target.entity_id;
                    candidate.physicality_id = container.physicality.physicality_id;
                    candidate.source_logical_ordinal = 0U;
                    candidate.target_logical_ordinal = target.logical_ordinal;
                    candidate.multiplicity = target.run_length;
                    candidate.relation = LAPLACE_OBSERVATION_QUERY_CONSTITUENT;
                    if (!append(candidate)) break;
                }
                if (*limited) break;
            }
        }
    }

    if (!*limited &&
        (binding.relation_mask & LAPLACE_OBSERVATION_QUERY_PREDECESSOR) != 0U) {
        ++*index_plan_count;
        if (occurrence_locations != index.occurrences_by_entity.end()) {
            for (const auto& location : occurrence_locations->second) {
                const auto& container = index.containers[location.container_index];
                const auto& source = container.occurrences[location.occurrence_index];
                if (!charge()) break;
                if (source.run_length > 1U) {
                    Candidate internal{};
                    internal.target_entity_id = source.entity_id;
                    internal.physicality_id = container.physicality.physicality_id;
                    internal.source_logical_ordinal = source.logical_ordinal + 1U;
                    internal.target_logical_ordinal = source.logical_ordinal;
                    internal.multiplicity =
                        static_cast<std::uint64_t>(source.run_length) - 1U;
                    internal.relation = LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
                    if (!append(internal)) break;
                }
                if (location.occurrence_index != 0U) {
                    const std::size_t previous_index = location.occurrence_index - 1U;
                    if (!ImmediatePrecedes(
                            container, previous_index, location.occurrence_index)) {
                        return false;
                    }
                    const auto& previous = container.occurrences[previous_index];
                    Candidate previous_candidate{};
                    previous_candidate.target_entity_id = previous.entity_id;
                    previous_candidate.physicality_id =
                        container.physicality.physicality_id;
                    previous_candidate.source_logical_ordinal = source.logical_ordinal;
                    previous_candidate.target_logical_ordinal =
                        previous.logical_ordinal +
                        static_cast<std::uint64_t>(previous.run_length) - 1U;
                    previous_candidate.relation =
                        LAPLACE_OBSERVATION_QUERY_PREDECESSOR;
                    if (!append(previous_candidate)) break;
                }
            }
        }
    }

    if (!*limited &&
        (binding.relation_mask & LAPLACE_OBSERVATION_QUERY_SUCCESSOR) != 0U) {
        ++*index_plan_count;
        if (occurrence_locations != index.occurrences_by_entity.end()) {
            for (const auto& location : occurrence_locations->second) {
                const auto& container = index.containers[location.container_index];
                const auto& source = container.occurrences[location.occurrence_index];
                if (!charge()) break;
                if (source.run_length > 1U) {
                    Candidate internal{};
                    internal.target_entity_id = source.entity_id;
                    internal.physicality_id = container.physicality.physicality_id;
                    internal.source_logical_ordinal = source.logical_ordinal;
                    internal.target_logical_ordinal = source.logical_ordinal + 1U;
                    internal.multiplicity =
                        static_cast<std::uint64_t>(source.run_length) - 1U;
                    internal.relation = LAPLACE_OBSERVATION_QUERY_SUCCESSOR;
                    if (!append(internal)) break;
                }
                const std::size_t next_index = location.occurrence_index + 1U;
                if (next_index < container.occurrences.size()) {
                    if (!ImmediatePrecedes(
                            container, location.occurrence_index, next_index)) {
                        return false;
                    }
                    const auto& next = container.occurrences[next_index];
                    Candidate next_candidate{};
                    next_candidate.target_entity_id = next.entity_id;
                    next_candidate.physicality_id =
                        container.physicality.physicality_id;
                    next_candidate.source_logical_ordinal =
                        source.logical_ordinal +
                        static_cast<std::uint64_t>(source.run_length) - 1U;
                    next_candidate.target_logical_ordinal = next.logical_ordinal;
                    next_candidate.relation = LAPLACE_OBSERVATION_QUERY_SUCCESSOR;
                    if (!append(next_candidate)) break;
                }
            }
        }
    }

    if (!*limited &&
        (binding.relation_mask & LAPLACE_OBSERVATION_QUERY_COOCCUR) != 0U) {
        ++*index_plan_count;
        if (occurrence_locations != index.occurrences_by_entity.end()) {
            for (const auto& location : occurrence_locations->second) {
                const auto& container = index.containers[location.container_index];
                const auto& source = container.occurrences[location.occurrence_index];
                for (std::size_t target_index = 0U;
                     target_index < container.occurrences.size(); ++target_index) {
                    if (!charge()) break;
                    const auto& target = container.occurrences[target_index];
                    if (target_index == location.occurrence_index &&
                        source.run_length <= 1U) {
                        continue;
                    }
                    int cooccurs = 0;
                    if (laplace_trajectory_entities_cooccur(
                            container.occurrences.data(),
                            container.occurrences.size(), &entity_id,
                            &target.entity_id, &cooccurs) != LAPLACE_TRAJECTORY_OK) {
                        return false;
                    }
                    if (cooccurs == 0) continue;
                    Candidate candidate{};
                    candidate.target_entity_id = target.entity_id;
                    candidate.physicality_id = container.physicality.physicality_id;
                    candidate.source_logical_ordinal = source.logical_ordinal;
                    candidate.target_logical_ordinal =
                        target_index == location.occurrence_index
                        ? source.logical_ordinal + 1U
                        : target.logical_ordinal;
                    candidate.multiplicity =
                        target_index == location.occurrence_index
                        ? static_cast<std::uint64_t>(source.run_length) - 1U
                        : target.run_length;
                    candidate.gap = AbsoluteDistance(
                        candidate.source_logical_ordinal,
                        candidate.target_logical_ordinal);
                    candidate.relation = LAPLACE_OBSERVATION_QUERY_COOCCUR;
                    if (!append(candidate)) break;
                }
                if (*limited) break;
            }
        }
    }

    if (*limited) {
        candidates->clear();
        return true;
    }
    std::sort(candidates->begin(), candidates->end(), CandidateLess);
    return true;
}

void HashCandidateCalculation(
    const laplace_observation_query_index& index,
    const laplace_id128& source_entity_id,
    const Candidate& candidate,
    laplace_digest256* const receipt) {
    blake3_hasher hasher{};
    StartDomain(&hasher, CalculationDomain);
    HashDigest(&hasher, index.summary.index_fingerprint);
    HashId(&hasher, source_entity_id);
    HashId(&hasher, candidate.target_entity_id);
    HashDigest(&hasher, candidate.physicality_id);
    HashU64(&hasher, candidate.source_logical_ordinal);
    HashU64(&hasher, candidate.target_logical_ordinal);
    HashU64(&hasher, candidate.multiplicity);
    HashU64(&hasher, candidate.gap);
    HashU32(&hasher, candidate.relation);
    Finish(&hasher, receipt);
}

bool BuildTransition(
    const laplace_observation_query_index& index,
    const laplace_observation_query_binding& binding,
    const laplace_query_search_state& source,
    const laplace_id128& source_entity_id,
    const Candidate& candidate,
    laplace_query_search_transition* const transition) {
    if (transition == nullptr || source.depth == UINT32_MAX) return false;
    laplace_digest256 target_coordinate{};
    if (EntityCoordinate(candidate.target_entity_id, &target_coordinate) !=
        LAPLACE_OBSERVATION_QUERY_OK) {
        return false;
    }

    laplace_query_search_state target{};
    target.anchor_id = target_coordinate;
    target.binding_fingerprint = source.binding_fingerprint;
    target.obligation_fingerprint = source.obligation_fingerprint;
    target.evidence_epoch = source.evidence_epoch;
    target.boundary_id = source.boundary_id;
    target.depth = source.depth + 1U;
    const bool terminal_all =
        (binding.flags & LAPLACE_OBSERVATION_QUERY_BINDING_TERMINAL_RESULTS) != 0U;
    const bool terminal_goal =
        (binding.flags & LAPLACE_OBSERVATION_QUERY_BINDING_GOAL_PRESENT) != 0U &&
        SameId(binding.goal_entity_id, candidate.target_entity_id);
    if (terminal_all || terminal_goal) {
        target.flags = LAPLACE_QUERY_SEARCH_STATE_TERMINAL;
    }

    blake3_hasher context_hasher{};
    StartDomain(&context_hasher, ContextDomain);
    HashDigest(&context_hasher, source.context_fingerprint);
    HashDigest(&context_hasher, candidate.physicality_id);
    HashU64(&context_hasher, candidate.source_logical_ordinal);
    HashU64(&context_hasher, candidate.target_logical_ordinal);
    HashU32(&context_hasher, candidate.relation);
    Finish(&context_hasher, &target.context_fingerprint);

    blake3_hasher dominance_hasher{};
    StartDomain(&dominance_hasher, DominanceDomain);
    HashDigest(&dominance_hasher, target_coordinate);
    HashDigest(&dominance_hasher, candidate.physicality_id);
    HashU64(&dominance_hasher, candidate.source_logical_ordinal);
    HashU64(&dominance_hasher, candidate.target_logical_ordinal);
    HashU32(&dominance_hasher, candidate.relation);
    Finish(&dominance_hasher, &target.dominance_key);

    blake3_hasher state_hasher{};
    StartDomain(&state_hasher, StateDomain);
    HashDigest(&state_hasher, source.state_id);
    HashDigest(&state_hasher, target.anchor_id);
    HashDigest(&state_hasher, target.dominance_key);
    HashDigest(&state_hasher, target.binding_fingerprint);
    HashDigest(&state_hasher, target.obligation_fingerprint);
    HashDigest(&state_hasher, target.context_fingerprint);
    HashDigest(&state_hasher, target.evidence_epoch);
    HashDigest(&state_hasher, target.boundary_id);
    HashU64(&state_hasher, target.heuristic_cost);
    HashU32(&state_hasher, target.depth);
    HashU32(&state_hasher, target.flags);
    Finish(&state_hasher, &target.state_id);

    *transition = laplace_query_search_transition{};
    transition->source_state_id = source.state_id;
    transition->target = target;
    transition->relation_family = candidate.relation;
    transition->source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
    transition->direction = DirectionForRelation(candidate.relation);
    if (transition->direction == 0U) return false;
    transition->cost_components[0] = 1U;
    transition->cost_components[1] = candidate.gap;

    blake3_hasher law_hasher{};
    StartDomain(&law_hasher, LawDomain);
    HashU32(&law_hasher, candidate.relation);
    Finish(&law_hasher, &transition->law_fingerprint);

    HashCandidateCalculation(
        index, source_entity_id, candidate, &transition->calculation_receipt);

    blake3_hasher transition_hasher{};
    StartDomain(&transition_hasher, TransitionDomain);
    HashDigest(&transition_hasher, transition->source_state_id);
    HashDigest(&transition_hasher, transition->target.state_id);
    HashDigest(&transition_hasher, transition->law_fingerprint);
    HashDigest(&transition_hasher, transition->calculation_receipt);
    HashU32(&transition_hasher, transition->relation_family);
    HashU32(&transition_hasher, transition->direction);
    Finish(&transition_hasher, &transition->transition_id);
    return true;
}

int ExpandObservationBatch(
    void* const opaque,
    const laplace_query_search_state* const states,
    const std::uint64_t* const accumulated_costs,
    const std::size_t state_count,
    laplace_query_search_transition* const transitions,
    const std::size_t transition_capacity,
    std::size_t* const transition_count,
    laplace_query_search_expansion_receipt* const receipt) {
    if (opaque == nullptr || states == nullptr || accumulated_costs == nullptr ||
        state_count == 0U || transitions == nullptr || transition_count == nullptr ||
        receipt == nullptr) {
        return 1;
    }
    auto& index = *static_cast<laplace_observation_query_index*>(opaque);
    *transition_count = 0U;
    *receipt = laplace_query_search_expansion_receipt{};
    receipt->frontier_state_count = static_cast<std::uint64_t>(state_count);

    std::vector<std::pair<std::size_t, Candidate>> pending;
    try {
        for (std::size_t state_index = 0U; state_index < state_count; ++state_index) {
            (void)accumulated_costs[state_index];
            const auto& state = states[state_index];
            const auto* const binding = FindBinding(index, state.binding_fingerprint);
            laplace_id128 source_entity_id{};
            if (binding == nullptr ||
                !SameDigest(state.boundary_id, index.summary.boundary_id) ||
                !SameDigest(state.evidence_epoch, index.summary.evidence_epoch) ||
                !ResolveCoordinate(index, state.anchor_id, &source_entity_id)) {
                receipt->limiting_disposition =
                    LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
                pending.clear();
                break;
            }
            std::vector<Candidate> candidates;
            std::uint64_t rows_examined = 0U;
            std::uint64_t index_plans = 0U;
            bool limited = false;
            if (!GenerateCandidates(
                    index, *binding, source_entity_id, &candidates,
                    &rows_examined, &index_plans, &limited)) {
                return 2;
            }
            if (!Add(
                    receipt->rows_examined, rows_examined,
                    &receipt->rows_examined) ||
                !Add(
                    receipt->index_plan_count, index_plans,
                    &receipt->index_plan_count)) {
                return 3;
            }
            if (limited) {
                receipt->limiting_disposition =
                    LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
                pending.clear();
                break;
            }
            for (const auto& candidate : candidates) {
                pending.emplace_back(state_index, candidate);
            }
        }
    } catch (const std::bad_alloc&) {
        return 4;
    }

    if (pending.size() > transition_capacity) {
        return 5;
    }
    for (std::size_t index_in_pending = 0U;
         index_in_pending < pending.size(); ++index_in_pending) {
        const auto state_index = pending[index_in_pending].first;
        laplace_id128 source_entity_id{};
        if (!ResolveCoordinate(
                index, states[state_index].anchor_id, &source_entity_id)) {
            return 6;
        }
        const auto* const binding = FindBinding(
            index, states[state_index].binding_fingerprint);
        if (binding == nullptr ||
            !BuildTransition(
                index, *binding, states[state_index], source_entity_id,
                pending[index_in_pending].second,
                &transitions[index_in_pending])) {
            return 7;
        }
    }
    *transition_count = pending.size();
    receipt->emitted_transition_count =
        static_cast<std::uint64_t>(pending.size());

    blake3_hasher receipt_hasher{};
    StartDomain(&receipt_hasher, TransitionDomain);
    HashDigest(&receipt_hasher, index.summary.index_fingerprint);
    HashU64(&receipt_hasher, receipt->frontier_state_count);
    HashU64(&receipt_hasher, receipt->rows_examined);
    HashU64(&receipt_hasher, receipt->index_plan_count);
    HashU64(&receipt_hasher, receipt->emitted_transition_count);
    HashU32(&receipt_hasher, receipt->limiting_disposition);
    for (std::size_t state_index = 0U; state_index < state_count; ++state_index) {
        HashDigest(&receipt_hasher, states[state_index].state_id);
    }
    for (std::size_t transition_index = 0U;
         transition_index < pending.size(); ++transition_index) {
        HashDigest(&receipt_hasher, transitions[transition_index].transition_id);
    }
    Finish(&receipt_hasher, &receipt->receipt_id);
    return 0;
}

void ProjectionFingerprint(
    const laplace_digest256& state_id,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    StartDomain(
        &hasher, LAPLACE_COGNITION_FORWARD_ENUMERATION_DOMAIN,
        sizeof(LAPLACE_COGNITION_FORWARD_ENUMERATION_DOMAIN) - 1U);
    HashDigest(&hasher, state_id);
    HashU64(&hasher, static_cast<std::uint64_t>(projection_count));
    for (std::size_t index = 0U; index < projection_count; ++index) {
        const auto& projection = projections[index];
        HashDigest(&hasher, projection.projection_id);
        HashDigest(&hasher, projection.obligation_id);
        HashDigest(&hasher, projection.binding_fingerprint);
        HashDigest(&hasher, projection.world_id);
        HashDigest(&hasher, projection.time_fingerprint);
        HashDigest(&hasher, projection.context_fingerprint);
        HashDigest(&hasher, projection.evidence_boundary);
        HashDigest(&hasher, projection.authority_id);
        HashDigest(&hasher, projection.result_contract_fingerprint);
        HashU32(&hasher, projection.query_kind);
        HashU32(&hasher, projection.obligation_flags);
    }
    Finish(&hasher, output);
}

bool OperationCost(
    const laplace_observation_query_index& index,
    std::uint64_t* const cost) {
    std::uint64_t doubled = 0U;
    if (!Add(
            index.summary.maximum_candidate_records_per_expansion,
            index.summary.maximum_candidate_records_per_expansion,
            &doubled) ||
        !Add(doubled, 1U, cost)) {
        return false;
    }
    return true;
}

void HashOperationField(
    const char* const domain,
    const std::size_t domain_bytes,
    const laplace_observation_query_index& index,
    const laplace_cognition_query_projection& projection,
    const laplace_observation_query_binding& binding,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    StartDomain(&hasher, domain, domain_bytes);
    HashDigest(&hasher, index.summary.index_fingerprint);
    HashDigest(&hasher, projection.projection_id);
    HashDigest(&hasher, projection.obligation_id);
    HashDigest(&hasher, binding.binding_fingerprint);
    HashId(&hasher, binding.anchor_entity_id);
    HashU32(&hasher, binding.relation_mask);
    HashU32(&hasher, binding.maximum_results);
    HashU32(&hasher, binding.flags);
    Finish(&hasher, output);
}

template <std::size_t Count>
void HashOperationField(
    const char (&domain)[Count],
    const laplace_observation_query_index& index,
    const laplace_cognition_query_projection& projection,
    const laplace_observation_query_binding& binding,
    laplace_digest256* const output) {
    HashOperationField(
        domain, Count - 1U, index, projection, binding, output);
}

bool MakeOperation(
    const laplace_observation_query_index& index,
    const laplace_cognition_query_projection& projection,
    const laplace_observation_query_binding& binding,
    laplace_cognition_guidance_operation* const operation) {
    if (operation == nullptr ||
        (projection.query_kind != LAPLACE_COGNITION_OPERATION_INDEXED_SEARCH &&
         projection.query_kind != LAPLACE_COGNITION_OPERATION_STRUCTURAL_CALCULATION) ||
        !SameDigest(projection.evidence_boundary, index.summary.boundary_id)) {
        return false;
    }
    std::uint64_t operation_cost = 0U;
    if (!OperationCost(index, &operation_cost)) return false;
    *operation = laplace_cognition_guidance_operation{};
    operation->target_obligation_id = projection.obligation_id;
    operation->authority_id = projection.authority_id;
    operation->expected_obligation_reduction = 1U;
    operation->information_value = 1U;
    operation->resource_cost = operation_cost;
    operation->kind = projection.query_kind;
    operation->flags = LAPLACE_COGNITION_OPERATION_INFORMATION_SEEKING;
    HashOperationField(
        OperationDomain, index, projection, binding, &operation->operation_id);
    HashOperationField(
        OperationOperandsDomain, index, projection, binding,
        &operation->operands_fingerprint);
    HashOperationField(
        OperationPreconditionsDomain, index, projection, binding,
        &operation->preconditions_fingerprint);
    HashOperationField(
        OperationEffectDomain, index, projection, binding,
        &operation->predicted_effect_fingerprint);
    HashOperationField(
        OperationReceiptContractDomain, index, projection, binding,
        &operation->receipt_contract_fingerprint);
    return true;
}

int EnumerateObservationOperations(
    void* const opaque,
    const laplace_cognition_guidance_state* const state,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_cognition_forward_grant* const grant,
    laplace_cognition_guidance_operation* const operations,
    const std::size_t operation_capacity,
    std::size_t* const operation_count,
    laplace_cognition_forward_enumeration_receipt* const receipt) {
    if (opaque == nullptr || state == nullptr || projections == nullptr ||
        projection_count == 0U || grant == nullptr || operations == nullptr ||
        operation_count == nullptr || receipt == nullptr) {
        return 1;
    }
    auto& index = *static_cast<laplace_observation_query_index*>(opaque);
    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(state, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        return 2;
    }
    std::size_t emitted = 0U;
    for (std::size_t projection_index = 0U;
         projection_index < projection_count; ++projection_index) {
        const auto& projection = projections[projection_index];
        const auto* const binding = FindBinding(index, projection.binding_fingerprint);
        if (binding == nullptr ||
            !SameDigest(projection.evidence_boundary, index.summary.boundary_id)) {
            continue;
        }
        if (emitted >= operation_capacity ||
            !MakeOperation(index, projection, *binding, &operations[emitted])) {
            return 3;
        }
        ++emitted;
    }
    *operation_count = emitted;
    *receipt = laplace_cognition_forward_enumeration_receipt{};
    receipt->state_id = state_id;
    ProjectionFingerprint(
        state_id, projections, projection_count,
        &receipt->projection_fingerprint);
    receipt->projected_query_count = static_cast<std::uint64_t>(projection_count);
    receipt->candidate_operation_count = static_cast<std::uint64_t>(emitted);

    blake3_hasher receipt_hasher{};
    StartDomain(&receipt_hasher, EnumerationReceiptDomain);
    HashDigest(&receipt_hasher, index.summary.index_fingerprint);
    HashDigest(&receipt_hasher, state_id);
    HashDigest(&receipt_hasher, receipt->projection_fingerprint);
    HashU64(&receipt_hasher, receipt->projected_query_count);
    HashU64(&receipt_hasher, receipt->candidate_operation_count);
    for (std::size_t operation_index = 0U;
         operation_index < emitted; ++operation_index) {
        HashDigest(&receipt_hasher, operations[operation_index].operation_id);
    }
    Finish(&receipt_hasher, &receipt->receipt_id);
    (void)grant;
    return 0;
}

laplace_query_search_state InitialSearchState(
    const laplace_observation_query_index& index,
    const laplace_observation_query_binding& binding,
    const laplace_cognition_query_projection& projection) {
    laplace_query_search_state state{};
    EntityCoordinate(binding.anchor_entity_id, &state.anchor_id);
    state.binding_fingerprint = binding.binding_fingerprint;
    state.obligation_fingerprint = projection.obligation_id;
    state.context_fingerprint = projection.context_fingerprint;
    state.evidence_epoch = index.summary.evidence_epoch;
    state.boundary_id = index.summary.boundary_id;

    blake3_hasher dominance_hasher{};
    StartDomain(&dominance_hasher, DominanceDomain);
    HashDigest(&dominance_hasher, state.anchor_id);
    HashDigest(&dominance_hasher, state.binding_fingerprint);
    HashDigest(&dominance_hasher, state.obligation_fingerprint);
    Finish(&dominance_hasher, &state.dominance_key);

    blake3_hasher state_hasher{};
    StartDomain(&state_hasher, StateDomain);
    HashDigest(&state_hasher, index.summary.index_fingerprint);
    HashDigest(&state_hasher, state.anchor_id);
    HashDigest(&state_hasher, state.dominance_key);
    HashDigest(&state_hasher, state.binding_fingerprint);
    HashDigest(&state_hasher, state.obligation_fingerprint);
    HashDigest(&state_hasher, state.context_fingerprint);
    HashDigest(&state_hasher, state.evidence_epoch);
    HashDigest(&state_hasher, state.boundary_id);
    HashU64(&state_hasher, state.heuristic_cost);
    HashU32(&state_hasher, state.depth);
    HashU32(&state_hasher, state.flags);
    Finish(&state_hasher, &state.state_id);
    return state;
}

bool SearchProgram(
    const laplace_observation_query_index& index,
    const laplace_observation_query_binding& binding,
    const laplace_cognition_query_projection& projection,
    laplace_query_search_program* const program) {
    if (program == nullptr ||
        index.summary.maximum_candidate_records_per_expansion > UINT32_MAX) {
        return false;
    }
    const std::uint64_t capacity =
        index.summary.maximum_candidate_records_per_expansion;
    std::uint64_t emitted_states = 0U;
    std::uint64_t memory_bytes = 0U;
    if (!Add(capacity, 1U, &emitted_states) ||
        capacity > (std::numeric_limits<std::uint64_t>::max() - UINT64_C(1048576)) /
            UINT64_C(1024)) {
        return false;
    }
    memory_bytes = UINT64_C(1048576) + capacity * UINT64_C(1024);

    *program = laplace_query_search_program{};
    blake3_hasher id_hasher{};
    StartDomain(&id_hasher, SearchProgramDomain);
    HashDigest(&id_hasher, index.summary.index_fingerprint);
    HashDigest(&id_hasher, binding.binding_fingerprint);
    HashDigest(&id_hasher, projection.projection_id);
    Finish(&id_hasher, &program->program_id);
    blake3_hasher goal_hasher{};
    StartDomain(&goal_hasher, SearchGoalDomain);
    HashDigest(&goal_hasher, projection.obligation_id);
    HashDigest(&goal_hasher, binding.binding_fingerprint);
    Finish(&goal_hasher, &program->goal_id);
    program->boundary_id = index.summary.boundary_id;
    program->evidence_epoch = index.summary.evidence_epoch;
    program->result_contract_fingerprint = projection.result_contract_fingerprint;
    program->cost_weights[0] = 1U;
    program->budget.max_expanded_states = 1U;
    program->budget.max_transition_records = capacity;
    program->budget.max_emitted_states = emitted_states;
    program->budget.max_frontier_states = emitted_states;
    program->budget.max_memory_bytes = memory_bytes;
    program->budget.max_io_operations = 0U;
    program->budget.max_database_operations = 0U;
    program->budget.max_provider_calls = 1U;
    program->budget.max_depth = 1U;
    program->budget.requested_path_count = binding.maximum_results;
    program->budget.frontier_batch_width = 2U;
    program->budget.transition_batch_capacity = static_cast<std::uint32_t>(capacity);
    program->flags =
        LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE |
        LAPLACE_QUERY_SEARCH_FLAG_REQUIRE_SET_ORIENTED;
    return true;
}

std::uint32_t ResolutionDisposition(const std::uint32_t search_disposition) {
    switch (search_disposition) {
        case LAPLACE_QUERY_SEARCH_DISPOSITION_UNSUPPORTED:
            return LAPLACE_COGNITION_OBLIGATION_UNSUPPORTED;
        case LAPLACE_QUERY_SEARCH_DISPOSITION_DENIED:
            return LAPLACE_COGNITION_OBLIGATION_DENIED;
        case LAPLACE_QUERY_SEARCH_DISPOSITION_EXHAUSTED:
            return LAPLACE_COGNITION_OBLIGATION_EXHAUSTED;
        case LAPLACE_QUERY_SEARCH_DISPOSITION_CONTRADICTED:
            return LAPLACE_COGNITION_OBLIGATION_CONTRADICTED;
        default:
            return LAPLACE_COGNITION_OBLIGATION_UNKNOWN;
    }
}

bool FindProjection(
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_digest256& obligation_id,
    const laplace_cognition_query_projection** const projection) {
    for (std::size_t index = 0U; index < projection_count; ++index) {
        if (SameDigest(projections[index].obligation_id, obligation_id)) {
            *projection = &projections[index];
            return true;
        }
    }
    return false;
}

int ExecuteObservationOperation(
    void* const opaque,
    const laplace_cognition_guidance_state* const state,
    const laplace_cognition_guidance_operation* const operation,
    const laplace_cognition_query_projection* const projections,
    const std::size_t projection_count,
    const laplace_cognition_forward_grant* const grant,
    laplace_cognition_resolution* const resolutions,
    const std::size_t resolution_capacity,
    std::size_t* const resolution_count,
    laplace_cognition_forward_execution_receipt* const receipt) {
    if (opaque == nullptr || state == nullptr || operation == nullptr ||
        projections == nullptr || projection_count == 0U || grant == nullptr ||
        resolutions == nullptr || resolution_capacity == 0U ||
        resolution_count == nullptr || receipt == nullptr) {
        return 1;
    }
    auto& index = *static_cast<laplace_observation_query_index*>(opaque);
    const laplace_cognition_query_projection* projection = nullptr;
    if (!FindProjection(
            projections, projection_count, operation->target_obligation_id,
            &projection)) {
        return 2;
    }
    const auto* const binding = FindBinding(index, projection->binding_fingerprint);
    if (binding == nullptr ||
        !SameDigest(projection->evidence_boundary, index.summary.boundary_id)) {
        return 3;
    }
    laplace_cognition_guidance_operation expected_operation{};
    if (!MakeOperation(index, *projection, *binding, &expected_operation) ||
        !SameDigest(expected_operation.operation_id, operation->operation_id)) {
        return 4;
    }

    laplace_query_search_program search_program{};
    if (!SearchProgram(index, *binding, *projection, &search_program)) return 5;
    const auto initial_state = InitialSearchState(index, *binding, *projection);
    laplace_query_search_provider_v1 search_provider{};
    search_provider.state = &index;
    search_provider.expand_batch = ExpandObservationBatch;
    search_provider.abi_major = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MAJOR;
    search_provider.abi_minor = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MINOR;

    laplace_query_search_result* search_result = nullptr;
    laplace_query_search_receipt search_receipt{};
    const auto search_status = laplace_query_search_execute(
        &search_program, &initial_state, 1U, &search_provider,
        &search_result, &search_receipt);
    if (search_status != LAPLACE_QUERY_SEARCH_OK || search_result == nullptr) {
        laplace_query_search_result_destroy(&search_result);
        return 6;
    }

    std::uint64_t execution_cost = 1U;
    if (!Add(
            execution_cost, search_receipt.rows_examined, &execution_cost) ||
        !Add(
            execution_cost, search_receipt.transition_record_count,
            &execution_cost) ||
        execution_cost > grant->remaining_resource_cost ||
        search_receipt.io_operations > grant->remaining_io_operations ||
        search_receipt.database_operations >
            grant->remaining_database_operations) {
        laplace_query_search_result_destroy(&search_result);
        return 7;
    }

    blake3_hasher result_hasher{};
    StartDomain(&result_hasher, SearchResultDomain);
    HashDigest(&result_hasher, index.summary.index_fingerprint);
    HashDigest(&result_hasher, search_receipt.receipt_id);
    HashDigest(&result_hasher, projection->obligation_id);
    HashDigest(&result_hasher, binding->binding_fingerprint);
    HashU32(&result_hasher, search_receipt.disposition);
    const std::size_t path_count =
        laplace_query_search_result_path_count(search_result);
    HashU64(&result_hasher, static_cast<std::uint64_t>(path_count));
    for (std::size_t path_index = 0U; path_index < path_count; ++path_index) {
        laplace_query_search_path_summary path{};
        if (laplace_query_search_result_path(
                search_result, path_index, &path) != LAPLACE_QUERY_SEARCH_OK) {
            laplace_query_search_result_destroy(&search_result);
            return 8;
        }
        HashDigest(&result_hasher, path.path_id);
        HashDigest(&result_hasher, path.terminal_state_id);
        HashU64(&result_hasher, path.total_cost);
        HashU64(&result_hasher, path.transition_count);
    }
    laplace_digest256 result_fingerprint{};
    Finish(&result_hasher, &result_fingerprint);

    blake3_hasher resolution_receipt_hasher{};
    StartDomain(&resolution_receipt_hasher, ResolutionReceiptDomain);
    HashDigest(&resolution_receipt_hasher, result_fingerprint);
    HashDigest(&resolution_receipt_hasher, search_receipt.receipt_id);
    HashDigest(&resolution_receipt_hasher, operation->operation_id);
    laplace_digest256 resolution_receipt{};
    Finish(&resolution_receipt_hasher, &resolution_receipt);

    resolutions[0] = laplace_cognition_resolution{};
    resolutions[0].obligation_id = projection->obligation_id;
    resolutions[0].receipt_id = resolution_receipt;
    if (path_count != 0U &&
        search_receipt.disposition == LAPLACE_QUERY_SEARCH_DISPOSITION_COMPLETE) {
        resolutions[0].value_id = result_fingerprint;
        resolutions[0].disposition = LAPLACE_COGNITION_OBLIGATION_SATISFIED;
    } else {
        resolutions[0].disposition = ResolutionDisposition(search_receipt.disposition);
    }
    *resolution_count = 1U;

    laplace_digest256 state_id{};
    if (laplace_cognition_guidance_state_identify(state, &state_id) !=
        LAPLACE_COGNITION_GUIDANCE_OK) {
        laplace_query_search_result_destroy(&search_result);
        return 9;
    }
    *receipt = laplace_cognition_forward_execution_receipt{};
    receipt->state_id = state_id;
    receipt->operation_id = operation->operation_id;
    receipt->result_fingerprint = result_fingerprint;
    receipt->resolution_count = 1U;
    receipt->resource_cost = execution_cost;
    receipt->io_operations = search_receipt.io_operations;
    receipt->database_operations = search_receipt.database_operations;

    blake3_hasher receipt_hasher{};
    StartDomain(&receipt_hasher, ExecutionReceiptDomain);
    HashDigest(&receipt_hasher, index.summary.index_fingerprint);
    HashDigest(&receipt_hasher, receipt->state_id);
    HashDigest(&receipt_hasher, receipt->operation_id);
    HashDigest(&receipt_hasher, receipt->result_fingerprint);
    HashDigest(&receipt_hasher, search_receipt.receipt_id);
    HashDigest(&receipt_hasher, resolution_receipt);
    HashU64(&receipt_hasher, receipt->resource_cost);
    Finish(&receipt_hasher, &receipt->receipt_id);

    laplace_query_search_result_destroy(&search_result);
    return 0;
}

}  // namespace laplace_observation_query_detail

extern "C" laplace_observation_query_status
laplace_observation_query_search_provider(
    laplace_observation_query_index* const index,
    laplace_query_search_provider_v1* const provider) {
    if (index == nullptr || provider == nullptr) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    *provider = laplace_query_search_provider_v1{};
    provider->state = index;
    provider->expand_batch =
        laplace_observation_query_detail::ExpandObservationBatch;
    provider->abi_major = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_QUERY_SEARCH_PROVIDER_ABI_MINOR;
    return LAPLACE_OBSERVATION_QUERY_OK;
}

extern "C" laplace_observation_query_status
laplace_observation_query_cognition_provider(
    laplace_observation_query_index* const index,
    laplace_cognition_forward_provider_v1* const provider) {
    if (index == nullptr || provider == nullptr) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    *provider = laplace_cognition_forward_provider_v1{};
    provider->state = index;
    provider->enumerate =
        laplace_observation_query_detail::EnumerateObservationOperations;
    provider->execute =
        laplace_observation_query_detail::ExecuteObservationOperation;
    provider->abi_major = LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_FORWARD_PROVIDER_ABI_MINOR;
    return LAPLACE_OBSERVATION_QUERY_OK;
}
