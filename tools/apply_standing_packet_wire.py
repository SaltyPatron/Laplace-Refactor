#!/usr/bin/env python3
from pathlib import Path


def one(path, old, new):
    p = Path(path)
    text = p.read_text()
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{path}: expected 1 match, got {n}: {old[:100]!r}")
    p.write_text(text.replace(old, new, 1))

compile_path = "engine/src/cognition_packet_compile.cpp"
one(compile_path,
'''constexpr std::size_t ConstraintBytes = 264U;
constexpr std::size_t ResultFixedBytes = 580U;''',
'''constexpr std::size_t ConstraintBytes = 264U;
constexpr std::size_t StandingStateBytes = 240U;
constexpr std::size_t ResultFixedBytes = 580U;''')

old_request = '''bool RequestBytes(
    const laplace_cognition_runtime_request& request,
    std::size_t* const bytes) {
    if (bytes == nullptr || request.fields == nullptr ||
        request.constraints == nullptr || request.initial_state == nullptr ||
        request.operator_program.eligible_relation_families == nullptr ||
        request.operator_program.eligible_relation_family_count == 0U ||
        request.field_count == 0U || request.constraint_count == 0U ||
        request.initial_state_count != request.field_count ||
        !FitsSize(request.field_count) || !FitsSize(request.constraint_count) ||
        !FitsSize(request.initial_state_count)) {
        return false;
    }
    std::size_t total = RequestFixedBytes;
    return AddMul(
               &total,
               request.operator_program.eligible_relation_family_count,
               sizeof(std::uint32_t)) &&
        AddMul(&total, static_cast<std::size_t>(request.field_count), FieldBytes) &&
        AddMul(
            &total,
            static_cast<std::size_t>(request.constraint_count),
            ConstraintBytes) &&
        AddMul(
            &total,
            static_cast<std::size_t>(request.initial_state_count),
            sizeof(double)) &&
        ((*bytes = total), true);
}
'''
new_request = '''bool RequestBytes(
    const laplace_cognition_runtime_request& request,
    std::size_t* const bytes) {
    if (bytes == nullptr || request.fields == nullptr ||
        request.constraints == nullptr || request.initial_state == nullptr ||
        request.operator_program.eligible_relation_families == nullptr ||
        request.operator_program.eligible_relation_family_count == 0U ||
        request.field_count == 0U || request.constraint_count == 0U ||
        request.initial_state_count != request.field_count ||
        !FitsSize(request.field_count) || !FitsSize(request.constraint_count) ||
        !FitsSize(request.initial_state_count)) {
        return false;
    }
    std::size_t total = RequestFixedBytes;
    if (!AddMul(
            &total,
            request.operator_program.eligible_relation_family_count,
            sizeof(std::uint32_t)) ||
        !AddMul(&total, static_cast<std::size_t>(request.field_count), FieldBytes) ||
        !AddMul(
            &total,
            static_cast<std::size_t>(request.constraint_count),
            ConstraintBytes)) {
        return false;
    }
    for (std::size_t index = 0U;
         index < static_cast<std::size_t>(request.constraint_count); ++index) {
        if (request.constraints[index].source_class ==
                LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING &&
            !AddMul(&total, 1U, StandingStateBytes)) {
            return false;
        }
    }
    if (!AddMul(
            &total,
            static_cast<std::size_t>(request.initial_state_count),
            sizeof(double))) {
        return false;
    }
    *bytes = total;
    return true;
}
'''
one(compile_path, old_request, new_request)

one(compile_path,
'''bool WriteConstraint(
    ByteWriter* const writer,
    const laplace_cognition_operator_constraint& constraint) {
    return writer != nullptr && writer->Digest(constraint.constraint_id) &&
        writer->Digest(constraint.plane_id) &&
        writer->Digest(constraint.law_fingerprint) &&
        writer->Digest(constraint.units_fingerprint) &&
        writer->Digest(constraint.evidence_root_id) &&
        writer->Digest(constraint.calculation_receipt_id) &&
        writer->U64(constraint.source_field_index) &&
        writer->U64(constraint.target_field_index) &&
        writer->F64(constraint.transport_scale) &&
        writer->F64(constraint.transport_offset) &&
        writer->F64(constraint.target_value) && writer->F64(constraint.precision) &&
        writer->U32(constraint.relation_family) &&
        writer->U32(constraint.source_class) && writer->U32(constraint.direction) &&
        writer->U32(constraint.transport_kind) && writer->U32(constraint.flags) &&
        writer->U32(constraint.reserved);
}
''',
'''bool WriteStandingState(
    ByteWriter* const writer,
    const laplace_standing_state& state) {
    return writer != nullptr && writer->Digest(state.state_id) &&
        writer->Digest(state.coordinate_id) && writer->Digest(state.arena_scope_id) &&
        writer->Digest(state.prior_state_id) && writer->Digest(state.epoch_id) &&
        writer->Digest(state.rating_recipe_id) && writer->F64(state.rating) &&
        writer->F64(state.rating_deviation) && writer->F64(state.volatility) &&
        writer->U64(state.eligible_match_count) && writer->U64(state.period_ordinal) &&
        writer->U32(state.rating_recipe_version) && writer->U32(state.flags);
}

bool WriteConstraint(
    ByteWriter* const writer,
    const laplace_cognition_operator_constraint& constraint) {
    const bool base = writer != nullptr && writer->Digest(constraint.constraint_id) &&
        writer->Digest(constraint.plane_id) &&
        writer->Digest(constraint.law_fingerprint) &&
        writer->Digest(constraint.units_fingerprint) &&
        writer->Digest(constraint.evidence_root_id) &&
        writer->Digest(constraint.calculation_receipt_id) &&
        writer->U64(constraint.source_field_index) &&
        writer->U64(constraint.target_field_index) &&
        writer->F64(constraint.transport_scale) &&
        writer->F64(constraint.transport_offset) &&
        writer->F64(constraint.target_value) && writer->F64(constraint.precision) &&
        writer->U32(constraint.relation_family) &&
        writer->U32(constraint.source_class) && writer->U32(constraint.direction) &&
        writer->U32(constraint.transport_kind) && writer->U32(constraint.flags) &&
        writer->U32(constraint.reserved);
    if (!base) return false;
    return constraint.source_class != LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING ||
        WriteStandingState(writer, constraint.standing);
}
''')

