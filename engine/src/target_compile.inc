#include "laplace/target_compile.h"

namespace {

constexpr char TargetRequestDomain[] = "laplace-target-compile-request-v1";
constexpr char TargetSlotDomain[] = "laplace-target-compile-slot-v1";
constexpr char TargetMatrixDomain[] = "laplace-target-neutral-operator-v1";
constexpr char TargetReceiptDomain[] = "laplace-target-compile-receipt-v1";

bool TargetRoleValid(const std::uint32_t role) {
    return role >= LAPLACE_TARGET_ROLE_COMPATIBILITY_QK &&
        role <= LAPLACE_TARGET_ROLE_OUTPUT;
}

bool TargetSameCoordinate(
    const laplace_target_compile_job& left,
    const laplace_target_compile_job& right) {
    return left.target_role == right.target_role &&
        left.layer_index == right.layer_index &&
        left.head_index == right.head_index &&
        left.expert_index == right.expert_index;
}

bool TargetJobLess(
    const laplace_target_compile_job* const left,
    const laplace_target_compile_job* const right) {
    const auto left_key = std::tie(
        left->target_role, left->layer_index, left->head_index, left->expert_index);
    const auto right_key = std::tie(
        right->target_role, right->layer_index, right->head_index, right->expert_index);
    if (left_key != right_key) return left_key < right_key;
    return DigestLess(left->role_fingerprint, right->role_fingerprint);
}

bool TargetJobValid(
    const laplace_target_compile_request& request,
    const laplace_target_compile_job& job) {
    if (!TargetRoleValid(job.target_role) || Zero(job.role_fingerprint) ||
        job.fields == nullptr || job.field_count == 0U ||
        job.constraints == nullptr || job.constraint_count == 0U ||
        job.flags != 0U || job.reserved != 0U ||
        !ProgramValid(job.operator_program) ||
        !Same(job.operator_program.boundary_id, request.evidence_boundary) ||
        !Same(job.operator_program.evidence_epoch, request.evidence_epoch)) {
        return false;
    }
    return true;
}

void HashTargetSlotCoordinate(
    blake3_hasher* const hasher,
    const laplace_target_compile_slot_receipt& slot) {
    HashU32(hasher, slot.target_role);
    HashU32(hasher, slot.layer_index);
    HashU32(hasher, slot.head_index);
    HashU32(hasher, slot.expert_index);
    HashDigest(hasher, slot.role_fingerprint);
}

void TargetMatrixFingerprint(
    const laplace_target_compile_slot_receipt& slot,
    const std::vector<double>& matrix,
    const std::vector<double>& rhs,
    laplace_digest256* const output) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetMatrixDomain, sizeof(TargetMatrixDomain) - 1U);
    HashTargetSlotCoordinate(&hasher, slot);
    HashDigest(&hasher, slot.operator_id);
    HashDigest(&hasher, slot.operator_receipt_id);
    HashDigest(&hasher, slot.materialization_receipt_id);
    HashU64(&hasher, static_cast<std::uint64_t>(matrix.size()));
    for (const double value : matrix) HashF64(&hasher, value);
    HashU64(&hasher, static_cast<std::uint64_t>(rhs.size()));
    for (const double value : rhs) HashF64(&hasher, value);
    Finish(&hasher, output);
}

void TargetSlotIdentify(laplace_target_compile_slot_receipt* const slot) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, TargetSlotDomain, sizeof(TargetSlotDomain) - 1U);
    HashTargetSlotCoordinate(&hasher, *slot);
    HashDigest(&hasher, slot->operator_id);
    HashDigest(&hasher, slot->operator_receipt_id);
    HashDigest(&hasher, slot->program_fingerprint);
    HashDigest(&hasher, slot->field_set_fingerprint);
    HashDigest(&hasher, slot->constraint_set_fingerprint);
    HashDigest(&hasher, slot->materialization_receipt_id);
    HashDigest(&hasher, slot->matrix_fingerprint);
    HashU64(&hasher, slot->field_count);
    HashU64(&hasher, slot->selected_constraint_count);
    HashU32(&hasher, slot->relation_plane_count);
    Finish(&hasher, &slot->slot_id);
}

}  // namespace

struct laplace_target_compile_result {
    std::vector<laplace_target_compile_slot_receipt> slots;
    std::vector<std::vector<double>> matrices;
    std::vector<std::vector<double>> rhs;
};

