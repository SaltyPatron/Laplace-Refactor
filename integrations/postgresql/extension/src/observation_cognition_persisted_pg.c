#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/array.h"
#include "utils/memutils.h"
#include "utils/lsyscache.h"
#include "utils/typcache.h"
#include "blake3.h"

#include "laplace/cognition_forward_pass.h"
#include "laplace/cognition_guidance.h"
#include "laplace/cognition_observation_request.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/framework.h"
#include "laplace/observation_query.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"
#include "cognition_provider_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_observation_execute_persisted);
PG_FUNCTION_INFO_V1(laplace_pg_trajectory_entity_ids);

static Datum persisted_required_attribute(
    HeapTuple tuple,
    TupleDesc descriptor,
    int attribute,
    const char* field) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, attribute, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                 errmsg("Laplace persisted cognition %s cannot be null", field)));
    }
    return value;
}

static void persisted_read_digest_datum(
    Datum datum,
    laplace_digest256* digest,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(digest->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace persisted cognition %s must contain exactly 32 bytes", field)));
    }
    memcpy(digest->bytes, VARDATA_ANY(value), sizeof(digest->bytes));
}

static void persisted_read_id_datum(
    Datum datum,
    laplace_id128* id,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(id->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace persisted cognition %s must contain exactly 16 bytes", field)));
    }
    memcpy(id->bytes, VARDATA_ANY(value), sizeof(id->bytes));
}

static uint32_t persisted_read_u32_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    const int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace persisted cognition %s cannot be negative", field)));
    }
    return (uint32_t)value;
}

static uint64_t persisted_read_u64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static void persisted_read_search_budget(
    HeapTupleHeader tuple,
    laplace_query_search_budget* budget) {
    memset(budget, 0, sizeof(*budget));
    budget->max_expanded_states = persisted_read_u64_attribute(
        tuple, 1, "search max_expanded_states");
    budget->max_transition_records = persisted_read_u64_attribute(
        tuple, 2, "search max_transition_records");
    budget->max_emitted_states = persisted_read_u64_attribute(
        tuple, 3, "search max_emitted_states");
    budget->max_frontier_states = persisted_read_u64_attribute(
        tuple, 4, "search max_frontier_states");
    budget->max_memory_bytes = persisted_read_u64_attribute(
        tuple, 5, "search max_memory_bytes");
    budget->max_io_operations = persisted_read_u64_attribute(
        tuple, 6, "search max_io_operations");
    budget->max_database_operations = persisted_read_u64_attribute(
        tuple, 7, "search max_database_operations");
    budget->max_provider_calls = persisted_read_u64_attribute(
        tuple, 8, "search max_provider_calls");
    budget->max_depth = persisted_read_u32_attribute(
        tuple, 9, "search max_depth");
    budget->requested_path_count = persisted_read_u32_attribute(
        tuple, 10, "search requested_path_count");
    budget->frontier_batch_width = persisted_read_u32_attribute(
        tuple, 11, "search frontier_batch_width");
    budget->transition_batch_capacity = persisted_read_u32_attribute(
        tuple, 12, "search transition_batch_capacity");
}

static void persisted_read_forward_limits(
    HeapTupleHeader tuple,
    laplace_cognition_observation_forward_limits* limits) {
    memset(limits, 0, sizeof(*limits));
    limits->max_layers = persisted_read_u64_attribute(
        tuple, 1, "forward max_layers");
    limits->max_provider_calls = persisted_read_u64_attribute(
        tuple, 2, "forward max_provider_calls");
    limits->max_projected_queries = persisted_read_u64_attribute(
        tuple, 3, "forward max_projected_queries");
    limits->max_candidate_operations = persisted_read_u64_attribute(
        tuple, 4, "forward max_candidate_operations");
    limits->max_resolutions = persisted_read_u64_attribute(
        tuple, 5, "forward max_resolutions");
    limits->max_resource_cost = persisted_read_u64_attribute(
        tuple, 6, "forward max_resource_cost");
    limits->max_io_operations = persisted_read_u64_attribute(
        tuple, 7, "forward max_io_operations");
    limits->max_database_operations = persisted_read_u64_attribute(
        tuple, 8, "forward max_database_operations");
    limits->candidate_operation_capacity = persisted_read_u32_attribute(
        tuple, 9, "forward candidate_operation_capacity");
    limits->resolution_capacity = persisted_read_u32_attribute(
        tuple, 10, "forward resolution_capacity");
}

