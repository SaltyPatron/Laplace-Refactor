#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "catalog/pg_type_d.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "blake3.h"
#include "laplace/cognition_materialization.h"
#include "laplace/contract/persistence.h"
#include "laplace/perfcache_modules.h"
#include "laplace/persistence.h"
#include "laplace_pg_internal.h"
#include "perfcache_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_realization_materialize_utf8);

typedef struct materialization_pg_state {
    laplace_pg_perfcache_pin* unicode_pin;
    laplace_digest256 provider_fingerprint;
} materialization_pg_state;

static void materialization_provider_fingerprint(laplace_digest256* output) {
    static const char domain[] = "laplace-postgresql-cognition-materialization-provider-v1";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

static void read_exact_bytes(
    Datum datum,
    uint8_t* output,
    size_t expected,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != expected) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace %s must contain exactly %zu bytes", field, expected)));
    }
    memcpy(output, VARDATA_ANY(value), expected);
}

static uint32_t read_u32_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace %s must be nonnegative", field)));
    }
    return (uint32_t)value;
}

static uint64_t read_u64_attribute(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    Datum value = laplace_pg_required_composite_attribute(tuple, attribute, field);
    return laplace_pg_uint64_from_numeric(value, field);
}

static void read_realization(
    HeapTupleHeader tuple,
    laplace_cognition_realization_result* result) {
    memset(result, 0, sizeof(*result));
    read_exact_bytes(
        laplace_pg_required_composite_attribute(tuple, 1, "realization content_id"),
        result->content_id.bytes, sizeof(result->content_id.bytes),
        "realization content_id");
    read_exact_bytes(
        laplace_pg_required_composite_attribute(tuple, 2, "realization language_id"),
        result->language_id.bytes, sizeof(result->language_id.bytes),
        "realization language_id");
    read_exact_bytes(
        laplace_pg_required_composite_attribute(tuple, 3, "realization candidate_receipt_id"),
        result->candidate_receipt_id.bytes,
        sizeof(result->candidate_receipt_id.bytes),
        "realization candidate_receipt_id");
    read_exact_bytes(
        laplace_pg_required_composite_attribute(tuple, 4, "realization realization_recipe_id"),
        result->realization_recipe_id.bytes,
        sizeof(result->realization_recipe_id.bytes),
        "realization realization_recipe_id");
    read_exact_bytes(
        laplace_pg_required_composite_attribute(tuple, 5, "realization missing_obligation_fingerprint"),
        result->missing_obligation_fingerprint.bytes,
        sizeof(result->missing_obligation_fingerprint.bytes),
        "realization missing_obligation_fingerprint");
    result->preference_rank = read_u64_attribute(tuple, 6, "realization preference_rank");
    result->reused_subtree_count = read_u64_attribute(tuple, 7, "realization reused_subtree_count");
    result->generated_composition_count = read_u64_attribute(tuple, 8, "realization generated_composition_count");
    result->structural_tier = read_u32_attribute(tuple, 9, "realization structural_tier");
    result->match_class = read_u32_attribute(tuple, 10, "realization match_class");
    result->missing_obligation_count = read_u32_attribute(tuple, 11, "realization missing_obligation_count");
    result->disposition = read_u32_attribute(tuple, 12, "realization disposition");
    result->flags = read_u32_attribute(tuple, 13, "realization flags");
    result->version = read_u32_attribute(tuple, 14, "realization version");
}

static void read_materialization_request(
    HeapTupleHeader tuple,
    laplace_cognition_materialization_request* request) {
    memset(request, 0, sizeof(*request));
    request->maximum_nodes = read_u64_attribute(tuple, 1, "materialization maximum_nodes");
    request->maximum_trajectory_carriers =
        read_u64_attribute(tuple, 2, "materialization maximum_trajectory_carriers");
    request->maximum_output_bytes =
        read_u64_attribute(tuple, 3, "materialization maximum_output_bytes");
    request->maximum_depth = read_u32_attribute(tuple, 4, "materialization maximum_depth");
    request->version = read_u32_attribute(tuple, 5, "materialization version");
}

