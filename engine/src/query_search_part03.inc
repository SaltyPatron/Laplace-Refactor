    laplace_query_search_status PopBatch(
        std::vector<std::size_t>* const expansion_batch,
        laplace_query_search_result* const result) {
        DiscardStaleTop();
        if (frontier_.empty()) return LAPLACE_QUERY_SEARCH_OK;
        const std::uint64_t minimum_priority = frontier_.top().priority;
        const std::uint64_t remaining_expansions =
            program_.budget.max_expanded_states - receipt_->expanded_state_count;
        const std::size_t batch_limit = static_cast<std::size_t>(std::min<std::uint64_t>(
            program_.budget.frontier_batch_width, remaining_expansions));
        while (expansion_batch->size() < batch_limit) {
            DiscardStaleTop();
            if (frontier_.empty() || frontier_.top().priority != minimum_priority) break;
            const auto entry = frontier_.top();
            frontier_.pop();
            auto& node = nodes_[entry.node_index];
            node.closed = true;
            if (active_frontier_count_ != 0U) --active_frontier_count_;
            if ((node.state.flags & LAPLACE_QUERY_SEARCH_STATE_TERMINAL) != 0U) {
                const auto status = AddPath(entry.node_index, result);
                if (status != LAPLACE_QUERY_SEARCH_OK) return status;
                if (result->paths.size() >= program_.budget.requested_path_count) {
                    break;
                }
            } else if (node.state.depth < program_.budget.max_depth) {
                expansion_batch->push_back(entry.node_index);
            }
        }
        return LAPLACE_QUERY_SEARCH_OK;
    }

    bool StopAfterTerminal(const laplace_query_search_result* const result) const {
#if defined(LAPLACE_TEST_QUERY_SEARCH_IGNORE_REQUESTED_PATH_COUNT)
        return !result->paths.empty();
#else
        return result->paths.size() >= program_.budget.requested_path_count;
#endif
    }

    laplace_query_search_status AddPath(
        const std::size_t terminal_index,
        laplace_query_search_result* const result) {
        std::vector<laplace_query_search_transition> reverse;
        std::set<DigestKey> roots;
        std::size_t cursor = terminal_index;
        while (nodes_[cursor].has_parent) {
            const auto& transition = nodes_[cursor].incoming;
            reverse.push_back(transition);
            if (!Zero(transition.evidence_root_fingerprint)) {
                roots.insert(Key(transition.evidence_root_fingerprint));
            }
            cursor = nodes_[cursor].parent;
            if (cursor >= nodes_.size()) return LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
        }
        std::reverse(reverse.begin(), reverse.end());
        Path path{};
        path.transitions = std::move(reverse);
        path.summary.terminal_state_id = nodes_[terminal_index].state.state_id;
        path.summary.total_cost = nodes_[terminal_index].accumulated_cost;
        path.summary.transition_count = path.transitions.size();
        path.summary.independent_evidence_root_count = roots.size();
        path.summary.terminal_depth = nodes_[terminal_index].state.depth;
        path.summary.rank = static_cast<std::uint32_t>(result->paths.size() + 1U);
        blake3_hasher hasher{};
        blake3_hasher_init(&hasher);
        blake3_hasher_update(
            &hasher, LAPLACE_QUERY_SEARCH_PATH_DOMAIN,
            sizeof(LAPLACE_QUERY_SEARCH_PATH_DOMAIN) - 1U);
        HashDigest(&hasher, program_.program_id);
        for (const auto& transition : path.transitions) {
            HashDigest(&hasher, transition.transition_id);
        }
        HashDigest(&hasher, path.summary.terminal_state_id);
        HashU64(&hasher, path.summary.total_cost);
        HashU64(&hasher, path.summary.transition_count);
        Finish(&hasher, &path.summary.path_id);
        HashDigest(&execution_hasher_, path.summary.path_id);
        result->paths.push_back(std::move(path));
        return LAPLACE_QUERY_SEARCH_OK;
    }

    laplace_query_search_status ExpandBatch(
        const std::vector<std::size_t>& batch) {
        if (batch.empty()) return LAPLACE_QUERY_SEARCH_OK;
        const auto remaining_transitions =
            program_.budget.max_transition_records - receipt_->transition_record_count;
        if (remaining_transitions == 0U) {
            resource_exhausted_ = true;
            return LAPLACE_QUERY_SEARCH_OK;
        }
        const std::size_t capacity = static_cast<std::size_t>(std::min<std::uint64_t>(
            program_.budget.transition_batch_capacity, remaining_transitions));
        std::vector<laplace_query_search_state> states;
        std::vector<std::uint64_t> costs;
        states.reserve(batch.size());
        costs.reserve(batch.size());
        std::map<DigestKey, std::size_t> source_indexes;
        for (const auto index : batch) {
            states.push_back(nodes_[index].state);
            costs.push_back(nodes_[index].accumulated_cost);
            source_indexes.emplace(Key(nodes_[index].state.state_id), index);
        }
        std::vector<laplace_query_search_transition> transitions(capacity);
        std::size_t transition_count = 0U;
        laplace_query_search_expansion_receipt provider_receipt{};
        const int provider_status = provider_.expand_batch(
            provider_.state, states.data(), costs.data(), states.size(),
            transitions.data(), transitions.size(), &transition_count,
            &provider_receipt);
        ++receipt_->provider_call_count;
        receipt_->expanded_state_count += batch.size();
        if (provider_status != 0) return LAPLACE_QUERY_SEARCH_PROVIDER_FAILURE;
        if (transition_count > transitions.size() ||
            provider_receipt.frontier_state_count != states.size() ||
            provider_receipt.emitted_transition_count != transition_count ||
            Zero(provider_receipt.receipt_id) || provider_receipt.flags != 0U ||
            !ProviderDispositionValid(provider_receipt.limiting_disposition)) {
            return LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
        }
        if (provider_receipt.io_operations >
                program_.budget.max_io_operations - receipt_->io_operations ||
            provider_receipt.database_operations >
                program_.budget.max_database_operations - receipt_->database_operations) {
            return LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
        }
        receipt_->transition_record_count += transition_count;
        receipt_->rows_examined += provider_receipt.rows_examined;
        receipt_->index_plan_count += provider_receipt.index_plan_count;
        receipt_->crossing_count += provider_receipt.crossing_count;
        receipt_->io_operations += provider_receipt.io_operations;
        receipt_->database_operations += provider_receipt.database_operations;
        provider_disposition_ = MergeProviderDisposition(
            provider_disposition_, provider_receipt.limiting_disposition);
        HashDigest(&execution_hasher_, provider_receipt.receipt_id);
        transitions.resize(transition_count);
        std::sort(
            transitions.begin(), transitions.end(),
            [](const auto& left, const auto& right) {
                int compared = std::memcmp(
                    left.source_state_id.bytes, right.source_state_id.bytes, 32U);
                if (compared != 0) return compared < 0;
                compared = std::memcmp(
                    left.target.state_id.bytes, right.target.state_id.bytes, 32U);
                if (compared != 0) return compared < 0;
                return std::memcmp(
                    left.transition_id.bytes, right.transition_id.bytes, 32U) < 0;
            });
        for (const auto& transition : transitions) {
            if (!TransitionValid(program_, transition)) {
                return LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
            }
            const auto source = source_indexes.find(Key(transition.source_state_id));
            if (source == source_indexes.end()) {
                return LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
            }
            const auto source_index = source->second;
            const auto& source_node = nodes_[source_index];
            if (source_node.state.depth == std::numeric_limits<std::uint32_t>::max() ||
                transition.target.depth != source_node.state.depth + 1U) {
                return LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
            }
            if (!transition_ids_.insert(Key(transition.transition_id)).second) {
                return LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
            }
            std::uint64_t edge_cost = 0U;
            std::uint64_t accumulated = 0U;
            if (!WeightedCost(program_, transition, &edge_cost) ||
                !Add(source_node.accumulated_cost, edge_cost, &accumulated)) {
                return LAPLACE_QUERY_SEARCH_OVERFLOW;
            }
            bool inserted = false;
            const auto status = InsertNode(
                transition.target, accumulated, source_index, &transition, &inserted);
            if (status != LAPLACE_QUERY_SEARCH_OK) return status;
            HashDigest(&execution_hasher_, transition.transition_id);
            HashU32(&execution_hasher_, inserted ? 1U : 0U);
            if (resource_exhausted_) break;
        }
        return LAPLACE_QUERY_SEARCH_OK;
    }

    void Finalize(laplace_query_search_result* const result) {
        if (frontier_exhausted_) {
            receipt_->flags |= LAPLACE_QUERY_SEARCH_RECEIPT_FRONTIER_EXHAUSTED;
        }
        if (resource_exhausted_) {
            receipt_->flags |= LAPLACE_QUERY_SEARCH_RECEIPT_RESOURCE_EXHAUSTED;
        }
        const bool boundary_complete =
            (program_.flags & LAPLACE_QUERY_SEARCH_FLAG_BOUNDARY_COMPLETE) != 0U;
