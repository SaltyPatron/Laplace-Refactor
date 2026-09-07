#include "laplace/cognition_observation_request.h"
#include <stdexcept>
#include <memory>

namespace laplace_observation_query_detail {

// Fixed storage for the same canonical generator. No growth occurs before an
// output-capacity failure, and no caller buffer is written before whole success.
struct CandidateSpan final {
    Candidate* storage{};
    std::size_t used{};
    void clear() { used = 0U; }
    std::size_t size() const { return used; }
    void push_back(const Candidate& value) { storage[used++] = value; }
    Candidate* begin() { return storage; }
    Candidate* end() { return used == 0U ? storage : storage + used; }
};

static_assert(sizeof(Candidate) <= sizeof(laplace_cognition_observation_candidate),
              "candidate projection exceeds its published workspace law");

}  // namespace laplace_observation_query_detail

/* Shared projection, not a second structural/semantic engine. GenerateCandidates
 * owns ordered occurrence/run, containment, direction and co-occurrence laws. */
extern "C" laplace_observation_query_status
laplace_observation_query_index_candidates_batch(
    const laplace_observation_query_index* const index,
    const laplace_observation_query_binding* const binding,
    const laplace_id128* const source_entity_ids,
    const std::size_t source_count,
    laplace_cognition_observation_candidate* const candidates,
    const std::size_t candidate_capacity,
    std::size_t* const candidate_count,
    laplace_cognition_observation_candidate_usage* const usage) {
    using namespace laplace_observation_query_detail;
    if (candidate_count != nullptr) *candidate_count = 0U;
    if (usage != nullptr) *usage = laplace_cognition_observation_candidate_usage{};
    if (index == nullptr || binding == nullptr || candidate_count == nullptr ||
        usage == nullptr || (source_count != 0U && source_entity_ids == nullptr) ||
        (candidate_capacity != 0U && candidates == nullptr)) {
        return LAPLACE_OBSERVATION_QUERY_INVALID_ARGUMENT;
    }
    if (source_count > std::numeric_limits<std::size_t>::max() / sizeof(laplace_id128) ||
        candidate_capacity > std::numeric_limits<std::size_t>::max() /
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_WORKSPACE_MULTIPLIER /
            sizeof(laplace_cognition_observation_candidate)) {
        return LAPLACE_OBSERVATION_QUERY_OVERFLOW;
    }
    laplace_digest256 identified{};
    if (BindingFingerprint(*binding, &identified) != LAPLACE_OBSERVATION_QUERY_OK ||
        !SameDigest(identified, binding->binding_fingerprint)) {
        return LAPLACE_OBSERVATION_QUERY_BINDING_INVALID;
    }
    if (source_count == 0U) return LAPLACE_OBSERVATION_QUERY_OK;
    std::unique_ptr<laplace_cognition_observation_candidate[]> staged;
    std::unique_ptr<Candidate[]> generated_storage;
    std::size_t staged_count = 0U;
    laplace_cognition_observation_candidate_usage measured{};
    try {
        if (candidate_capacity != 0U) {
            staged.reset(new laplace_cognition_observation_candidate[candidate_capacity]);
            generated_storage.reset(new Candidate[candidate_capacity]);
        }
        for (std::size_t source_index = 0U; source_index < source_count; ++source_index) {
            CandidateSpan generated{generated_storage.get(), 0U};
            std::uint64_t examined = 0U, plans = 0U;
            bool limited = false;
            if (!GenerateCandidates(*index, *binding, source_entity_ids[source_index],
                                    &generated, &examined, &plans, &limited,
                                    candidate_capacity - staged_count)) {
                return LAPLACE_OBSERVATION_QUERY_TRAJECTORY_INVALID;
            }
            if (limited || generated.size() > candidate_capacity - staged_count ||
                !Add(measured.rows_examined, examined, &measured.rows_examined) ||
                !Add(measured.index_plan_count, plans, &measured.index_plan_count)) {
                return LAPLACE_OBSERVATION_QUERY_OVERFLOW;
            }
            for (const auto& item : generated) {
                laplace_cognition_observation_candidate projected{};
                projected.target_entity_id = item.target_entity_id;
                // Exact immutable physicality plus ordered crossing fields is the
                // observation. A temporary index/cache generation is not identity.
                projected.observation_fingerprint = item.physicality_id;
                projected.source_state_index = static_cast<std::uint64_t>(source_index);
                projected.source_logical_ordinal = item.source_logical_ordinal;
                projected.target_logical_ordinal = item.target_logical_ordinal;
                projected.multiplicity = item.multiplicity;
                projected.gap = item.gap;
                projected.relation_family = item.relation;
                projected.source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_PHYSICALITY;
                switch (item.relation) {
                    case LAPLACE_OBSERVATION_QUERY_CONTAINER:
                    case LAPLACE_OBSERVATION_QUERY_PREDECESSOR:
                        projected.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_REVERSE;
                        break;
                    case LAPLACE_OBSERVATION_QUERY_CONSTITUENT:
                    case LAPLACE_OBSERVATION_QUERY_SUCCESSOR:
                        projected.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_FORWARD;
                        break;
                    case LAPLACE_OBSERVATION_QUERY_COOCCUR:
                        projected.direction = LAPLACE_OBSERVATION_QUERY_DIRECTION_SYMMETRIC;
                        break;
                    default:
                        return LAPLACE_OBSERVATION_QUERY_BINDING_INVALID;
                }
                staged[staged_count++] = projected;
            }
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_OBSERVATION_QUERY_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        return LAPLACE_OBSERVATION_QUERY_OVERFLOW;
    }
    measured.crossing_count = static_cast<std::uint64_t>(staged_count);
    if (staged_count != 0U) {
        std::copy_n(staged.get(), staged_count, candidates);
    }
    *candidate_count = staged_count;
    *usage = measured;
    return LAPLACE_OBSERVATION_QUERY_OK;
}
