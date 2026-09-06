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

#include "laplace/cognition_forward_pass.h"
#include "laplace/cognition_guidance.h"
#include "laplace/cognition_observation_request.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/framework.h"
#include "laplace/observation_query.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"
#include "laplace_pg_internal.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_observation_execute_persisted);

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

static void persisted_cleanup(
    laplace_cognition_guidance_state** final_state,
    laplace_cognition_forward_result** forward_result,
    laplace_observation_query_index** index) {
    laplace_cognition_guidance_state_destroy(final_state);
    laplace_cognition_forward_result_destroy(forward_result);
    laplace_observation_query_index_destroy(index);
}

Datum laplace_pg_cognition_observation_execute_persisted(PG_FUNCTION_ARGS) {
    static const char estate_sql[] =
        "SELECT physicality_id, entity_id, physicality_type, vertex_class, "
        "recipe_version, structural_form, dimension_count, flags, "
        "recipe_fingerprint, geometry_epoch, trajectory_fingerprint, "
        "centroid_x, centroid_y, centroid_z, centroid_m, radius, "
        "logical_count, vertex_count, trajectory "
        "FROM " LAPLACE_PG_SCHEMA ".physicality ORDER BY physicality_id";
    laplace_framework_context context;
    laplace_cognition_observation_request request;
    laplace_persistence_physicality_record* physicalities = NULL;
    laplace_persistence_trajectory_segment_record* segments = NULL;
    laplace_observation_query_index_base_input index_input;
    laplace_observation_query_index* index = NULL;
    laplace_observation_query_index_summary index_summary;
    laplace_cognition_forward_result* forward_result = NULL;
    laplace_cognition_forward_receipt forward_receipt;
    laplace_cognition_guidance_state* final_state = NULL;
    laplace_cognition_obligation final_obligation;
    laplace_digest256 request_fingerprint;
    laplace_digest256 final_state_id;
    uint64_t memory_limit;
    uint64_t maximum_rows;
    uint64_t row_count;
    uint64_t total_vertices = 0u;
    uint64_t required_bytes;
    size_t physicality_count;
    size_t segment_count;
    size_t segment_cursor = 0u;
    uint64_t row;
    int spi_status;
    Datum result_values[30];
    bool result_nulls[30] = {false};
    HeapTuple result_tuple;
    laplace_cognition_observation_request_status request_status;
    laplace_observation_query_status query_status;
    laplace_cognition_forward_status forward_status;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    persisted_read_request(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &request);

    if (request.search_budget.max_memory_bytes == 0u ||
        request.search_budget.max_memory_bytes > context.resource_grant.memory_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persisted cognition search memory exceeds the execution-context grant"),
                 errdetail("request_bytes=%llu granted_bytes=%llu",
                           (unsigned long long)request.search_budget.max_memory_bytes,
                           (unsigned long long)context.resource_grant.memory_bytes)));
    }
    if (request.search_budget.max_database_operations == 0u ||
        request.forward_limits.max_database_operations == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persisted cognition requires a declared database-operation budget")));
    }

    memory_limit = request.search_budget.max_memory_bytes;
    maximum_rows = memory_limit / (uint64_t)sizeof(laplace_persistence_physicality_record);
    if (maximum_rows == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persisted cognition memory grant cannot hold one physicality record")));
    }
    if (maximum_rows >= (uint64_t)LONG_MAX) {
        maximum_rows = (uint64_t)LONG_MAX - 1u;
    }

    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace persisted cognition could not connect to PostgreSQL state")));
    }
    spi_status = SPI_execute(estate_sql, true, (long)(maximum_rows + 1u));
    if (spi_status != SPI_OK_SELECT) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persisted cognition could not read the durable physicality estate"),
                 errdetail("spi_status=%d", spi_status)));
    }
    row_count = (uint64_t)SPI_processed;
    if (row_count == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_NO_DATA_FOUND),
                 errmsg("Laplace persisted cognition has no durable physicality state")));
    }
    if (row_count > maximum_rows) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persisted cognition cannot prove the complete physicality boundary within the memory grant"),
                 errdetail("physicality_scan_limit=%llu", (unsigned long long)maximum_rows)));
    }
    if (row_count > (uint64_t)SIZE_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persisted cognition physicality count exceeds native addressability")));
    }

    physicality_count = (size_t)row_count;
    physicalities = (laplace_persistence_physicality_record*)SPI_palloc(
        physicality_count * sizeof(*physicalities));
    memset(physicalities, 0, physicality_count * sizeof(*physicalities));

    for (row = 0u; row < row_count; ++row) {
        laplace_persistence_physicality_record* physicality =
            &physicalities[(size_t)row];
        persisted_read_physicality_row(
            SPI_tuptable->vals[row], SPI_tuptable->tupdesc, physicality);
        if (UINT64_MAX - total_vertices < physicality->vertex_count) {
            ereport(ERROR,
                    (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                     errmsg("Laplace persisted cognition trajectory cardinality overflowed")));
        }
        total_vertices += physicality->vertex_count;
    }

    if (total_vertices > (uint64_t)SIZE_MAX ||
        total_vertices > (uint64_t)SIZE_MAX / sizeof(*segments)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persisted cognition trajectory estate exceeds native addressability")));
    }
    if ((uint64_t)physicality_count > UINT64_MAX / sizeof(*physicalities) ||
        total_vertices > UINT64_MAX / sizeof(*segments)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persisted cognition estate memory calculation overflowed")));
    }
    required_bytes = (uint64_t)physicality_count * sizeof(*physicalities) +
        total_vertices * sizeof(*segments);
    if (required_bytes > memory_limit) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace persisted cognition complete estate exceeds the declared memory grant"),
                 errdetail("required_bytes=%llu granted_bytes=%llu",
                           (unsigned long long)required_bytes,
                           (unsigned long long)memory_limit)));
    }

    segment_count = (size_t)total_vertices;
    if (segment_count != 0u) {
        segments = (laplace_persistence_trajectory_segment_record*)SPI_palloc(
            segment_count * sizeof(*segments));
        memset(segments, 0, segment_count * sizeof(*segments));
    }

    for (row = 0u; row < row_count; ++row) {
        const laplace_persistence_physicality_record* physicality =
            &physicalities[(size_t)row];
        Datum trajectory_datum = persisted_required_attribute(
            SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 19, "trajectory");
        bytea* trajectory = DatumGetByteaPP(trajectory_datum);
        const size_t trajectory_bytes = (size_t)VARSIZE_ANY_EXHDR(trajectory);
        uint64_t vertex;
        uint64_t logical_ordinal = 1u;

        if (physicality->vertex_count > (uint64_t)SIZE_MAX /
                sizeof(laplace_trajectory_carrier) ||
            trajectory_bytes != (size_t)physicality->vertex_count *
                sizeof(laplace_trajectory_carrier)) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace persisted cognition physicality trajectory length is inconsistent")));
        }

        for (vertex = 0u; vertex < physicality->vertex_count; ++vertex) {
            laplace_persistence_trajectory_segment_record* segment =
                &segments[segment_cursor++];
            const uint8_t* carrier_bytes =
                (const uint8_t*)VARDATA_ANY(trajectory) +
                (size_t)vertex * sizeof(laplace_trajectory_carrier);
            segment->physicality_id = physicality->physicality_id;
            segment->vertex_index = vertex;
            memcpy(&segment->carrier, carrier_bytes, sizeof(segment->carrier));
            if (laplace_trajectory_composition_decode_one(
                    &segment->carrier, logical_ordinal, &segment->occurrence) !=
                LAPLACE_TRAJECTORY_OK) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace persisted cognition found an invalid canonical trajectory carrier")));
            }
            if (UINT64_MAX - logical_ordinal <
                (uint64_t)segment->occurrence.run_length) {
                ereport(ERROR,
                        (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                         errmsg("Laplace persisted cognition logical trajectory overflowed")));
            }
            logical_ordinal += (uint64_t)segment->occurrence.run_length;
        }
        if (physicality->vertex_count != 0u &&
            logical_ordinal - 1u != physicality->logical_count) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace persisted cognition trajectory logical count disagrees with durable physicality")));
        }
    }

    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace persisted cognition could not finish its PostgreSQL read")));
    }

    memset(&request_fingerprint, 0, sizeof(request_fingerprint));
    request_status = laplace_cognition_observation_request_identify(
        &request, &request_fingerprint);
    if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace persisted cognition request identification failed"),
                 errdetail("request_status=%d", (int)request_status)));
    }

    memset(&index_input, 0, sizeof(index_input));
    index_input.physicalities = physicalities;
    index_input.physicality_count = physicality_count;
    index_input.trajectory_segments = segments;
    index_input.trajectory_segment_count = segment_count;
    index_input.boundary_id = request.evidence_boundary;
    index_input.evidence_epoch = request.evidence_epoch;
    index_input.maximum_candidate_records_per_expansion =
        (uint64_t)request.search_budget.transition_batch_capacity;
    if ((uint64_t)request.maximum_results >
        index_input.maximum_candidate_records_per_expansion) {
        index_input.maximum_candidate_records_per_expansion =
            (uint64_t)request.maximum_results;
    }

    query_status = laplace_observation_query_index_create_base(&index_input, &index);
    if (query_status != LAPLACE_OBSERVATION_QUERY_OK || index == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persisted cognition could not compile the durable estate into the canonical query index"),
                 errdetail("observation_query_status=%d", (int)query_status)));
    }

    memset(&index_summary, 0, sizeof(index_summary));
    query_status = laplace_observation_query_index_summary_get(index, &index_summary);
    if (query_status != LAPLACE_OBSERVATION_QUERY_OK) {
        laplace_observation_query_index_destroy(&index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persisted cognition query-index summary failed"),
                 errdetail("observation_query_status=%d", (int)query_status)));
    }

    memset(&forward_receipt, 0, sizeof(forward_receipt));
    request_status = laplace_cognition_observation_request_execute(
        index, &request, &forward_result, &forward_receipt);
    if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK ||
        forward_result == NULL) {
        persisted_cleanup(&final_state, &forward_result, &index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persisted cognition execution failed"),
                 errdetail("request_status=%d", (int)request_status)));
    }

    memset(&final_obligation, 0, sizeof(final_obligation));
    memset(&final_state_id, 0, sizeof(final_state_id));
    forward_status = laplace_cognition_forward_result_final_state_clone(
        forward_result, &final_state);
    if (forward_status != LAPLACE_COGNITION_FORWARD_OK || final_state == NULL ||
        laplace_cognition_guidance_state_obligation_count(final_state) != 1u ||
        laplace_cognition_guidance_state_obligation(
            final_state, 0u, &final_obligation) != LAPLACE_COGNITION_GUIDANCE_OK ||
        laplace_cognition_guidance_state_identify(
            final_state, &final_state_id) != LAPLACE_COGNITION_GUIDANCE_OK) {
        persisted_cleanup(&final_state, &forward_result, &index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persisted cognition final guidance state is not readable")));
    }
    if (memcmp(final_state_id.bytes, forward_receipt.final_state_id.bytes,
               sizeof(final_state_id.bytes)) != 0) {
        persisted_cleanup(&final_state, &forward_result, &index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace persisted cognition final state disagrees with its forward receipt")));
    }

    result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        request_fingerprint.bytes, sizeof(request_fingerprint.bytes)));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        index_summary.index_fingerprint.bytes,
        sizeof(index_summary.index_fingerprint.bytes)));
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
    result_values[10] = laplace_pg_numeric_from_uint64(index_summary.physicality_count);
    result_values[11] = laplace_pg_numeric_from_uint64(index_summary.trajectory_segment_count);
    result_values[12] = laplace_pg_numeric_from_uint64(index_summary.occurrence_run_count);
    result_values[13] = laplace_pg_numeric_from_uint64(index_summary.logical_occurrence_count);
    result_values[14] = laplace_pg_numeric_from_uint64(index_summary.indexed_entity_count);
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

    persisted_cleanup(&final_state, &forward_result, &index);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 30);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
