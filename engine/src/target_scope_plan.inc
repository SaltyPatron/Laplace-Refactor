#include "laplace/target_scope_plan.h"

namespace {

constexpr char TargetScopeRoleDomain[] = "laplace-target-scope-role-v1";
constexpr char TargetScopeProgramDomain[] = "laplace-target-scope-program-v1";
constexpr char TargetScopeSlotSetDomain[] = "laplace-target-scope-slot-set-v1";
constexpr char TargetScopeRequestDomain[] = "laplace-target-scope-request-v1";
constexpr char TargetScopeReceiptDomain[] = "laplace-target-scope-receipt-v1";

struct TargetScopeStorage {
    laplace_target_scope_slot_spec slot{};
    std::vector<std::uint32_t> families;
    laplace_digest256 role_fingerprint{};
    laplace_digest256 program_id{};
    std::uint64_t selected_constraint_count{};
};

bool TargetScopeAttentionRole(const std::uint32_t role) {
    return role == LAPLACE_TARGET_ROLE_COMPATIBILITY_QK ||
        role == LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
}

bool TargetScopeSlotLess(
    const TargetScopeStorage* const left,
    const TargetScopeStorage* const right) {
    const auto left_key = std::tie(
        left->slot.target_role, left->slot.layer_index,
        left->slot.head_index, left->slot.expert_index);
    const auto right_key = std::tie(
        right->slot.target_role, right->slot.layer_index,
        right->slot.head_index, right->slot.expert_index);
    return left_key < right_key;
}

bool TargetScopeSameCoordinate(
    const TargetScopeStorage& left,
    const TargetScopeStorage& right) {
    return left.slot.target_role == right.slot.target_role &&
        left.slot.layer_index == right.slot.layer_index &&
        left.slot.head_index == right.slot.head_index &&
        left.slot.expert_index == right.slot.expert_index;
}

void TargetScopeHashSelector(
    blake3_hasher* const hasher,
    const TargetScopeStorage& storage) {
    HashU32(hasher, storage.slot.target_role);
    HashU32(hasher, storage.slot.layer_index);
    HashU32(hasher, storage.slot.head_index);
    HashU32(hasher, storage.slot.expert_index);
    HashU64(hasher, static_cast<std::uint64_t>(storage.families.size()));
    for (const auto family : storage.families) HashU32(hasher, family);
    HashU32(hasher, storage.slot.eligible_source_mask);
}

void TargetScopeRoleIdentify(
    const laplace_target_scope_plan_request& request,
    TargetScopeStorage* const storage) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetScopeRoleDomain, sizeof(TargetScopeRoleDomain) - 1U);
    TargetScopeHashSelector(&hasher, *storage);
    HashDigest(&hasher, request.target_contract_fingerprint);
    Finish(&hasher, &storage->role_fingerprint);
}

void TargetScopeProgramIdentify(
    const laplace_target_scope_plan_request& request,
    TargetScopeStorage* const storage) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetScopeProgramDomain, sizeof(TargetScopeProgramDomain) - 1U);
    HashDigest(&hasher, storage->role_fingerprint);
    HashDigest(&hasher, request.evidence_boundary);
    HashDigest(&hasher, request.context_fingerprint);
    HashDigest(&hasher, request.evidence_epoch);
    HashDigest(&hasher, request.recipe_fingerprint);
    HashDigest(&hasher, request.target_contract_fingerprint);
    HashU32(&hasher, request.operator_program_flags);
    HashF64(&hasher, request.numeric_tolerance);
    TargetScopeHashSelector(&hasher, *storage);
    Finish(&hasher, &storage->program_id);
}

bool TargetScopeRequestValid(const laplace_target_scope_plan_request& request) {
    return !Zero(request.evidence_boundary) && !Zero(request.evidence_epoch) &&
        !Zero(request.recipe_fingerprint) &&
        !Zero(request.target_contract_fingerprint) &&
        !Zero(request.context_fingerprint) && request.fields != nullptr &&
        request.field_count > 0U && request.constraints != nullptr &&
        request.constraint_count > 0U && request.slots != nullptr &&
        request.slot_count > 0U && std::isfinite(request.numeric_tolerance) &&
        request.numeric_tolerance > 0.0 &&
        (request.operator_program_flags &
            ~LAPLACE_COGNITION_OPERATOR_PROGRAM_KNOWN_MASK) == 0U &&
        (request.target_compile_flags & ~LAPLACE_TARGET_COMPILE_KNOWN_FLAGS) == 0U &&
        request.version == LAPLACE_TARGET_SCOPE_PLAN_VERSION &&
        request.reserved == 0U;
}

