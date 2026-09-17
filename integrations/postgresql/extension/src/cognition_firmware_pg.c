#include "postgres.h"

#include <stddef.h>
#include <string.h>

#include "executor/spi.h"
#include "utils/memutils.h"

#include "cognition_firmware_pg.h"
#include "laplace/cognition_observation_request.h"
#include "semantic_cognition_pg.h"

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
    laplace_cognition_observation_request semantic_scope;
    laplace_cognition_observation_candidate_provider_v1 provider;
    laplace_cognition_observation_candidate_provider_v1 physical_provider;
    laplace_cognition_observation_candidate_provider_v1 semantic_provider;
    laplace_cognition_observation_candidate_provider_v1 durable_providers[2];
    laplace_cognition_observation_candidate_provider_set* provider_set = NULL;
    laplace_pg_cognition_provider* physical_owner = NULL;
    laplace_pg_semantic_provider_state* semantic_owner = NULL;
    laplace_pg_semantic_provider_report semantic_reads;
    ErrorData* volatile physical_error = NULL;
    ErrorData* volatile semantic_error = NULL;
    laplace_digest256 program_id;
    laplace_cognition_firmware_status status;
    laplace_cognition_observation_provider_set_status provider_set_status;
    size_t durable_provider_count = 0u;
    uint32_t physical_relation_mask = 0u;
    uint32_t first_provider_step = UINT32_MAX;
    uint32_t step;
    bool need_physical = false;
    bool need_semantic = false;
    volatile bool spi_connected = false;
    const uint32_t semantic_mask = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    const uint32_t structural_mask =
        LAPLACE_OBSERVATION_QUERY_RELATION_MASK & ~semantic_mask;
    const uint64_t epochs = (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_FIRMWARE) |
                            (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_EVIDENCE);

    if (result != NULL) *result = NULL;
    if (reads != NULL) memset(reads, 0, sizeof(*reads));
    if (database_error != NULL) *database_error = NULL;
    if (error != NULL) memset(error, 0, sizeof(*error));
    memset(&semantic_reads, 0, sizeof(semantic_reads));
    memset(&provider, 0, sizeof(provider));
    memset(&physical_provider, 0, sizeof(physical_provider));
    memset(&semantic_provider, 0, sizeof(semantic_provider));
    memset(durable_providers, 0, sizeof(durable_providers));

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

    /* Relation families and evidence/source planes are orthogonal. A single
     * native cognition step may legitimately ask for structural and semantic
     * candidates together; provider combination must therefore use the shared
     * native provider-set owner rather than a PostgreSQL-local ordering policy. */
    for (step = 0u; step < program->step_count; ++step) {
        const uint32_t relation_mask = program->steps[step].relation_mask;
        const bool step_semantic = (relation_mask & semantic_mask) != 0u;
        const uint32_t step_structural = relation_mask & structural_mask;
        if ((relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) != 0u) {
            return firmware_host_error(
                error,
                LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT,
                step,
                0u);
        }
        if (step_semantic || step_structural != 0u) {
            if (first_provider_step == UINT32_MAX) {
                first_provider_step = step;
            }
        }
        if (step_semantic) {
            need_semantic = true;
        }
        if (step_structural != 0u) {
            need_physical = true;
            physical_relation_mask |= step_structural;
        }
    }

    /* Native firmware always composes the admitted prompt-structure provider with
     * the durable provider set. A transition batch smaller than two cannot carry
     * even one prompt-structure and one durable contribution without making
     * provider ordering observable, so reject it before provider I/O. */
    if ((need_physical || need_semantic) &&
        request->search_budget.transition_batch_capacity < 2u) {
        return firmware_host_error(
            error,
            LAPLACE_COGNITION_FIRMWARE_LIMIT,
            first_provider_step,
            0u);
    }

    if (laplace_cognition_prompt_admission_view_get(admission, &input) !=
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE, UINT32_MAX, 0u);

    /* The native firmware owner composes the admitted prompt-structure provider
     * with this durable PostgreSQL provider set at execution time. The supplied
     * transition batch is the finite capacity for the actual combined query; if
     * the complete typed contribution does not fit, the shared provider-set owner
     * returns a typed bounded-search disposition instead of publishing a prefix. */
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
    scope.maximum_results = 1u;
    scope.flags = request->boundary_flags |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    scope.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

    if (need_physical) {
        scope.relation_mask = physical_relation_mask;
        laplace_pg_cognition_provider_create(
            context,
            &scope,
            provider_workspace_bytes,
            &physical_owner,
            &physical_provider);
        durable_providers[durable_provider_count++] = physical_provider;
    }
    if (need_semantic) {
        semantic_scope = scope;
        semantic_scope.relation_mask = semantic_mask;
        laplace_pg_semantic_provider_create(
            &semantic_scope,
            (uint64_t)scope.search_budget.transition_batch_capacity,
            &semantic_owner,
            &semantic_provider);
        durable_providers[durable_provider_count++] = semantic_provider;
    }

    if (durable_provider_count != 0u) {
        provider_set_status =
            laplace_cognition_observation_candidate_provider_set_create(
                durable_providers,
                durable_provider_count,
                &provider_set,
                &provider);
        if (provider_set_status != LAPLACE_COGNITION_OBSERVATION_PROVIDER_SET_OK) {
            laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
            laplace_pg_cognition_provider_destroy(&physical_owner);
            laplace_pg_semantic_provider_destroy(&semantic_owner);
            return firmware_host_error(
                error,
                LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT,
                first_provider_step,
                (uint32_t)provider_set_status);
        }
        if (SPI_connect() != SPI_OK_CONNECT) {
            laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
            laplace_pg_cognition_provider_destroy(&physical_owner);
            laplace_pg_semantic_provider_destroy(&semantic_owner);
            ereport(ERROR, (errmsg("Laplace firmware provider could not connect to PostgreSQL")));
        }
        spi_connected = true;
    }

    status = laplace_cognition_firmware_execute_with_provider_workspace(
        program,
        request,
        context,
        admission,
        previous_checkpoint,
        previous_checkpoint_bytes,
        durable_provider_count != 0u ? &provider : NULL,
        durable_provider_count != 0u ? 1u : 0u,
        realization,
        materialization,
        cancel,
        cancel_state,
        provider_workspace_bytes,
        result,
        error);

    /* Release the generic native provider-set state before PostgreSQL diagnostics
     * are retrieved. SPI_finish cannot leave a returned native result orphaned if
     * the host itself errors. */
    PG_TRY();
    {
        if (spi_connected && SPI_finish() != SPI_OK_FINISH)
            ereport(ERROR, (errmsg("Laplace firmware provider SPI cleanup failed")));
        laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
        if (physical_owner != NULL) {
            laplace_pg_cognition_provider_summary(physical_owner, reads);
            physical_error =
                laplace_pg_cognition_provider_take_error(physical_owner);
            laplace_pg_cognition_provider_destroy(&physical_owner);
        }
        if (semantic_owner != NULL) {
            laplace_pg_semantic_provider_summary(
                semantic_owner, &semantic_reads);
            semantic_error =
                laplace_pg_semantic_provider_take_error(semantic_owner);
            laplace_pg_semantic_provider_destroy(&semantic_owner);
            reads->semantic_provider_fingerprint =
                semantic_reads.provider_fingerprint;
            reads->semantic_rows_examined =
                semantic_reads.rows_examined;
            reads->semantic_database_operations =
                semantic_reads.database_operations;
            reads->semantic_provider_calls =
                semantic_reads.provider_calls;
        }
        if (physical_error != NULL) {
            *database_error = physical_error;
            if (semantic_error != NULL) {
                FreeErrorData(semantic_error);
                semantic_error = NULL;
            }
        } else {
            *database_error = semantic_error;
        }
    }
    PG_CATCH();
    {
        laplace_cognition_firmware_result_destroy(result);
        laplace_cognition_observation_candidate_provider_set_destroy(&provider_set);
        laplace_pg_cognition_provider_destroy(&physical_owner);
        laplace_pg_semantic_provider_destroy(&semantic_owner);
        PG_RE_THROW();
    }
    PG_END_TRY();

    if (*database_error != NULL) {
        laplace_cognition_firmware_result_destroy(result);
        return firmware_host_error(
            error,
            LAPLACE_COGNITION_FIRMWARE_COGNITION_FAILURE,
            error != NULL ? error->step_index : UINT32_MAX,
            0u);
    }
    return status;
}