static void persisted_read_request(
    HeapTupleHeader tuple,
    laplace_cognition_observation_request* request) {
    HeapTupleHeader search_tuple;
    HeapTupleHeader forward_tuple;

    memset(request, 0, sizeof(*request));
    persisted_read_id_datum(
        laplace_pg_required_composite_attribute(tuple, 1, "anchor_entity_id"),
        &request->anchor_entity_id, "anchor_entity_id");
    persisted_read_id_datum(
        laplace_pg_required_composite_attribute(tuple, 2, "goal_entity_id"),
        &request->goal_entity_id, "goal_entity_id");
    persisted_read_digest_datum(
        laplace_pg_required_composite_attribute(tuple, 3, "world_id"),
        &request->world_id, "world_id");
    persisted_read_digest_datum(
        laplace_pg_required_composite_attribute(tuple, 4, "time_fingerprint"),
        &request->time_fingerprint, "time_fingerprint");
    persisted_read_digest_datum(
        laplace_pg_required_composite_attribute(tuple, 5, "context_fingerprint"),
        &request->context_fingerprint, "context_fingerprint");
    persisted_read_digest_datum(
        laplace_pg_required_composite_attribute(tuple, 6, "evidence_boundary"),
        &request->evidence_boundary, "evidence_boundary");
    persisted_read_digest_datum(
        laplace_pg_required_composite_attribute(tuple, 7, "evidence_epoch"),
        &request->evidence_epoch, "evidence_epoch");
    persisted_read_digest_datum(
        laplace_pg_required_composite_attribute(tuple, 8, "authority_id"),
        &request->authority_id, "authority_id");
    persisted_read_digest_datum(
        laplace_pg_required_composite_attribute(tuple, 9, "result_contract_fingerprint"),
        &request->result_contract_fingerprint, "result_contract_fingerprint");

    search_tuple = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(tuple, 10, "search_budget"));
    forward_tuple = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(tuple, 11, "forward_limits"));
    persisted_read_search_budget(search_tuple, &request->search_budget);
    persisted_read_forward_limits(forward_tuple, &request->forward_limits);

    request->relation_mask = persisted_read_u32_attribute(
        tuple, 12, "relation_mask");
    request->maximum_results = persisted_read_u32_attribute(
        tuple, 13, "maximum_results");
    request->flags = persisted_read_u32_attribute(tuple, 14, "flags");
    request->version = persisted_read_u32_attribute(tuple, 15, "version");
}

static void persisted_read_physicality_row(
    HeapTuple tuple,
    TupleDesc descriptor,
    laplace_persistence_physicality_record* value) {
    memset(value, 0, sizeof(*value));
    persisted_read_digest_datum(
        persisted_required_attribute(tuple, descriptor, 1, "physicality_id"),
        &value->physicality_id, "physicality_id");
    persisted_read_id_datum(
        persisted_required_attribute(tuple, descriptor, 2, "entity_id"),
        &value->entity_id, "entity_id");
    value->physicality_type = (uint32_t)DatumGetInt32(
        persisted_required_attribute(tuple, descriptor, 3, "physicality_type"));
    value->vertex_class = (uint32_t)DatumGetInt32(
        persisted_required_attribute(tuple, descriptor, 4, "vertex_class"));
    value->recipe_version = (uint32_t)DatumGetInt32(
        persisted_required_attribute(tuple, descriptor, 5, "recipe_version"));
    value->structural_form = (uint32_t)DatumGetInt32(
        persisted_required_attribute(tuple, descriptor, 6, "structural_form"));
    value->dimension_count = (uint32_t)DatumGetInt32(
        persisted_required_attribute(tuple, descriptor, 7, "dimension_count"));
    value->flags = (uint32_t)DatumGetInt32(
        persisted_required_attribute(tuple, descriptor, 8, "flags"));
    persisted_read_digest_datum(
        persisted_required_attribute(tuple, descriptor, 9, "recipe_fingerprint"),
        &value->recipe_fingerprint, "recipe_fingerprint");
    persisted_read_digest_datum(
        persisted_required_attribute(tuple, descriptor, 10, "geometry_epoch"),
        &value->geometry_epoch, "geometry_epoch");
    persisted_read_digest_datum(
        persisted_required_attribute(tuple, descriptor, 11, "trajectory_fingerprint"),
        &value->trajectory_fingerprint, "trajectory_fingerprint");
    value->centroid.component[0] = DatumGetFloat8(
        persisted_required_attribute(tuple, descriptor, 12, "centroid_x"));
    value->centroid.component[1] = DatumGetFloat8(
        persisted_required_attribute(tuple, descriptor, 13, "centroid_y"));
    value->centroid.component[2] = DatumGetFloat8(
        persisted_required_attribute(tuple, descriptor, 14, "centroid_z"));
    value->centroid.component[3] = DatumGetFloat8(
        persisted_required_attribute(tuple, descriptor, 15, "centroid_m"));
    value->radius = DatumGetFloat8(
        persisted_required_attribute(tuple, descriptor, 16, "radius"));
    value->logical_count = laplace_pg_uint64_from_numeric(
        persisted_required_attribute(tuple, descriptor, 17, "logical_count"),
        "persisted cognition logical_count");
    value->vertex_count = laplace_pg_uint64_from_numeric(
        persisted_required_attribute(tuple, descriptor, 18, "vertex_count"),
        "persisted cognition vertex_count");
}


/* GIN keys are a rebuildable structural projection of the canonical packed
 * trajectory. Keys never certify a crossing: each fetched record is subsequently
 * validated by the native physicality/index owner. No edge-pair table is stored. */