namespace {

bool TargetRequestValid(const laplace_target_compile_request& request) {
    return !Zero(request.evidence_boundary) && !Zero(request.evidence_epoch) &&
        !Zero(request.recipe_fingerprint) &&
        !Zero(request.target_contract_fingerprint) && request.jobs != nullptr &&
        request.job_count > 0U &&
        (request.flags & ~LAPLACE_TARGET_COMPILE_KNOWN_FLAGS) == 0U &&
        request.version == LAPLACE_TARGET_COMPILE_VERSION && request.reserved == 0U;
}

laplace_target_compile_status CompileTargetSlot(
    const laplace_target_compile_job& job,
    laplace_target_compile_slot_receipt* const slot,
    std::vector<double>* const matrix,
    std::vector<double>* const rhs) {
    if (job.field_count > std::numeric_limits<std::size_t>::max() / job.field_count) {
        return LAPLACE_TARGET_COMPILE_INVALID_JOB;
    }
    const std::size_t matrix_count = job.field_count * job.field_count;
    try {
        matrix->assign(matrix_count, 0.0);
        rhs->assign(job.field_count, 0.0);
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_COMPILE_MEMORY_FAILURE;
    }

    laplace_cognition_operator* operator_value = nullptr;
    laplace_cognition_operator_receipt operator_receipt{};
    const auto operator_status = laplace_cognition_operator_create(
        &job.operator_program, job.fields, job.field_count, job.constraints,
        job.constraint_count, &operator_value, &operator_receipt);
    if (operator_status != LAPLACE_COGNITION_OPERATOR_OK || operator_value == nullptr) {
        laplace_cognition_operator_destroy(&operator_value);
        return LAPLACE_TARGET_COMPILE_OPERATOR_FAILURE;
    }

    laplace_digest256 materialization_receipt{};
    const auto materialization_status = laplace_cognition_operator_materialize_dense(
        operator_value, matrix->data(), matrix->size(), rhs->data(), rhs->size(),
        &materialization_receipt);
    laplace_cognition_operator_destroy(&operator_value);
    if (materialization_status != LAPLACE_COGNITION_OPERATOR_OK ||
        Zero(materialization_receipt)) {
        return LAPLACE_TARGET_COMPILE_MATERIALIZATION_FAILURE;
    }

    *slot = laplace_target_compile_slot_receipt{};
    slot->role_fingerprint = job.role_fingerprint;
    slot->operator_id = operator_receipt.operator_id;
    slot->operator_receipt_id = operator_receipt.receipt_id;
    slot->program_fingerprint = operator_receipt.program_fingerprint;
    slot->field_set_fingerprint = operator_receipt.field_set_fingerprint;
    slot->constraint_set_fingerprint = operator_receipt.constraint_set_fingerprint;
    slot->materialization_receipt_id = materialization_receipt;
    slot->field_count = operator_receipt.field_count;
    slot->selected_constraint_count = operator_receipt.selected_constraint_count;
    slot->matrix_value_count = static_cast<std::uint64_t>(matrix->size());
    slot->rhs_value_count = static_cast<std::uint64_t>(rhs->size());
    slot->target_role = job.target_role;
    slot->layer_index = job.layer_index;
    slot->head_index = job.head_index;
    slot->expert_index = job.expert_index;
    slot->relation_plane_count = operator_receipt.relation_plane_count;
    slot->status = LAPLACE_TARGET_COMPILE_OK;
    TargetMatrixFingerprint(*slot, *matrix, *rhs, &slot->matrix_fingerprint);
    TargetSlotIdentify(slot);
    return LAPLACE_TARGET_COMPILE_OK;
}

bool TargetFlattenedQkVo(
    const std::vector<laplace_target_compile_slot_receipt>& slots) {
    bool has_qk = false;
    bool has_vo = false;
    for (const auto& left : slots) {
        if (left.target_role == LAPLACE_TARGET_ROLE_COMPATIBILITY_QK) has_qk = true;
        if (left.target_role == LAPLACE_TARGET_ROLE_CONTRIBUTION_VO) has_vo = true;
    }
    if (!has_qk || !has_vo) return true;
    for (const auto& qk : slots) {
        if (qk.target_role != LAPLACE_TARGET_ROLE_COMPATIBILITY_QK) continue;
        for (const auto& vo : slots) {
            if (vo.target_role != LAPLACE_TARGET_ROLE_CONTRIBUTION_VO) continue;
            if (Same(qk.operator_id, vo.operator_id) ||
                Same(qk.matrix_fingerprint, vo.matrix_fingerprint)) {
                return true;
            }
        }
    }
    return false;
}

void IdentifyTargetCompilation(
    const laplace_target_compile_request& request,
    const laplace_target_compile_result& result,
    laplace_target_compile_receipt* const receipt) {
    blake3_hasher request_hasher{};
    blake3_hasher_init(&request_hasher);
    blake3_hasher_update(
        &request_hasher, TargetRequestDomain, sizeof(TargetRequestDomain) - 1U);
    HashDigest(&request_hasher, request.evidence_boundary);
    HashDigest(&request_hasher, request.evidence_epoch);
    HashDigest(&request_hasher, request.recipe_fingerprint);
    HashDigest(&request_hasher, request.target_contract_fingerprint);
    HashU32(&request_hasher, request.flags);
    HashU32(&request_hasher, request.version);
    HashU64(&request_hasher, static_cast<std::uint64_t>(result.slots.size()));
    for (const auto& slot : result.slots) {
        HashTargetSlotCoordinate(&request_hasher, slot);
        HashDigest(&request_hasher, slot.operator_id);
        HashDigest(&request_hasher, slot.program_fingerprint);
        HashDigest(&request_hasher, slot.field_set_fingerprint);
        HashDigest(&request_hasher, slot.constraint_set_fingerprint);
    }
    Finish(&request_hasher, &receipt->request_fingerprint);

    blake3_hasher receipt_hasher{};
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, TargetReceiptDomain, sizeof(TargetReceiptDomain) - 1U);
    HashDigest(&receipt_hasher, receipt->request_fingerprint);
    for (const auto& slot : result.slots) {
        HashDigest(&receipt_hasher, slot.slot_id);
        HashDigest(&receipt_hasher, slot.materialization_receipt_id);
        HashDigest(&receipt_hasher, slot.matrix_fingerprint);
    }
    HashU64(&receipt_hasher, receipt->slot_count);
    HashU64(&receipt_hasher, receipt->distinct_operator_count);
    HashU64(&receipt_hasher, receipt->matrix_value_count);
    HashU64(&receipt_hasher, receipt->rhs_value_count);
    HashU32(&receipt_hasher, receipt->flags);
    HashU32(&receipt_hasher, receipt->version);
    Finish(&receipt_hasher, &receipt->receipt_id);
}

}  // namespace

