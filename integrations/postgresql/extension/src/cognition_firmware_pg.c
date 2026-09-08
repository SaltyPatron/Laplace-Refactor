#include "postgres.h"

#include <string.h>
#include "executor/spi.h"
#include "utils/memutils.h"
#include "cognition_firmware_pg.h"

static laplace_cognition_firmware_status firmware_host_error(
    laplace_cognition_firmware_error* error,
    laplace_cognition_firmware_status status, uint32_t step, uint32_t native) {
    if (error != NULL) {
        error->status = (uint32_t)status;
        error->step_index = step;
        error->native_status = native;
        error->native_disposition = 0u;
    }
    return status;
}

laplace_cognition_firmware_status laplace_pg_cognition_firmware_execute_indexed(
    const laplace_cognition_firmware_program* program,
    const laplace_cognition_firmware_request* request,
    const laplace_framework_context* context,
    laplace_cognition_prompt_admission* admission,
    const uint8_t* previous_checkpoint, size_t previous_checkpoint_bytes,
    const laplace_cognition_realization_provider_v1* realization,
    const laplace_cognition_materialization_provider_v1* materialization,
    uint64_t provider_workspace_bytes,
    laplace_framework_cancel_requested_fn cancel, void* cancel_state,
    laplace_cognition_firmware_result** result,
    laplace_cognition_firmware_error* error,
    laplace_pg_cognition_provider_report* reads,
    ErrorData** database_error) {
    laplace_cognition_prompt_admission_view input;
    laplace_cognition_observation_request scope;
    laplace_cognition_observation_candidate_provider_v1 provider;
    laplace_pg_cognition_provider* owner = NULL;
    laplace_digest256 program_id;
    laplace_cognition_firmware_status status;
    uint32_t step;
    const uint64_t epochs = (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_FIRMWARE) |
                            (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_EVIDENCE);
    if (result != NULL) *result = NULL;
    if (reads != NULL) memset(reads, 0, sizeof(*reads));
    if (database_error != NULL) *database_error = NULL;
    if (error != NULL) memset(error, 0, sizeof(*error));
    if (program == NULL || request == NULL || context == NULL || admission == NULL ||
        realization == NULL || materialization == NULL || result == NULL ||
        reads == NULL || database_error == NULL)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_INVALID_ARGUMENT, UINT32_MAX, 0u);
    if (cancel != NULL && cancel(cancel_state) != 0)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_CANCELLED, UINT32_MAX, 0u);
    if (laplace_framework_context_validate(context) != LAPLACE_FRAMEWORK_OK ||
        (context->epoch_mask & epochs) != epochs)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_CONTEXT_INVALID, UINT32_MAX, 0u);
    /* Bound program serialization before its native identity calculation. The
     * native owner later reserves all retained/scratch allocations together. */
    if (provider_workspace_bytes < 2048u ||
        provider_workspace_bytes >= context->resource_grant.memory_bytes ||
        (uint64_t)program->step_count >
            (context->resource_grant.memory_bytes - provider_workspace_bytes) / 120u)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_LIMIT, UINT32_MAX, 0u);
    status = laplace_cognition_firmware_identify(program, &program_id);
    if (status != LAPLACE_COGNITION_FIRMWARE_OK)
        return firmware_host_error(error, status, UINT32_MAX, 0u);
    if (memcmp(program_id.bytes, request->selected_program.bytes, sizeof(program_id.bytes)) != 0 ||
        memcmp(program_id.bytes, context->epochs[LAPLACE_FRAMEWORK_EPOCH_FIRMWARE].bytes,
               sizeof(program_id.bytes)) != 0)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_PROGRAM_MISMATCH, UINT32_MAX, 0u);
    for (step = 0u; step < program->step_count; ++step) {
        if ((program->steps[step].relation_mask & LAPLACE_OBSERVATION_QUERY_SEMANTIC) != 0u)
            return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT, step, 0u);
    }
    if (laplace_cognition_prompt_admission_view_get(admission, &input) !=
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE, UINT32_MAX, 0u);
    /* This descriptor scopes the physical reader. It does not execute or choose
     * a goal; each actual step request comes from the native firmware owner. */
    memset(&scope, 0, sizeof(scope));
    scope.anchor_entity_id = input.trunk_entity_id;
    scope.world_id = input.turn.world_id;
    scope.time_fingerprint = input.turn.time_fingerprint;
    scope.context_fingerprint = input.turn.context_fingerprint;
    scope.evidence_boundary = request->evidence_boundary;
    scope.evidence_epoch = context->epochs[LAPLACE_FRAMEWORK_EPOCH_EVIDENCE];
    scope.authority_id = context->authority_fingerprint;
    scope.result_contract_fingerprint = request->result_contract_fingerprint;
    scope.search_budget = request->search_budget;
    scope.forward_limits = request->forward_limits;
    scope.relation_mask = program->steps[0].relation_mask;
    scope.maximum_results = 1u;
    scope.flags = request->boundary_flags | LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    scope.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;
    laplace_pg_cognition_provider_create(&scope, provider_workspace_bytes, &owner, &provider);
    if (SPI_connect() != SPI_OK_CONNECT)
        ereport(ERROR, (errmsg("Laplace firmware provider could not connect to PostgreSQL")));
    status = laplace_cognition_firmware_execute_with_provider_workspace(
        program, request, context, admission, previous_checkpoint, previous_checkpoint_bytes,
        &provider, 1u, realization, materialization, cancel, cancel_state,
        provider_workspace_bytes, result, error);
    /* No C++ stack remains when PostgreSQL diagnostics are retrieved. SPI_finish
     * cannot leave a returned native result orphaned if the host itself errors. */
    PG_TRY();
    {
        if (SPI_finish() != SPI_OK_FINISH)
            ereport(ERROR, (errmsg("Laplace firmware provider SPI cleanup failed")));
        laplace_pg_cognition_provider_summary(owner, reads);
        *database_error = laplace_pg_cognition_provider_take_error(owner);
        laplace_pg_cognition_provider_destroy(&owner);
    }
    PG_CATCH();
    {
        laplace_cognition_firmware_result_destroy(result);
        PG_RE_THROW();
    }
    PG_END_TRY();
    if (*database_error != NULL) {
        laplace_cognition_firmware_result_destroy(result);
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_COGNITION_FAILURE,
                                   error != NULL ? error->step_index : UINT32_MAX, 0u);
    }
    return status;
}