Datum laplace_pg_trajectory_entity_ids(PG_FUNCTION_ARGS) {
    bytea* payload = PG_GETARG_BYTEA_PP(0);
    size_t bytes = (size_t)VARSIZE_ANY_EXHDR(payload);
    size_t count;
    size_t i;
    uint64_t ordinal = 1u;
    Datum* ids;
    ArrayType* result;
    if (bytes % sizeof(laplace_trajectory_carrier) != 0u) {
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace trajectory key projection received a partial carrier")));
    }
    count = bytes / sizeof(laplace_trajectory_carrier);
    if (count == 0u) PG_RETURN_ARRAYTYPE_P(construct_empty_array(BYTEAOID));
    if (count > (size_t)INT_MAX || count > MaxAllocSize / sizeof(Datum)) {
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace trajectory key projection exceeds addressability")));
    }
    ids = palloc(count * sizeof(Datum));
    for (i = 0u; i < count; ++i) {
        laplace_trajectory_carrier carrier;
        laplace_composition_occurrence occurrence;
        memcpy(&carrier, VARDATA_ANY(payload) + i * sizeof(carrier), sizeof(carrier));
        if (laplace_trajectory_composition_decode_one(&carrier, ordinal, &occurrence) !=
                LAPLACE_TRAJECTORY_OK ||
            UINT64_MAX - ordinal < (uint64_t)occurrence.run_length) {
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace trajectory key projection received invalid canonical structure")));
        }
        ordinal += (uint64_t)occurrence.run_length;
        ids[i] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            occurrence.entity_id.bytes, sizeof(occurrence.entity_id.bytes)));
    }
    result = construct_array(ids, (int)count, BYTEAOID, -1, false, TYPALIGN_INT);
    PG_RETURN_ARRAYTYPE_P(result);
}

struct laplace_pg_cognition_provider {
    laplace_cognition_observation_request request_storage;
    const laplace_cognition_observation_request* request;
    uint64_t memory_limit;
    uint64_t rows_fetched;
    uint64_t carriers_decoded;
    uint64_t logical_occurrences;
    uint64_t indexed_entities;
    uint64_t trajectory_bytes;
    uint64_t database_operations;
    uint64_t batch_count;
    laplace_digest256 provider_fingerprint;
    blake3_hasher readset;
    MemoryContext caller_context;
    MemoryContext scratch_context;
    ErrorData* error;
    laplace_observation_query_index* index;
    MemoryContextCallback cleanup;
};
typedef struct laplace_pg_cognition_provider persisted_provider_state;

StaticAssertDecl(LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION == 1, "SQL physicality kind drift");
StaticAssertDecl(LAPLACE_OBSERVATION_QUERY_CONSTITUENT == 2, "SQL constituent mask drift");
StaticAssertDecl((LAPLACE_OBSERVATION_QUERY_CONTAINER | LAPLACE_OBSERVATION_QUERY_PREDECESSOR |
                  LAPLACE_OBSERVATION_QUERY_SUCCESSOR | LAPLACE_OBSERVATION_QUERY_COOCCUR) == 29,
                 "SQL member lookup mask drift");

static const char persisted_metadata_sql[] =
    "SELECT physicality_id, entity_id, physicality_type, vertex_class, "
    "recipe_version, structural_form, dimension_count, flags, "
    "recipe_fingerprint, geometry_epoch, trajectory_fingerprint, "
    "centroid_x, centroid_y, centroid_z, centroid_m, radius, logical_count, vertex_count, "
    "octet_length(trajectory) "
    "FROM " LAPLACE_PG_SCHEMA ".physicality AS p "
    "WHERE p.physicality_type=1 AND ((($2 & 2)<>0 AND p.entity_id=ANY($1)) "
    "OR (($2 & 29)<>0 AND " LAPLACE_PG_SCHEMA ".trajectory_entity_ids(p.trajectory) && $1)) "
    "ORDER BY p.physicality_id LIMIT $3";

static const char persisted_payload_sql[] =
    "SELECT physicality_id, trajectory FROM " LAPLACE_PG_SCHEMA ".physicality "
    "WHERE physicality_id=ANY($1) ORDER BY physicality_id";

static void persisted_limit(const char* reason) {
    ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
        errmsg("Laplace indexed persisted cognition resource limit: %s", reason)));
}

static uint64_t persisted_add(uint64_t a, uint64_t b) {
    if (UINT64_MAX - a < b) persisted_limit("counter overflow");
    return a + b;
}

static void persisted_charge_query(persisted_provider_state* state) {
    /* Reserve before executing, not after an unbounded provider has done work. */
    if (state->database_operations >= state->request->search_budget.max_database_operations ||
        state->database_operations >= state->request->forward_limits.max_database_operations)
        persisted_limit("database-operation budget exhausted");
    ++state->database_operations;
}

