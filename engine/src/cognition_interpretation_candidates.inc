#include "laplace/cognition_interpretation_candidates.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace laplace_cognition_interpretation_candidates_detail {

constexpr char FactorProgramDomain[] =
    "laplace-cognition-interpretation-factor-program-v1";
constexpr char FactorContextDomain[] =
    "laplace-cognition-interpretation-factor-query-context-v1";

bool DigestZero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool SameDigest(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool SameId(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
}

bool IdLess(const laplace_id128& left, const laplace_id128& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

bool DigestLess(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) < 0;
}

void HashU32(blake3_hasher* const hasher, const std::uint32_t value) {
    const std::array<std::uint8_t, 4> bytes{{
        static_cast<std::uint8_t>(value),
        static_cast<std::uint8_t>(value >> 8U),
        static_cast<std::uint8_t>(value >> 16U),
        static_cast<std::uint8_t>(value >> 24U)}};
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashU64(blake3_hasher* const hasher, const std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(value >> (index * 8U));
    }
    blake3_hasher_update(hasher, bytes.data(), bytes.size());
}

void HashDigest(blake3_hasher* const hasher, const laplace_digest256& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void HashId(blake3_hasher* const hasher, const laplace_id128& value) {
    blake3_hasher_update(hasher, value.bytes, sizeof(value.bytes));
}

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool ScopeMatches(
    const laplace_cognition_interpretation_program& program,
    const laplace_cognition_turn_input& turn,
    const laplace_cognition_turn_policy& policy) {
    return SameId(program.observation_root, turn.observation_entity_id) &&
        SameDigest(program.occurrence_id, turn.observation_occurrence_id) &&
        SameDigest(program.world_id, turn.world_id) &&
        SameDigest(program.time_fingerprint, turn.time_fingerprint) &&
        SameDigest(program.context_id, turn.context_fingerprint) &&
        SameDigest(program.evidence_epoch, policy.evidence_epoch) &&
        SameDigest(program.boundary_id, policy.evidence_boundary) &&
        SameDigest(program.authority_id, policy.authority_id);
}

laplace_cognition_interpretation_status Identify(
    const laplace_cognition_interpretation_factor_program& program,
    laplace_digest256* const identity) {
    if (identity != nullptr) *identity = laplace_digest256{};
    if (identity == nullptr ||
        program.version != LAPLACE_COGNITION_INTERPRETATION_FACTOR_PROGRAM_VERSION ||
        program.reserved != 0U || program.factors == nullptr ||
        program.factor_count == 0U ||
        program.factor_count > static_cast<std::size_t>(UINT32_MAX)) {
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    }
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, FactorProgramDomain, sizeof(FactorProgramDomain) - 1U);
    HashU32(&hasher, program.version);
    HashU64(&hasher, static_cast<std::uint64_t>(program.factor_count));
    for (std::size_t index = 0U; index < program.factor_count; ++index) {
        const auto& factor = program.factors[index];
        if (DigestZero(factor.factor_id) || DigestZero(factor.law_id) ||
            factor.relation_mask == 0U ||
            (factor.relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) != 0U ||
            factor.maximum_rows == 0U || factor.reserved0 != 0U ||
            factor.reserved1 != 0U || factor.source_slot == factor.target_slot) {
            return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
        }
        HashDigest(&hasher, factor.factor_id);
        HashDigest(&hasher, factor.law_id);
        HashU32(&hasher, factor.source_slot);
        HashU32(&hasher, factor.target_slot);
        HashU32(&hasher, factor.relation_mask);
        HashU32(&hasher, factor.maximum_rows);
    }
    Finish(&hasher, identity);
    return LAPLACE_COGNITION_INTERPRETATION_OK;
}

laplace_digest256 FactorContext(
    const laplace_digest256& factor_program_id,
    const laplace_digest256& context,
    const laplace_digest256& factor_id,
    const laplace_id128& source) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, FactorContextDomain, sizeof(FactorContextDomain) - 1U);
    HashDigest(&hasher, factor_program_id);
    HashDigest(&hasher, context);
    HashDigest(&hasher, factor_id);
    HashId(&hasher, source);
    laplace_digest256 output{};
    Finish(&hasher, &output);
    return output;
}

