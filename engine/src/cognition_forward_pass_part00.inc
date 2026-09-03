#include "laplace/cognition_forward_pass.h"

#include "blake3.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace {

bool Zero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool Same(const laplace_digest256& left, const laplace_digest256& right) {
    return std::memcmp(left.bytes, right.bytes, sizeof(left.bytes)) == 0;
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

void Finish(blake3_hasher* const hasher, laplace_digest256* const output) {
    blake3_hasher_finalize(hasher, output->bytes, sizeof(output->bytes));
}

bool Add(
    const std::uint64_t left,
    const std::uint64_t right,
    std::uint64_t* const output) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) return false;
    *output = left + right;
    return true;
}

bool ProgramValid(const laplace_cognition_forward_program& program) {
    return !Zero(program.program_id) && !Zero(program.result_contract_fingerprint) &&
        program.max_layers != 0U && program.max_provider_calls != 0U &&
        program.max_projected_queries != 0U &&
        program.max_candidate_operations != 0U && program.max_resolutions != 0U &&
        program.max_resource_cost != 0U && program.max_io_operations != 0U &&
        program.max_database_operations != 0U &&
        program.candidate_operation_capacity != 0U && program.resolution_capacity != 0U &&
        (program.flags & ~LAPLACE_COGNITION_FORWARD_KNOWN_FLAGS) == 0U &&
        program.version == LAPLACE_COGNITION_FORWARD_VERSION &&
        program.reserved == 0U;
}

void HashProgram(
    const laplace_cognition_forward_program& program,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_FORWARD_PROGRAM_DOMAIN,
        sizeof(LAPLACE_COGNITION_FORWARD_PROGRAM_DOMAIN) - 1U);
    HashDigest(&hasher, program.program_id);
    HashDigest(&hasher, program.result_contract_fingerprint);
    HashU64(&hasher, program.max_layers);
    HashU64(&hasher, program.max_provider_calls);
    HashU64(&hasher, program.max_projected_queries);
    HashU64(&hasher, program.max_candidate_operations);
    HashU64(&hasher, program.max_resolutions);
    HashU64(&hasher, program.max_resource_cost);
    HashU64(&hasher, program.max_io_operations);
    HashU64(&hasher, program.max_database_operations);
    HashU32(&hasher, program.candidate_operation_capacity);
    HashU32(&hasher, program.resolution_capacity);
    HashU32(&hasher, program.flags);
    HashU32(&hasher, program.version);
    Finish(&hasher, output);
}

void HashProjection(
    blake3_hasher* const hasher,
    const laplace_cognition_query_projection& projection) {
    HashDigest(hasher, projection.projection_id);
    HashDigest(hasher, projection.obligation_id);
    HashDigest(hasher, projection.binding_fingerprint);
    HashDigest(hasher, projection.world_id);
    HashDigest(hasher, projection.time_fingerprint);
    HashDigest(hasher, projection.context_fingerprint);
    HashDigest(hasher, projection.evidence_boundary);
    HashDigest(hasher, projection.authority_id);
    HashDigest(hasher, projection.result_contract_fingerprint);
    HashU32(hasher, projection.query_kind);
    HashU32(hasher, projection.obligation_flags);
}

void ProjectionFingerprint(
    const laplace_digest256& state_id,
    const std::vector<laplace_cognition_query_projection>& projections,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_FORWARD_ENUMERATION_DOMAIN,
        sizeof(LAPLACE_COGNITION_FORWARD_ENUMERATION_DOMAIN) - 1U);
    HashDigest(&hasher, state_id);
    HashU64(&hasher, static_cast<std::uint64_t>(projections.size()));
    for (const auto& projection : projections) HashProjection(&hasher, projection);
    Finish(&hasher, output);
}

bool OperationLess(
    const laplace_cognition_guidance_operation& left,
    const laplace_cognition_guidance_operation& right) {
    return DigestLess(left.operation_id, right.operation_id);
}

bool CandidateSetCanonicalize(
    std::vector<laplace_cognition_guidance_operation>* const operations) {
    std::sort(operations->begin(), operations->end(), OperationLess);
    for (std::size_t index = 0U; index < operations->size(); ++index) {
        if (Zero((*operations)[index].operation_id)) return false;
        if (index != 0U && Same(
                (*operations)[index - 1U].operation_id,
                (*operations)[index].operation_id)) {
            return false;
        }
    }
    return true;
}

laplace_cognition_forward_grant Grant(
    const laplace_cognition_forward_program& program,
    const laplace_cognition_forward_receipt& receipt) {
    return laplace_cognition_forward_grant{
        program.max_resource_cost - receipt.resource_cost,
        program.max_io_operations - receipt.io_operations,
        program.max_database_operations - receipt.database_operations,
        program.max_candidate_operations - receipt.candidate_operation_count,
        program.max_resolutions - receipt.resolution_count,
        program.max_provider_calls - receipt.provider_call_count,
        program.max_layers - receipt.layer_count};
}

bool ReceiptCostFits(
    const laplace_cognition_forward_grant& grant,
    const std::uint64_t resource_cost,
    const std::uint64_t io_operations,
    const std::uint64_t database_operations) {
    return resource_cost <= grant.remaining_resource_cost &&
        io_operations <= grant.remaining_io_operations &&
        database_operations <= grant.remaining_database_operations;
}

}  // namespace

struct laplace_cognition_forward_result {
    laplace_cognition_guidance_state* final_state{};
    std::vector<laplace_cognition_forward_layer_receipt> layers;

    ~laplace_cognition_forward_result() {
        laplace_cognition_guidance_state_destroy(&final_state);
    }
};

namespace {

void HashLayer(
    const laplace_cognition_forward_layer_receipt& layer,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_FORWARD_LAYER_DOMAIN,
        sizeof(LAPLACE_COGNITION_FORWARD_LAYER_DOMAIN) - 1U);
    HashDigest(&hasher, layer.prior_state_id);
    HashDigest(&hasher, layer.projection_fingerprint);
    HashDigest(&hasher, layer.enumeration_receipt_id);
    HashDigest(&hasher, layer.decision_receipt_id);
    HashDigest(&hasher, layer.selected_operation_id);
    HashDigest(&hasher, layer.execution_receipt_id);
    HashDigest(&hasher, layer.resolution_set_fingerprint);
    HashDigest(&hasher, layer.next_state_id);
    HashU64(&hasher, layer.projected_query_count);
    HashU64(&hasher, layer.candidate_operation_count);
    HashU64(&hasher, layer.eligible_operation_count);
    HashU64(&hasher, layer.resolution_count);
    HashU64(&hasher, layer.remaining_open_count);
    HashU64(&hasher, layer.resource_cost);
    HashU64(&hasher, layer.io_operations);
    HashU64(&hasher, layer.database_operations);
    HashU32(&hasher, layer.selected_kind);
    HashU32(&hasher, layer.completion);
    HashU32(&hasher, layer.status);
    HashU32(&hasher, layer.flags);
    Finish(&hasher, output);
}

}  // namespace
