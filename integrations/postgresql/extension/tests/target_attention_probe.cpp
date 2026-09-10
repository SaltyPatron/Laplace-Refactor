#include "laplace/framework.h"
#include "laplace/target_attention_projection.h"
#include "laplace/target_compile.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

laplace_digest256 Digest(const std::uint8_t byte) {
    laplace_digest256 value{};
    std::memset(value.bytes, byte, sizeof(value.bytes));
    return value;
}

laplace_id128 Id(const std::uint8_t byte) {
    laplace_id128 value{};
    std::memset(value.bytes, byte, sizeof(value.bytes));
    return value;
}

void PrintDigest(const char* name, const laplace_digest256& value) {
    std::printf("%s=", name);
    for (const auto byte : value.bytes) std::printf("%02x", byte);
    std::printf("\n");
}

laplace_framework_context Context() {
    laplace_framework_context context{};
    for (std::size_t index = 0U; index < LAPLACE_FRAMEWORK_EPOCH_COUNT; ++index) {
        context.epochs[index] = Digest(static_cast<std::uint8_t>(index + 1U));
    }
    context.authority_fingerprint = Digest(0xa0U);
    context.resource_grant.memory_bytes = UINT64_C(1048576);
    context.resource_grant.cpu_slots = 4U;
    context.resource_grant.io_slots = 1U;
    context.epoch_mask = UINT64_C(1023);
    context.major = LAPLACE_FRAMEWORK_MAJOR;
    context.minor = LAPLACE_FRAMEWORK_MINOR;
    context.flags = LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY;
    return context;
}

laplace_cognition_operator_field Field(
    const std::uint8_t seed,
    const std::uint64_t ordinal) {
    laplace_cognition_operator_field field{};
    field.field_id = Digest(seed);
    field.entity_id = Id(static_cast<std::uint8_t>(seed + 40U));
    field.physicality_id = Digest(0U);
    field.role_id = Digest(0U);
    field.recipe_fingerprint = Digest(static_cast<std::uint8_t>(seed + 80U));
    field.ordinal = ordinal;
    field.value_dimension = 1U;
    return field;
}

laplace_cognition_operator_constraint Constraint(
    const std::uint8_t seed,
    const std::uint32_t family,
    const std::uint64_t source,
    const std::uint64_t target,
    const double precision) {
    laplace_cognition_operator_constraint constraint{};
    constraint.constraint_id = Digest(seed);
    constraint.plane_id = Digest(static_cast<std::uint8_t>(seed + 20U));
    constraint.law_fingerprint = Digest(static_cast<std::uint8_t>(seed + 40U));
    constraint.units_fingerprint = Digest(static_cast<std::uint8_t>(seed + 60U));
    constraint.evidence_root_id = Digest(static_cast<std::uint8_t>(seed + 70U));
    constraint.calculation_receipt_id = Digest(static_cast<std::uint8_t>(seed + 80U));
    constraint.source_field_index = source;
    constraint.target_field_index = target;
    constraint.transport_scale = 1.0;
    constraint.precision = precision;
    constraint.relation_family = family;
    constraint.source_class = LAPLACE_COGNITION_OPERATOR_SOURCE_PHYSICALITY;
    constraint.direction = LAPLACE_COGNITION_OPERATOR_DIRECTION_SOURCE_TO_TARGET;
    constraint.transport_kind = LAPLACE_COGNITION_OPERATOR_TRANSPORT_IDENTITY;
    return constraint;
}

struct Program {
    std::array<std::uint32_t, 2> families{{11U, 12U}};
    laplace_cognition_operator_program value{};

    Program(
        const std::uint8_t seed,
        const laplace_digest256& boundary,
        const laplace_digest256& epoch,
        const laplace_digest256& context_fingerprint) {
        value.program_id = Digest(seed);
        value.boundary_id = boundary;
        value.context_fingerprint = context_fingerprint;
        value.evidence_epoch = epoch;
        value.result_contract_fingerprint = Digest(static_cast<std::uint8_t>(seed + 2U));
        value.eligible_relation_families = families.data();
        value.eligible_relation_family_count = families.size();
        value.eligible_source_mask = 7U;
        value.flags =
            LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_POSITIVE_SEMIDEFINITE_PRECISION |
            LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_RELATION_PLANE_SEPARATION |
            LAPLACE_COGNITION_OPERATOR_PROGRAM_REQUIRE_MATRIX_FREE_MATERIALIZED_PARITY;
        value.numeric_tolerance = 1e-12;
        value.version = LAPLACE_COGNITION_OPERATOR_VERSION;
    }
};

}  // namespace