static int fetch_entity_witness(
    const laplace_id128* entity_id,
    laplace_digest256* witness) {
    static const char sql[] =
        "SELECT identity_witness FROM " LAPLACE_PG_SCHEMA ".entity WHERE entity_id=$1";
    Oid types[1] = {BYTEAOID};
    Datum values[1];
    bytea* entity = laplace_pg_bytes_to_bytea(entity_id->bytes, sizeof(entity_id->bytes));
    bool is_null = false;
    Datum datum;
    bytea* value;
    int status;

    values[0] = PointerGetDatum(entity);
    status = SPI_execute_with_args(sql, 1, types, values, NULL, true, 2);
    if (status != SPI_OK_SELECT || SPI_processed != 1u) {
        return 1;
    }
    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null) return 2;
    value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != sizeof(witness->bytes)) return 3;
    memcpy(witness->bytes, VARDATA_ANY(value), sizeof(witness->bytes));
    return 0;
}

static int resolve_unicode_atom(
    materialization_pg_state* state,
    const laplace_id128* entity_id,
    const laplace_digest256* witness,
    uint32_t* atom,
    uint8_t* found) {
    laplace_unicode_identity_key key;
    laplace_perfcache_registry_status status;
    memset(&key, 0, sizeof(key));
    key.content_id = *entity_id;
    key.identity_preimage_fingerprint = *witness;
    *atom = 0u;
    *found = 0u;
    status = laplace_perfcache_unicode_identity_reverse_resolve_batch(
        &state->unicode_pin->native_pin, &key, 1u, atom, found);
    return status == LAPLACE_PERFCACHE_REGISTRY_OK ? 0 : 1;
}

static int resolve_composition(
    const laplace_id128* entity_id,
    const laplace_digest256* witness,
    laplace_cognition_materialization_node* node) {
    static const char sql[] =
        "SELECT physicality_id, trajectory_fingerprint, logical_count, vertex_count, trajectory "
        "FROM " LAPLACE_PG_SCHEMA ".physicality "
        "WHERE entity_id=$1 AND physicality_type=$2 AND vertex_class=$3 AND structural_form=$4 "
        "ORDER BY physicality_id LIMIT 2";
    Oid types[4] = {BYTEAOID, INT4OID, INT4OID, INT4OID};
    Datum values[4];
    bytea* entity = laplace_pg_bytes_to_bytea(entity_id->bytes, sizeof(entity_id->bytes));
    bool is_null = false;
    Datum datum;
    bytea* binary;
    bytea* trajectory;
    uint64_t vertex_count;
    uint64_t logical_count;
    size_t expected_bytes;
    laplace_trajectory_carrier* carriers;
    laplace_composition_occurrence* occurrences;
    uint64_t decoded_logical_count = 0u;
    uint8_t max_child_tier = 0u;
    size_t index;
    int status;

    values[0] = PointerGetDatum(entity);
    values[1] = Int32GetDatum((int32)LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION);
    values[2] = Int32GetDatum((int32)LAPLACE_PERSISTENCE_VERTEX_TRAJECTORY_CARRIER);
    values[3] = Int32GetDatum((int32)LAPLACE_PERSISTENCE_STRUCTURAL_ORDERED_COMPOSITION);
    status = SPI_execute_with_args(sql, 4, types, values, NULL, true, 2);
    if (status != SPI_OK_SELECT || SPI_processed != 1u) return 1;

    memset(node, 0, sizeof(*node));
    node->entity_id = *entity_id;
    node->identity_witness = *witness;

    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null) return 2;
    binary = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(binary) != sizeof(node->physicality_id.bytes)) return 3;
    memcpy(node->physicality_id.bytes, VARDATA_ANY(binary), sizeof(node->physicality_id.bytes));
    node->node_receipt_id = node->physicality_id;

    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2, &is_null);
    if (is_null) return 4;
    binary = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(binary) != sizeof(node->trajectory_fingerprint.bytes)) return 5;
    memcpy(node->trajectory_fingerprint.bytes, VARDATA_ANY(binary), sizeof(node->trajectory_fingerprint.bytes));

    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3, &is_null);
    if (is_null) return 6;
    logical_count = laplace_pg_uint64_from_numeric(datum, "materialization physicality logical_count");
    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 4, &is_null);
    if (is_null) return 7;
    vertex_count = laplace_pg_uint64_from_numeric(datum, "materialization physicality vertex_count");
    if (vertex_count == 0u || vertex_count > (uint64_t)(SIZE_MAX / sizeof(laplace_trajectory_carrier))) return 8;

    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 5, &is_null);
    if (is_null) return 9;
    trajectory = DatumGetByteaPP(datum);
    expected_bytes = (size_t)vertex_count * sizeof(laplace_trajectory_carrier);
    if ((size_t)VARSIZE_ANY_EXHDR(trajectory) != expected_bytes) return 10;

    carriers = (laplace_trajectory_carrier*)palloc(expected_bytes);
    occurrences = (laplace_composition_occurrence*)palloc(
        (Size)((size_t)vertex_count * sizeof(*occurrences)));
    memcpy(carriers, VARDATA_ANY(trajectory), expected_bytes);
    if (laplace_trajectory_composition_decode(
            carriers, (size_t)vertex_count, occurrences, (size_t)vertex_count,
            &decoded_logical_count) != LAPLACE_TRAJECTORY_OK ||
        decoded_logical_count != logical_count) {
        return 11;
    }
    for (index = 0u; index < (size_t)vertex_count; ++index) {
        if (occurrences[index].tier > max_child_tier) {
            max_child_tier = occurrences[index].tier;
        }
    }
    if (max_child_tier == UINT8_MAX) return 12;

    node->logical_count = logical_count;
    node->carrier_count = vertex_count;
    node->kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION;
    node->tier_floor = (uint8_t)(max_child_tier + 1u);
    return 0;
}

