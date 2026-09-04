#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "laplace/cognition_observation_request.h"
#include "laplace/cognition_forward_pass.h"
#include "laplace/cognition_guidance.h"
#include "laplace/framework.h"
#include "laplace/observation_query.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_observation_execute);

static void observation_read_digest_attribute(
    HeapTupleHeader tuple,
    int attribute,
    laplace_digest256* digest,
    const char* field) {
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, attribute, field),
        digest, field);
}

static void observation_read_id128_attribute(
    HeapTupleHeader tuple,
    int attribute,
    laplace_id128* id,
    const char* field) {
    bytea* value = DatumGetByteaPP(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(id->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace %s must contain exactly 16 bytes", field)));
    }
    memcpy(id->bytes, VARDATA_ANY(value), sizeof(id->bytes));
}

static uint32_t observation_read_u32_attribute(
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

static uint64_t observation_read_u64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static double observation_read_f64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    return DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
}

static Datum* observation_deconstruct_composite_array(
    ArrayType* array,
    const char* type_name,
    int* count,
    bool** nulls,
    bool allow_empty) {
    const Oid type_oid = laplace_pg_composite_type_oid(type_name);
    Datum* values = NULL;
    int16 type_length;
    bool type_by_value;
    char type_alignment;

    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace observation cognition %s input must be a one-dimensional exact composite array",
                        type_name)));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, nulls, count);
    if (!allow_empty && *count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace observation cognition %s input cannot be empty",
                        type_name)));
    }
    return values;
}

static laplace_persistence_physicality_record* observation_read_physicalities(
    ArrayType* array,
    size_t* physicality_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    int index;
    laplace_persistence_physicality_record* physicalities;

    values = observation_deconstruct_composite_array(
        array, "physicality_record", &count, &nulls, false);
    physicalities = (laplace_persistence_physicality_record*)palloc0(
        sizeof(*physicalities) * (size_t)count);

    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_persistence_physicality_record* value;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace observation physicalities cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        value = &physicalities[index];
        observation_read_digest_attribute(
            tuple, 1, &value->physicality_id, "observation physicality_id");
        observation_read_id128_attribute(
            tuple, 2, &value->entity_id, "observation physicality entity_id");
        value->physicality_type = observation_read_u32_attribute(
            tuple, 3, "observation physicality type");
        value->vertex_class = observation_read_u32_attribute(
            tuple, 4, "observation physicality vertex_class");
        value->recipe_version = observation_read_u32_attribute(
            tuple, 5, "observation physicality recipe_version");
        value->structural_form = observation_read_u32_attribute(
            tuple, 6, "observation physicality structural_form");
        value->dimension_count = observation_read_u32_attribute(
            tuple, 7, "observation physicality dimension_count");
        value->flags = observation_read_u32_attribute(
            tuple, 8, "observation physicality flags");
        observation_read_digest_attribute(
            tuple, 9, &value->recipe_fingerprint,
            "observation physicality recipe_fingerprint");
        observation_read_digest_attribute(
            tuple, 10, &value->geometry_epoch,
            "observation physicality geometry_epoch");
        observation_read_digest_attribute(
            tuple, 11, &value->trajectory_fingerprint,
            "observation physicality trajectory_fingerprint");
        value->centroid.component[0] = observation_read_f64_attribute(
            tuple, 12, "observation physicality centroid_x");
        value->centroid.component[1] = observation_read_f64_attribute(
            tuple, 13, "observation physicality centroid_y");
        value->centroid.component[2] = observation_read_f64_attribute(
            tuple, 14, "observation physicality centroid_z");
        value->centroid.component[3] = observation_read_f64_attribute(
            tuple, 15, "observation physicality centroid_m");
        value->radius = observation_read_f64_attribute(
            tuple, 16, "observation physicality radius");
        value->logical_count = observation_read_u64_attribute(
            tuple, 17, "observation physicality logical_count");
        value->vertex_count = observation_read_u64_attribute(
            tuple, 18, "observation physicality vertex_count");
    }
    *physicality_count = (size_t)count;
    return physicalities;
}

static void observation_read_carrier_attribute(
    HeapTupleHeader tuple,
    int attribute,
    laplace_trajectory_carrier* carrier,
    const char* field) {
    bytea* value = DatumGetByteaPP(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(*carrier)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace %s must contain exactly %zu bytes",
                        field, sizeof(*carrier))));
    }
    memcpy(carrier, VARDATA_ANY(value), sizeof(*carrier));
}

