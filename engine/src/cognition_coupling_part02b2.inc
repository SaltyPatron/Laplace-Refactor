extern "C" size_t
laplace_cognition_coupling_response_count(
    const laplace_cognition_coupling_result* result) {
    return result == nullptr ? 0U : result->responses.size();
}

extern "C" laplace_cognition_coupling_status
laplace_cognition_coupling_response_read(
    const laplace_cognition_coupling_result* result,
    size_t response_index,
    laplace_cognition_coupling_response* response) {
    if (response != nullptr) {
        *response = laplace_cognition_coupling_response{};
    }
    if (result == nullptr || response == nullptr) {
        return LAPLACE_COGNITION_COUPLING_INVALID_ARGUMENT;
    }
    if (response_index >= result->responses.size()) {
        return LAPLACE_COGNITION_COUPLING_RESULT_RANGE;
    }
    *response = result->responses[response_index];
    return LAPLACE_COGNITION_COUPLING_OK;
}

extern "C" size_t
laplace_cognition_coupling_witness_count(
    const laplace_cognition_coupling_result* result) {
    return result == nullptr ? 0U : result->witnesses.size();
}
