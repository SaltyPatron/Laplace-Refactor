extern "C" laplace_cognition_operator_status
laplace_cognition_operator_materialize_dense(
    const laplace_cognition_operator* const operator_value,
    double* const matrix_row_major,
    const size_t matrix_capacity,
    double* const right_hand_side,
    const size_t rhs_capacity,
    laplace_digest256* const materialization_receipt_id) {
    if (materialization_receipt_id != nullptr) {
        *materialization_receipt_id = laplace_digest256{};
    }
    if (operator_value == nullptr || matrix_row_major == nullptr ||
        right_hand_side == nullptr || materialization_receipt_id == nullptr) {
        return LAPLACE_COGNITION_OPERATOR_INVALID_ARGUMENT;
    }
    const std::size_t n = operator_value->fields.size();
    if (n != 0U && n > std::numeric_limits<std::size_t>::max() / n) {
        return LAPLACE_COGNITION_OPERATOR_RANGE;
    }
    const std::size_t matrix_count = n * n;
    if (matrix_capacity < matrix_count || rhs_capacity < n) {
        return LAPLACE_COGNITION_OPERATOR_RANGE;
    }
    std::fill(matrix_row_major, matrix_row_major + matrix_count, 0.0);
    std::fill(right_hand_side, right_hand_side + n, 0.0);
    for (const auto& constraint : operator_value->constraints) {
        const auto c = ConstraintCoefficients(constraint);
        const double w = constraint.precision;
        const double a = c.first_value;
        const double b = c.second_value;
        matrix_row_major[c.first * n + c.first] += w * a * a;
        matrix_row_major[c.first * n + c.second] += w * a * b;
        matrix_row_major[c.second * n + c.first] += w * b * a;
        matrix_row_major[c.second * n + c.second] += w * b * b;
        right_hand_side[c.first] += -w * c.constant * a;
        right_hand_side[c.second] += -w * c.constant * b;
    }
    for (std::size_t index = 0U; index < matrix_count; ++index) {
        if (!std::isfinite(matrix_row_major[index])) {
            return LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
        }
    }
    for (std::size_t index = 0U; index < n; ++index) {
        if (!std::isfinite(right_hand_side[index])) {
            return LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
        }
    }
    blake3_hasher hasher{};
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, LAPLACE_COGNITION_OPERATOR_MATERIALIZATION_DOMAIN,
        sizeof(LAPLACE_COGNITION_OPERATOR_MATERIALIZATION_DOMAIN) - 1U);
    HashDigest(&hasher, operator_value->receipt.operator_id);
    HashU64(&hasher, static_cast<std::uint64_t>(n));
    for (std::size_t index = 0U; index < matrix_count; ++index) {
        HashF64(&hasher, matrix_row_major[index]);
    }
    for (std::size_t index = 0U; index < n; ++index) {
        HashF64(&hasher, right_hand_side[index]);
    }
    Finish(&hasher, materialization_receipt_id);
    return LAPLACE_COGNITION_OPERATOR_OK;
}