struct RowStorage final {
    std::vector<laplace_id128> values;
    laplace_digest256 observation{};
    laplace_digest256 evidence{};
    laplace_digest256 calculation{};
};

struct FactorStorage final {
    laplace_digest256 factor_id{};
    laplace_digest256 law_id{};
    std::vector<std::uint32_t> slots;
    std::vector<RowStorage> storage;
    std::vector<laplace_cognition_interpretation_row> rows;
    laplace_cognition_interpretation_factor factor{};

    void Finalize() {
        rows.clear();
        rows.reserve(storage.size());
        for (auto& row : storage) {
            laplace_cognition_interpretation_row visible{};
            visible.values = row.values.data();
            visible.observation_id = row.observation;
            visible.evidence_root_id = row.evidence;
            visible.calculation_receipt_id = row.calculation;
            rows.push_back(visible);
        }
        factor = laplace_cognition_interpretation_factor{};
        factor.factor_id = factor_id;
        factor.law_id = law_id;
        factor.slots = slots.data();
        factor.slot_count = slots.size();
        factor.rows = rows.empty() ? nullptr : rows.data();
        factor.row_count = rows.size();
    }
};

void AddDomainValue(
    std::vector<std::vector<laplace_id128>>& domains,
    const std::uint32_t slot,
    const laplace_id128& value) {
    auto& domain = domains[slot];
    const auto found = std::lower_bound(domain.begin(), domain.end(), value, IdLess);
    if (found == domain.end() || !SameId(*found, value)) domain.insert(found, value);
}

bool BuildRequest(
    const laplace_cognition_interpretation_program& interpretation,
    const laplace_cognition_turn_policy& policy,
    const laplace_cognition_interpretation_factor_rule& rule,
    const laplace_digest256& factor_program_id,
    const laplace_id128& source,
    laplace_cognition_observation_request* const request) {
    if (request == nullptr || rule.maximum_rows > policy.maximum_results ||
        rule.maximum_rows > policy.search_budget.requested_path_count ||
        (rule.relation_mask & ~policy.relation_mask) != 0U) {
        return false;
    }
    *request = laplace_cognition_observation_request{};
    request->anchor_entity_id = source;
    request->world_id = interpretation.world_id;
    request->time_fingerprint = interpretation.time_fingerprint;
    request->context_fingerprint = FactorContext(
        factor_program_id, interpretation.context_id, rule.factor_id, source);
    request->evidence_boundary = interpretation.boundary_id;
    request->evidence_epoch = interpretation.evidence_epoch;
    request->authority_id = interpretation.authority_id;
    request->result_contract_fingerprint = factor_program_id;
    request->search_budget = policy.search_budget;
    request->search_budget.requested_path_count = rule.maximum_rows;
    request->forward_limits = policy.forward_limits;
    request->relation_mask = rule.relation_mask;
    request->maximum_results = rule.maximum_rows;
    request->flags = LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    if ((policy.request_flags &
         LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE) != 0U) {
        request->flags |= LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE;
    }
    if ((policy.request_flags &
         LAPLACE_COGNITION_OBSERVATION_REQUEST_ALLOW_TYPED_UNRESOLVED) != 0U) {
        request->flags |= LAPLACE_COGNITION_OBSERVATION_REQUEST_ALLOW_TYPED_UNRESOLVED;
    }
    request->version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;
    return true;
}

