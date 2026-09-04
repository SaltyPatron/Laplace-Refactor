extern "C" laplace_observation_query_status
laplace_observation_query_index_create_base(
    const laplace_observation_query_index_base_input* const input,
    laplace_observation_query_index** const index) {
    using namespace laplace_observation_query_detail;
    if (index != nullptr) *index = nullptr;
    if (input == nullptr || index == nullptr ||
        input->physicalities == nullptr || input->physicality_count == 0U ||
        (input->trajectory_segments == nullptr &&
         input->trajectory_segment_count != 0U) ||
        input->maximum_candidate_records_per_expansion == 0U ||
        input->flags != 0U || input->reserved != 0U ||
        DigestZero(input->boundary_id) || DigestZero(input->evidence_epoch)) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    try {
        auto* created = new laplace_observation_query_index{};
        created->summary.boundary_id = input->boundary_id;
        created->summary.evidence_epoch = input->evidence_epoch;
        created->summary.physicality_count =
            static_cast<std::uint64_t>(input->physicality_count);
        created->summary.trajectory_segment_count =
            static_cast<std::uint64_t>(input->trajectory_segment_count);
        created->summary.binding_count = 0U;
        created->summary.maximum_candidate_records_per_expansion =
            input->maximum_candidate_records_per_expansion;

        std::vector<laplace_persistence_physicality_record> ordered_physicalities(
            input->physicalities,
            input->physicalities + input->physicality_count);
        std::sort(
            ordered_physicalities.begin(), ordered_physicalities.end(),
            PhysicalityLess);
        created->containers.reserve(ordered_physicalities.size());
        std::map<DigestKey, std::size_t> physicality_lookup;
        for (const auto& physicality : ordered_physicalities) {
            laplace_digest256 expected{};
            if (laplace_persistence_physicality_identify(
                    &physicality, &expected) != LAPLACE_PERSISTENCE_OK ||
                !SameDigest(expected, physicality.physicality_id)) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_PHYSICALITY_INVALID;
            }
            const auto physicality_key = Key(physicality.physicality_id);
            if (physicality_lookup.find(physicality_key) !=
                physicality_lookup.end()) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_DUPLICATE;
            }
            const std::size_t container_index = created->containers.size();
            physicality_lookup.emplace(physicality_key, container_index);
            created->containers.push_back(Container{});
            created->containers.back().physicality = physicality;
            auto status = RegisterEntity(created, physicality.entity_id);
            if (status != LAPLACE_OBSERVATION_QUERY_OK) {
                delete created;
                return status;
            }
            created->containers_by_entity[Key(physicality.entity_id)].push_back(
                container_index);
        }

        for (std::size_t segment_index = 0U;
             segment_index < input->trajectory_segment_count; ++segment_index) {
            const auto& segment = input->trajectory_segments[segment_index];
            const auto found =
                physicality_lookup.find(Key(segment.physicality_id));
            if (found == physicality_lookup.end()) {
                delete created;
                return LAPLACE_OBSERVATION_QUERY_REFERENCE_INVALID;
            }
            created->containers[found->second].segments.push_back(segment);
        }

        for (auto& container : created->containers) {
            const auto status = ValidateAndDecodeContainer(created, &container);
            if (status != LAPLACE_OBSERVATION_QUERY_OK) {
                delete created;
                return status;
            }
        }

        created->summary.indexed_entity_count =
            static_cast<std::uint64_t>(created->coordinate_to_entity.size());
        HashIndex(*created, &created->summary.index_fingerprint);
        created->summary.status = LAPLACE_OBSERVATION_QUERY_OK;
        *index = created;
        return LAPLACE_OBSERVATION_QUERY_OK;
    } catch (const std::bad_alloc&) {
        return LAPLACE_OBSERVATION_QUERY_MEMORY_FAILURE;
    }
}
