#include "laplace/target_observation_compile.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>

namespace {

bool TargetObservationZero(const laplace_digest256& value) {
    std::uint8_t aggregate = 0U;
    for (const auto byte : value.bytes) {
        aggregate = static_cast<std::uint8_t>(aggregate | byte);
    }
    return aggregate == 0U;
}

bool TargetObservationProgramValid(
    const laplace_cognition_operator_program& program) {
    if (TargetObservationZero(program.program_id) ||
        TargetObservationZero(program.boundary_id) ||
        TargetObservationZero(program.context_fingerprint) ||
        TargetObservationZero(program.evidence_epoch) ||
        TargetObservationZero(program.result_contract_fingerprint) ||
        program.eligible_relation_families == nullptr ||
        program.eligible_relation_family_count == 0U ||
        program.eligible_source_mask == 0U ||
        (program.eligible_source_mask & ~UINT32_C(7)) != 0U ||
        (program.flags & ~LAPLACE_COGNITION_OPERATOR_PROGRAM_KNOWN_MASK) != 0U ||
        !std::isfinite(program.numeric_tolerance) ||
        program.numeric_tolerance <= 0.0 ||
        program.version != LAPLACE_COGNITION_OPERATOR_VERSION ||
        program.reserved != 0U) {
        return false;
    }
    std::set<std::uint32_t> families;
    for (std::size_t index = 0U;
         index < program.eligible_relation_family_count; ++index) {
        const std::uint32_t family = program.eligible_relation_families[index];
        if (family == 0U || !families.insert(family).second) return false;
    }
    return true;
}

}  // namespace

extern "C" laplace_target_scope_plan_status laplace_target_observation_compile(
    const laplace_target_observation_compile_request* const request,
    laplace_target_compile_result** const result,
    laplace_target_compile_receipt* const compile_receipt,
    laplace_target_scope_plan_receipt* const scope_receipt) {
    if (result != nullptr) *result = nullptr;
    if (compile_receipt != nullptr) {
        *compile_receipt = laplace_target_compile_receipt{};
        compile_receipt->version = LAPLACE_TARGET_COMPILE_VERSION;
    }
    if (scope_receipt != nullptr) {
        *scope_receipt = laplace_target_scope_plan_receipt{};
        scope_receipt->version = LAPLACE_TARGET_SCOPE_PLAN_VERSION;
    }
    if (request == nullptr || result == nullptr || compile_receipt == nullptr ||
        scope_receipt == nullptr) {
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_ARGUMENT;
    }
    if (request->observation_result == nullptr ||
        TargetObservationZero(request->recipe_fingerprint) ||
        TargetObservationZero(request->target_contract_fingerprint) ||
        request->slots == nullptr || request->slot_count == 0U ||
        (request->target_compile_flags & ~LAPLACE_TARGET_COMPILE_KNOWN_FLAGS) != 0U ||
        request->version != LAPLACE_TARGET_OBSERVATION_COMPILE_VERSION ||
        request->reserved != 0U) {
        scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
    }

    laplace_cognition_observation_operator_view source{};
    const auto source_status = laplace_cognition_observation_result_operator_view(
        request->observation_result, request->answer_index, &source);
    if (source_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK ||
        source.fields == nullptr || source.field_count == 0U ||
        source.constraints == nullptr || source.constraint_count == 0U ||
        !TargetObservationProgramValid(source.program)) {
        scope_receipt->status = LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
        return LAPLACE_TARGET_SCOPE_PLAN_INVALID_REQUEST;
    }

    laplace_target_scope_plan_request scope_request{};
    scope_request.evidence_boundary = source.program.boundary_id;
    scope_request.evidence_epoch = source.program.evidence_epoch;
    scope_request.recipe_fingerprint = request->recipe_fingerprint;
    scope_request.target_contract_fingerprint = request->target_contract_fingerprint;
    scope_request.context_fingerprint = source.program.context_fingerprint;
    scope_request.fields = source.fields;
    scope_request.field_count = source.field_count;
    scope_request.constraints = source.constraints;
    scope_request.constraint_count = source.constraint_count;
    scope_request.slots = request->slots;
    scope_request.slot_count = request->slot_count;
    scope_request.numeric_tolerance = source.program.numeric_tolerance;
    scope_request.operator_program_flags = source.program.flags;
    scope_request.target_compile_flags = request->target_compile_flags;
    scope_request.version = LAPLACE_TARGET_SCOPE_PLAN_VERSION;
    return laplace_target_scope_plan_compile(
        &scope_request, result, compile_receipt, scope_receipt);
}
