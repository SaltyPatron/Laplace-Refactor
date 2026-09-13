#include "laplace/cognition_interpretation.h"
#include "blake3.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <memory_resource>
#include <new>
#include <stdexcept>
#include <vector>

namespace laplace_interpretation_detail {

struct Exhausted {};

class Budget final : public std::pmr::memory_resource {
public:
    explicit Budget(uint64_t limit) : limit_(limit) {}
private:
    void* do_allocate(size_t bytes, size_t alignment) override {
        if (bytes > limit_ - used_) throw Exhausted{};
        void* memory = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        used_ += bytes;
        return memory;
    }
    void do_deallocate(void* memory, size_t bytes, size_t alignment) override {
        std::pmr::new_delete_resource()->deallocate(memory, bytes, alignment);
        used_ -= bytes;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
    uint64_t limit_;
    uint64_t used_ = 0;
};

bool Zero(const laplace_digest256& value) {
    uint8_t bits = 0;
    for (uint8_t byte : value.bytes) bits = static_cast<uint8_t>(bits | byte);
    return bits == 0;
}

struct Hash {
    blake3_hasher state{};
    explicit Hash(const char* domain) {
        blake3_hasher_init(&state);
        blake3_hasher_update(&state, domain, std::strlen(domain));
    }
    void Bytes(const void* bytes, size_t count) {
        blake3_hasher_update(&state, bytes, count);
    }
    void Number(uint64_t number) {
        uint8_t bytes[8];
        for (size_t i = 0; i < 8; ++i) bytes[i] = static_cast<uint8_t>(number >> (8 * i));
        Bytes(bytes, sizeof(bytes));
    }
    laplace_digest256 Finish() {
        laplace_digest256 digest{};
        blake3_hasher_finalize(&state, digest.bytes, sizeof(digest.bytes));
        return digest;
    }
};

void Seal(laplace_cognition_interpretation_receipt& receipt) {
    Hash hash("laplace-cognition-interpretation-receipt-v1");
    hash.Bytes(receipt.input_fingerprint.bytes, 32);
    hash.Bytes(receipt.result_fingerprint.bytes, 32);
    hash.Number(receipt.comparisons);
    hash.Number(receipt.rejected_pairs);
    hash.Number(receipt.generated_states);
    hash.Number(receipt.peak_frontier_states);
    hash.Number(receipt.derivation_count);
    hash.Number(receipt.completed_factor_count);
    hash.Number(receipt.disposition);
    hash.Number(receipt.version);
    receipt.receipt_id = hash.Finish();
}

bool Validate(const laplace_cognition_interpretation_program& program,
              const laplace_cognition_interpretation_factor* factors,
              size_t count, laplace_digest256& fingerprint) {
    if (program.version != LAPLACE_COGNITION_INTERPRETATION_VERSION || program.reserved != 0 ||
        program.slot_ids == nullptr || program.slot_count == 0 ||
        program.slot_count > UINT32_MAX || factors == nullptr || count == 0 ||
        program.maximum_comparisons == 0 || program.maximum_states == 0 ||
        program.maximum_memory_bytes == 0 || Zero(program.occurrence_id) ||
        Zero(program.world_id) || Zero(program.time_fingerprint) || Zero(program.context_id) ||
        Zero(program.authority_id) ||
        Zero(program.evidence_epoch) || Zero(program.boundary_id)) return false;
    if (count > SIZE_MAX / sizeof(*factors) ||
        program.slot_count > SIZE_MAX / sizeof(*program.slot_ids)) return false;
    Hash hash("laplace-cognition-interpretation-input-v1");
    hash.Bytes(program.observation_root.bytes, 16);
    for (const auto* value : {&program.occurrence_id, &program.world_id, &program.time_fingerprint,
            &program.context_id, &program.evidence_epoch, &program.boundary_id, &program.authority_id})
        hash.Bytes(value->bytes, 32);
    hash.Number(program.maximum_comparisons);
    hash.Number(program.maximum_states);
    hash.Number(program.maximum_memory_bytes);
    hash.Number(program.slot_count);
    for (size_t i = 0; i < program.slot_count; ++i) {
        if (Zero(program.slot_ids[i]) || (i != 0 &&
            std::memcmp(program.slot_ids[i - 1].bytes, program.slot_ids[i].bytes, 32) >= 0))
            return false;
        hash.Bytes(program.slot_ids[i].bytes, 32);
    }
    hash.Number(count);
    for (size_t f = 0; f < count; ++f) {
        const auto& factor = factors[f];
        if (Zero(factor.factor_id) || Zero(factor.law_id) || factor.slots == nullptr ||
            factor.slot_count == 0 || factor.slot_count > program.slot_count ||
            (factor.row_count != 0 && factor.rows == nullptr) ||
            factor.row_count > SIZE_MAX / sizeof(*factor.rows) ||
            (f != 0 && std::memcmp(factors[f - 1].factor_id.bytes,
                                  factor.factor_id.bytes, 32) >= 0)) return false;
        hash.Bytes(factor.factor_id.bytes, 32);
        hash.Bytes(factor.law_id.bytes, 32);
        hash.Number(factor.slot_count);
        for (size_t s = 0; s < factor.slot_count; ++s) {
            if (factor.slots[s] >= program.slot_count ||
                (s != 0 && factor.slots[s - 1] >= factor.slots[s])) return false;
            hash.Number(factor.slots[s]);
        }
        hash.Number(factor.row_count);
        for (size_t r = 0; r < factor.row_count; ++r) {
            const auto& row = factor.rows[r];
            if (row.values == nullptr || Zero(row.observation_id) ||
                (Zero(row.evidence_root_id) && Zero(row.calculation_receipt_id))) return false;
            hash.Bytes(row.observation_id.bytes, 32);
            hash.Bytes(row.evidence_root_id.bytes, 32);
            hash.Bytes(row.calculation_receipt_id.bytes, 32);
            for (size_t s = 0; s < factor.slot_count; ++s)
                hash.Bytes(row.values[s].bytes, 16);
        }
    }
    fingerprint = hash.Finish();
    return true;
}

} // namespace laplace_interpretation_detail

struct laplace_cognition_interpretation_result {
    laplace_interpretation_detail::Budget budget;
    std::pmr::vector<laplace_id128> values;
    std::pmr::vector<uint64_t> rows;
    std::pmr::vector<laplace_digest256> slot_ids;
    std::pmr::vector<laplace_cognition_interpretation_witness> witnesses;
    std::pmr::vector<size_t> witness_offsets;
    laplace_id128 observation_root{};
    laplace_digest256 occurrence_id{}, world_id{}, context_id{}, evidence_epoch{}, boundary_id{};
    laplace_digest256 time_fingerprint{}, authority_id{};
    laplace_cognition_interpretation_receipt receipt{};
    size_t slots;
    size_t factors;
    laplace_cognition_interpretation_result(uint64_t memory, size_t s, size_t f)
        : budget(memory), values(&budget), rows(&budget), slot_ids(&budget),
          witnesses(&budget), witness_offsets(&budget), slots(s), factors(f) {}
};

namespace laplace_interpretation_detail {

bool SameScope(const laplace_cognition_interpretation_result& interpretation,
               const laplace_cognition_turn_input& turn,
               const laplace_digest256& evidence_epoch,
               const laplace_digest256& evidence_boundary,
               const laplace_digest256& authority_id) {
    return std::memcmp(interpretation.observation_root.bytes, turn.observation_entity_id.bytes, 16) == 0 &&
        std::memcmp(interpretation.occurrence_id.bytes, turn.observation_occurrence_id.bytes, 32) == 0 &&
        std::memcmp(interpretation.world_id.bytes, turn.world_id.bytes, 32) == 0 &&
        std::memcmp(interpretation.time_fingerprint.bytes, turn.time_fingerprint.bytes, 32) == 0 &&
        std::memcmp(interpretation.context_id.bytes, turn.context_fingerprint.bytes, 32) == 0 &&
        std::memcmp(interpretation.evidence_epoch.bytes, evidence_epoch.bytes, 32) == 0 &&
        std::memcmp(interpretation.boundary_id.bytes, evidence_boundary.bytes, 32) == 0 &&
        std::memcmp(interpretation.authority_id.bytes, authority_id.bytes, 32) == 0;
}

struct GuidanceOwner {
    laplace_cognition_guidance_state* state{};
    ~GuidanceOwner() { laplace_cognition_guidance_state_destroy(&state); }
};

} // namespace laplace_interpretation_detail

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_execute(
    const laplace_cognition_interpretation_program* program,
    const laplace_cognition_interpretation_factor* factors, size_t factor_count,
    laplace_cognition_interpretation_result** result,
    laplace_cognition_interpretation_receipt* receipt) {
    using namespace laplace_interpretation_detail;
    if (result != nullptr) *result = nullptr;
    if (receipt != nullptr) *receipt = {};
    if (program == nullptr || result == nullptr || receipt == nullptr ||
        !Validate(*program, factors, factor_count, receipt->input_fingerprint))
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    receipt->version = LAPLACE_COGNITION_INTERPRETATION_VERSION;
    try {
        if (program->maximum_memory_bytes < sizeof(laplace_cognition_interpretation_result))
            throw Exhausted{};
        auto output = std::make_unique<laplace_cognition_interpretation_result>(
            program->maximum_memory_bytes - sizeof(laplace_cognition_interpretation_result),
            program->slot_count, factor_count);
        auto& values = output->values;
        auto& paths = output->rows;
        output->slot_ids.assign(program->slot_ids, program->slot_ids + program->slot_count);
        output->observation_root = program->observation_root;
        output->occurrence_id = program->occurrence_id;
        output->world_id = program->world_id;
        output->time_fingerprint = program->time_fingerprint;
        output->context_id = program->context_id;
        output->evidence_epoch = program->evidence_epoch;
        output->boundary_id = program->boundary_id;
        output->authority_id = program->authority_id;
        for (size_t f = 0; f < factor_count; ++f) {
            output->witness_offsets.push_back(output->witnesses.size());
            for (size_t r = 0; r < factors[f].row_count; ++r) {
                const auto& row = factors[f].rows[r];
                output->witnesses.push_back({factors[f].factor_id, factors[f].law_id,
                    row.observation_id, row.evidence_root_id, row.calculation_receipt_id});
            }
        }
        output->witness_offsets.push_back(output->witnesses.size());
        std::pmr::vector<uint8_t> bound(program->slot_count, 0, &output->budget);
        std::pmr::vector<uint8_t> completed(factor_count, 0, &output->budget);
        std::pmr::vector<laplace_id128> next_values(&output->budget);
        std::pmr::vector<uint64_t> next_paths(&output->budget);
        std::pmr::vector<size_t> row_index(&output->budget);
        std::pmr::vector<size_t> shared_slots(&output->budget);
        auto compare = [&](const laplace_id128& left, const laplace_id128& right) {
            if (receipt->comparisons == program->maximum_comparisons) throw Exhausted{};
            ++receipt->comparisons;
            return std::memcmp(left.bytes, right.bytes, 16);
        };
        values.resize(program->slot_count);
        paths.resize(factor_count, UINT64_MAX);
        receipt->peak_frontier_states = 1;
        for (size_t round = 0; round < factor_count; ++round) {
            size_t f = factor_count, best_shared = 0;
            // Choose a physical join order, never a winning interpretation:
            // connect already-bound slots first, then the smaller relation.
            // Every factor and every compatible row still participates.
            for (size_t candidate = 0; candidate < factor_count; ++candidate) {
                if (completed[candidate]) continue;
                size_t shared = 0;
                for (size_t s = 0; s < factors[candidate].slot_count; ++s)
                    shared += bound[factors[candidate].slots[s]] != 0 ? 1U : 0U;
                if (f == factor_count || shared > best_shared ||
                    (shared == best_shared && factors[candidate].row_count < factors[f].row_count)) {
                    f = candidate;
                    best_shared = shared;
                }
            }
            const auto& factor = factors[f];
            next_values.clear();
            next_paths.clear();
            const size_t states = values.size() / program->slot_count;
            shared_slots.clear();
            for (size_t s = 0; s < factor.slot_count; ++s)
                if (bound[factor.slots[s]]) shared_slots.push_back(s);
            row_index.resize(states == 0 ? 0 : factor.row_count);
            for (size_t r = 0; r < row_index.size(); ++r) row_index[r] = r;
            // Index only the slots already bound by the preceding factors.
            // A native join probes this index; it does not scan the relation
            // once for every hypothesis. Equal keys preserve every proof row.
            if (!shared_slots.empty())
                std::sort(row_index.begin(), row_index.end(), [&](size_t left, size_t right) {
                    for (const size_t s : shared_slots) {
                        const int order = compare(factor.rows[left].values[s], factor.rows[right].values[s]);
                        if (order != 0) return order < 0;
                    }
                    return left < right;
                });
            for (size_t state = 0; state < states; ++state) {
                auto row_order = [&](size_t position) {
                    for (const size_t s : shared_slots) {
                        const int order = compare(factor.rows[row_index[position]].values[s],
                            values[state * program->slot_count + factor.slots[s]]);
                        if (order != 0) return order;
                    }
                    return 0;
                };
                size_t first = 0, last = row_index.size();
                if (!shared_slots.empty()) {
                    size_t high = last;
                    while (first < high) {
                        const size_t middle = first + (high - first) / 2;
                        if (row_order(middle) < 0) first = middle + 1;
                        else high = middle;
                    }
                    size_t low = first;
                    while (low < last) {
                        const size_t middle = low + (last - low) / 2;
                        if (row_order(middle) <= 0) low = middle + 1;
                        else last = middle;
                    }
                }
                const uint64_t rejected = factor.row_count - (last - first);
                if (rejected > UINT64_MAX - receipt->rejected_pairs)
                    throw std::length_error("interpretation rejection count overflow");
                receipt->rejected_pairs += rejected;
                for (size_t position = first; position < last; ++position) {
                    const size_t r = row_index[position];
                    if (receipt->generated_states == program->maximum_states)
                        throw Exhausted{};
                    ++receipt->generated_states;
                    const size_t base = next_values.size();
                    next_values.insert(next_values.end(),
                        values.begin() + static_cast<ptrdiff_t>(state * program->slot_count),
                        values.begin() + static_cast<ptrdiff_t>((state + 1) * program->slot_count));
                    next_paths.insert(next_paths.end(),
                        paths.begin() + static_cast<ptrdiff_t>(state * factor_count),
                        paths.begin() + static_cast<ptrdiff_t>((state + 1) * factor_count));
                    for (size_t s = 0; s < factor.slot_count; ++s)
                        next_values[base + factor.slots[s]] = factor.rows[r].values[s];
                    next_paths[next_paths.size() - factor_count + f] = r;
                    receipt->peak_frontier_states = std::max(receipt->peak_frontier_states,
                        static_cast<uint64_t>(next_values.size() / program->slot_count));
                }
            }
            values.swap(next_values);
            paths.swap(next_paths);
            for (size_t s = 0; s < factor.slot_count; ++s) bound[factor.slots[s]] = 1;
            completed[f] = 1;
            ++receipt->completed_factor_count;
        }
        if (std::find(bound.begin(), bound.end(), 0) != bound.end())
            return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
        receipt->derivation_count = values.size() / program->slot_count;
        receipt->disposition = values.empty() ? LAPLACE_COGNITION_INTERPRETATION_NO_COMPATIBLE_BINDING
                                             : LAPLACE_COGNITION_INTERPRETATION_UNIQUE;
        Hash hash("laplace-cognition-interpretation-result-v1");
        hash.Bytes(receipt->input_fingerprint.bytes, 32);
        hash.Number(receipt->derivation_count);
        for (size_t i = 0; i < values.size(); ++i) {
            hash.Bytes(values[i].bytes, 16);
            if (std::memcmp(values[i].bytes, values[i % program->slot_count].bytes, 16) != 0)
                receipt->disposition = LAPLACE_COGNITION_INTERPRETATION_AMBIGUOUS;
        }
        for (const auto row : paths) hash.Number(row);
        receipt->result_fingerprint = hash.Finish();
        Seal(*receipt);
        output->receipt = *receipt;
        *result = output.release();
        return LAPLACE_COGNITION_INTERPRETATION_OK;
    } catch (const Exhausted&) {
        Seal(*receipt);
        return LAPLACE_COGNITION_INTERPRETATION_EXHAUSTED;
    } catch (const std::bad_alloc&) {
        Seal(*receipt);
        return LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        Seal(*receipt);
        return LAPLACE_COGNITION_INTERPRETATION_RANGE;
    }
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_prepare_turn(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* goal_slot_id,
    const laplace_cognition_turn_input* turn,
    const laplace_cognition_turn_policy* policy,
    laplace_cognition_turn_input* inferred_turn,
    laplace_cognition_turn_policy* inferred_policy) {
    using namespace laplace_interpretation_detail;
    const auto source_turn = turn != nullptr ? *turn : laplace_cognition_turn_input{};
    const auto source_policy = policy != nullptr ? *policy : laplace_cognition_turn_policy{};
    const bool inputs_present = turn != nullptr && policy != nullptr;
    turn = &source_turn;
    policy = &source_policy;
    if (inferred_turn != nullptr) *inferred_turn = {};
    if (inferred_policy != nullptr) *inferred_policy = {};
    if (interpretation == nullptr || goal_slot_id == nullptr || !inputs_present ||
        inferred_turn == nullptr || inferred_policy == nullptr)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    if (interpretation->receipt.disposition != LAPLACE_COGNITION_INTERPRETATION_UNIQUE)
        return LAPLACE_COGNITION_INTERPRETATION_NOT_UNIQUE;
    const laplace_id128 zero{};
    if ((policy->request_flags & LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT) != 0 ||
        std::memcmp(policy->goal_entity_id.bytes, zero.bytes, 16) != 0)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    if (!SameScope(*interpretation, *turn, policy->evidence_epoch,
            policy->evidence_boundary, policy->authority_id))
        return LAPLACE_COGNITION_INTERPRETATION_SCOPE_MISMATCH;
    const auto found = std::lower_bound(interpretation->slot_ids.begin(), interpretation->slot_ids.end(),
        *goal_slot_id, [](const laplace_digest256& left, const laplace_digest256& right) {
            return std::memcmp(left.bytes, right.bytes, 32) < 0;
        });
    if (found == interpretation->slot_ids.end() || std::memcmp(found->bytes, goal_slot_id->bytes, 32) != 0)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    const size_t slot = static_cast<size_t>(found - interpretation->slot_ids.begin());
    *inferred_policy = *policy;
    inferred_policy->goal_entity_id = interpretation->values[slot];
    inferred_policy->request_flags |= LAPLACE_COGNITION_OBSERVATION_REQUEST_GOAL_PRESENT;
    inferred_policy->request_flags &= ~LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    *inferred_turn = *turn;
    Hash context("laplace-cognition-interpreted-turn-context-v1");
    context.Bytes(turn->context_fingerprint.bytes, 32);
    context.Bytes(interpretation->receipt.receipt_id.bytes, 32);
    context.Bytes(goal_slot_id->bytes, 32);
    inferred_turn->context_fingerprint = context.Finish();
    return LAPLACE_COGNITION_INTERPRETATION_OK;
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_compile_turn(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* goal_slot_id,
    const laplace_cognition_turn_input* turn,
    const laplace_cognition_turn_policy* policy,
    const uint8_t* previous_frame, size_t previous_frame_bytes,
    laplace_cognition_observation_request* request,
    laplace_cognition_turn_receipt* receipt) {
    if (request != nullptr) *request = {};
    if (receipt != nullptr) *receipt = {};
    if (request == nullptr || receipt == nullptr)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    laplace_cognition_turn_input inferred_turn{};
    laplace_cognition_turn_policy inferred_policy{};
    const auto status = laplace_cognition_interpretation_prepare_turn(
        interpretation, goal_slot_id, turn, policy, &inferred_turn, &inferred_policy);
    if (status != LAPLACE_COGNITION_INTERPRETATION_OK) return status;
    if (laplace_cognition_turn_compile(&inferred_turn, &inferred_policy,
            previous_frame, previous_frame_bytes, request, receipt) != LAPLACE_COGNITION_TURN_OK)
        return LAPLACE_COGNITION_INTERPRETATION_TURN_FAILURE;
    return LAPLACE_COGNITION_INTERPRETATION_OK;
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_read(
    const laplace_cognition_interpretation_result* result, size_t derivation,
    laplace_id128* values, size_t value_capacity,
    uint64_t* selected_rows, size_t row_capacity) {
    if (result == nullptr || values == nullptr || selected_rows == nullptr ||
        value_capacity < result->slots || row_capacity < result->factors ||
        derivation >= result->values.size() / result->slots)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    std::copy_n(result->values.data() + derivation * result->slots, result->slots, values);
    std::copy_n(result->rows.data() + derivation * result->factors, result->factors, selected_rows);
    return LAPLACE_COGNITION_INTERPRETATION_OK;
}

extern "C" void laplace_cognition_interpretation_destroy(
    laplace_cognition_interpretation_result** result) {
    if (result != nullptr) { delete *result; *result = nullptr; }
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_read_witness(
    const laplace_cognition_interpretation_result* result,
    size_t factor_index, size_t row_index,
    laplace_cognition_interpretation_witness* witness) {
    if (witness != nullptr) *witness = {};
    if (result == nullptr || witness == nullptr || factor_index >= result->factors)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    const size_t first = result->witness_offsets[factor_index];
    const size_t last = result->witness_offsets[factor_index + 1];
    if (row_index >= last - first) return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    *witness = result->witnesses[first + row_index];
    return LAPLACE_COGNITION_INTERPRETATION_OK;
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_read_binding(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_digest256* slot_id, laplace_id128* value) {
    if (value != nullptr) *value = {};
    if (interpretation == nullptr || slot_id == nullptr || value == nullptr)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    if (interpretation->receipt.disposition != LAPLACE_COGNITION_INTERPRETATION_UNIQUE)
        return LAPLACE_COGNITION_INTERPRETATION_NOT_UNIQUE;
    const auto found = std::lower_bound(interpretation->slot_ids.begin(), interpretation->slot_ids.end(),
        *slot_id, [](const laplace_digest256& left, const laplace_digest256& right) {
            return std::memcmp(left.bytes, right.bytes, 32) < 0;
        });
    if (found == interpretation->slot_ids.end() || std::memcmp(found->bytes, slot_id->bytes, 32) != 0)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    *value = interpretation->values[static_cast<size_t>(found - interpretation->slot_ids.begin())];
    return LAPLACE_COGNITION_INTERPRETATION_OK;
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_compile_guidance(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_cognition_interpretation_program_rule* rule,
    laplace_cognition_guidance_state** guidance) {
    using namespace laplace_interpretation_detail;
    if (guidance != nullptr) *guidance = nullptr;
    if (interpretation == nullptr || rule == nullptr || guidance == nullptr ||
        rule->version != LAPLACE_COGNITION_INTERPRETATION_VERSION || rule->reserved != 0 ||
        Zero(rule->recipe_id) || Zero(rule->witness_id) || Zero(rule->result_contract_fingerprint) ||
        rule->obligations == nullptr || rule->obligation_count == 0 ||
        rule->obligation_count > SIZE_MAX / sizeof(*rule->obligations))
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    if (interpretation->receipt.disposition != LAPLACE_COGNITION_INTERPRETATION_UNIQUE)
        return LAPLACE_COGNITION_INTERPRETATION_NOT_UNIQUE;
    laplace_id128 program_entity{}, goal_entity{};
    if (laplace_cognition_interpretation_read_binding(interpretation, &rule->program_slot_id,
            &program_entity) != LAPLACE_COGNITION_INTERPRETATION_OK ||
        laplace_cognition_interpretation_read_binding(interpretation, &rule->goal_slot_id,
            &goal_entity) != LAPLACE_COGNITION_INTERPRETATION_OK ||
        std::memcmp(program_entity.bytes, rule->program_entity_id.bytes, 16) != 0)
        return LAPLACE_COGNITION_INTERPRETATION_SCOPE_MISMATCH;
    try {
        std::vector<laplace_cognition_obligation> obligations;
        if (rule->obligation_count > obligations.max_size())
            return LAPLACE_COGNITION_INTERPRETATION_RANGE;
        obligations.resize(rule->obligation_count);
        Hash program("laplace-cognition-interpreted-program-v1");
        program.Bytes(program_entity.bytes, 16);
        program.Bytes(rule->program_slot_id.bytes, 32);
        program.Bytes(rule->goal_slot_id.bytes, 32);
        program.Bytes(rule->recipe_id.bytes, 32);
        program.Bytes(rule->witness_id.bytes, 32);
        program.Bytes(rule->result_contract_fingerprint.bytes, 32);
        program.Bytes(interpretation->receipt.receipt_id.bytes, 32);
        program.Number(rule->obligation_count);
        laplace_cognition_guidance_header header{};
        header.bindings_fingerprint = interpretation->receipt.result_fingerprint;
        header.scope_fingerprint = interpretation->boundary_id;
        header.world_id = interpretation->world_id;
        header.time_fingerprint = interpretation->time_fingerprint;
        header.context_fingerprint = interpretation->context_id;
        header.evidence_epoch = interpretation->evidence_epoch;
        header.authority_id = interpretation->authority_id;
        header.result_contract_fingerprint = rule->result_contract_fingerprint;
        header.version = LAPLACE_COGNITION_GUIDANCE_VERSION;
        Hash goal("laplace-cognition-interpreted-goal-v1");
        goal.Bytes(goal_entity.bytes, 16);
        goal.Bytes(header.bindings_fingerprint.bytes, 32);
        header.goal_id = goal.Finish();
        for (size_t i = 0; i < rule->obligation_count; ++i) {
            const auto& declared = rule->obligations[i];
            if (Zero(declared.law_id) || Zero(declared.result_contract_fingerprint) ||
                declared.kind == 0 ||
                (declared.flags & ~LAPLACE_COGNITION_OBLIGATION_KNOWN_FLAGS) != 0 ||
                declared.operand_slots == nullptr || declared.operand_count == 0 ||
                declared.operand_count > SIZE_MAX / sizeof(*declared.operand_slots))
                return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
            Hash binding("laplace-cognition-interpreted-obligation-binding-v1");
            binding.Bytes(header.bindings_fingerprint.bytes, 32);
            binding.Bytes(declared.law_id.bytes, 32);
            binding.Bytes(declared.result_contract_fingerprint.bytes, 32);
            binding.Number(declared.kind);
            binding.Number(declared.flags);
            binding.Number(declared.operand_count);
            for (size_t operand = 0; operand < declared.operand_count; ++operand) {
                laplace_id128 value{};
                if (laplace_cognition_interpretation_read_binding(interpretation,
                        declared.operand_slots + operand, &value) != LAPLACE_COGNITION_INTERPRETATION_OK)
                    return LAPLACE_COGNITION_INTERPRETATION_SCOPE_MISMATCH;
                binding.Bytes(declared.operand_slots[operand].bytes, 32);
                binding.Bytes(value.bytes, 16);
            }
            auto& obligation = obligations[i];
            obligation.binding_fingerprint = binding.Finish();
            Hash identity("laplace-cognition-interpreted-obligation-v1");
            identity.Bytes(rule->recipe_id.bytes, 32);
            identity.Bytes(rule->witness_id.bytes, 32);
            identity.Bytes(obligation.binding_fingerprint.bytes, 32);
            identity.Number(i);
            obligation.obligation_id = identity.Finish();
            obligation.world_id = header.world_id;
            obligation.time_fingerprint = header.time_fingerprint;
            obligation.context_fingerprint = header.context_fingerprint;
            obligation.evidence_boundary = header.scope_fingerprint;
            obligation.authority_id = header.authority_id;
            obligation.result_contract_fingerprint = header.result_contract_fingerprint;
            obligation.kind = declared.kind;
            obligation.disposition = LAPLACE_COGNITION_OBLIGATION_OPEN;
            obligation.flags = declared.flags;
            program.Bytes(obligation.obligation_id.bytes, 32);
        }
        header.program_id = program.Finish();
        const auto status = laplace_cognition_guidance_state_create(
            &header, obligations.data(), obligations.size(), guidance);
        if (status == LAPLACE_COGNITION_GUIDANCE_MEMORY_FAILURE)
            return LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE;
        return status == LAPLACE_COGNITION_GUIDANCE_OK ? LAPLACE_COGNITION_INTERPRETATION_OK
            : LAPLACE_COGNITION_INTERPRETATION_GUIDANCE_FAILURE;
    } catch (const std::bad_alloc&) {
        return LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE;
    } catch (const std::length_error&) {
        return LAPLACE_COGNITION_INTERPRETATION_RANGE;
    }
}

extern "C" laplace_cognition_interpretation_status
laplace_cognition_interpretation_forward_execute(
    const laplace_cognition_interpretation_result* interpretation,
    const laplace_cognition_interpretation_program_rule* rule,
    const laplace_cognition_forward_program* limits,
    const laplace_cognition_forward_provider_v1* provider,
    laplace_cognition_forward_result** result,
    laplace_cognition_forward_receipt* receipt) {
    using namespace laplace_interpretation_detail;
    if (result != nullptr) *result = nullptr;
    if (receipt != nullptr) *receipt = {};
    if (interpretation == nullptr || rule == nullptr ||
        limits == nullptr || provider == nullptr || result == nullptr || receipt == nullptr)
        return LAPLACE_COGNITION_INTERPRETATION_INVALID_ARGUMENT;
    GuidanceOwner guidance;
    const auto compiled = laplace_cognition_interpretation_compile_guidance(
        interpretation, rule, &guidance.state);
    if (compiled != LAPLACE_COGNITION_INTERPRETATION_OK) return compiled;
    laplace_cognition_guidance_header header{};
    if (laplace_cognition_guidance_state_header(guidance.state, &header) !=
        LAPLACE_COGNITION_GUIDANCE_OK)
        return LAPLACE_COGNITION_INTERPRETATION_GUIDANCE_FAILURE;
    auto program = *limits;
    program.program_id = header.program_id;
    program.result_contract_fingerprint = header.result_contract_fingerprint;
    const auto executed = laplace_cognition_forward_pass_execute(
        &program, guidance.state, provider, result, receipt);
    if (executed == LAPLACE_COGNITION_FORWARD_MEMORY_FAILURE)
        return LAPLACE_COGNITION_INTERPRETATION_MEMORY_FAILURE;
    return executed == LAPLACE_COGNITION_FORWARD_OK ? LAPLACE_COGNITION_INTERPRETATION_OK
        : LAPLACE_COGNITION_INTERPRETATION_FORWARD_FAILURE;
}
