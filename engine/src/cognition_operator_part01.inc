void HashConstraint(
    blake3_hasher* const hasher,
    const laplace_cognition_operator_constraint& constraint) {
    HashDigest(hasher, constraint.constraint_id);
#if !defined(LAPLACE_TEST_COGNITION_OPERATOR_FLATTEN_RELATION_PLANE)
    HashDigest(hasher, constraint.plane_id);
    HashDigest(hasher, constraint.law_fingerprint);
    HashDigest(hasher, constraint.units_fingerprint);
#endif
    HashDigest(hasher, constraint.evidence_root_id);
    HashDigest(hasher, constraint.calculation_receipt_id);
    HashU64(hasher, constraint.source_field_index);
    HashU64(hasher, constraint.target_field_index);
    HashF64(hasher, constraint.transport_scale);
    HashF64(hasher, constraint.transport_offset);
    HashF64(hasher, constraint.target_value);
    HashF64(hasher, constraint.precision);
#if !defined(LAPLACE_TEST_COGNITION_OPERATOR_FLATTEN_RELATION_PLANE)
    HashU32(hasher, constraint.relation_family);
    HashU32(hasher, constraint.source_class);
    HashU32(hasher, constraint.direction);
    HashU32(hasher, constraint.transport_kind);
#endif
    HashU32(hasher, constraint.flags);
}

void HashProgram(
    const laplace_cognition_operator_program& program,
    laplace_digest256* const output) {
    std::vector<std::uint32_t> families(
        program.eligible_relation_families,
        program.eligible_relation_families + program.eligible_relation_family_count);
    std::sort(families.begin(), families.end());
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_OPERATOR_PROGRAM_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_PROGRAM_DOMAIN) - 1U);
    HashDigest(&hasher, program.program_id);
    HashDigest(&hasher, program.boundary_id);
    HashDigest(&hasher, program.context_fingerprint);
    HashDigest(&hasher, program.evidence_epoch);
    HashDigest(&hasher, program.result_contract_fingerprint);
    HashU64(&hasher, static_cast<std::uint64_t>(families.size()));
    for (const auto family : families) HashU32(&hasher, family);
    HashU32(&hasher, program.eligible_source_mask);
    HashU32(&hasher, program.flags);
    HashF64(&hasher, program.numeric_tolerance);
    HashU32(&hasher, program.version);
    Finish(&hasher, output);
}

struct EvidenceKey {
    DigestKey root{};
    DigestKey plane{};
    DigestKey law{};
    std::uint64_t source{};
    std::uint64_t target{};
    std::uint32_t relation{};
    std::uint32_t direction{};
    std::uint32_t transport{};

    bool operator<(const EvidenceKey& other) const {
        return std::tie(root, plane, law, source, target, relation, direction, transport) <
            std::tie(other.root, other.plane, other.law, other.source, other.target,
                     other.relation, other.direction, other.transport);
    }
};

EvidenceKey MakeEvidenceKey(
    const laplace_cognition_operator_constraint& constraint) {
    return EvidenceKey{
        Key(constraint.evidence_root_id), Key(constraint.plane_id),
        Key(constraint.law_fingerprint), constraint.source_field_index,
        constraint.target_field_index, constraint.relation_family,
        constraint.direction, constraint.transport_kind};
}

bool SameEvidenceSemantic(
    const laplace_cognition_operator_constraint& left,
    const laplace_cognition_operator_constraint& right) {
    return Same(left.units_fingerprint, right.units_fingerprint) &&
        left.transport_scale == right.transport_scale &&
        left.transport_offset == right.transport_offset &&
        left.target_value == right.target_value &&
        left.precision == right.precision &&
        left.source_class == right.source_class && left.flags == right.flags;
}

struct Coefficients {
    std::size_t first{};
    std::size_t second{};
    double first_value{};
    double second_value{};
    double constant{};
};

Coefficients ConstraintCoefficients(
    const laplace_cognition_operator_constraint& constraint) {
    const double target =
        (constraint.flags & LAPLACE_COGNITION_OPERATOR_CONSTRAINT_HAS_TARGET) != 0U
            ? constraint.target_value
            : 0.0;
    if (constraint.direction ==
        LAPLACE_COGNITION_OPERATOR_DIRECTION_TARGET_TO_SOURCE) {
        return Coefficients{
            static_cast<std::size_t>(constraint.source_field_index),
            static_cast<std::size_t>(constraint.target_field_index),
            1.0, -constraint.transport_scale,
            -(constraint.transport_offset + target)};
    }
    return Coefficients{
        static_cast<std::size_t>(constraint.source_field_index),
        static_cast<std::size_t>(constraint.target_field_index),
        -constraint.transport_scale, 1.0,
        -(constraint.transport_offset + target)};
}