laplace_cognition_interpretation_status MaterializeSource(
    const laplace_cognition_interpretation_program& interpretation,
    const laplace_cognition_turn_policy& policy,
    const laplace_cognition_interpretation_factor_rule& rule,
    const laplace_digest256& factor_program_id,
    const laplace_id128& source,
    const laplace_cognition_observation_candidate_provider_v1& provider,
    FactorStorage* const output,
    std::vector<std::vector<laplace_id128>>& domains) {
    using CandidateState =
        laplace_cognition_observation_candidate_provider_detail::ProviderState;
    laplace_cognition_observation_request request{};
    if (!BuildRequest(
            interpretation, policy, rule, factor_program_id, source, &request)) {
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    }
    laplace_cognition_observation_compiled_request compiled{};
    if (laplace_cognition_observation_request_compile(&request, &compiled) !=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        laplace_cognition_observation_compiled_request_destroy(&compiled);
        return LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
    }

    try {
        CandidateState state{};
        state.external = &provider;
        state.request_fingerprint = compiled.request_fingerprint;
        state.result_contract_fingerprint = request.result_contract_fingerprint;
        state.binding = compiled.binding;
        state.initial_search_state = compiled.initial_search_state;
        state.search_program = compiled.search_program;
        if (!laplace_cognition_observation_candidate_provider_detail::RegisterEntity(
                &state, source)) {
            laplace_cognition_observation_compiled_request_destroy(&compiled);
            return LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
        }

        const std::uint64_t provider_capacity =
            provider.maximum_candidate_records_per_expansion;
        const std::uint64_t bounded_capacity =
            std::min<std::uint64_t>(provider_capacity, rule.maximum_rows);
        if (bounded_capacity == 0U ||
            bounded_capacity > static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max())) {
            laplace_cognition_observation_compiled_request_destroy(&compiled);
            return LAPLACE_COGNITION_INTERPRETATION_RANGE;
        }
        std::vector<laplace_cognition_observation_candidate> candidates(
            static_cast<std::size_t>(bounded_capacity));
        std::size_t candidate_count = 0U;
        laplace_cognition_observation_candidate_usage usage{};
        const std::uint64_t zero_cost = 0U;
        const int provider_status = provider.enumerate_candidates(
            provider.state, &compiled.binding, &source,
            &compiled.initial_search_state, &zero_cost, 1U,
            candidates.data(), candidates.size(), &candidate_count, &usage);
        if (provider_status != 0 || candidate_count > candidates.size() ||
            usage.flags != 0U ||
            (usage.limiting_disposition != 0U && candidate_count != 0U)) {
            laplace_cognition_observation_compiled_request_destroy(&compiled);
            return LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
        }
        candidates.resize(candidate_count);
        for (const auto& candidate : candidates) {
            if (!laplace_cognition_observation_candidate_provider_detail::CandidateValid(
                    state, candidate, 1U)) {
                laplace_cognition_observation_compiled_request_destroy(&compiled);
                return LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
            }
            laplace_query_search_transition transition{};
            if (!laplace_cognition_observation_candidate_provider_detail::BuildTransition(
                    state, compiled.initial_search_state, source, candidate,
                    &transition)) {
                laplace_cognition_observation_compiled_request_destroy(&compiled);
                return LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
            }
            if (!SameDigest(transition.law_fingerprint, rule.law_id)) continue;
            if (output->storage.size() == rule.maximum_rows) {
                laplace_cognition_observation_compiled_request_destroy(&compiled);
                return LAPLACE_COGNITION_INTERPRETATION_EXHAUSTED;
            }

            RowStorage row{};
            if (rule.source_slot ==
                LAPLACE_COGNITION_INTERPRETATION_FACTOR_SOURCE_OBSERVATION) {
                row.values.push_back(candidate.target_entity_id);
            } else if (rule.source_slot < rule.target_slot) {
                row.values.push_back(source);
                row.values.push_back(candidate.target_entity_id);
            } else {
                row.values.push_back(candidate.target_entity_id);
                row.values.push_back(source);
            }
            row.observation = candidate.observation_fingerprint;
            row.evidence = candidate.evidence_root_fingerprint;
            row.calculation = transition.calculation_receipt;
            output->storage.push_back(std::move(row));
            AddDomainValue(domains, rule.target_slot, candidate.target_entity_id);
        }
        laplace_cognition_observation_compiled_request_destroy(&compiled);
        return LAPLACE_COGNITION_INTERPRETATION_OK;
    } catch (const std::bad_alloc&) {
        laplace_cognition_observation_compiled_request_destroy(&compiled);
        return LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        laplace_cognition_observation_compiled_request_destroy(&compiled);
        return LAPLACE_COGNITION_INTERPRETATION_RANGE;
    }
}

}  // namespace laplace_cognition_interpretation_candidates_detail

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_factor_program_identify(
    const laplace_cognition_interpretation_factor_program* const program,
    laplace_digest256* const identity) {
    if (program == nullptr) {
        if (identity != nullptr) *identity = laplace_digest256{};
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    }
    return laplace_cognition_interpretation_candidates_detail::Identify(
        *program, identity);
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_execute_with_candidate_provider(
    const laplace_cognition_interpretation_program* const interpretation_program,
    const laplace_cognition_interpretation_factor_program* const factor_program,
    const laplace_cognition_turn_input* const turn,
    const laplace_cognition_turn_policy* const policy,
    const laplace_cognition_observation_candidate_provider_v1* const provider,
    laplace_cognition_interpretation_result** const result,
    laplace_cognition_interpretation_receipt* const receipt) {
    using namespace laplace_cognition_interpretation_candidates_detail;
    if (result != nullptr) *result = nullptr;
    if (receipt != nullptr) *receipt = laplace_cognition_interpretation_receipt{};
    if (interpretation_program == nullptr || factor_program == nullptr ||
        turn == nullptr || policy == nullptr || provider == nullptr ||
        result == nullptr || receipt == nullptr ||
        policy->version != LAPLACE_COGNITION_TURN_POLICY_VERSION ||
        turn->version != LAPLACE_COGNITION_TURN_VERSION ||
        !ScopeMatches(*interpretation_program, *turn, *policy) ||
        !laplace_cognition_observation_candidate_provider_detail::ProviderValid(
            laplace_cognition_observation_request{}, *provider)) {
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    }

    laplace_digest256 factor_program_id{};
    const auto identified = Identify(*factor_program, &factor_program_id);
    if (identified != LAPLACE_COGNITION_INTERPRETATION_OK) return identified;

    try {
        std::vector<std::vector<laplace_id128>> domains(
            interpretation_program->slot_count);
        std::vector<FactorStorage> materialized;
        materialized.reserve(factor_program->factor_count);
        for (std::size_t index = 0U; index < factor_program->factor_count; ++index) {
            const auto& rule = factor_program->factors[index];
            if (rule.target_slot >= interpretation_program->slot_count ||
                (rule.source_slot !=
                     LAPLACE_COGNITION_INTERPRETATION_FACTOR_SOURCE_OBSERVATION &&
                 rule.source_slot >= interpretation_program->slot_count)) {
                return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
            }
            FactorStorage factor{};
            factor.factor_id = rule.factor_id;
            factor.law_id = rule.law_id;
            if (rule.source_slot ==
                LAPLACE_COGNITION_INTERPRETATION_FACTOR_SOURCE_OBSERVATION) {
                factor.slots.push_back(rule.target_slot);
                const auto status = MaterializeSource(
                    *interpretation_program, *policy, rule, factor_program_id,
                    interpretation_program->observation_root, *provider,
                    &factor, domains);
                if (status != LAPLACE_COGNITION_INTERPRETATION_OK) return status;
            } else {
                const auto sources = domains[rule.source_slot];
                if (rule.source_slot < rule.target_slot) {
                    factor.slots = {rule.source_slot, rule.target_slot};
                } else {
                    factor.slots = {rule.target_slot, rule.source_slot};
                }
                for (const auto& source : sources) {
                    const auto status = MaterializeSource(
                        *interpretation_program, *policy, rule, factor_program_id,
                        source, *provider, &factor, domains);
                    if (status != LAPLACE_COGNITION_INTERPRETATION_OK) return status;
                }
            }
            materialized.push_back(std::move(factor));
        }

        std::sort(materialized.begin(), materialized.end(),
            [](const FactorStorage& left, const FactorStorage& right) {
                return DigestLess(left.factor_id, right.factor_id);
            });
        for (std::size_t index = 1U; index < materialized.size(); ++index) {
            if (!DigestLess(materialized[index - 1U].factor_id,
                            materialized[index].factor_id)) {
                return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
            }
        }
        std::vector<laplace_cognition_interpretation_factor> factors;
        factors.reserve(materialized.size());
        for (auto& factor : materialized) {
            factor.Finalize();
            factors.push_back(factor.factor);
        }
        return laplace_cognition_interpretation_execute(
            interpretation_program, factors.data(), factors.size(), result, receipt);
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        return LAPLACE_COGNITION_INTERPRETATION_RANGE;
    }
}
