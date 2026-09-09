#include "postgres.h"

#include <string.h>

#include "executor/spi.h"
#include "utils/memutils.h"

#include "blake3.h"
#include "cognition_firmware_pg.h"
#include "semantic_cognition_pg.h"

typedef struct laplace_pg_firmware_provider_router {
    const laplace_cognition_observation_candidate_provider_v1* physical;
    const laplace_cognition_observation_candidate_provider_v1* semantic;
    laplace_digest256 provider_fingerprint;
    uint64_t maximum_candidate_records_per_expansion;
} laplace_pg_firmware_provider_router;

static void firmware_hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void firmware_router_identify(
    laplace_pg_firmware_provider_router* router) {
    static const char domain[] =
        "laplace-postgresql-firmware-provider-router-v1";
    const laplace_cognition_observation_candidate_provider_v1* children[2];
    blake3_hasher hasher;
    uint8_t present;
    size_t index;

    children[0] = router->physical;
    children[1] = router->semantic;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    for (index = 0u; index < 2u; ++index) {
        present = children[index] != NULL ? 1u : 0u;
        blake3_hasher_update(&hasher, &present, sizeof(present));
        if (children[index] != NULL) {
            blake3_hasher_update(
                &hasher,
                children[index]->provider_fingerprint.bytes,
                sizeof(children[index]->provider_fingerprint.bytes));
            firmware_hash_u64(
                &hasher,
                children[index]->maximum_candidate_records_per_expansion);
        }
    }
    firmware_hash_u64(
        &hasher, router->maximum_candidate_records_per_expansion);
    blake3_hasher_finalize(
        &hasher,
        router->provider_fingerprint.bytes,
        sizeof(router->provider_fingerprint.bytes));
}

static int firmware_router_enumerate(
    void* opaque,
    const laplace_observation_query_binding* binding,
    const laplace_id128* source_entity_ids,
    const laplace_query_search_state* frontier_states,
    const uint64_t* accumulated_costs,
    size_t frontier_state_count,
    laplace_cognition_observation_candidate* candidates,
    size_t candidate_capacity,
    size_t* candidate_count,
    laplace_cognition_observation_candidate_usage* usage) {
    laplace_pg_firmware_provider_router* router =
        (laplace_pg_firmware_provider_router*)opaque;
    const laplace_cognition_observation_candidate_provider_v1* selected = NULL;
    const uint32_t semantic_mask = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    const uint32_t structural_mask =
        LAPLACE_OBSERVATION_QUERY_RELATION_MASK & ~semantic_mask;
    const bool wants_semantic =
        binding != NULL && (binding->relation_mask & semantic_mask) != 0u;
    const bool wants_structural =
        binding != NULL && (binding->relation_mask & structural_mask) != 0u;

    if (candidate_count != NULL) {
        *candidate_count = 0u;
    }
    if (usage != NULL) {
        memset(usage, 0, sizeof(*usage));
    }
    if (router == NULL || binding == NULL || candidate_count == NULL ||
        usage == NULL ||
        (binding->relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) != 0u ||
        (wants_semantic && wants_structural)) {
        return 1;
    }
    if (!wants_semantic && !wants_structural) {
        return 0;
    }

    selected = wants_semantic ? router->semantic : router->physical;
    if (selected == NULL) {
        usage->limiting_disposition =
            LAPLACE_QUERY_SEARCH_DISPOSITION_UNSUPPORTED;
        return 0;
    }
    return selected->enumerate_candidates(
        selected->state,
        binding,
        source_entity_ids,
        frontier_states,
        accumulated_costs,
        frontier_state_count,
        candidates,
        candidate_capacity,
        candidate_count,
        usage);
}

