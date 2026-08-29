    std::map<DigestKey, std::vector<std::size_t>> labels_;
    std::map<DigestKey, laplace_query_search_state> identities_;
    std::set<DigestKey> transition_ids_;
    std::uint64_t next_ordinal_{};
    std::uint64_t active_frontier_count_{};
    bool resource_exhausted_{};
    bool frontier_exhausted_{};
    std::uint32_t provider_disposition_{};
    blake3_hasher execution_hasher_{};

    std::uint64_t Priority(const Node& node) const {
#if defined(LAPLACE_TEST_QUERY_SEARCH_IGNORE_HEURISTIC)
        return node.accumulated_cost;
#else
        if ((program_.flags & LAPLACE_QUERY_SEARCH_FLAG_ASTAR) == 0U) {
            return node.accumulated_cost;
        }
        std::uint64_t result = 0U;
        return Add(node.accumulated_cost, node.state.heuristic_cost, &result)
            ? result
            : std::numeric_limits<std::uint64_t>::max();
#endif
    }

    std::uint64_t EstimatedMemory(const std::size_t additional_nodes = 0U) const {
        if (additional_nodes > std::numeric_limits<std::size_t>::max() - nodes_.size()) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        const std::size_t node_count = nodes_.size() + additional_nodes;
        const std::size_t per_node =
            sizeof(Node) + sizeof(QueueEntry) + EstimatedNodeOverhead;
        if (node_count > std::numeric_limits<std::uint64_t>::max() ||
            per_node > std::numeric_limits<std::uint64_t>::max()) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        std::uint64_t node_bytes = 0U;
        std::uint64_t transition_bytes = 0U;
        std::uint64_t total = 0U;
        if (!Multiply(
                static_cast<std::uint64_t>(node_count),
                static_cast<std::uint64_t>(per_node), &node_bytes) ||
            !Multiply(
                program_.budget.transition_batch_capacity,
                static_cast<std::uint64_t>(sizeof(laplace_query_search_transition)),
                &transition_bytes) ||
            !Add(node_bytes, transition_bytes, &total)) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return total;
    }

    laplace_query_search_status RegisterIdentity(
        const laplace_query_search_state& state) {
        const auto key = Key(state.state_id);
        const auto found = identities_.find(key);
        if (found == identities_.end()) {
            identities_.emplace(key, state);
            return LAPLACE_QUERY_SEARCH_OK;
        }
        return SameState(found->second, state)
            ? LAPLACE_QUERY_SEARCH_OK
            : LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
    }

    DigestKey DominanceKey(const laplace_query_search_state& state) const {
#if defined(LAPLACE_TEST_QUERY_SEARCH_ANCHOR_ONLY_DOMINANCE)
        return Key(state.anchor_id);
#else
        return Key(state.dominance_key);
#endif
    }

    bool Dominates(
        const Node& left,
        const std::uint64_t cost,
        const std::uint32_t depth) const {
#if defined(LAPLACE_TEST_QUERY_SEARCH_ANCHOR_ONLY_DOMINANCE)
        (void)depth;
        return !left.dominated && left.accumulated_cost <= cost;
#else
        return !left.dominated && left.accumulated_cost <= cost &&
            left.state.depth <= depth;
#endif
    }

    bool NewDominates(
        const std::uint64_t cost,
        const std::uint32_t depth,
        const Node& old) const {
#if defined(LAPLACE_TEST_QUERY_SEARCH_ANCHOR_ONLY_DOMINANCE)
        (void)depth;
        return !old.dominated && cost < old.accumulated_cost;
#else
        return !old.dominated && cost <= old.accumulated_cost &&
            depth <= old.state.depth &&
            (cost < old.accumulated_cost || depth < old.state.depth);
#endif
    }

    laplace_query_search_status InsertNode(
        const laplace_query_search_state& state,
        const std::uint64_t cost,
        const std::size_t parent,
        const laplace_query_search_transition* const incoming,
        bool* const inserted) {
        *inserted = false;
        auto status = RegisterIdentity(state);
        if (status != LAPLACE_QUERY_SEARCH_OK) return status;
        const auto key = DominanceKey(state);
        auto& labels = labels_[key];
        for (const auto index : labels) {
            if (Dominates(nodes_[index], cost, state.depth)) {
                ++receipt_->pruned_state_count;
                return LAPLACE_QUERY_SEARCH_OK;
            }
        }
        bool reopens = false;
        for (const auto index : labels) {
            auto& old = nodes_[index];
            if (NewDominates(cost, state.depth, old)) {
                if (old.closed &&
                    (program_.flags & LAPLACE_QUERY_SEARCH_FLAG_ALLOW_REOPEN) == 0U) {
                    return LAPLACE_QUERY_SEARCH_PROVIDER_CONTRACT;
                }
                reopens = reopens || old.closed;
                if (!old.closed && !old.dominated && active_frontier_count_ != 0U) {
                    --active_frontier_count_;
                }
                old.dominated = true;
            }
        }
        if (receipt_->emitted_state_count >= program_.budget.max_emitted_states ||
            active_frontier_count_ >= program_.budget.max_frontier_states ||
            EstimatedMemory(1U) > program_.budget.max_memory_bytes) {
            resource_exhausted_ = true;
            return LAPLACE_QUERY_SEARCH_OK;
        }
        Node node{};
        node.state = state;
        node.accumulated_cost = cost;
        node.parent = parent;
        node.ordinal = next_ordinal_++;
        node.has_parent = incoming != nullptr;
        if (incoming != nullptr) node.incoming = *incoming;
        const std::size_t node_index = nodes_.size();
        nodes_.push_back(node);
        labels.push_back(node_index);
        const std::uint64_t priority = Priority(nodes_.back());
        if (priority == std::numeric_limits<std::uint64_t>::max() &&
            (nodes_.back().accumulated_cost != priority ||
             nodes_.back().state.heuristic_cost != 0U)) {
            return LAPLACE_QUERY_SEARCH_OVERFLOW;
        }
        frontier_.push(QueueEntry{
            node_index, priority, cost, state.depth,
            Key(state.state_id), node.ordinal});
        ++active_frontier_count_;
        ++receipt_->emitted_state_count;
        if (reopens) ++receipt_->reopened_state_count;
        receipt_->estimated_peak_memory_bytes = std::max(
            receipt_->estimated_peak_memory_bytes, EstimatedMemory());
        *inserted = true;
        return LAPLACE_QUERY_SEARCH_OK;
    }

    laplace_query_search_status AddInitial(
        const laplace_query_search_state& state) {
        bool inserted = false;
        const auto status = InsertNode(state, 0U, 0U, nullptr, &inserted);
        if (status != LAPLACE_QUERY_SEARCH_OK) return status;
        if (!inserted && !resource_exhausted_) {
            return LAPLACE_QUERY_SEARCH_STATE_INVALID;
        }
        return resource_exhausted_
            ? LAPLACE_QUERY_SEARCH_PROGRAM_INVALID
            : LAPLACE_QUERY_SEARCH_OK;
    }

    bool EntryLive(const QueueEntry& entry) const {
        if (entry.node_index >= nodes_.size()) return false;
        const auto& node = nodes_[entry.node_index];
        return !node.dominated && !node.closed && node.ordinal == entry.ordinal;
    }

    void DiscardStaleTop() {
        while (!frontier_.empty() && !EntryLive(frontier_.top())) {
            frontier_.pop();
        }
    }