static int persisted_enumerate_impl(
    persisted_provider_state* state, const laplace_observation_query_binding* binding,
    const laplace_id128* source_ids, size_t source_count,
    laplace_cognition_observation_candidate* candidates, size_t capacity,
    size_t* count, laplace_cognition_observation_candidate_usage* usage) {
    Oid types[3] = {BYTEAARRAYOID, INT4OID, INT8OID};
    Datum args[3];
    Datum* source_values;
    Datum* physicality_values;
    uint64_t max_rows, candidate_workspace, frontier_workspace;
    uint64_t rows, row, vertices = 0u, encoded_bytes = 0u;
    uint64_t reserved;
    uint64_t before_operations = state->database_operations;
    laplace_persistence_physicality_record* records;
    laplace_persistence_trajectory_segment_record* segments;
    size_t segment_cursor = 0u;
    laplace_observation_query_index_base_input input;
    laplace_observation_query_index_summary summary;
    laplace_observation_query_status status;
    if (source_count == 0u) return 0;
    if (source_count > (size_t)INT_MAX ||
        source_count > MaxAllocSize / sizeof(Datum) ||
        (uint64_t)source_count > state->memory_limit / UINT64_C(128))
        persisted_limit("frontier argument budget exhausted");
    if (capacity > UINT64_MAX /
            LAPLACE_COGNITION_OBSERVATION_CANDIDATE_WORKSPACE_MULTIPLIER /
            sizeof(laplace_cognition_observation_candidate))
        persisted_limit("candidate workspace overflow");
    candidate_workspace = (uint64_t)capacity *
        LAPLACE_COGNITION_OBSERVATION_CANDIDATE_WORKSPACE_MULTIPLIER *
        sizeof(laplace_cognition_observation_candidate);
    frontier_workspace = (uint64_t)source_count * UINT64_C(128);
    reserved = persisted_add(candidate_workspace, frontier_workspace);
    if (reserved >= state->memory_limit)
        persisted_limit("candidate and frontier workspace exceeds provider grant");
    max_rows = (state->memory_limit - reserved) / UINT64_C(2048);
    if (max_rows == 0u) persisted_limit("no physicality workspace");
    if (max_rows >= (uint64_t)LONG_MAX) max_rows = (uint64_t)LONG_MAX - 1u;
    source_values = palloc(source_count * sizeof(Datum));
    for (size_t i = 0u; i < source_count; ++i)
        source_values[i] = PointerGetDatum(laplace_pg_bytes_to_bytea(source_ids[i].bytes, 16u));
    args[0] = PointerGetDatum(construct_array(source_values, (int)source_count,
                            BYTEAOID, -1, false, TYPALIGN_INT));
    args[1] = Int32GetDatum((int32)binding->relation_mask);
    args[2] = Int64GetDatum((int64)(max_rows + 1u));
    persisted_charge_query(state);
    if (SPI_execute_with_args(persisted_metadata_sql, 3, types, args,
                              NULL, true, (long)(max_rows + 1u)) != SPI_OK_SELECT)
        ereport(ERROR, (errmsg("Laplace indexed physicality metadata read failed")));
    rows = (uint64_t)SPI_processed;
    if (rows > max_rows) persisted_limit("matching physicality metadata exceeds workspace");
    state->rows_fetched = persisted_add(state->rows_fetched, rows);
    ++state->batch_count;
    if (rows == 0u) {
        usage->database_operations = state->database_operations - before_operations;
        return 0;
    }
    if (rows > MaxAllocSize / sizeof(*records) || rows > MaxAllocSize / sizeof(Datum))
        persisted_limit("physicality array exceeds addressability");
    records = palloc0((size_t)rows * sizeof(*records));
    physicality_values = palloc((size_t)rows * sizeof(Datum));
    for (row = 0u; row < rows; ++row) {
        int32 bytes;
        persisted_read_physicality_row(SPI_tuptable->vals[row], SPI_tuptable->tupdesc,
                                      &records[row]);
        bytes = DatumGetInt32(persisted_required_attribute(
            SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 19, "trajectory byte length"));
        if (bytes < 0 || records[row].vertex_count > UINT64_MAX / 32u ||
            (uint64_t)bytes != records[row].vertex_count * UINT64_C(32))
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace persisted trajectory metadata has inconsistent length")));
        vertices = persisted_add(vertices, records[row].vertex_count);
        encoded_bytes = persisted_add(encoded_bytes, (uint64_t)bytes);
        physicality_values[row] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            records[row].physicality_id.bytes, 32u));
    }
    /* Conservatively bound metadata, copied payload, decoded carriers, native
     * maps/vectors and result staging before detoasting trajectory bytes. The
     * caller separately reserves the native search's declared memory budget. */
    if (vertices > UINT64_MAX / UINT64_C(1024)) persisted_limit("vertex budget overflow");
    reserved = persisted_add(reserved, rows * UINT64_C(2048));
    reserved = persisted_add(reserved, vertices * UINT64_C(1024));
    if (reserved > state->memory_limit || vertices > MaxAllocSize / sizeof(*segments))
        persisted_limit("selected trajectories exceed physical-provider workspace");
    segments = palloc0((size_t)vertices * sizeof(*segments));
    args[0] = PointerGetDatum(construct_array(physicality_values, (int)rows,
                            BYTEAOID, -1, false, TYPALIGN_INT));
    SPI_freetuptable(SPI_tuptable);
    persisted_charge_query(state);
    if (SPI_execute_with_args(persisted_payload_sql, 1, types, args, NULL,
                              true, (long)(rows + 1u)) != SPI_OK_SELECT || SPI_processed != rows)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace selected physicality payload set changed within snapshot")));
    for (row = 0u; row < rows; ++row) {
        laplace_digest256 id;
        bytea* trajectory;
        uint64_t vertex, ordinal = 1u;
        persisted_read_digest_datum(persisted_required_attribute(SPI_tuptable->vals[row],
            SPI_tuptable->tupdesc, 1, "physicality_id"), &id, "physicality_id");
        if (memcmp(id.bytes, records[row].physicality_id.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace selected physicality payload identity differs")));
        trajectory = DatumGetByteaPP(persisted_required_attribute(SPI_tuptable->vals[row],
            SPI_tuptable->tupdesc, 2, "trajectory"));
        if ((uint64_t)VARSIZE_ANY_EXHDR(trajectory) != records[row].vertex_count * 32u)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace selected physicality payload length differs")));
        for (vertex = 0u; vertex < records[row].vertex_count; ++vertex) {
            laplace_persistence_trajectory_segment_record* segment = &segments[segment_cursor++];
            segment->physicality_id = id;
            segment->vertex_index = vertex;
            memcpy(&segment->carrier, VARDATA_ANY(trajectory) + (size_t)vertex * 32u, 32u);
            if (laplace_trajectory_composition_decode_one(&segment->carrier, ordinal,
                                                        &segment->occurrence) != LAPLACE_TRAJECTORY_OK)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace selected trajectory carrier is invalid")));
            ordinal = persisted_add(ordinal, (uint64_t)segment->occurrence.run_length);
        }
        if (ordinal - 1u != records[row].logical_count)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace selected trajectory logical count differs")));
        blake3_hasher_update(&state->readset, id.bytes, 32u);
        blake3_hasher_update(&state->readset, records[row].trajectory_fingerprint.bytes, 32u);
    }
    SPI_freetuptable(SPI_tuptable);
    memset(&input, 0, sizeof(input));
    input.physicalities = records;
    input.physicality_count = (size_t)rows;
    input.trajectory_segments = segments;
    input.trajectory_segment_count = (size_t)vertices;
    input.boundary_id = state->request->evidence_boundary;
    input.evidence_epoch = state->request->evidence_epoch;
    input.maximum_candidate_records_per_expansion = state->memory_limit / 64u;
    status = laplace_observation_query_index_create_base(&input, &state->index);
    if (status != LAPLACE_OBSERVATION_QUERY_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace selected physicality native validation failed: %d", (int)status)));
    memset(&summary, 0, sizeof(summary));
    status = laplace_observation_query_index_summary_get(state->index, &summary);
    if (status != LAPLACE_OBSERVATION_QUERY_OK)
        ereport(ERROR, (errmsg("Laplace selected physicality summary failed: %d", (int)status)));
    status = laplace_observation_query_index_candidates_batch(state->index, binding,
             source_ids, source_count, candidates, capacity, count, usage);
    if (status == LAPLACE_OBSERVATION_QUERY_OVERFLOW ||
        status == LAPLACE_OBSERVATION_QUERY_MEMORY_FAILURE)
        persisted_limit("native candidate batch exceeds capacity");
    if (status != LAPLACE_OBSERVATION_QUERY_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace native structural candidate projection failed: %d", (int)status)));
    state->carriers_decoded = persisted_add(state->carriers_decoded, vertices);
    state->logical_occurrences = persisted_add(state->logical_occurrences, summary.logical_occurrence_count);
    state->indexed_entities = persisted_add(state->indexed_entities, summary.indexed_entity_count);
    state->trajectory_bytes = persisted_add(state->trajectory_bytes, encoded_bytes);
    usage->rows_examined = persisted_add(usage->rows_examined, rows);
    usage->database_operations = state->database_operations - before_operations;
    laplace_observation_query_index_destroy(&state->index);
    return 0;
}

static int persisted_enumerate(
    void* opaque, const laplace_observation_query_binding* binding,
    const laplace_id128* source_ids, const laplace_query_search_state* frontier,
    const uint64_t* costs, size_t source_count,
    laplace_cognition_observation_candidate* candidates, size_t capacity,
    size_t* count, laplace_cognition_observation_candidate_usage* usage) {
    persisted_provider_state* state = (persisted_provider_state*)opaque;
    volatile int status = 0;
    MemoryContext previous;
    (void)frontier;
    (void)costs;
    *count = 0u;
    memset(usage, 0, sizeof(*usage));
    if (state->error != NULL || state->scratch_context == NULL) return 1;
    previous = MemoryContextSwitchTo(state->scratch_context);
    /* PostgreSQL ERROR uses longjmp. Catch it inside the C callback, release the
     * native index and return normally through C++ destructors before rethrowing
     * the original database diagnostic at the public C entry boundary. */
    PG_TRY();
    {
        status = persisted_enumerate_impl(state, binding, source_ids, source_count,
                                         candidates, capacity, count, usage);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(state->caller_context);
        state->error = CopyErrorData();
        FlushErrorState();
        laplace_observation_query_index_destroy(&state->index);
        *count = 0u;
        memset(usage, 0, sizeof(*usage));
        status = 1;
    }
    PG_END_TRY();
    MemoryContextSwitchTo(previous);
    MemoryContextReset(state->scratch_context);
    return (int)status;
}

static void persisted_provider_release(void* opaque) {
    persisted_provider_state* state = (persisted_provider_state*)opaque;
    laplace_observation_query_index_destroy(&state->index);
    /* Parent reset/delete has already released its children. This callback
     * owns only external native resources, never a child MemoryContext. */
    state->scratch_context = NULL;
    if (state->error != NULL) {
        FreeErrorData(state->error);
        state->error = NULL;
    }
}

void laplace_pg_cognition_provider_create(
    const laplace_cognition_observation_request* request,
    uint64_t provider_memory_bytes,
    laplace_pg_cognition_provider** owner,
    laplace_cognition_observation_candidate_provider_v1* provider) {
    static const char domain[] = "laplace-postgresql-indexed-physicality-provider-v1";
    laplace_digest256 request_fingerprint;
    blake3_hasher identifier;
    persisted_provider_state* state;
    if (owner != NULL) *owner = NULL;
    if (provider != NULL) memset(provider, 0, sizeof(*provider));
    if (request == NULL || owner == NULL || provider == NULL ||
        laplace_cognition_observation_request_identify(request, &request_fingerprint) !=
            LAPLACE_COGNITION_OBSERVATION_REQUEST_OK)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace persisted candidate provider request is invalid")));
    if (request->search_budget.max_database_operations == 0u ||
        request->forward_limits.max_database_operations == 0u)
        persisted_limit("database-operation budget is absent");
    if (provider_memory_bytes < 2048u)
        persisted_limit("insufficient physical-provider workspace");
    state = palloc0(sizeof(*state));
    state->request_storage = *request;
    state->request = &state->request_storage;
    state->memory_limit = provider_memory_bytes > MaxAllocSize ? MaxAllocSize : provider_memory_bytes;
    state->caller_context = CurrentMemoryContext;
    state->scratch_context = AllocSetContextCreate(CurrentMemoryContext,
                            "Laplace indexed cognition batch", ALLOCSET_DEFAULT_SIZES);
    state->cleanup.func = persisted_provider_release;
    state->cleanup.arg = state;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &state->cleanup);
    blake3_hasher_init(&identifier);
    blake3_hasher_update(&identifier, domain, sizeof(domain)-1u);
    blake3_hasher_update(&identifier, request_fingerprint.bytes, 32u);
    blake3_hasher_finalize(&identifier, state->provider_fingerprint.bytes, 32u);
    blake3_hasher_init(&state->readset);
    blake3_hasher_update(&state->readset, state->provider_fingerprint.bytes, 32u);
    provider->state = state;
    provider->provider_fingerprint = state->provider_fingerprint;
    provider->maximum_candidate_records_per_expansion = request->search_budget.transition_batch_capacity;
    if (provider->maximum_candidate_records_per_expansion > state->memory_limit / 64u)
        provider->maximum_candidate_records_per_expansion = state->memory_limit / 64u;
    provider->enumerate_candidates = persisted_enumerate;
    provider->abi_major = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_OBSERVATION_CANDIDATE_PROVIDER_ABI_MINOR;
    *owner = state;
}

void laplace_pg_cognition_provider_summary(
    const laplace_pg_cognition_provider* owner,
    laplace_pg_cognition_provider_report* report) {
    if (report == NULL) return;
    memset(report, 0, sizeof(*report));
    if (owner == NULL) return;
    report->provider_fingerprint = owner->provider_fingerprint;
    blake3_hasher_finalize(&owner->readset, report->readset_fingerprint.bytes, 32u);
    report->rows_fetched = owner->rows_fetched;
    report->carriers_decoded = owner->carriers_decoded;
    report->logical_occurrences = owner->logical_occurrences;
    report->indexed_entities = owner->indexed_entities;
    report->trajectory_bytes = owner->trajectory_bytes;
    report->database_operations = owner->database_operations;
    report->batch_count = owner->batch_count;
}

ErrorData* laplace_pg_cognition_provider_take_error(laplace_pg_cognition_provider* owner) {
    ErrorData* error;
    if (owner == NULL) return NULL;
    error = owner->error;
    owner->error = NULL;
    return error;
}

void laplace_pg_cognition_provider_destroy(laplace_pg_cognition_provider** owner) {
    if (owner == NULL || *owner == NULL) return;
    if ((*owner)->scratch_context != NULL) {
        MemoryContextDelete((*owner)->scratch_context);
        (*owner)->scratch_context = NULL;
    }
    persisted_provider_release(*owner);
    /* The reset-callback target itself remains caller-context-owned. */
    *owner = NULL;
}

static void persisted_cleanup(
    laplace_cognition_guidance_state** final_state,
    laplace_cognition_forward_result** forward_result,
    laplace_cognition_observation_result** observation) {
    laplace_cognition_guidance_state_destroy(final_state);
    laplace_cognition_forward_result_destroy(forward_result);
    laplace_cognition_observation_result_destroy(observation);
}

typedef struct persisted_native_owner {
    laplace_cognition_guidance_state* final_state;
    laplace_cognition_forward_result* forward_result;
    laplace_cognition_observation_result* observation;
    MemoryContextCallback cleanup;
} persisted_native_owner;

static void persisted_native_release(void* opaque) {
    persisted_native_owner* owner = (persisted_native_owner*)opaque;
    persisted_cleanup(&owner->final_state, &owner->forward_result, &owner->observation);
}

Datum laplace_pg_cognition_observation_execute_persisted(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_cognition_observation_request request;
    laplace_cognition_observation_candidate_provider_v1 provider;
    laplace_pg_cognition_provider* stored_provider = NULL;
    laplace_pg_cognition_provider_report state;
    ErrorData* provider_error;
    laplace_cognition_forward_receipt forward_receipt;
    laplace_cognition_obligation final_obligation;
    laplace_digest256 request_fingerprint, final_state_id, readset;
    Datum result_values[30], wrapper_values[6];
    bool result_nulls[30] = {false}, wrapper_nulls[6] = {false};
    laplace_cognition_observation_request_status request_status;
    laplace_cognition_forward_status forward_status;
    HeapTuple execution_tuple, result_tuple;
    TupleDesc execution_desc, answer_desc;
    Oid execution_oid, answer_oid;
    size_t answer_count, i;
    Datum* answers;
    persisted_native_owner* owner = palloc0(sizeof(*owner));
    owner->cleanup.func = persisted_native_release;
    owner->cleanup.arg = owner;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &owner->cleanup);
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    persisted_read_request(DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &request);
    if (request.search_budget.max_memory_bytes == 0u ||
        request.search_budget.max_memory_bytes >= context.resource_grant.memory_bytes)
        persisted_limit("context must reserve both search and physical-provider memory");
    if ((request.relation_mask & LAPLACE_OBSERVATION_QUERY_SEMANTIC) != 0u)
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
            errmsg("Laplace physicality provider cannot claim testimony or semantic relations")));
    if (request.search_budget.max_database_operations == 0u ||
        request.forward_limits.max_database_operations == 0u)
        persisted_limit("database-operation budget is absent");
    request_status = laplace_cognition_observation_request_identify(&request, &request_fingerprint);
    if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace persisted cognition request is invalid: %d", (int)request_status)));
    laplace_pg_cognition_provider_create(&request,
        context.resource_grant.memory_bytes - request.search_budget.max_memory_bytes,
        &stored_provider, &provider);
    if (SPI_connect() != SPI_OK_CONNECT)
        ereport(ERROR, (errmsg("Laplace persisted cognition could not connect to PostgreSQL")));
    memset(&forward_receipt, 0, sizeof(forward_receipt));
    request_status = laplace_cognition_observation_request_execute_with_candidate_provider(
        &request, &provider, &owner->observation, &owner->forward_result, &forward_receipt);
    SPI_finish();
    laplace_pg_cognition_provider_summary(stored_provider, &state);
    provider_error = laplace_pg_cognition_provider_take_error(stored_provider);
    laplace_pg_cognition_provider_destroy(&stored_provider);
    if (provider_error != NULL) {
        persisted_cleanup(&owner->final_state, &owner->forward_result, &owner->observation);
        ReThrowError(provider_error);
    }
    if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK || owner->forward_result == NULL) {
        persisted_cleanup(&owner->final_state, &owner->forward_result, &owner->observation);
        ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION),
            errmsg("Laplace indexed persisted cognition execution failed: %d", (int)request_status)));
    }
    memset(&final_obligation, 0, sizeof(final_obligation));
    memset(&final_state_id, 0, sizeof(final_state_id));
    forward_status = laplace_cognition_forward_result_final_state_clone(
        owner->forward_result, &owner->final_state);
    if (forward_status != LAPLACE_COGNITION_FORWARD_OK || owner->final_state == NULL ||
        laplace_cognition_guidance_state_obligation_count(owner->final_state) != 1u ||
        laplace_cognition_guidance_state_obligation(
            owner->final_state, 0u, &final_obligation) != LAPLACE_COGNITION_GUIDANCE_OK ||
        laplace_cognition_guidance_state_identify(
            owner->final_state, &final_state_id) != LAPLACE_COGNITION_GUIDANCE_OK) {
        persisted_cleanup(&owner->final_state, &owner->forward_result, &owner->observation);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persisted cognition final guidance state is not readable")));
    }
    if (memcmp(final_state_id.bytes, forward_receipt.final_state_id.bytes,
               sizeof(final_state_id.bytes)) != 0) {
        persisted_cleanup(&owner->final_state, &owner->forward_result, &owner->observation);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persisted cognition final state disagrees with its forward receipt")));
    }

    result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        request_fingerprint.bytes, sizeof(request_fingerprint.bytes)));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state.provider_fingerprint.bytes,
        sizeof(state.provider_fingerprint.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        final_obligation.value_id.bytes, sizeof(final_obligation.value_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        final_obligation.resolution_receipt_id.bytes,
        sizeof(final_obligation.resolution_receipt_id.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        forward_receipt.receipt_id.bytes, sizeof(forward_receipt.receipt_id.bytes)));
    result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        forward_receipt.program_fingerprint.bytes,
        sizeof(forward_receipt.program_fingerprint.bytes)));
    result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        forward_receipt.initial_state_id.bytes,
        sizeof(forward_receipt.initial_state_id.bytes)));
    result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        forward_receipt.final_state_id.bytes,
        sizeof(forward_receipt.final_state_id.bytes)));
    result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        forward_receipt.layer_trace_fingerprint.bytes,
        sizeof(forward_receipt.layer_trace_fingerprint.bytes)));
    result_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        forward_receipt.output_fingerprint.bytes,
        sizeof(forward_receipt.output_fingerprint.bytes)));
    result_values[10] = laplace_pg_numeric_from_uint64(state.rows_fetched);
    result_values[11] = laplace_pg_numeric_from_uint64(state.carriers_decoded);
    result_values[12] = laplace_pg_numeric_from_uint64(state.carriers_decoded);
    result_values[13] = laplace_pg_numeric_from_uint64(state.logical_occurrences);
    result_values[14] = laplace_pg_numeric_from_uint64(state.indexed_entities);
    result_values[15] = laplace_pg_numeric_from_uint64(forward_receipt.layer_count);
    result_values[16] = laplace_pg_numeric_from_uint64(forward_receipt.provider_call_count);
    result_values[17] = laplace_pg_numeric_from_uint64(forward_receipt.projected_query_count);
    result_values[18] = laplace_pg_numeric_from_uint64(forward_receipt.candidate_operation_count);
    result_values[19] = laplace_pg_numeric_from_uint64(forward_receipt.resolution_count);
    result_values[20] = laplace_pg_numeric_from_uint64(forward_receipt.resource_cost);
    result_values[21] = laplace_pg_numeric_from_uint64(forward_receipt.io_operations);
    result_values[22] = laplace_pg_numeric_from_uint64(forward_receipt.database_operations);
    result_values[23] = laplace_pg_numeric_from_uint64(
        forward_receipt.final_remaining_required_count);
    result_values[24] = Int32GetDatum((int32)forward_receipt.final_completion);
    result_values[25] = Int32GetDatum((int32)forward_receipt.disposition);
    result_values[26] = Int32GetDatum((int32)final_obligation.disposition);
    result_values[27] = Int32GetDatum((int32)forward_receipt.status);
    result_values[28] = Int32GetDatum((int32)forward_receipt.version);
    result_values[29] = Int32GetDatum((int32)forward_receipt.flags);


    execution_oid = laplace_pg_composite_type_oid("cognition_observation_result");
    execution_desc = lookup_rowtype_tupdesc(execution_oid, -1);
    execution_tuple = heap_form_tuple(execution_desc, result_values, result_nulls);
    ReleaseTupleDesc(execution_desc);
    wrapper_values[0] = HeapTupleGetDatum(execution_tuple);
    answer_oid = laplace_pg_composite_type_oid("cognition_observation_answer");
    answer_desc = lookup_rowtype_tupdesc(answer_oid, -1);
    answer_count = laplace_cognition_observation_result_answer_count(owner->observation);
    if (answer_count > (size_t)INT_MAX || answer_count > MaxAllocSize / sizeof(Datum)) {
        persisted_cleanup(&owner->final_state, &owner->forward_result, &owner->observation);
        persisted_limit("answer array exceeds addressability");
    }
    answers = palloc(answer_count * sizeof(Datum));
    for (i = 0u; i < answer_count; ++i) {
        laplace_cognition_observation_answer answer;
        Datum fields[12];
        bool nulls[12] = {false};
        if (laplace_cognition_observation_result_answer(owner->observation, i, &answer) !=
                LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
            persisted_cleanup(&owner->final_state, &owner->forward_result, &owner->observation);
            ereport(ERROR, (errmsg("Laplace retained cognition answer is unreadable")));
        }
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(answer.entity_id.bytes, 16u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(answer.relation_id.bytes, 16u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(answer.path_id.bytes, 32u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(answer.terminal_state_id.bytes, 32u));
        fields[4] = laplace_pg_numeric_from_uint64(answer.total_cost);
        fields[5] = laplace_pg_numeric_from_uint64(answer.transition_count);
        fields[6] = laplace_pg_numeric_from_uint64(answer.independent_evidence_root_count);
        fields[7] = Int32GetDatum((int32)answer.relation_family);
        fields[8] = Int32GetDatum((int32)answer.source_layer);
        fields[9] = Int32GetDatum((int32)answer.direction);
        fields[10] = Int32GetDatum((int32)answer.rank);
        fields[11] = Int32GetDatum((int32)answer.flags);
        answers[i] = HeapTupleGetDatum(heap_form_tuple(answer_desc, fields, nulls));
    }
    ReleaseTupleDesc(answer_desc);
    wrapper_values[1] = PointerGetDatum(answer_count == 0u ? construct_empty_array(answer_oid) :
        construct_array(answers, (int)answer_count, answer_oid, -1, false, TYPALIGN_DOUBLE));
    wrapper_values[2] = laplace_pg_numeric_from_uint64(state.rows_fetched);
    wrapper_values[3] = laplace_pg_numeric_from_uint64(state.trajectory_bytes);
    wrapper_values[4] = laplace_pg_numeric_from_uint64(state.batch_count);
    readset = state.readset_fingerprint;
    wrapper_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(readset.bytes, 32u));
    persisted_cleanup(&owner->final_state, &owner->forward_result, &owner->observation);
    result_tuple = laplace_pg_form_result_tuple(fcinfo, wrapper_values, wrapper_nulls, 6);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