extern "C" laplace_target_compile_status laplace_target_compile_execute(
    const laplace_target_compile_request* const request,
    laplace_target_compile_result** const result,
    laplace_target_compile_receipt* const receipt) {
    if (request == nullptr || result == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_COMPILE_INVALID_ARGUMENT;
    }
    *result = nullptr;
    *receipt = laplace_target_compile_receipt{};
    if (!TargetRequestValid(*request)) return LAPLACE_TARGET_COMPILE_INVALID_REQUEST;

    std::vector<const laplace_target_compile_job*> jobs;
    try {
        jobs.reserve(request->job_count);
        for (std::size_t index = 0U; index < request->job_count; ++index) {
            if (!TargetJobValid(*request, request->jobs[index])) {
                return LAPLACE_TARGET_COMPILE_INVALID_JOB;
            }
            jobs.push_back(&request->jobs[index]);
        }
        std::sort(jobs.begin(), jobs.end(), TargetJobLess);
        for (std::size_t index = 1U; index < jobs.size(); ++index) {
            if (TargetSameCoordinate(*jobs[index - 1U], *jobs[index])) {
                return LAPLACE_TARGET_COMPILE_INVALID_JOB;
            }
        }
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_COMPILE_MEMORY_FAILURE;
    }

    auto compiled = std::unique_ptr<laplace_target_compile_result>(
        new (std::nothrow) laplace_target_compile_result());
    if (!compiled) return LAPLACE_TARGET_COMPILE_MEMORY_FAILURE;
    try {
        compiled->slots.resize(jobs.size());
        compiled->matrices.resize(jobs.size());
        compiled->rhs.resize(jobs.size());
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_COMPILE_MEMORY_FAILURE;
    }

    for (std::size_t index = 0U; index < jobs.size(); ++index) {
        const auto status = CompileTargetSlot(
            *jobs[index], &compiled->slots[index], &compiled->matrices[index],
            &compiled->rhs[index]);
        if (status != LAPLACE_TARGET_COMPILE_OK) return status;
    }

    if ((request->flags & LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO) != 0U &&
        TargetFlattenedQkVo(compiled->slots)) {
        return LAPLACE_TARGET_COMPILE_FLATTENED_OPERATOR;
    }

    std::set<DigestKey> distinct_operators;
    std::uint64_t matrix_values = 0U;
    std::uint64_t rhs_values = 0U;
    for (const auto& slot : compiled->slots) {
        distinct_operators.insert(Key(slot.operator_id));
        if (matrix_values > std::numeric_limits<std::uint64_t>::max() -
                slot.matrix_value_count ||
            rhs_values > std::numeric_limits<std::uint64_t>::max() -
                slot.rhs_value_count) {
            return LAPLACE_TARGET_COMPILE_INVALID_REQUEST;
        }
        matrix_values += slot.matrix_value_count;
        rhs_values += slot.rhs_value_count;
    }

    receipt->evidence_boundary = request->evidence_boundary;
    receipt->evidence_epoch = request->evidence_epoch;
    receipt->recipe_fingerprint = request->recipe_fingerprint;
    receipt->target_contract_fingerprint = request->target_contract_fingerprint;
    receipt->slot_count = static_cast<std::uint64_t>(compiled->slots.size());
    receipt->distinct_operator_count =
        static_cast<std::uint64_t>(distinct_operators.size());
    receipt->matrix_value_count = matrix_values;
    receipt->rhs_value_count = rhs_values;
    receipt->flags = request->flags;
    receipt->status = LAPLACE_TARGET_COMPILE_OK;
    receipt->version = LAPLACE_TARGET_COMPILE_VERSION;
    IdentifyTargetCompilation(*request, *compiled, receipt);

    *result = compiled.release();
    return LAPLACE_TARGET_COMPILE_OK;
}