static void firmware_router_create(
    const laplace_cognition_observation_candidate_provider_v1* physical,
    const laplace_cognition_observation_candidate_provider_v1* semantic,
    laplace_pg_firmware_provider_router* router,
    laplace_cognition_observation_candidate_provider_v1* provider) {
    uint64_t maximum = 0u;

    memset(router, 0, sizeof(*router));
    memset(provider, 0, sizeof(*provider));
    router->physical = physical;
    router->semantic = semantic;
    if (physical != NULL) {
        maximum = physical->maximum_candidate_records_per_expansion;
    }
    if (semantic != NULL &&
        semantic->maximum_candidate_records_per_expansion > maximum) {
        maximum = semantic->maximum_candidate_records_per_expansion;
    }
    router->maximum_candidate_records_per_expansion = maximum;
    firmware_router_identify(router);

    provider->state = router;
    provider->provider_fingerprint = router->provider_fingerprint;
    provider->maximum_candidate_records_per_expansion = maximum;
    provider->enumerate_candidates = firmware_router_enumerate;
    provider->abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider->abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
}

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
    laplace_cognition_observation_candidate_provider_v1 structural;
    laplace_pg_firmware_provider_router router;
    laplace_pg_cognition_provider* physical_owner = NULL;
    laplace_pg_semantic_provider_state* semantic_owner = NULL;
    laplace_pg_semantic_provider_report semantic_reads;
    ErrorData* physical_error = NULL;
    ErrorData* semantic_error = NULL;
    laplace_digest256 program_id;
    laplace_cognition_firmware_status status;
    uint32_t physical_relation_mask = 0u;
    uint32_t step;
    bool need_physical = false;
    bool need_semantic = false;
    bool spi_connected = false;
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
    memset(&physical_provider, 0, sizeof(physical_provider));
    memset(&semantic_provider, 0, sizeof(semantic_provider));

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

    /* A step has one finite provider-family route. Physicality and semantic
     * relations may both appear in one firmware program, but a single step may
     * not flatten both classes into one unpartitioned candidate budget. */
    for (step = 0u; step < program->step_count; ++step) {
        const uint32_t relation_mask = program->steps[step].relation_mask;
        const bool step_semantic = (relation_mask & semantic_mask) != 0u;
        const uint32_t step_structural = relation_mask & structural_mask;
        if ((relation_mask & ~LAPLACE_OBSERVATION_QUERY_RELATION_MASK) != 0u ||
            (step_semantic && step_structural != 0u)) {
            return firmware_host_error(
                error,
                LAPLACE_COGNITION_FIRMWARE_PROVIDER_CONTRACT,
                step,
                0u);
        }
        if (step_semantic) {
            need_semantic = true;
        }
        if (step_structural != 0u) {
            need_physical = true;
            physical_relation_mask |= step_structural;
        }
    }

    if (laplace_cognition_prompt_admission_view_get(admission, &input) !=
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE, UINT32_MAX, 0u);
    /* The native program also composes the admitted prompt structure. Reserve
     * its advertised candidate bound before granting the remainder to durable
     * providers. The router invokes at most one durable provider for each step,
     * so structural and semantic routes reuse one conserved candidate budget. */
    if (laplace_cognition_prompt_admission_structural_provider(admission, &structural) !=
        LAPLACE_COGNITION_PROMPT_ADMISSION_OK)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_ADMISSION_FAILURE, UINT32_MAX, 0u);
    if (structural.maximum_candidate_records_per_expansion >=
        request->search_budget.transition_batch_capacity)
        return firmware_host_error(error, LAPLACE_COGNITION_FIRMWARE_LIMIT, UINT32_MAX, 0u);

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
    scope.search_budget.transition_batch_capacity -=
        (uint32_t)structural.maximum_candidate_records_per_expansion;
    scope.forward_limits = request->forward_limits;
    scope.maximum_results = 1u;
    scope.flags = request->boundary_flags |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    scope.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;

    if (need_physical) {
        scope.relation_mask = physical_relation_mask;
        laplace_pg_cognition_provider_create(
            &scope,
            provider_workspace_bytes,
            &physical_owner,
            &physical_provider);
    }
    if (need_semantic) {
        semantic_scope = scope;
        semantic_scope.relation_mask = semantic_mask;
        laplace_pg_semantic_provider_create(
            &semantic_scope,
            (uint64_t)scope.search_budget.transition_batch_capacity,
            &semantic_owner,
            &semantic_provider);
    }

    if (need_physical || need_semantic) {
        firmware_router_create(
            need_physical ? &physical_provider : NULL,
            need_semantic ? &semantic_provider : NULL,
            &router,
            &provider);
        if (provider.maximum_candidate_records_per_expansion == 0u ||
            provider.maximum_candidate_records_per_expansion >
                scope.search_budget.transition_batch_capacity) {
            laplace_pg_cognition_provider_destroy(&physical_owner);
            laplace_pg_semantic_provider_destroy(&semantic_owner);
            return firmware_host_error(
                error,
                LAPLACE_COGNITION_FIRMWARE_LIMIT,
                UINT32_MAX,
                0u);
        }
        if (SPI_connect() != SPI_OK_CONNECT) {
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
        (need_physical || need_semantic) ? &provider : NULL,
        (need_physical || need_semantic) ? 1u : 0u,
        realization,
        materialization,
        cancel,
        cancel_state,
        provider_workspace_bytes,
        result,
        error);

    /* No C++ stack remains when PostgreSQL diagnostics are retrieved. SPI_finish
     * cannot leave a returned native result orphaned if the host itself errors. */
    PG_TRY();
    {
        if (spi_connected && SPI_finish() != SPI_OK_FINISH)
            ereport(ERROR, (errmsg("Laplace firmware provider SPI cleanup failed")));
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
