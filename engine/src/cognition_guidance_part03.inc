extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_state_clone(
    const laplace_cognition_guidance_state* const state,
    laplace_cognition_guidance_state** const clone) {
    if (clone != nullptr) *clone = nullptr;
    if (state == nullptr || clone == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    return BuildState(
        state->header, state->obligations.data(), state->obligations.size(), clone);
}

extern "C" laplace_cognition_guidance_status
laplace_cognition_guidance_state_header(
    const laplace_cognition_guidance_state* const state,
    laplace_cognition_guidance_header* const header) {
    if (state == nullptr || header == nullptr) {
        return LAPLACE_COGNITION_GUIDANCE_INVALID_ARGUMENT;
    }
    *header = state->header;
    return LAPLACE_COGNITION_GUIDANCE_OK;
}