bool ConstraintLess(
    const laplace_cognition_operator_constraint& left,
    const laplace_cognition_operator_constraint& right) {
    return DigestLess(left.constraint_id, right.constraint_id);
}

}  // namespace

struct laplace_cognition_operator {
    laplace_cognition_operator_program program{};
    std::vector<std::uint32_t> eligible_families;
    std::vector<laplace_cognition_operator_field> fields;
    std::vector<laplace_cognition_operator_constraint> constraints;
    laplace_cognition_operator_receipt receipt{};
};

namespace {

void HashOperatorSets(laplace_cognition_operator* const value) {
    blake3_hasher field_hasher{};
    blake3_hasher_init(&field_hasher);
    blake3_hasher_update(
        &field_hasher, LAPLACE_COGNITION_OPERATOR_FIELD_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_FIELD_DOMAIN) - 1U);
    HashU64(&field_hasher, static_cast<std::uint64_t>(value->fields.size()));
    for (const auto& field : value->fields) HashField(&field_hasher, field);
    Finish(&field_hasher, &value->receipt.field_set_fingerprint);

    blake3_hasher constraint_hasher{};
    blake3_hasher_init(&constraint_hasher);
    blake3_hasher_update(
        &constraint_hasher, LAPLACE_COGNITION_OPERATOR_CONSTRAINT_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_CONSTRAINT_DOMAIN) - 1U);
    HashU64(
        &constraint_hasher,
        static_cast<std::uint64_t>(value->constraints.size()));
    for (const auto& constraint : value->constraints) {
        HashConstraint(&constraint_hasher, constraint);
    }
    Finish(&constraint_hasher, &value->receipt.constraint_set_fingerprint);

    blake3_hasher operator_hasher{};
    blake3_hasher_init(&operator_hasher);
    blake3_hasher_update(
        &operator_hasher, LAPLACE_COGNITION_OPERATOR_OPERATOR_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_OPERATOR_DOMAIN) - 1U);
    HashDigest(&operator_hasher, value->receipt.program_fingerprint);
    HashDigest(&operator_hasher, value->receipt.field_set_fingerprint);
    HashDigest(&operator_hasher, value->receipt.constraint_set_fingerprint);
    HashU64(&operator_hasher, value->receipt.field_count);
    HashU64(&operator_hasher, value->receipt.selected_constraint_count);
    Finish(&operator_hasher, &value->receipt.operator_id);

    blake3_hasher receipt_hasher{};
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, LAPLACE_COGNITION_OPERATOR_OPERATOR_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_OPERATOR_DOMAIN) - 1U);
    HashDigest(&receipt_hasher, value->receipt.operator_id);
    HashDigest(&receipt_hasher, value->receipt.program_fingerprint);
    HashDigest(&receipt_hasher, value->receipt.field_set_fingerprint);
    HashDigest(&receipt_hasher, value->receipt.constraint_set_fingerprint);
    HashU64(&receipt_hasher, value->receipt.field_count);
    HashU64(&receipt_hasher, value->receipt.input_constraint_count);
    HashU64(&receipt_hasher, value->receipt.selected_constraint_count);
    HashU64(&receipt_hasher, value->receipt.deduplicated_dependent_count);
    HashU64(&receipt_hasher, value->receipt.physicality_constraint_count);
    HashU64(&receipt_hasher, value->receipt.testimony_constraint_count);
    HashU64(&receipt_hasher, value->receipt.derived_constraint_count);
    HashU32(&receipt_hasher, value->receipt.relation_plane_count);
    HashU32(&receipt_hasher, value->receipt.version);
    Finish(&receipt_hasher, &value->receipt.receipt_id);
}

bool ApplyInternal(
    const laplace_cognition_operator& value,
    const double* const input,
    double* const output,
    double* const energy) {
    std::fill(output, output + value.fields.size(), 0.0);
    double total_energy = 0.0;
    for (const auto& constraint : value.constraints) {
        const auto coefficients = ConstraintCoefficients(constraint);
        const double residual =
            coefficients.first_value * input[coefficients.first] +
            coefficients.second_value * input[coefficients.second] +
            coefficients.constant;
        if (!std::isfinite(residual)) return false;
        const double weighted = constraint.precision * residual;
        output[coefficients.first] += coefficients.first_value * weighted;
        output[coefficients.second] += coefficients.second_value * weighted;
        total_energy += 0.5 * constraint.precision * residual * residual;
        if (!std::isfinite(output[coefficients.first]) ||
            !std::isfinite(output[coefficients.second]) ||
            !std::isfinite(total_energy)) {
            return false;
        }
    }
    *energy = total_energy;
    return true;
}

}  // namespace
