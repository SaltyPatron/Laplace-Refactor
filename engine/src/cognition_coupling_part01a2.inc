struct Aggregate final {
    laplace_cognition_coupling_response response{};
    std::set<std::uint64_t> seeds;
    std::set<DigestKey> evidence_roots;
    std::vector<DigestKey> witness_ids;
};