int main() {
    const auto context = Context();
    laplace_digest256 context_fingerprint{};
    if (laplace_framework_context_fingerprint(&context, &context_fingerprint) !=
        LAPLACE_FRAMEWORK_OK) {
        return 2;
    }

    const auto boundary = Digest(0xb0U);
    const auto epoch = Digest(0xb1U);
    Program qk_program(20U, boundary, epoch, context_fingerprint);
    Program vo_program(30U, boundary, epoch, context_fingerprint);
    const std::vector fields{
        Field(40U, 0U), Field(41U, 1U), Field(42U, 2U), Field(43U, 3U)};
    const std::vector qk_constraints{Constraint(50U, 11U, 0U, 1U, 2.0)};
    const std::vector vo_constraints{Constraint(60U, 12U, 2U, 3U, 3.0)};

    std::array<laplace_target_compile_job, 2> jobs{};
    jobs[0].target_role = LAPLACE_TARGET_ROLE_COMPATIBILITY_QK;
    jobs[0].layer_index = 2U;
    jobs[0].head_index = 5U;
    jobs[0].role_fingerprint = Digest(70U);
    jobs[0].operator_program = qk_program.value;
    jobs[0].fields = fields.data();
    jobs[0].field_count = fields.size();
    jobs[0].constraints = qk_constraints.data();
    jobs[0].constraint_count = qk_constraints.size();

    jobs[1].target_role = LAPLACE_TARGET_ROLE_CONTRIBUTION_VO;
    jobs[1].layer_index = 2U;
    jobs[1].head_index = 5U;
    jobs[1].role_fingerprint = Digest(71U);
    jobs[1].operator_program = vo_program.value;
    jobs[1].fields = fields.data();
    jobs[1].field_count = fields.size();
    jobs[1].constraints = vo_constraints.data();
    jobs[1].constraint_count = vo_constraints.size();

    laplace_target_compile_request compile_request{};
    compile_request.evidence_boundary = boundary;
    compile_request.evidence_epoch = epoch;
    compile_request.recipe_fingerprint = Digest(80U);
    compile_request.target_contract_fingerprint = Digest(81U);
    compile_request.jobs = jobs.data();
    compile_request.job_count = jobs.size();
    compile_request.flags = LAPLACE_TARGET_COMPILE_REQUIRE_DISTINCT_QK_VO;
    compile_request.version = LAPLACE_TARGET_COMPILE_VERSION;

    laplace_target_compile_result* compiled = nullptr;
    laplace_target_compile_receipt compile_receipt{};
    if (laplace_target_compile_execute(
            &compile_request, &compiled, &compile_receipt) != LAPLACE_TARGET_COMPILE_OK ||
        compiled == nullptr) {
        return 3;
    }

    laplace_target_attention_head_spec head{};
    head.qk_slot_index = 0U;
    head.vo_slot_index = 1U;
    head.head_rank = 1U;
    laplace_target_attention_projection_request projection_request{};
    projection_request.heads = &head;
    projection_request.head_count = 1U;
    projection_request.hidden_width = 2U;
    projection_request.relative_tolerance = 1e-10;
    projection_request.flags = LAPLACE_TARGET_ATTENTION_PROJECTION_REQUIRE_EXACT;
    projection_request.version = LAPLACE_TARGET_ATTENTION_PROJECTION_VERSION;

    laplace_target_attention_projection_result* projection = nullptr;
    laplace_target_attention_projection_receipt projection_receipt{};
    if (laplace_target_attention_project(
            compiled, &projection_request, &projection, &projection_receipt) !=
            LAPLACE_TARGET_ATTENTION_PROJECTION_OK || projection == nullptr) {
        laplace_target_compile_result_destroy(&compiled);
        return 4;
    }
    laplace_target_attention_head_receipt head_receipt{};
    if (laplace_target_attention_projection_head_receipt(
            projection, 0U, &head_receipt) !=
            LAPLACE_TARGET_ATTENTION_PROJECTION_OK) {
        laplace_target_attention_projection_result_destroy(&projection);
        laplace_target_compile_result_destroy(&compiled);
        return 5;
    }

    PrintDigest("CONTEXT_FINGERPRINT", context_fingerprint);
    PrintDigest("COMPILE_RECEIPT_ID", compile_receipt.receipt_id);
    PrintDigest("PROJECTION_ID", projection_receipt.projection_id);
    PrintDigest("EMBEDDING_FINGERPRINT", projection_receipt.embedding_fingerprint);
    PrintDigest("HEAD_RECEIPT_ID", head_receipt.head_projection_id);

    laplace_target_attention_projection_result_destroy(&projection);
    laplace_target_compile_result_destroy(&compiled);
    return 0;
}
