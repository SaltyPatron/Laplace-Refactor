#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"

#include "blake3.h"
#include "laplace/cognition_forward_pass.h"
#include "laplace/cognition_observation_request.h"
#include "laplace/framework.h"
#include "laplace/observation_query.h"
#include "laplace_pg_internal.h"
#include "semantic_cognition_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_semantic_execute);

#define LAPLACE_PG_SEMANTIC_PROVIDER_DOMAIN \
    "laplace-postgresql-reference-mapping-candidate-provider-v1"

struct laplace_pg_semantic_provider_state {
    laplace_digest256 boundary_id;
    laplace_digest256 evidence_epoch;
    laplace_digest256 provider_fingerprint;
    uint64_t maximum_candidate_records_per_expansion;
    uint64_t rows_examined;
    uint64_t database_operations;
    uint64_t provider_calls;
    MemoryContext caller_context;
    MemoryContext scratch_context;
    ErrorData* error;
    MemoryContextCallback cleanup;
};

static void semantic_read_digest_attribute(
    HeapTupleHeader tuple,
    int attribute,
    laplace_digest256* digest,
    const char* field) {
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, attribute, field),
        digest, field);
}

static void semantic_read_id128_datum(
    Datum datum,
    laplace_id128* id,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(id->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace %s must contain exactly 16 bytes", field)));
    }
    memcpy(id->bytes, VARDATA_ANY(value), sizeof(id->bytes));
}

static void semantic_read_id128_attribute(
    HeapTupleHeader tuple,
    int attribute,
    laplace_id128* id,
    const char* field) {
    semantic_read_id128_datum(
        laplace_pg_required_composite_attribute(tuple, attribute, field),
        id, field);
}

static void semantic_read_digest_datum(
    Datum datum,
    laplace_digest256* digest,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(digest->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace %s must contain exactly 32 bytes", field)));
    }
    memcpy(digest->bytes, VARDATA_ANY(value), sizeof(digest->bytes));
}

static uint32_t semantic_read_u32_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    const int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace %s cannot be negative", field)));
    }
    return (uint32_t)value;
}

static uint64_t semantic_read_u64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static void semantic_read_search_budget(
    HeapTupleHeader tuple,
    laplace_query_search_budget* budget) {
    memset(budget, 0, sizeof(*budget));
    budget->max_expanded_states = semantic_read_u64_attribute(
        tuple, 1, "semantic search max_expanded_states");
    budget->max_transition_records = semantic_read_u64_attribute(
        tuple, 2, "semantic search max_transition_records");
    budget->max_emitted_states = semantic_read_u64_attribute(
        tuple, 3, "semantic search max_emitted_states");
    budget->max_frontier_states = semantic_read_u64_attribute(
        tuple, 4, "semantic search max_frontier_states");
    budget->max_memory_bytes = semantic_read_u64_attribute(
        tuple, 5, "semantic search max_memory_bytes");
    budget->max_io_operations = semantic_read_u64_attribute(
        tuple, 6, "semantic search max_io_operations");
    budget->max_database_operations = semantic_read_u64_attribute(
        tuple, 7, "semantic search max_database_operations");
    budget->max_provider_calls = semantic_read_u64_attribute(
        tuple, 8, "semantic search max_provider_calls");
    budget->max_depth = semantic_read_u32_attribute(
        tuple, 9, "semantic search max_depth");
    budget->requested_path_count = semantic_read_u32_attribute(
        tuple, 10, "semantic search requested_path_count");
    budget->frontier_batch_width = semantic_read_u32_attribute(
        tuple, 11, "semantic search frontier_batch_width");
    budget->transition_batch_capacity = semantic_read_u32_attribute(
        tuple, 12, "semantic search transition_batch_capacity");
}

