extern "C" laplace_cognition_operator_status
laplace_cognition_operator_receipt_get(
    const laplace_cognition_operator* const operator_value,
    laplace_cognition_operator_receipt* const receipt) {
    if (operator_value == nullptr || receipt == nullptr) {
        return LAPLACE_COGNITION_OPERATOR_INVALID_ARGUMENT;
    }
    *receipt = operator_value->receipt;
    return LAPLACE_COGNITION_OPERATOR_OK;
}

extern "C" laplace_cognition_operator_status
laplace_cognition_operator_apply_linear(
    const laplace_cognition_operator* const operator_value,
    const double* const input,
    const size_t input_count,
    double* const output,
    const size_t output_count) {
    if (operator_value == nullptr || input == nullptr || output == nullptr) {
        return LAPLACE_COGNITION_OPERATOR_INVALID_ARGUMENT;
    }
    if (input_count != operator_value->fields.size() || output_count != input_count) {
        return LAPLACE_COGNITION_OPERATOR_RANGE;
    }
    for (std::size_t index = 0U; index < input_count; ++index) {
        if (!std::isfinite(input[index])) {
            return LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
        }
        output[index] = 0.0;
    }
    for (const auto& constraint : operator_value->constraints) {
        const auto coefficients = ConstraintCoefficients(constraint);
        const double homogeneous =
            coefficients.first_value * input[coefficients.first] +
            coefficients.second_value * input[coefficients.second];
        const double weighted = constraint.precision * homogeneous;
        output[coefficients.first] += coefficients.first_value * weighted;
        output[coefficients.second] += coefficients.second_value * weighted;
        if (!std::isfinite(output[coefficients.first]) ||
            !std::isfinite(output[coefficients.second])) {
            return LAPLACE_COGNITION_OPERATOR_NUMERIC_FAILURE;
        }
    }
    return LAPLACE_COGNITION_OPERATOR_OK;
}