static laplace_persistence_trajectory_segment_record* observation_read_segments(
    ArrayType* array,
    size_t* segment_count) {
    Datum* values;
    bool* nulls = NULL;
    int count = 0;
    int index;
    laplace_persistence_trajectory_segment_record* segments;

    values = observation_deconstruct_composite_array(
        array, "physicality_trajectory_segment_record", &count, &nulls, true);
    if (count == 0) {
        *segment_count = 0u;
        return NULL;
    }
    segments = (laplace_persistence_trajectory_segment_record*)palloc0(
        sizeof(*segments) * (size_t)count);

    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        laplace_persistence_trajectory_segment_record* segment;
        laplace_composition_occurrence decoded;
        laplace_id128 declared_entity;
        const int64 metadata_signed;
        const int64 atom_signed;
        const int32 packed_ordinal;
        const int32 run_length;
        const int16 tier;
        const bool has_atom;

        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace observation trajectory segments cannot contain null records")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        segment = &segments[index];
        observation_read_digest_attribute(
            tuple, 1, &segment->physicality_id,
            "observation trajectory physicality_id");
        segment->vertex_index = observation_read_u64_attribute(
            tuple, 2, "observation trajectory vertex_index");
        observation_read_carrier_attribute(
            tuple, 3, &segment->carrier, "observation trajectory carrier");
        observation_read_id128_attribute(
            tuple, 4, &declared_entity,
            "observation trajectory constituent_entity_id");
        segment->occurrence.logical_ordinal = observation_read_u64_attribute(
            tuple, 5, "observation trajectory logical_ordinal");

        metadata_signed = DatumGetInt64(
            laplace_pg_required_composite_attribute(
                tuple, 6, "observation trajectory metadata"));
        atom_signed = DatumGetInt64(
            laplace_pg_required_composite_attribute(
                tuple, 7, "observation trajectory atom"));
        packed_ordinal = DatumGetInt32(
            laplace_pg_required_composite_attribute(
                tuple, 8, "observation trajectory packed_ordinal"));
        run_length = DatumGetInt32(
            laplace_pg_required_composite_attribute(
                tuple, 9, "observation trajectory run_length"));
        tier = DatumGetInt16(
            laplace_pg_required_composite_attribute(
                tuple, 10, "observation trajectory tier"));
        has_atom = DatumGetBool(
            laplace_pg_required_composite_attribute(
                tuple, 11, "observation trajectory has_atom"));

        if (atom_signed < 0 || (uint64_t)atom_signed > UINT32_MAX ||
            packed_ordinal < 0 || packed_ordinal > UINT16_MAX ||
            run_length <= 0 || run_length > UINT16_MAX ||
            tier < 0 || tier > UINT8_MAX) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Laplace observation trajectory transport fields exceed native ranges")));
        }

        memset(&decoded, 0, sizeof(decoded));
        if (laplace_trajectory_composition_decode_one(
                &segment->carrier,
                segment->occurrence.logical_ordinal,
                &decoded) != LAPLACE_TRAJECTORY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                     errmsg("Laplace observation trajectory carrier is not a valid composition carrier")));
        }
        if (memcmp(decoded.entity_id.bytes, declared_entity.bytes,
                   sizeof(decoded.entity_id.bytes)) != 0 ||
            decoded.logical_ordinal != segment->occurrence.logical_ordinal ||
            decoded.metadata != (uint64_t)metadata_signed ||
            decoded.atom != (uint32_t)atom_signed ||
            decoded.packed_ordinal != (uint16_t)packed_ordinal ||
            decoded.run_length != (uint16_t)run_length ||
            decoded.tier != (uint8_t)tier ||
            decoded.has_atom != (uint8_t)(has_atom ? 1u : 0u)) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                     errmsg("Laplace observation trajectory carrier disagrees with its declared decoded transport fields")));
        }
        segment->occurrence = decoded;
    }

    *segment_count = (size_t)count;
    return segments;
}

static void observation_read_search_budget(
    HeapTupleHeader tuple,
    laplace_query_search_budget* budget) {
    memset(budget, 0, sizeof(*budget));
    budget->max_expanded_states = observation_read_u64_attribute(
        tuple, 1, "observation search max_expanded_states");
    budget->max_transition_records = observation_read_u64_attribute(
        tuple, 2, "observation search max_transition_records");
    budget->max_emitted_states = observation_read_u64_attribute(
        tuple, 3, "observation search max_emitted_states");
    budget->max_frontier_states = observation_read_u64_attribute(
        tuple, 4, "observation search max_frontier_states");
    budget->max_memory_bytes = observation_read_u64_attribute(
        tuple, 5, "observation search max_memory_bytes");
    budget->max_io_operations = observation_read_u64_attribute(
        tuple, 6, "observation search max_io_operations");
    budget->max_database_operations = observation_read_u64_attribute(
        tuple, 7, "observation search max_database_operations");
    budget->max_provider_calls = observation_read_u64_attribute(
        tuple, 8, "observation search max_provider_calls");
    budget->max_depth = observation_read_u32_attribute(
        tuple, 9, "observation search max_depth");
    budget->requested_path_count = observation_read_u32_attribute(
        tuple, 10, "observation search requested_path_count");
    budget->frontier_batch_width = observation_read_u32_attribute(
        tuple, 11, "observation search frontier_batch_width");
    budget->transition_batch_capacity = observation_read_u32_attribute(
        tuple, 12, "observation search transition_batch_capacity");
}

