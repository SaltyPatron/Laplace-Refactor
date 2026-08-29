namespace {

class ForwardPass {
public:
    ForwardPass(
        const laplace_cognition_forward_program& program,
        const laplace_cognition_forward_provider_v1& provider,
        laplace_cognition_forward_result* result,
        laplace_cognition_forward_receipt* receipt)
        : program_(program), provider_(provider), result_(result), receipt_(receipt) {
        blake3_hasher_init(&trace_hasher_);
        blake3_hasher_update(
            &trace_hasher_, LAPLACE_COGNITION_FORWARD_LAYER_DOMAIN,
            sizeof(LAPLACE_COGNITION_FORWARD_LAYER_DOMAIN) - 1U);
    }

    laplace_cognition_forward_status Run(
        const laplace_cognition_guidance_state* const initial_state) {
        if (laplace_cognition_guidance_state_clone(
                initial_state, &result_->final_state) !=
            LAPLACE_COGNITION_GUIDANCE_OK) {
            return LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE;
        }
        if (laplace_cognition_guidance_state_identify(
                result_->final_state, &receipt_->initial_state_id) !=
            LAPLACE_COGNITION_GUIDANCE_OK) {
            return LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE;
        }
        HashDigest(&trace_hasher_, receipt_->program_fingerprint);
        HashDigest(&trace_hasher_, receipt_->initial_state_id);

        for (;;) {
            std::uint32_t completion = 0U;
            std::uint64_t remaining_required = 0U;
            if (laplace_cognition_guidance_completion(
                    result_->final_state, &completion, &remaining_required) !=
                LAPLACE_COGNITION_GUIDANCE_OK) {
                return LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE;
            }
            if (completion == LAPLACE_COGNITION_COMPLETION_COMPLETE) {
                return Finalize(LAPLACE_COGNITION_FORWARD_COMPLETE);
            }
            if (completion == LAPLACE_COGNITION_COMPLETION_BLOCKED) {
                return Finalize(LAPLACE_COGNITION_FORWARD_BLOCKED);
            }
            if (receipt_->layer_count >= program_.max_layers) {
                return Finalize(LAPLACE_COGNITION_FORWARD_LAYER_LIMIT);
            }
            if (receipt_->provider_call_count >= program_.max_provider_calls) {
                return Finalize(LAPLACE_COGNITION_FORWARD_PROVIDER_LIMIT);
            }

            const auto layer_status = ExecuteLayer();
            if (layer_status.hard_status != LAPLACE_COGNITION_FORWARD_OK) {
                return layer_status.hard_status;
            }
            if (layer_status.disposition != 0U) {
                return Finalize(layer_status.disposition);
            }
        }
    }

private:
    struct LayerStatus {
        laplace_cognition_forward_status hard_status{
            LAPLACE_COGNITION_FORWARD_OK};
        std::uint32_t disposition{};
    };

    const laplace_cognition_forward_program& program_;
    const laplace_cognition_forward_provider_v1& provider_;
    laplace_cognition_forward_result* result_;
    laplace_cognition_forward_receipt* receipt_;
    blake3_hasher trace_hasher_{};

    bool Accumulate(
        std::uint64_t* const total,
        const std::uint64_t value,
        const std::uint64_t maximum) {
        std::uint64_t next = 0U;
        if (!Add(*total, value, &next) || next > maximum) return false;
        *total = next;
        return true;
    }

    LayerStatus ExecuteLayer() {
        laplace_digest256 state_id{};
        if (laplace_cognition_guidance_state_identify(
                result_->final_state, &state_id) != LAPLACE_COGNITION_GUIDANCE_OK) {
            return {LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE, 0U};
        }

        const std::size_t obligation_count =
            laplace_cognition_guidance_state_obligation_count(result_->final_state);
        std::vector<laplace_cognition_query_projection> projections(obligation_count);
        std::size_t projection_count = 0U;
        const auto projection_status = laplace_cognition_guidance_project_queries(
            result_->final_state, projections.data(), projections.size(),
            &projection_count);
        if (projection_status != LAPLACE_COGNITION_GUIDANCE_OK) {
            return {LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE, 0U};
        }
        projections.resize(projection_count);
        if (projection_count == 0U) {
            return {LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT, 0U};
        }
        if (projection_count >
            program_.max_projected_queries - receipt_->projected_query_count) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_RESOURCE_LIMIT};
        }
        receipt_->projected_query_count += projection_count;

        laplace_digest256 projection_fingerprint{};
        ProjectionFingerprint(state_id, projections, &projection_fingerprint);

        if (receipt_->provider_call_count >= program_.max_provider_calls) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_PROVIDER_LIMIT};
        }
        auto enumerate_grant = Grant(program_, *receipt_);
        const std::uint64_t candidate_capacity64 = std::min<std::uint64_t>(
            program_.candidate_operation_capacity,
            enumerate_grant.remaining_candidate_operations);
        if (candidate_capacity64 == 0U ||
            candidate_capacity64 > std::numeric_limits<std::size_t>::max()) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_RESOURCE_LIMIT};
        }
        std::vector<laplace_cognition_guidance_operation> operations(
            static_cast<std::size_t>(candidate_capacity64));
        std::size_t operation_count = 0U;
        laplace_cognition_forward_enumeration_receipt enumeration{};
        const int enumerate_status = provider_.enumerate(
            provider_.state, result_->final_state, projections.data(), projections.size(),
            &enumerate_grant, operations.data(), operations.size(), &operation_count,
            &enumeration);
        ++receipt_->provider_call_count;
        if (enumerate_status != 0) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_PROVIDER_FAILURE};
        }
        if (operation_count > operations.size() || Zero(enumeration.receipt_id) ||
            enumeration.status != 0U || enumeration.flags != 0U ||
            !Same(enumeration.state_id, state_id) ||
            !Same(enumeration.projection_fingerprint, projection_fingerprint) ||
            enumeration.projected_query_count != projection_count ||
            enumeration.candidate_operation_count != operation_count ||
            !ReceiptCostFits(
                enumerate_grant, enumeration.resource_cost,
                enumeration.io_operations, enumeration.database_operations)) {
            return {LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT, 0U};
        }
        if (!Accumulate(
                &receipt_->resource_cost, enumeration.resource_cost,
                program_.max_resource_cost) ||
            !Accumulate(
                &receipt_->io_operations, enumeration.io_operations,
                program_.max_io_operations) ||
            !Accumulate(
                &receipt_->database_operations, enumeration.database_operations,
                program_.max_database_operations) ||
            !Accumulate(
                &receipt_->candidate_operation_count,
                static_cast<std::uint64_t>(operation_count),
                program_.max_candidate_operations)) {
            return {LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT, 0U};
        }
        operations.resize(operation_count);
        if (!CandidateSetCanonicalize(&operations)) {
            return {LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT, 0U};
        }
        if (operations.empty()) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_NO_OPERATION};
        }

        const auto post_enumeration_grant = Grant(program_, *receipt_);
        std::vector<laplace_cognition_guidance_operation> schedulable;
        schedulable.reserve(operations.size());
        for (const auto& operation : operations) {
            if (operation.resource_cost <= post_enumeration_grant.remaining_resource_cost) {
                schedulable.push_back(operation);
            }
        }
        if (schedulable.empty()) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_RESOURCE_LIMIT};
        }

        laplace_cognition_guidance_decision decision{};
        const auto decision_status = laplace_cognition_guidance_select_operation(
            result_->final_state, schedulable.data(), schedulable.size(), &decision);
        if (decision_status == LAPLACE_COGNITION_GUIDANCE_NO_OPERATION) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_NO_OPERATION};
        }
        if (decision_status != LAPLACE_COGNITION_GUIDANCE_OK ||
            Zero(decision.decision_receipt_id) ||
            Zero(decision.selected_operation_id) ||
            !Same(decision.state_id, state_id)) {
            return {LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE, 0U};
        }
        const auto selected = std::lower_bound(
            schedulable.begin(), schedulable.end(), decision.selected_operation_id,
            [](const auto& operation, const auto& id) {
                return DigestLess(operation.operation_id, id);
            });
        if (selected == schedulable.end() ||
            !Same(selected->operation_id, decision.selected_operation_id)) {
            return {LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT, 0U};
        }

        if (receipt_->provider_call_count >= program_.max_provider_calls) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_PROVIDER_LIMIT};
        }
        auto execute_grant = Grant(program_, *receipt_);
        const std::uint64_t resolution_capacity64 = std::min<std::uint64_t>(
            program_.resolution_capacity, execute_grant.remaining_resolutions);
        if (resolution_capacity64 == 0U ||
            resolution_capacity64 > std::numeric_limits<std::size_t>::max()) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_RESOURCE_LIMIT};
        }
        std::vector<laplace_cognition_resolution> resolutions(
            static_cast<std::size_t>(resolution_capacity64));
        std::size_t resolution_count = 0U;
        laplace_cognition_forward_execution_receipt execution{};
        const int execution_status = provider_.execute(
            provider_.state, result_->final_state, &*selected,
            projections.data(), projections.size(), &execute_grant,
            resolutions.data(), resolutions.size(), &resolution_count, &execution);
        ++receipt_->provider_call_count;
        if (execution_status != 0) {
            return {LAPLACE_COGNITION_FORWARD_OK,
                    LAPLACE_COGNITION_FORWARD_PROVIDER_FAILURE};
        }
        if (resolution_count == 0U || resolution_count > resolutions.size() ||
            Zero(execution.receipt_id) || Zero(execution.result_fingerprint) ||
            execution.status != 0U || execution.flags != 0U ||
            !Same(execution.state_id, state_id) ||
            !Same(execution.operation_id, selected->operation_id) ||
            execution.resolution_count != resolution_count ||
            !ReceiptCostFits(
                execute_grant, execution.resource_cost,
                execution.io_operations, execution.database_operations)) {
            return {LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT, 0U};
        }
        if (!Accumulate(
                &receipt_->resource_cost, execution.resource_cost,
                program_.max_resource_cost) ||
            !Accumulate(
                &receipt_->io_operations, execution.io_operations,
                program_.max_io_operations) ||
            !Accumulate(
                &receipt_->database_operations, execution.database_operations,
                program_.max_database_operations) ||
            !Accumulate(
                &receipt_->resolution_count,
                static_cast<std::uint64_t>(resolution_count),
                program_.max_resolutions)) {
            return {LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT, 0U};
        }
        resolutions.resize(resolution_count);

        laplace_cognition_guidance_state* next_state = nullptr;
        laplace_cognition_guidance_transition_receipt transition{};
        const auto transition_status = laplace_cognition_guidance_apply_resolutions(
            result_->final_state, resolutions.data(), resolutions.size(),
            &next_state, &transition);
        if (transition_status != LAPLACE_COGNITION_GUIDANCE_OK ||
            next_state == nullptr || Zero(transition.transition_receipt_id) ||
            !Same(transition.prior_state_id, state_id) ||
            transition.applied_resolution_count != resolution_count) {
            laplace_cognition_guidance_state_destroy(&next_state);
            return {LAPLACE_COGNITION_FORWARD_GUIDANCE_FAILURE, 0U};
        }
        if ((program_.flags & LAPLACE_COGNITION_FORWARD_REQUIRE_STATE_PROGRESS) != 0U &&
            Same(transition.prior_state_id, transition.next_state_id)) {
            laplace_cognition_guidance_state_destroy(&next_state);
            return {LAPLACE_COGNITION_FORWARD_PROVIDER_CONTRACT, 0U};
        }

        laplace_cognition_forward_layer_receipt layer{};
        layer.prior_state_id = state_id;
        layer.projection_fingerprint = projection_fingerprint;
        layer.enumeration_receipt_id = enumeration.receipt_id;
        layer.decision_receipt_id = decision.decision_receipt_id;
        layer.selected_operation_id = decision.selected_operation_id;
        layer.execution_receipt_id = execution.receipt_id;
        layer.resolution_set_fingerprint = transition.resolution_set_fingerprint;
        layer.next_state_id = transition.next_state_id;
        layer.projected_query_count = projection_count;
        layer.candidate_operation_count = operation_count;
        layer.eligible_operation_count = decision.eligible_operation_count;
        layer.resolution_count = resolution_count;
        layer.remaining_open_count = transition.remaining_open_count;
        layer.resource_cost = enumeration.resource_cost + execution.resource_cost;
        layer.io_operations = enumeration.io_operations + execution.io_operations;
        layer.database_operations =
            enumeration.database_operations + execution.database_operations;
        layer.selected_kind = decision.selected_kind;
        layer.completion = transition.completion;
        layer.status = 0U;
        HashLayer(layer, &layer.layer_receipt_id);
        HashDigest(&trace_hasher_, layer.layer_receipt_id);
        result_->layers.push_back(layer);
        receipt_->layer_count = result_->layers.size();
        laplace_cognition_guidance_state_destroy(&result_->final_state);
        result_->final_state = next_state;
        return {LAPLACE_COGNITION_FORWARD_OK, 0U};
    }

    laplace_cognition_forward_status Finalize(const std::uint32_t disposition);
};

}  // namespace