static void semantic_read_forward_limits(
    HeapTupleHeader tuple,
    laplace_cognition_observation_forward_limits* limits) {
    memset(limits, 0, sizeof(*limits));
    limits->max_layers = semantic_read_u64_attribute(
        tuple, 1, "semantic forward max_layers");
    limits->max_provider_calls = semantic_read_u64_attribute(
        tuple, 2, "semantic forward max_provider_calls");
    limits->max_projected_queries = semantic_read_u64_attribute(
        tuple, 3, "semantic forward max_projected_queries");
    limits->max_candidate_operations = semantic_read_u64_attribute(
        tuple, 4, "semantic forward max_candidate_operations");
    limits->max_resolutions = semantic_read_u64_attribute(
        tuple, 5, "semantic forward max_resolutions");
    limits->max_resource_cost = semantic_read_u64_attribute(
        tuple, 6, "semantic forward max_resource_cost");
    limits->max_io_operations = semantic_read_u64_attribute(
        tuple, 7, "semantic forward max_io_operations");
    limits->max_database_operations = semantic_read_u64_attribute(
        tuple, 8, "semantic forward max_database_operations");
    limits->candidate_operation_capacity = semantic_read_u32_attribute(
        tuple, 9, "semantic forward candidate_operation_capacity");
    limits->resolution_capacity = semantic_read_u32_attribute(
        tuple, 10, "semantic forward resolution_capacity");
}

static void semantic_read_request(
    HeapTupleHeader tuple,
    laplace_cognition_observation_request* request) {
    HeapTupleHeader search_tuple;
    HeapTupleHeader forward_tuple;

    memset(request, 0, sizeof(*request));
    semantic_read_id128_attribute(
        tuple, 1, &request->anchor_entity_id,
        "semantic request anchor_entity_id");
    semantic_read_id128_attribute(
        tuple, 2, &request->goal_entity_id,
        "semantic request goal_entity_id");
    semantic_read_digest_attribute(
        tuple, 3, &request->world_id, "semantic request world_id");
    semantic_read_digest_attribute(
        tuple, 4, &request->time_fingerprint,
        "semantic request time_fingerprint");
    semantic_read_digest_attribute(
        tuple, 5, &request->context_fingerprint,
        "semantic request context_fingerprint");
    semantic_read_digest_attribute(
        tuple, 6, &request->evidence_boundary,
        "semantic request evidence_boundary");
    semantic_read_digest_attribute(
        tuple, 7, &request->evidence_epoch,
        "semantic request evidence_epoch");
    semantic_read_digest_attribute(
        tuple, 8, &request->authority_id,
        "semantic request authority_id");
    semantic_read_digest_attribute(
        tuple, 9, &request->result_contract_fingerprint,
        "semantic request result_contract_fingerprint");

    search_tuple = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(
            tuple, 10, "semantic request search_budget"));
    forward_tuple = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(
            tuple, 11, "semantic request forward_limits"));
    semantic_read_search_budget(search_tuple, &request->search_budget);
    semantic_read_forward_limits(forward_tuple, &request->forward_limits);

    request->relation_mask = semantic_read_u32_attribute(
        tuple, 12, "semantic request relation_mask");
    request->maximum_results = semantic_read_u32_attribute(
        tuple, 13, "semantic request maximum_results");
    request->flags = semantic_read_u32_attribute(
        tuple, 14, "semantic request flags");
    request->version = semantic_read_u32_attribute(
        tuple, 15, "semantic request version");
}

static void semantic_provider_identify(
    const laplace_digest256* boundary_id,
    const laplace_digest256* evidence_epoch,
    laplace_digest256* provider_fingerprint) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher,
        LAPLACE_PG_SEMANTIC_PROVIDER_DOMAIN,
        sizeof(LAPLACE_PG_SEMANTIC_PROVIDER_DOMAIN) - 1u);
    blake3_hasher_update(
        &hasher, boundary_id->bytes, sizeof(boundary_id->bytes));
    blake3_hasher_update(
        &hasher, evidence_epoch->bytes, sizeof(evidence_epoch->bytes));
    blake3_hasher_finalize(
        &hasher, provider_fingerprint->bytes,
        sizeof(provider_fingerprint->bytes));
}