packet = "engine/src/cognition_packet.cpp"
one(packet,
'''bool ReadConstraint(
    Reader* const reader,
    laplace_cognition_operator_constraint* const constraint) {
    if (reader == nullptr || constraint == nullptr) return false;
    return reader->Digest(&constraint->constraint_id) &&
        reader->Digest(&constraint->plane_id) &&
        reader->Digest(&constraint->law_fingerprint) &&
        reader->Digest(&constraint->units_fingerprint) &&
        reader->Digest(&constraint->evidence_root_id) &&
        reader->Digest(&constraint->calculation_receipt_id) &&
        reader->U64(&constraint->source_field_index) &&
        reader->U64(&constraint->target_field_index) &&
        reader->F64(&constraint->transport_scale) &&
        reader->F64(&constraint->transport_offset) &&
        reader->F64(&constraint->target_value) &&
        reader->F64(&constraint->precision) &&
        reader->U32(&constraint->relation_family) &&
        reader->U32(&constraint->source_class) &&
        reader->U32(&constraint->direction) &&
        reader->U32(&constraint->transport_kind) &&
        reader->U32(&constraint->flags) &&
        reader->U32(&constraint->reserved);
}
''',
'''bool ReadStandingState(
    Reader* const reader,
    laplace_standing_state* const state) {
    return reader != nullptr && state != nullptr &&
        reader->Digest(&state->state_id) && reader->Digest(&state->coordinate_id) &&
        reader->Digest(&state->arena_scope_id) && reader->Digest(&state->prior_state_id) &&
        reader->Digest(&state->epoch_id) && reader->Digest(&state->rating_recipe_id) &&
        reader->F64(&state->rating) && reader->F64(&state->rating_deviation) &&
        reader->F64(&state->volatility) && reader->U64(&state->eligible_match_count) &&
        reader->U64(&state->period_ordinal) && reader->U32(&state->rating_recipe_version) &&
        reader->U32(&state->flags);
}

bool ReadConstraint(
    Reader* const reader,
    laplace_cognition_operator_constraint* const constraint) {
    if (reader == nullptr || constraint == nullptr) return false;
    *constraint = laplace_cognition_operator_constraint{};
    const bool base = reader->Digest(&constraint->constraint_id) &&
        reader->Digest(&constraint->plane_id) &&
        reader->Digest(&constraint->law_fingerprint) &&
        reader->Digest(&constraint->units_fingerprint) &&
        reader->Digest(&constraint->evidence_root_id) &&
        reader->Digest(&constraint->calculation_receipt_id) &&
        reader->U64(&constraint->source_field_index) &&
        reader->U64(&constraint->target_field_index) &&
        reader->F64(&constraint->transport_scale) &&
        reader->F64(&constraint->transport_offset) &&
        reader->F64(&constraint->target_value) &&
        reader->F64(&constraint->precision) &&
        reader->U32(&constraint->relation_family) &&
        reader->U32(&constraint->source_class) &&
        reader->U32(&constraint->direction) &&
        reader->U32(&constraint->transport_kind) &&
        reader->U32(&constraint->flags) &&
        reader->U32(&constraint->reserved);
    if (!base) return false;
    return constraint->source_class != LAPLACE_COGNITION_OPERATOR_SOURCE_STANDING ||
        ReadStandingState(reader, &constraint->standing);
}
''')

Path("tools/apply_standing_packet_wire.py").unlink(missing_ok=True)
Path(".github/workflows/apply-standing-packet-wire.yml").unlink(missing_ok=True)