static void observation_read_forward_limits(
    HeapTupleHeader tuple,
    laplace_cognition_observation_forward_limits* limits) {
    memset(limits, 0, sizeof(*limits));
    limits->max_layers = observation_read_u64_attribute(
        tuple, 1, "observation forward max_layers");
    limits->max_provider_calls = observation_read_u64_attribute(
        tuple, 2, "observation forward max_provider_calls");
    limits->max_projected_queries = observation_read_u64_attribute(
        tuple, 3, "observation forward max_projected_queries");
    limits->max_candidate_operations = observation_read_u64_attribute(
        tuple, 4, "observation forward max_candidate_operations");
    limits->max_resolutions = observation_read_u64_attribute(
        tuple, 5, "observation forward max_resolutions");
    limits->max_resource_cost = observation_read_u64_attribute(
        tuple, 6, "observation forward max_resource_cost");
    limits->max_io_operations = observation_read_u64_attribute(
        tuple, 7, "observation forward max_io_operations");
    limits->max_database_operations = observation_read_u64_attribute(
        tuple, 8, "observation forward max_database_operations");
    limits->candidate_operation_capacity = observation_read_u32_attribute(
        tuple, 9, "observation forward candidate_operation_capacity");
    limits->resolution_capacity = observation_read_u32_attribute(
        tuple, 10, "observation forward resolution_capacity");
}

static void observation_read_request(
    HeapTupleHeader tuple,
    laplace_cognition_observation_request* request) {
    HeapTupleHeader search_tuple;
    HeapTupleHeader forward_tuple;

    memset(request, 0, sizeof(*request));
    observation_read_id128_attribute(
        tuple, 1, &request->anchor_entity_id,
        "observation request anchor_entity_id");
    observation_read_id128_attribute(
        tuple, 2, &request->goal_entity_id,
        "observation request goal_entity_id");
    observation_read_digest_attribute(
        tuple, 3, &request->world_id, "observation request world_id");
    observation_read_digest_attribute(
        tuple, 4, &request->time_fingerprint,
        "observation request time_fingerprint");
    observation_read_digest_attribute(
        tuple, 5, &request->context_fingerprint,
        "observation request context_fingerprint");
    observation_read_digest_attribute(
        tuple, 6, &request->evidence_boundary,
        "observation request evidence_boundary");
    observation_read_digest_attribute(
        tuple, 7, &request->evidence_epoch,
        "observation request evidence_epoch");
    observation_read_digest_attribute(
        tuple, 8, &request->authority_id,
        "observation request authority_id");
    observation_read_digest_attribute(
        tuple, 9, &request->result_contract_fingerprint,
        "observation request result_contract_fingerprint");

    search_tuple = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(
            tuple, 10, "observation request search_budget"));
    forward_tuple = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(
            tuple, 11, "observation request forward_limits"));
    observation_read_search_budget(search_tuple, &request->search_budget);
    observation_read_forward_limits(forward_tuple, &request->forward_limits);

    request->relation_mask = observation_read_u32_attribute(
        tuple, 12, "observation request relation_mask");
    request->maximum_results = observation_read_u32_attribute(
        tuple, 13, "observation request maximum_results");
    request->flags = observation_read_u32_attribute(
        tuple, 14, "observation request flags");
    request->version = observation_read_u32_attribute(
        tuple, 15, "observation request version");
}

static void observation_cleanup(
    laplace_cognition_guidance_state** final_state,
    laplace_cognition_forward_result** forward_result,
    laplace_cognition_observation_request_provider** provider_state,
    laplace_cognition_observation_compiled_request* compiled,
    laplace_observation_query_index** index) {
    laplace_cognition_guidance_state_destroy(final_state);
    laplace_cognition_forward_result_destroy(forward_result);
    laplace_cognition_observation_request_provider_destroy(provider_state);
    laplace_cognition_observation_compiled_request_destroy(compiled);
    laplace_observation_query_index_destroy(index);
}