static bool semantic_add_u64(uint64_t* total, uint64_t value) {
    if (*total > UINT64_MAX - value) {
        return false;
    }
    *total += value;
    return true;
}

static int semantic_enumerate_impl(
    laplace_pg_semantic_provider_state* state,
    const laplace_observation_query_binding* binding,
    const laplace_id128* source_entity_ids,
    const laplace_query_search_state* frontier_states,
    const uint64_t* accumulated_costs,
    size_t frontier_state_count,
    laplace_cognition_observation_candidate* candidates,
    size_t candidate_capacity,
    size_t* candidate_count,
    laplace_cognition_observation_candidate_usage* usage) {
    static const char query[] =
        "WITH src AS MATERIALIZED ("
        " SELECT entity_id, ordinality - 1 AS source_state_index"
        " FROM unnest($1::bytea[]) WITH ORDINALITY s(entity_id, ordinality)"
        "), edges AS MATERIALIZED ("
        " SELECT (s.source_state_index)::bigint AS source_state_index,"
        " CASE WHEN o.left_value_entity_id=s.entity_id"
        "      THEN o.right_value_entity_id ELSE o.left_value_entity_id END AS target_entity_id,"
        " o.occurrence_id AS observation_fingerprint,"
        " p.proposition_id, p.relation_id, er.root_node_id AS evidence_root_id,"
        " CASE WHEN p.flags=2 THEN 3"
        "      WHEN o.left_value_entity_id=s.entity_id THEN 1 ELSE 2 END AS direction"
        " FROM src s"
        " JOIN " LAPLACE_PG_SCHEMA ".reference_mapping_occurrence o"
        "   ON o.boundary_id=$2"
        "  AND o.disposition=1"
        "  AND (o.left_value_entity_id=s.entity_id OR o.right_value_entity_id=s.entity_id)"
        " JOIN " LAPLACE_PG_SCHEMA ".reference_mapping_proposition p"
        "   ON p.proposition_id=o.proposition_id"
        " JOIN " LAPLACE_PG_SCHEMA ".evidence_testimony et"
        "   ON et.source_profile_id=o.source_profile_id"
        " JOIN " LAPLACE_PG_SCHEMA ".evidence_node en"
        "   ON en.node_id=et.evidence_node_id"
        "  AND en.proposition_id=o.row_entity_id"
        " JOIN " LAPLACE_PG_SCHEMA ".evidence_root_projection er"
        "   ON er.node_id=en.node_id"
        "  AND er.proposition_id=en.proposition_id"
        " WHERE p.flags IN (1,2)"
        "), dedup AS ("
        " SELECT DISTINCT ON (source_state_index, proposition_id, target_entity_id, evidence_root_id)"
        " source_state_index,target_entity_id,observation_fingerprint,relation_id,evidence_root_id,direction"
        " FROM edges"
        " ORDER BY source_state_index,proposition_id,target_entity_id,evidence_root_id,observation_fingerprint"
        ")"
        " SELECT source_state_index,target_entity_id,observation_fingerprint,relation_id,evidence_root_id,direction"
        " FROM dedup"
        " ORDER BY source_state_index,observation_fingerprint,target_entity_id,evidence_root_id";
    Datum* source_values;
    ArrayType* source_array;
    bytea* boundary;
    Oid argument_types[2];
    Datum argument_values[2];
    size_t source_index;
    uint64_t processed;
    uint64_t provider_limit;
    size_t effective_capacity;
    long row_limit;
    int result;

    if (state == NULL || binding == NULL || source_entity_ids == NULL ||
        frontier_states == NULL || accumulated_costs == NULL ||
        frontier_state_count == 0u || candidates == NULL ||
        candidate_capacity == 0u || candidate_count == NULL || usage == NULL) {
        return 1;
    }

    *candidate_count = 0u;
    memset(usage, 0, sizeof(*usage));
    if ((binding->relation_mask & LAPLACE_OBSERVATION_QUERY_SEMANTIC) == 0u) {
        return 0;
    }
    if (frontier_state_count > (size_t)INT_MAX ||
        state->maximum_candidate_records_per_expansion >
            UINT64_MAX / (uint64_t)frontier_state_count) {
        return 2;
    }
    provider_limit = state->maximum_candidate_records_per_expansion *
        (uint64_t)frontier_state_count;
    effective_capacity = candidate_capacity;
    if (provider_limit < (uint64_t)effective_capacity) {
        effective_capacity = (size_t)provider_limit;
    }
    if (effective_capacity == 0u) {
        usage->limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
        return 0;
    }
    row_limit = effective_capacity >= (size_t)LONG_MAX
        ? LONG_MAX
        : (long)effective_capacity + 1L;

    source_values = (Datum*)palloc(sizeof(*source_values) * frontier_state_count);
    for (source_index = 0u; source_index < frontier_state_count; ++source_index) {
        source_values[source_index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            source_entity_ids[source_index].bytes,
            sizeof(source_entity_ids[source_index].bytes)));
        (void)frontier_states[source_index];
        (void)accumulated_costs[source_index];
    }
    source_array = construct_array(
        source_values, (int)frontier_state_count, BYTEAOID, -1, false, TYPALIGN_INT);
    boundary = laplace_pg_bytes_to_bytea(
        state->boundary_id.bytes, sizeof(state->boundary_id.bytes));

    argument_types[0] = get_array_type(BYTEAOID);
    argument_types[1] = BYTEAOID;
    if (argument_types[0] == InvalidOid) {
        return 3;
    }
    argument_values[0] = PointerGetDatum(source_array);
    argument_values[1] = PointerGetDatum(boundary);

    result = SPI_execute_with_args(
        query, 2, argument_types, argument_values, NULL, true, row_limit);
    if (result != SPI_OK_SELECT || SPI_tuptable == NULL) {
        return 4;
    }
    processed = (uint64_t)SPI_processed;
    if (!semantic_add_u64(&state->rows_examined, processed) ||
        !semantic_add_u64(&state->database_operations, 1u) ||
        !semantic_add_u64(&state->provider_calls, 1u)) {
        return 5;
    }

    usage->rows_examined = processed;
    usage->index_plan_count = 1u;
    usage->database_operations = 1u;
    if (SPI_processed > effective_capacity) {
        usage->limiting_disposition = LAPLACE_QUERY_SEARCH_DISPOSITION_UNKNOWN;
        SPI_freetuptable(SPI_tuptable);
        return 0;
    }

    for (source_index = 0u; source_index < (size_t)SPI_processed; ++source_index) {
        HeapTuple tuple = SPI_tuptable->vals[source_index];
        TupleDesc tuple_desc = SPI_tuptable->tupdesc;
        laplace_cognition_observation_candidate* candidate = &candidates[source_index];
        bool is_null = false;
        Datum value;
        int64 state_index;
        int32 direction;

        memset(candidate, 0, sizeof(*candidate));
        value = SPI_getbinval(tuple, tuple_desc, 1, &is_null);
        if (is_null) return 6;
        state_index = DatumGetInt64(value);
        if (state_index < 0 || (uint64_t)state_index >= frontier_state_count) {
            return 7;
        }
        candidate->source_state_index = (uint64_t)state_index;

        value = SPI_getbinval(tuple, tuple_desc, 2, &is_null);
        if (is_null) return 8;
        semantic_read_id128_datum(
            value, &candidate->target_entity_id, "semantic target_entity_id");

        value = SPI_getbinval(tuple, tuple_desc, 3, &is_null);
        if (is_null) return 9;
        semantic_read_digest_datum(
            value, &candidate->observation_fingerprint,
            "semantic observation_fingerprint");

        value = SPI_getbinval(tuple, tuple_desc, 4, &is_null);
        if (is_null) return 10;
        semantic_read_id128_datum(
            value, &candidate->relation_id, "semantic relation_id");

        value = SPI_getbinval(tuple, tuple_desc, 5, &is_null);
        if (is_null) return 11;
        semantic_read_digest_datum(
            value, &candidate->evidence_root_fingerprint,
            "semantic evidence_root_id");

        value = SPI_getbinval(tuple, tuple_desc, 6, &is_null);
        if (is_null) return 12;
        direction = DatumGetInt32(value);
        if (direction < 0) return 13;

        candidate->source_logical_ordinal = 0u;
        candidate->target_logical_ordinal = 0u;
        candidate->multiplicity = 1u;
        candidate->gap = 1u;
        candidate->relation_family = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
        candidate->source_layer = LAPLACE_OBSERVATION_QUERY_SOURCE_TESTIMONY;
        candidate->direction = (uint32_t)direction;
        candidate->flags =
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_RELATION_ID_PRESENT;
    }

    *candidate_count = (size_t)SPI_processed;
    usage->crossing_count = processed;
    SPI_freetuptable(SPI_tuptable);
    return 0;
}

