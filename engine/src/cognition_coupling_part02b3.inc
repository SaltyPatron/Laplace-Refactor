extern "C" laplace_cognition_coupling_status
laplace_cognition_coupling_witness_read(
    const laplace_cognition_coupling_result* result,
    size_t witness_index,
    laplace_cognition_coupling_witness* witness) {
    if (witness != nullptr) {
        *witness = laplace_cognition_coupling_witness{};
    }
    if (result == nullptr || witness == nullptr) {
        return LAPLACE_COGNITION_COUPLING_INVALID_ARGUMENT;
    }
    if (witness_index >= result->witnesses.size()) {
        return LAPLACE_COGNITION_COUPLING_RESULT_RANGE;
    }
    *witness = result->witnesses[witness_index];
    return LAPLACE_COGNITION_COUPLING_OK;
}

extern "C" void
laplace_cognition_coupling_result_destroy(
    laplace_cognition_coupling_result** result) {
    if (result == nullptr || *result == nullptr) return;
    delete *result;
    *result = nullptr;
}