extern "C" void laplace_target_compile_result_destroy(
    laplace_target_compile_result** const result) {
    if (result == nullptr) return;
    delete *result;
    *result = nullptr;
}

extern "C" size_t laplace_target_compile_result_slot_count(
    const laplace_target_compile_result* const result) {
    return result == nullptr ? 0U : result->slots.size();
}

extern "C" laplace_target_compile_status laplace_target_compile_result_slot_receipt(
    const laplace_target_compile_result* const result,
    const size_t slot_index,
    laplace_target_compile_slot_receipt* const receipt) {
    if (result == nullptr || receipt == nullptr) {
        return LAPLACE_TARGET_COMPILE_INVALID_ARGUMENT;
    }
    if (slot_index >= result->slots.size()) return LAPLACE_TARGET_COMPILE_INVALID_ARGUMENT;
    *receipt = result->slots[slot_index];
    return LAPLACE_TARGET_COMPILE_OK;
}

extern "C" laplace_target_compile_status laplace_target_compile_result_matrix(
    const laplace_target_compile_result* const result,
    const size_t slot_index,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (result == nullptr || required_count == nullptr ||
        (output == nullptr && output_capacity != 0U) ||
        slot_index >= result->matrices.size()) {
        return LAPLACE_TARGET_COMPILE_INVALID_ARGUMENT;
    }
    const auto& matrix = result->matrices[slot_index];
    *required_count = matrix.size();
    if (output_capacity < matrix.size()) {
        return LAPLACE_TARGET_COMPILE_CAPACITY_INSUFFICIENT;
    }
    if (!matrix.empty() && output == nullptr) {
        return LAPLACE_TARGET_COMPILE_CAPACITY_INSUFFICIENT;
    }
    std::copy(matrix.begin(), matrix.end(), output);
    return LAPLACE_TARGET_COMPILE_OK;
}

extern "C" laplace_target_compile_status laplace_target_compile_result_rhs(
    const laplace_target_compile_result* const result,
    const size_t slot_index,
    double* const output,
    const size_t output_capacity,
    size_t* const required_count) {
    if (result == nullptr || required_count == nullptr ||
        (output == nullptr && output_capacity != 0U) ||
        slot_index >= result->rhs.size()) {
        return LAPLACE_TARGET_COMPILE_INVALID_ARGUMENT;
    }
    const auto& rhs = result->rhs[slot_index];
    *required_count = rhs.size();
    if (output_capacity < rhs.size()) {
        return LAPLACE_TARGET_COMPILE_CAPACITY_INSUFFICIENT;
    }
    if (!rhs.empty() && output == nullptr) {
        return LAPLACE_TARGET_COMPILE_CAPACITY_INSUFFICIENT;
    }
    std::copy(rhs.begin(), rhs.end(), output);
    return LAPLACE_TARGET_COMPILE_OK;
}
