    HashU64(&hasher, program.budget.max_expanded_states);
    HashU64(&hasher, program.budget.max_transition_records);
    HashU64(&hasher, program.budget.max_emitted_states);
    HashU64(&hasher, program.budget.max_frontier_states);
    HashU64(&hasher, program.budget.max_memory_bytes);
    HashU64(&hasher, program.budget.max_io_operations);
    HashU64(&hasher, program.budget.max_database_operations);
    HashU64(&hasher, program.budget.max_provider_calls);
    HashU32(&hasher, program.budget.max_depth);
    HashU32(&hasher, program.budget.requested_path_count);
    HashU32(&hasher, program.budget.frontier_batch_width);
    HashU32(&hasher, program.budget.transition_batch_capacity);
    HashU32(&hasher, program.flags);
    Finish(&hasher, output);
}

bool Add(const std::uint64_t left, const std::uint64_t right, std::uint64_t* const out) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return false;
    }
    *out = left + right;
    return true;
}

bool Multiply(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t* const out) {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        return false;
    }
    *out = left * right;
    return true;
}

bool WeightedCost(
    const laplace_query_search_program& program,
    const laplace_query_search_transition& transition,
    std::uint64_t* const output) {
    std::uint64_t total = 0U;
    for (std::size_t index = 0U; index < CostCount; ++index) {
        std::uint64_t component = 0U;
        if (!Multiply(
                transition.cost_components[index],
                program.cost_weights[index], &component) ||
            !Add(total, component, &total)) {
            return false;
        }
    }
    *output = total;
    return true;
}

bool ProviderDispositionValid(const std::uint32_t value) {
    return value == 0U || value == LAPLACE_QUERY_SEARCH_DISPOSITION_UNSUPPORTED ||
        value == LAPLACE_QUERY_SEARCH_DISPOSITION_DENIED ||
        value == LAPLACE_QUERY_SEARCH_DISPOSITION_CONTRADICTED ||
        value == LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
}

std::uint32_t MergeProviderDisposition(
    const std::uint32_t current,
    const std::uint32_t next) {
    if (next == 0U) return current;
    if (current == 0U || current == next) return next;
    return LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
}

bool TransitionValid(
    const laplace_query_search_program& program,
    const laplace_query_search_transition& transition) {
    return !Zero(transition.transition_id) &&
        !Zero(transition.source_state_id) &&
        StateValid(program, transition.target) &&
        !Zero(transition.law_fingerprint) &&
        !Zero(transition.calculation_receipt) &&
        transition.relation_family != 0U && transition.source_layer != 0U &&
        transition.direction != 0U && transition.flags == 0U;
}

struct Node {
    laplace_query_search_state state{};
    std::uint64_t accumulated_cost{};
    std::size_t parent{};
    laplace_query_search_transition incoming{};
    std::uint64_t ordinal{};
    bool has_parent{};
    bool closed{};
    bool dominated{};
};

struct QueueEntry {
    std::size_t node_index{};
    std::uint64_t priority{};
    std::uint64_t accumulated_cost{};
    std::uint32_t depth{};
    DigestKey state_id{};
    std::uint64_t ordinal{};
};

struct QueueLater {
    bool operator()(const QueueEntry& left, const QueueEntry& right) const {
        if (left.priority != right.priority) {
            return left.priority > right.priority;
        }
        if (left.accumulated_cost != right.accumulated_cost) {
            return left.accumulated_cost > right.accumulated_cost;
        }
        if (left.depth != right.depth) {
            return left.depth > right.depth;
        }
        if (left.state_id != right.state_id) {
            return left.state_id > right.state_id;
        }
        return left.ordinal > right.ordinal;
    }
};

struct Path {
    laplace_query_search_path_summary summary{};
    std::vector<laplace_query_search_transition> transitions;
};

}  // namespace

struct laplace_query_search_result {
    std::vector<Path> paths;
};

namespace {

class Search {
public:
    Search(
        const laplace_query_search_program& program,
        const laplace_query_search_provider_v1& provider,
        laplace_query_search_receipt* receipt)
        : program_(program), provider_(provider), receipt_(receipt) {
        blake3_hasher_init(&execution_hasher_);
        blake3_hasher_update(
            &execution_hasher_, LAPLACE_QUERY_SEARCH_OUTPUT_DOMAIN,
            sizeof(LAPLACE_QUERY_SEARCH_OUTPUT_DOMAIN) - 1U);
    }

    laplace_query_search_status Run(
        const laplace_query_search_state* initial_states,
        const std::size_t initial_count,
        laplace_query_search_result* const result) {
        if (initial_count > program_.budget.max_frontier_states ||
            initial_count > program_.budget.max_emitted_states) {
            return LAPLACE_QUERY_SEARCH_PROGRAM_INVALID;
        }
        for (std::size_t index = 0U; index < initial_count; ++index) {
            if (!StateValid(program_, initial_states[index])) {
                return LAPLACE_QUERY_SEARCH_STATE_INVALID;
            }
            const auto status = AddInitial(initial_states[index]);
            if (status != LAPLACE_QUERY_SEARCH_OK) return status;
        }
        while (true) {
            DiscardStaleTop();
            if (frontier_.empty()) {
                frontier_exhausted_ = true;
                break;
            }
            if (receipt_->expanded_state_count >=
                    program_.budget.max_expanded_states ||
                receipt_->provider_call_count >= program_.budget.max_provider_calls) {
                resource_exhausted_ = true;
                break;
            }
            std::vector<std::size_t> batch;
            const auto status = PopBatch(&batch, result);
            if (status != LAPLACE_QUERY_SEARCH_OK) return status;
            if (StopAfterTerminal(result)) break;
            if (batch.empty()) continue;
            const auto expand_status = ExpandBatch(batch);
            if (expand_status != LAPLACE_QUERY_SEARCH_OK) return expand_status;
            if (resource_exhausted_) break;
        }
        Finalize(result);
        return LAPLACE_QUERY_SEARCH_OK;
    }

private:
    const laplace_query_search_program& program_;
    const laplace_query_search_provider_v1& provider_;
    laplace_query_search_receipt* receipt_;
    std::vector<Node> nodes_;
    std::priority_queue<QueueEntry, std::vector<QueueEntry>, QueueLater> frontier_;