static int semantic_enumerate_candidates(
    void* provider_state,
    const laplace_observation_query_binding* binding,
    const laplace_id128* source_entity_ids,
    const laplace_query_search_state* frontier_states,
    const uint64_t* accumulated_costs,
    size_t frontier_state_count,
    laplace_cognition_observation_candidate* candidates,
    size_t candidate_capacity,
    size_t* candidate_count,
    laplace_cognition_observation_candidate_usage* usage) {
    laplace_pg_semantic_provider_state* state =
        (laplace_pg_semantic_provider_state*)provider_state;
    volatile int status = 0;
    MemoryContext previous;

    if (candidate_count != NULL) {
        *candidate_count = 0u;
    }
    if (usage != NULL) {
        memset(usage, 0, sizeof(*usage));
    }
    if (state == NULL || state->error != NULL || state->scratch_context == NULL) {
        return 1;
    }

    previous = MemoryContextSwitchTo(state->scratch_context);
    PG_TRY();
    {
        status = semantic_enumerate_impl(
            state, binding, source_entity_ids, frontier_states, accumulated_costs,
            frontier_state_count, candidates, candidate_capacity,
            candidate_count, usage);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(state->caller_context);
        state->error = CopyErrorData();
        FlushErrorState();
        if (candidate_count != NULL) {
            *candidate_count = 0u;
        }
        if (usage != NULL) {
            memset(usage, 0, sizeof(*usage));
        }
        status = 1;
    }
    PG_END_TRY();
    MemoryContextSwitchTo(previous);
    MemoryContextReset(state->scratch_context);
    return (int)status;
}