static int pg_resolve_node(
    void* opaque,
    const laplace_id128* entity_id,
    laplace_cognition_materialization_node* node) {
    materialization_pg_state* state = (materialization_pg_state*)opaque;
    laplace_digest256 witness;
    uint32_t atom = 0u;
    uint8_t found = 0u;
    if (state == NULL || entity_id == NULL || node == NULL) return 1;
    memset(&witness, 0, sizeof(witness));
    if (fetch_entity_witness(entity_id, &witness) != 0) return 2;
    if (resolve_unicode_atom(state, entity_id, &witness, &atom, &found) != 0) return 3;
    if (found != 0u) {
        memset(node, 0, sizeof(*node));
        node->entity_id = *entity_id;
        node->identity_witness = witness;
        node->node_receipt_id = witness;
        node->logical_count = 1u;
        node->atom = atom;
        node->kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
        node->tier_floor = 0u;
        return 0;
    }
    return resolve_composition(entity_id, &witness, node) == 0 ? 0 : 4;
}

static int pg_read_trajectory(
    void* opaque,
    const laplace_cognition_materialization_node* node,
    laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_digest256* read_receipt_id) {
    static const char sql[] =
        "SELECT trajectory, trajectory_fingerprint FROM " LAPLACE_PG_SCHEMA
        ".physicality WHERE physicality_id=$1 AND entity_id=$2";
    Oid types[2] = {BYTEAOID, BYTEAOID};
    Datum values[2];
    bytea* physicality;
    bytea* entity;
    bool is_null = false;
    Datum datum;
    bytea* trajectory;
    bytea* fingerprint;
    size_t expected_bytes;
    int status;
    (void)opaque;
    if (node == NULL || carriers == NULL || read_receipt_id == NULL ||
        node->carrier_count != (uint64_t)carrier_count ||
        carrier_count > SIZE_MAX / sizeof(*carriers)) return 1;

    physicality = laplace_pg_bytes_to_bytea(node->physicality_id.bytes, sizeof(node->physicality_id.bytes));
    entity = laplace_pg_bytes_to_bytea(node->entity_id.bytes, sizeof(node->entity_id.bytes));
    values[0] = PointerGetDatum(physicality);
    values[1] = PointerGetDatum(entity);
    status = SPI_execute_with_args(sql, 2, types, values, NULL, true, 1);
    if (status != SPI_OK_SELECT || SPI_processed != 1u) return 2;
    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null) return 3;
    trajectory = DatumGetByteaPP(datum);
    expected_bytes = carrier_count * sizeof(*carriers);
    if ((size_t)VARSIZE_ANY_EXHDR(trajectory) != expected_bytes) return 4;
    memcpy(carriers, VARDATA_ANY(trajectory), expected_bytes);

    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2, &is_null);
    if (is_null) return 5;
    fingerprint = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(fingerprint) != sizeof(read_receipt_id->bytes)) return 6;
    memcpy(read_receipt_id->bytes, VARDATA_ANY(fingerprint), sizeof(read_receipt_id->bytes));
    return 0;
}