Datum laplace_pg_cognition_observation_execute(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_persistence_physicality_record* physicalities;
    laplace_persistence_trajectory_segment_record* segments;
    laplace_cognition_observation_request request;
    laplace_observation_query_index_base_input index_input;
    laplace_observation_query_index* index = NULL;
    laplace_observation_query_index_summary index_summary;
    laplace_cognition_observation_compiled_request compiled;
    laplace_cognition_observation_request_provider* provider_state = NULL;
    laplace_cognition_forward_provider_v1 provider;
    laplace_cognition_forward_result* forward_result = NULL;
    laplace_cognition_forward_receipt forward_receipt;
    laplace_cognition_guidance_state* final_state = NULL;
    laplace_cognition_obligation final_obligation;
    laplace_digest256 final_state_id;
    size_t physicality_count = 0u;
    size_t segment_count = 0u;
    Datum result_values[30];
    bool result_nulls[30] = {false};
    HeapTuple result_tuple;
    laplace_cognition_observation_request_status request_status;
    laplace_observation_query_status query_status;
    laplace_cognition_forward_status forward_status;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    physicalities = observation_read_physicalities(
        PG_GETARG_ARRAYTYPE_P(1), &physicality_count);
    segments = observation_read_segments(
        PG_GETARG_ARRAYTYPE_P(2), &segment_count);
    observation_read_request(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(3)), &request);

    if (request.search_budget.max_memory_bytes > context.resource_grant.memory_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace observation request search memory exceeds the execution-context grant"),
                 errdetail("request_bytes=%llu granted_bytes=%llu",
                           (unsigned long long)request.search_budget.max_memory_bytes,
                           (unsigned long long)context.resource_grant.memory_bytes)));
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

    query_status = laplace_observation_query_index_create_base(&index_input, &index);
    if (query_status != LAPLACE_OBSERVATION_QUERY_OK || index == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace observation estate could not be compiled into the canonical query index"),
                 errdetail("observation_query_status=%d", (int)query_status)));
    }
    memset(&index_summary, 0, sizeof(index_summary));
    query_status = laplace_observation_query_index_summary_get(index, &index_summary);
    if (query_status != LAPLACE_OBSERVATION_QUERY_OK) {
        laplace_observation_query_index_destroy(&index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace observation query index summary failed"),
                 errdetail("observation_query_status=%d", (int)query_status)));
    }

    memset(&compiled, 0, sizeof(compiled));
    request_status = laplace_cognition_observation_request_compile(
        &request, &compiled);
    if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        laplace_observation_query_index_destroy(&index);
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace typed observation request compilation failed"),
                 errdetail("request_status=%d", (int)request_status)));
    }

    memset(&provider, 0, sizeof(provider));
    request_status = laplace_cognition_observation_request_cognition_provider(
        index, &compiled, &provider_state, &provider);
    if (request_status != LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        laplace_cognition_observation_compiled_request_destroy(&compiled);
        laplace_observation_query_index_destroy(&index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace request-bound observation cognition provider publication failed"),
                 errdetail("request_status=%d", (int)request_status)));
    }

    memset(&forward_receipt, 0, sizeof(forward_receipt));
    forward_status = laplace_cognition_forward_pass_execute(
        &compiled.forward_program,
        compiled.guidance_state,
        &provider,
        &forward_result,
        &forward_receipt);
    if (forward_status != LAPLACE_COGNITION_FORWARD_OK || forward_result == NULL) {
        observation_cleanup(
            &final_state, &forward_result, &provider_state, &compiled, &index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace typed observation cognition forward execution failed"),
                 errdetail("forward_status=%d", (int)forward_status)));
    }

    forward_status = laplace_cognition_forward_result_final_state_clone(
        forward_result, &final_state);
    if (forward_status != LAPLACE_COGNITION_FORWARD_OK || final_state == NULL ||
        laplace_cognition_guidance_state_obligation_count(final_state) != 1u ||
        laplace_cognition_guidance_state_obligation(
            final_state, 0u, &final_obligation) != LAPLACE_COGNITION_GUIDANCE_OK ||
        laplace_cognition_guidance_state_identify(
            final_state, &final_state_id) != LAPLACE_COGNITION_GUIDANCE_OK) {
        observation_cleanup(
            &final_state, &forward_result, &provider_state, &compiled, &index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace observation cognition final guidance state is not readable")));
    }
    if (memcmp(final_state_id.bytes, forward_receipt.final_state_id.bytes,
               sizeof(final_state_id.bytes)) != 0) {
        observation_cleanup(
            &final_state, &forward_result, &provider_state, &compiled, &index);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace observation cognition final state disagrees with its forward receipt")));
    }

    result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        compiled.request_fingerprint.bytes,
        sizeof(compiled.request_fingerprint.bytes)));
    result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        index_summary.index_fingerprint.bytes,
        sizeof(index_summary.index_fingerprint.bytes)));
    result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        final_obligation.value_id.bytes, sizeof(final_obligation.value_id.bytes)));
    result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        final_obligation.resolution_receipt_id.bytes,
        sizeof(final_obligation.resolution_receipt_id.bytes)));
    result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        forward_receipt.receipt_id.bytes,
        sizeof(forward_receipt.receipt_id.bytes)));
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

    observation_cleanup(
        &final_state, &forward_result, &provider_state, &compiled, &index);

    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 30);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