static void semantic_provider_release(void* opaque) {
    laplace_pg_semantic_provider_state* state =
        (laplace_pg_semantic_provider_state*)opaque;
    if (state == NULL) {
        return;
    }
    state->scratch_context = NULL;
    if (state->error != NULL) {
        FreeErrorData(state->error);
        state->error = NULL;
    }
}

void laplace_pg_semantic_provider_create(
    const laplace_cognition_observation_request* request,
    uint64_t maximum_candidate_records_per_expansion,
    laplace_pg_semantic_provider_state** owner,
    laplace_cognition_observation_candidate_provider_v1* provider) {
    laplace_digest256 request_fingerprint;
    laplace_pg_semantic_provider_state* state;

    if (owner != NULL) {
        *owner = NULL;
    }
    if (provider != NULL) {
        memset(provider, 0, sizeof(*provider));
    }
    if (request == NULL || owner == NULL || provider == NULL ||
        maximum_candidate_records_per_expansion == 0u ||
        maximum_candidate_records_per_expansion >
            request->search_budget.transition_batch_capacity ||
        (request->relation_mask & LAPLACE_OBSERVATION_QUERY_SEMANTIC) == 0u ||
        laplace_cognition_observation_request_identify(
            request, &request_fingerprint) !=
            LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace semantic candidate provider request is invalid")));
    }

    state = (laplace_pg_semantic_provider_state*)palloc0(sizeof(*state));
    state->boundary_id = request->evidence_boundary;
    state->evidence_epoch = request->evidence_epoch;
    state->maximum_candidate_records_per_expansion =
        maximum_candidate_records_per_expansion;
    state->caller_context = CurrentMemoryContext;
    state->scratch_context = AllocSetContextCreate(
        CurrentMemoryContext,
        "Laplace semantic cognition batch",
        ALLOCSET_DEFAULT_SIZES);
    state->cleanup.func = semantic_provider_release;
    state->cleanup.arg = state;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &state->cleanup);
    semantic_provider_identify(
        &state->boundary_id,
        &state->evidence_epoch,
        &state->provider_fingerprint);

    provider->state = state;
    provider->provider_fingerprint = state->provider_fingerprint;
    provider->maximum_candidate_records_per_expansion =
        maximum_candidate_records_per_expansion;
    provider->enumerate_candidates = semantic_enumerate_candidates;
    provider->abi_major =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider->abi_minor =
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    *owner = state;
}