Datum laplace_pg_cognition_realization_materialize_utf8(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_cognition_realization_result realization;
    laplace_cognition_materialization_request request;
    materialization_pg_state state;
    laplace_cognition_materialization_provider_v1 provider;
    laplace_cognition_materialization_receipt receipt;
    laplace_cognition_materialization_status native_status;
    uint8_t* output;
    size_t output_bytes = 0u;
    Datum values[15] = {0};
    bool nulls[15] = {false};
    HeapTuple tuple;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    read_realization(DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &realization);
    read_materialization_request(DatumGetHeapTupleHeader(PG_GETARG_DATUM(2)), &request);

    if (request.maximum_output_bytes > context.resource_grant.memory_bytes ||
        request.maximum_output_bytes > (uint64_t)MaxAllocSize) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace materialization output exceeds the execution-context memory grant"),
                 errdetail("requested_bytes=%llu granted_bytes=%llu",
                           (unsigned long long)request.maximum_output_bytes,
                           (unsigned long long)context.resource_grant.memory_bytes)));
    }

    memset(&state, 0, sizeof(state));
    materialization_provider_fingerprint(&state.provider_fingerprint);
    memset(&provider, 0, sizeof(provider));
    provider.state = &state;
    provider.provider_fingerprint = state.provider_fingerprint;
    provider.resolve_node = pg_resolve_node;
    provider.read_trajectory = pg_read_trajectory;
    provider.abi_major = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR;

    if (laplace_pg_perfcache_pin_active(0u, NULL, &state.unicode_pin) !=
        LAPLACE_PG_PERFCACHE_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace materialization requires an active Unicode perfcache generation")));
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        laplace_pg_perfcache_pin_release(&state.unicode_pin);
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace materialization could not connect to SPI")));
    }

    output = (uint8_t*)palloc((Size)request.maximum_output_bytes);
    memset(&receipt, 0, sizeof(receipt));
    PG_TRY();
    {
        native_status = laplace_cognition_realization_materialize_utf8(
            &realization, &request, &provider, output,
            (size_t)request.maximum_output_bytes, &output_bytes, &receipt);
        SPI_finish();
        laplace_pg_perfcache_pin_release(&state.unicode_pin);
    }
    PG_CATCH();
    {
        SPI_finish();
        laplace_pg_perfcache_pin_release(&state.unicode_pin);
        PG_RE_THROW();
    }
    PG_END_TRY();

    if (native_status == LAPLACE_COGNITION_MATERIALIZATION_OK) {
        values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(output, output_bytes));
        values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.materialization_id.bytes, sizeof(receipt.materialization_id.bytes)));
        values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.source_candidate_receipt_id.bytes, sizeof(receipt.source_candidate_receipt_id.bytes)));
        values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.source_recipe_id.bytes, sizeof(receipt.source_recipe_id.bytes)));
        values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.provider_fingerprint.bytes, sizeof(receipt.provider_fingerprint.bytes)));
        values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.readset_fingerprint.bytes, sizeof(receipt.readset_fingerprint.bytes)));
        values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.output_fingerprint.bytes, sizeof(receipt.output_fingerprint.bytes)));
        values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt.root_content_id.bytes, sizeof(receipt.root_content_id.bytes)));
        values[8] = laplace_pg_numeric_from_uint64(receipt.resolved_node_count);
        values[9] = laplace_pg_numeric_from_uint64(receipt.trajectory_carrier_count);
        values[10] = laplace_pg_numeric_from_uint64(receipt.codepoint_count);
        values[11] = laplace_pg_numeric_from_uint64(receipt.output_bytes);
        values[12] = Int32GetDatum((int32)receipt.maximum_depth_observed);
        values[14] = Int32GetDatum((int32)receipt.version);
    } else {
        int index;
        for (index = 0; index <= 12; ++index) nulls[index] = true;
        values[14] = Int32GetDatum((int32)LAPLACE_COGNITION_MATERIALIZATION_RECEIPT_VERSION);
    }
    values[13] = Int32GetDatum((int32)native_status);

    tuple = laplace_pg_form_result_tuple(fcinfo, values, nulls, 15);
    PG_RETURN_DATUM(HeapTupleGetDatum(tuple));
}