laplace_target_scope_plan_status TargetScopePrepareSlot(
    const laplace_target_scope_plan_request& request,
    const laplace_target_scope_slot_spec& slot,
    TargetScopeStorage* const storage) {
    if (!TargetRoleValid(slot.target_role) ||
        slot.eligible_relation_families == nullptr ||
        slot.eligible_relation_family_count == 0U ||
        slot.eligible_source_mask == 0U ||
        (slot.eligible_source_mask & ~UINT32_C(7)) != 0U ||
        slot.flags != 0U || slot.reserved != 0U) {
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_SLOT;
    }
    if (TargetScopeAttentionRole(slot.target_role)) {
        if (slot.head_rank == 0U || slot.head_rank > request.field_count) {
            return LAPLACE_TARGET_SCOPE_PLAN_INVALID_SLOT;
        }
    } else if (slot.head_rank != 0U) {
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_SLOT;
    }

    storage->slot = slot;
    try {
        storage->families.assign(
            slot.eligible_relation_families,
            slot.eligible_relation_families + slot.eligible_relation_family_count);
        std::sort(storage->families.begin(), storage->families.end());
    } catch (const std::bad_alloc&) {
        return LAPLACE_TARGET_SCOPE_PLAN_MEMORY_FAILURE;
    }
    if (storage->families.front() == 0U ||
        std::adjacent_find(storage->families.begin(), storage->families.end()) !=
            storage->families.end()) {
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_SLOT;
    }

    TargetScopeRoleIdentify(request, storage);
    TargetScopeProgramIdentify(request, storage);
    if (Zero(storage->role_fingerprint) || Zero(storage->program_id)) {
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_SLOT;
    }
    return LAPLACE_TARGET_SCOPE_PLAN_OK;
}

void TargetScopeIdentifyRequest(
    const laplace_target_scope_plan_request& request,
    const std::vector<const TargetScopeStorage*>& canonical,
    laplace_target_scope_plan_receipt* const receipt) {
    blake3_hasher slots{};
    blake3_hasher_init(&slots);
    blake3_hasher_update(
        &slots, TargetScopeSlotSetDomain, sizeof(TargetScopeSlotSetDomain) - 1U);
    HashU64(&slots, static_cast<std::uint64_t>(canonical.size()));
    for (const auto* const storage : canonical) {
        TargetScopeHashSelector(&slots, *storage);
        HashDigest(&slots, storage->role_fingerprint);
        HashDigest(&slots, storage->program_id);
        HashU64(&slots, storage->slot.head_rank);
    }
    Finish(&slots, &receipt->slot_set_fingerprint);

    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetScopeRequestDomain, sizeof(TargetScopeRequestDomain) - 1U);
    HashDigest(&hasher, request.evidence_boundary);
    HashDigest(&hasher, request.evidence_epoch);
    HashDigest(&hasher, request.recipe_fingerprint);
    HashDigest(&hasher, request.target_contract_fingerprint);
    HashDigest(&hasher, request.context_fingerprint);
    HashDigest(&hasher, receipt->slot_set_fingerprint);
    HashF64(&hasher, request.numeric_tolerance);
    HashU32(&hasher, request.operator_program_flags);
    HashU32(&hasher, request.target_compile_flags);
    HashU32(&hasher, request.version);
    Finish(&hasher, &receipt->request_fingerprint);
}