void laplace_pg_semantic_provider_summary(
    const laplace_pg_semantic_provider_state* owner,
    laplace_pg_semantic_provider_report* report) {
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
    if (owner == NULL) {
        return;
    }
    report->provider_fingerprint = owner->provider_fingerprint;
    report->rows_examined = owner->rows_examined;
    report->database_operations = owner->database_operations;
    report->provider_calls = owner->provider_calls;
}

ErrorData* laplace_pg_semantic_provider_take_error(
    laplace_pg_semantic_provider_state* owner) {
    ErrorData* error;
    if (owner == NULL) {
        return NULL;
    }
    error = owner->error;
    owner->error = NULL;
    return error;
}

void laplace_pg_semantic_provider_destroy(
    laplace_pg_semantic_provider_state** owner) {
    if (owner == NULL || *owner == NULL) {
        return;
    }
    if ((*owner)->scratch_context != NULL) {
        MemoryContextDelete((*owner)->scratch_context);
        (*owner)->scratch_context = NULL;
    }
    semantic_provider_release(*owner);
    *owner = NULL;
}

Datum laplace_pg_cognition_semantic_execute(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_cognition_observation_request request;
    laplace_cognition_observation_candidate_provider_v1 provider;
    laplace_pg_semantic_provider_state* provider_state = NULL;
    laplace_pg_semantic_provider_report provider_report;
    ErrorData* provider_error = NULL;
    laplace_cognition_observation_result* observation_result = NULL;
    laplace_cognition_forward_result* forward_result = NULL;
    laplace_cognition_forward_receipt forward_receipt;
    laplace_cognition_observation_answer primary_answer;
    laplace_digest256 request_fingerprint;
    laplace_cognition_observation_request_status request_status;
    size_t answer_count;
    Datum result_values[21];
    bool result_nulls[21] = {false};
    HeapTuple result_tuple;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    semantic_read_request(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &request);

    if (request.relation_mask != LAPLACE_OBSERVATION_QUERY_SEMANTIC) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace live semantic cognition requires the exact semantic relation family"),
                 errdetail("relation_mask=%u", request.relation_mask)));
    }
    if (request.search_budget.max_memory_bytes > context.resource_grant.memory_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace live semantic cognition search memory exceeds the execution-context grant")));
    }

    memset(&request_fingerprint, 0, sizeof(request_fingerprint));
    request_status = laplace_cognition_observation_request_identify(
        &request, &request_fingerprint);
    if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace live semantic cognition request identification failed"),
                 errdetail("request_status=%d", (int)request_status)));
    }

    laplace_pg_semantic_provider_create(
        &request,
        (uint64_t)request.search_budget.transition_batch_capacity,
        &provider_state,
        &provider);

    if (SPI_connect() != SPI_OK_CONNECT) {
        laplace_pg_semantic_provider_destroy(&provider_state);
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace live semantic cognition could not connect to the durable mapping estate")));
    }

    memset(&forward_receipt, 0, sizeof(forward_receipt));
    request_status = laplace_cognition_observation_request_execute_with_candidate_provider(
        &request, &provider, &observation_result, &forward_result, &forward_receipt);

    if (SPI_finish() != SPI_OK_FINISH) {
        laplace_cognition_observation_result_destroy(&observation_result);
        laplace_cognition_forward_result_destroy(&forward_result);
        laplace_pg_semantic_provider_destroy(&provider_state);
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace live semantic cognition could not close its durable mapping read")));
    }

    laplace_pg_semantic_provider_summary(provider_state, &provider_report);
    provider_error = laplace_pg_semantic_provider_take_error(provider_state);
    laplace_pg_semantic_provider_destroy(&provider_state);
    if (provider_error != NULL) {
        laplace_cognition_observation_result_destroy(&observation_result);
        laplace_cognition_forward_result_destroy(&forward_result);
        ReThrowError(provider_error);
    }

    if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK ||
        observation_result == NULL || forward_result == NULL) {
        laplace_cognition_observation_result_destroy(&observation_result);
        laplace_cognition_forward_result_destroy(&forward_result);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace live semantic cognition execution failed"),
                 errdetail("request_status=%d", (int)request_status)));
    }

    answer_count = laplace_cognition_observation_result_answer_count(
        observation_result);
    memset(&primary_answer, 0, sizeof(primary_answer));
    if (answer_count == 0u ||
        laplace_cognition_observation_result_answer(
            observation_result, 0u, &primary_answer) !=
            LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        laplace_cognition_observation_result_destroy(&observation_result);
        laplace_cognition_forward_result_destroy(&forward_result);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace live semantic cognition produced no readable terminal answer")));
    }

    result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        request_fingerprint.bytes, sizeof(request_fingerprint.bytes)));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        provider_report.provider_fingerprint.bytes,
        sizeof(provider_report.provider_fingerprint.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        primary_answer.entity_id.bytes, sizeof(primary_answer.entity_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        primary_answer.relation_id.bytes, sizeof(primary_answer.relation_id.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        primary_answer.path_id.bytes, sizeof(primary_answer.path_id.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        primary_answer.terminal_state_id.bytes,
        sizeof(primary_answer.terminal_state_id.bytes)));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        forward_receipt.receipt_id.bytes, sizeof(forward_receipt.receipt_id.bytes)));
    result_values[7] = laplace_pg_numeric_from_uint64((uint64_t)answer_count);
    result_values[8] = laplace_pg_numeric_from_uint64(primary_answer.total_cost);
    result_values[9] = laplace_pg_numeric_from_uint64(primary_answer.transition_count);
    result_values[10] = laplace_pg_numeric_from_uint64(
        primary_answer.independent_evidence_root_count);
    result_values[11] = Int32GetDatum((int32)primary_answer.relation_family);
    result_values[12] = Int32GetDatum((int32)primary_answer.source_layer);
    result_values[13] = Int32GetDatum((int32)primary_answer.direction);
    result_values[14] = Int32GetDatum((int32)primary_answer.rank);
    result_values[15] = laplace_pg_numeric_from_uint64(provider_report.rows_examined);
    result_values[16] = laplace_pg_numeric_from_uint64(
        provider_report.database_operations);
    result_values[17] = laplace_pg_numeric_from_uint64(
        forward_receipt.database_operations);
    result_values[18] = Int32GetDatum((int32)forward_receipt.final_completion);
    result_values[19] = Int32GetDatum((int32)forward_receipt.disposition);
    result_values[20] = Int32GetDatum((int32)forward_receipt.status);

    laplace_cognition_observation_result_destroy(&observation_result);
    laplace_cognition_forward_result_destroy(&forward_result);

    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 21);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
