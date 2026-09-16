bool AddUsage(
    laplace_cognition_coupling_receipt* receipt,
    const laplace_cognition_observation_candidate_usage& usage) noexcept {
    return Add(
               receipt->rows_examined,
               usage.rows_examined,
               &receipt->rows_examined) &&
        Add(
               receipt->index_plan_count,
               usage.index_plan_count,
               &receipt->index_plan_count) &&
        Add(
               receipt->crossing_count,
               usage.crossing_count,
               &receipt->crossing_count) &&
        Add(
               receipt->io_operations,
               usage.io_operations,
               &receipt->io_operations) &&
        Add(
               receipt->database_operations,
               usage.database_operations,
               &receipt->database_operations);
}

bool MemoryBoundValid(
    const laplace_cognition_coupling_request& request) noexcept {
    std::uint64_t witness_bytes = 0U;
    std::uint64_t frontier_bytes = 0U;
    std::uint64_t total = 0U;
    const std::uint64_t witness_unit =
        static_cast<std::uint64_t>(sizeof(laplace_cognition_coupling_witness)) +
        UINT64_C(160);
    const std::uint64_t frontier_unit =
        static_cast<std::uint64_t>(sizeof(FrontierNode)) + UINT64_C(96);
    return Multiply(
               request.maximum_witnesses,
               witness_unit,
               &witness_bytes) &&
        Multiply(
               request.maximum_frontier_states,
               frontier_unit,
               &frontier_bytes) &&
        Add(witness_bytes, frontier_bytes, &total) &&
        total <= request.maximum_memory_bytes;
}

}  // namespace laplace_cognition_coupling_detail

struct laplace_cognition_coupling_result {
    std::vector<laplace_cognition_coupling_response> responses;
    std::vector<laplace_cognition_coupling_witness> witnesses;
};