void TargetScopeIdentifyReceipt(laplace_target_scope_plan_receipt* const receipt) {
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, TargetScopeReceiptDomain, sizeof(TargetScopeReceiptDomain) - 1U);
    HashDigest(&hasher, receipt->request_fingerprint);
    HashDigest(&hasher, receipt->slot_set_fingerprint);
    HashDigest(&hasher, receipt->compile_receipt_id);
    HashDigest(&hasher, receipt->compile_request_fingerprint);
    HashDigest(&hasher, receipt->context_fingerprint);
    HashU64(&hasher, receipt->slot_count);
    HashU64(&hasher, receipt->field_count);
    HashU64(&hasher, receipt->constraint_count);
    HashU64(&hasher, receipt->selected_constraint_count);
    HashU32(&hasher, receipt->source_mask_union);
    HashU32(&hasher, receipt->compile_status);
    HashU32(&hasher, receipt->status);
    HashU32(&hasher, receipt->version);
    Finish(&hasher, &receipt->receipt_id);
}

}  // namespace

extern "C" laplace_target_scope_plan_status laplace_target_scope_plan_compile(
    const laplace_target_scope_plan_request* const request,
    laplace_target_compile_result** const result,
    laplace_target_compile_receipt* const compile_receipt,
    laplace_target_scope_plan_receipt* const scope_receipt) {
    if (request == nullptr || result == nullptr || compile_receipt == nullptr ||
        scope_receipt == nullptr) {
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_ARGUMENT;
    }
    *result = nullptr;
    *compile_receipt = laplace_target_compile_receipt{};
    *scope_receipt = laplace_target_scope_plan_receipt{};
    scope_receipt->version = LAPLACE_TARGET_SCOPE_PLAN_VERSION;
    scope_receipt->context_fingerprint = request->context_fingerprint;
    if (!TargetScopeRequestValid(*request)) {
        scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
    }
    if (request->field_count > std::numeric_limits<std::uint64_t>::max() ||
        request->constraint_count > std::numeric_limits<std::uint64_t>::max() ||
        request->slot_count > std::numeric_limits<std::uint64_t>::max()) {
        scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
    }
    for (std::size_t index = 0U; index < request->field_count; ++index) {
        if (!FieldValid(request->fields[index])) {
            scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
            return LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
        }
    }
    for (std::size_t index = 0U; index < request->constraint_count; ++index) {
        if (!ConstraintValid(request->constraints[index], request->field_count)) {
            scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
            return LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
        }
    }

    std::vector<TargetScopeStorage> storage;
    std::vector<const TargetScopeStorage*> canonical;
    std::vector<laplace_target_compile_job> jobs;
    try {
        storage.reserve(request->slot_count);
        for (std::size_t index = 0U; index < request->slot_count; ++index) {
            storage.emplace_back();
            const auto status = TargetScopePrepareSlot(
                *request, request->slots[index], &storage.back());
            if (status != LAPLACE_TARGET_SCOPE_PLAN_OK) {
                scope_receipt->status = status;
                return status;
            }
        }
        canonical.reserve(storage.size());
        for (const auto& item : storage) canonical.push_back(&item);
        std::sort(canonical.begin(), canonical.end(), TargetScopeSlotLess);
        for (std::size_t index = 1U; index < canonical.size(); ++index) {
            if (TargetScopeSameCoordinate(*canonical[index - 1U], *canonical[index])) {
                scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_INVALID_SLOT;
                return LAPLACE_TARGET_SCOPE_PLAN_INVALID_SLOT;
            }
        }
        TargetScopeIdentifyRequest(*request, canonical, scope_receipt);

        jobs.resize(storage.size());
        for (std::size_t index = 0U; index < storage.size(); ++index) {
            auto& item = storage[index];
            laplace_cognition_operator_program program{};
            program.program_id = item.program_id;
            program.boundary_id = request->evidence_boundary;
            program.context_fingerprint = request->context_fingerprint;
            program.evidence_epoch = request->evidence_epoch;
            program.result_contract_fingerprint = request->target_contract_fingerprint;
            program.eligible_relation_families = item.families.data();
            program.eligible_relation_family_count = item.families.size();
            program.eligible_source_mask = item.slot.eligible_source_mask;
            program.flags = request->operator_program_flags;
            program.numeric_tolerance = request->numeric_tolerance;
            program.version = LAPLACE_COGNITION_OPERATOR_VERSION;

            for (std::size_t constraint_index = 0U;
                 constraint_index < request->constraint_count; ++constraint_index) {
                if (ConstraintEligible(program, request->constraints[constraint_index])) {
                    ++item.selected_constraint_count;
                }
            }
            if (item.selected_constraint_count == 0U) {
                scope_receipt->status =
                    LAPLACE_TARGET_SCOPE_PLAN_NO_MATCHING_CONSTRAINTS;
                return LAPLACE_TARGET_SCOPE_PLAN_NO_MATCHING_CONSTRAINTS;
            }

            auto& job = jobs[index];
            job.target_role = item.slot.target_role;
            job.layer_index = item.slot.layer_index;
            job.head_index = item.slot.head_index;
            job.expert_index = item.slot.expert_index;
            job.role_fingerprint = item.role_fingerprint;
            job.operator_program = program;
            job.fields = request->fields;
            job.field_count = request->field_count;
            job.constraints = request->constraints;
            job.constraint_count = request->constraint_count;
        }
    } catch (const std::bad_alloc&) {
        scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_MEMORY_FAILURE;
        return LAPLACE_TARGET_SCOPE_PLAN_MEMORY_FAILURE;
    }

    laplace_target_compile_request compile_request{};
    compile_request.evidence_boundary = request->evidence_boundary;
    compile_request.evidence_epoch = request->evidence_epoch;
    compile_request.recipe_fingerprint = request->recipe_fingerprint;
    compile_request.target_contract_fingerprint = request->target_contract_fingerprint;
    compile_request.jobs = jobs.data();
    compile_request.job_count = jobs.size();
    compile_request.flags = request->target_compile_flags;
    compile_request.version = LAPLACE_TARGET_COMPILE_VERSION;

    const auto compile_status =
        laplace_target_compile_execute(&compile_request, result, compile_receipt);
    scope_receipt->compile_status = static_cast<std::uint32_t>(compile_status);
    if (compile_status != LAPLACE_TARGET_COMPILE_OK || *result == nullptr) {
        laplace_target_compile_result_destroy(result);
        scope_receipt->status = compile_status == LAPLACE_TARGET_COMPILE_MEMORY_FAILURE
            ? LAPLACE_TARGET_SCOPE_PLAN_MEMORY_FAILURE
            : LAPLACE_TARGET_SCOPE_PLAN_TARGET_COMPILE_FAILURE;
        return static_cast<laplace_target_scope_plan_status>(scope_receipt->status);
    }

    std::uint64_t selected = 0U;
    std::uint32_t source_mask_union = 0U;
    const std::size_t compiled_slots = laplace_target_compile_result_slot_count(*result);
    if (compiled_slots != storage.size()) {
        laplace_target_compile_result_destroy(result);
        *compile_receipt = laplace_target_compile_receipt{};
        scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_TARGET_COMPILE_FAILURE;
        return LAPLACE_TARGET_SCOPE_PLAN_TARGET_COMPILE_FAILURE;
    }
    for (std::size_t index = 0U; index < compiled_slots; ++index) {
        laplace_target_compile_slot_receipt slot{};
        if (laplace_target_compile_result_slot_receipt(*result, index, &slot) !=
                LAPLACE_TARGET_COMPILE_OK ||
            selected > std::numeric_limits<std::uint64_t>::max() -
                slot.selected_constraint_count) {
            laplace_target_compile_result_destroy(result);
            *compile_receipt = laplace_target_compile_receipt{};
            scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_TARGET_COMPILE_FAILURE;
            return LAPLACE_TARGET_SCOPE_PLAN_TARGET_COMPILE_FAILURE;
        }
        selected += slot.selected_constraint_count;
    }
    for (const auto& item : storage) source_mask_union |= item.slot.eligible_source_mask;

    scope_receipt->compile_receipt_id = compile_receipt->receipt_id;
    scope_receipt->compile_request_fingerprint = compile_receipt->request_fingerprint;
    scope_receipt->slot_count = static_cast<std::uint64_t>(storage.size());
    scope_receipt->field_count = static_cast<std::uint64_t>(request->field_count);
    scope_receipt->constraint_count = static_cast<std::uint64_t>(request->constraint_count);
    scope_receipt->selected_constraint_count = selected;
    scope_receipt->source_mask_union = source_mask_union;
    scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_OK;
    TargetScopeIdentifyReceipt(scope_receipt);
    return LAPLACE_TARGET_SCOPE_PLAN_OK;
}
