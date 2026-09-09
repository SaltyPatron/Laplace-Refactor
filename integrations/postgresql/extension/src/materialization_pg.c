#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "blake3.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/perfcache_modules.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"
#include "laplace_pg_internal.h"
#include "materialization_pg.h"
#include "perfcache_pg.h"

struct laplace_pg_materialization_provider_state {
    laplace_framework_context context;
    laplace_digest256 provider_fingerprint;
    uint64_t resolved_nodes;
    uint64_t trajectory_reads;
    uint64_t trajectory_bytes;
    uint64_t database_operations;
    blake3_hasher readset;
    MemoryContext caller_context;
    MemoryContext scratch_context;
    ErrorData* error;
    MemoryContextCallback cleanup;
};

static bool materialization_digest_equal(
    const laplace_digest256* left,
    const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static Datum materialization_required_value(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    const char* field) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization %s cannot be null", field)));
    }
    return value;
}

static void materialization_read_exact(
    Datum datum,
    uint8_t* output,
    size_t expected,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != expected) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization %s has invalid width", field),
                 errdetail("expected=%zu actual=%zu", expected,
                           (size_t)VARSIZE_ANY_EXHDR(value))));
    }
    memcpy(output, VARDATA_ANY(value), expected);
}

static uint64_t materialization_numeric_u64(
    Datum datum,
    const char* field) {
    return laplace_pg_uint64_from_numeric(datum, field);
}

static void materialization_hash_u32(
    blake3_hasher* hasher,
    uint32_t value) {
    uint8_t bytes[4];
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void materialization_hash_u64(
    blake3_hasher* hasher,
    uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void materialization_provider_identify(
    const laplace_framework_context* context,
    laplace_digest256* fingerprint) {
    static const char domain[] =
        "laplace-postgresql-cognition-materialization-provider-v1";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(
        &hasher,
        context->epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes,
        sizeof(context->epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes));
    blake3_hasher_update(
        &hasher,
        context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes,
        sizeof(context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes));
    blake3_hasher_finalize(
        &hasher, fingerprint->bytes, sizeof(fingerprint->bytes));
}

static void materialization_node_receipt(
    const laplace_pg_materialization_provider_state* state,
    const laplace_cognition_materialization_node* node,
    laplace_digest256* receipt) {
    static const char domain[] =
        "laplace-postgresql-materialization-node-read-v1";
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(
        &hasher,
        state->provider_fingerprint.bytes,
        sizeof(state->provider_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, node->entity_id.bytes, sizeof(node->entity_id.bytes));
    blake3_hasher_update(
        &hasher,
        node->identity_witness.bytes,
        sizeof(node->identity_witness.bytes));
    blake3_hasher_update(
        &hasher,
        node->physicality_id.bytes,
        sizeof(node->physicality_id.bytes));
    blake3_hasher_update(
        &hasher,
        node->trajectory_fingerprint.bytes,
        sizeof(node->trajectory_fingerprint.bytes));
    materialization_hash_u64(&hasher, node->logical_count);
    materialization_hash_u64(&hasher, node->carrier_count);
    materialization_hash_u32(&hasher, node->atom);
    materialization_hash_u32(&hasher, node->kind);
    materialization_hash_u32(&hasher, node->tier_floor);
    blake3_hasher_finalize(&hasher, receipt->bytes, sizeof(receipt->bytes));
}

static void materialization_note_node(
    laplace_pg_materialization_provider_state* state,
    const laplace_cognition_materialization_node* node) {
    static const uint8_t tag = 1u;
    blake3_hasher_update(&state->readset, &tag, sizeof(tag));
    blake3_hasher_update(
        &state->readset, node->node_receipt_id.bytes,
        sizeof(node->node_receipt_id.bytes));
}

static void materialization_note_trajectory(
    laplace_pg_materialization_provider_state* state,
    const laplace_digest256* receipt) {
    static const uint8_t tag = 2u;
    blake3_hasher_update(&state->readset, &tag, sizeof(tag));
    blake3_hasher_update(
        &state->readset, receipt->bytes, sizeof(receipt->bytes));
}

static void materialization_require_active_perfcache(
    const laplace_pg_materialization_provider_state* state,
    laplace_pg_perfcache_pin** pin) {
    laplace_pg_perfcache_status status =
        laplace_pg_perfcache_pin_active(0u, NULL, pin);
    if (status != LAPLACE_PG_PERFCACHE_OK || pin == NULL || *pin == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace materialization could not pin the active Unicode generation"),
                 errdetail("status=%u", (unsigned int)status)));
    }
    if (!materialization_digest_equal(
            &state->context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE],
            &(*pin)->epoch.epoch_fingerprint)) {
        laplace_pg_perfcache_pin_release(pin);
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace materialization context does not pin the active Unicode generation")));
    }
}

static void materialization_read_entity_witness(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id,
    laplace_digest256* witness) {
    static const char sql[] =
        "SELECT identity_witness FROM " LAPLACE_PG_SCHEMA
        ".entity WHERE entity_id=$1";
    Oid types[1] = {BYTEAOID};
    Datum values[1];
    int result;
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        entity_id->bytes, sizeof(entity_id->bytes)));
    result = SPI_execute_with_args(sql, 1, types, values, NULL, true, 1);
    ++state->database_operations;
    if (result != SPI_OK_SELECT || SPI_processed != 1u || SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_NO_DATA_FOUND),
                 errmsg("Laplace materialization entity is not uniquely persisted"),
                 errdetail("matches=%llu",
                           (unsigned long long)SPI_processed)));
    }
    materialization_read_exact(
        materialization_required_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
            "identity witness"),
        witness->bytes, sizeof(witness->bytes), "identity witness");
    SPI_freetuptable(SPI_tuptable);
}

static bool materialization_resolve_atom(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id,
    const laplace_digest256* witness,
    laplace_cognition_materialization_node* node) {
    laplace_pg_perfcache_pin* pin = NULL;
    laplace_unicode_identity_key key;
    uint32_t position = 0u;
    uint8_t reverse_found = 0u;
    laplace_unicode_atom_record_view atom;
    uint8_t atom_found = 0u;
    laplace_perfcache_registry_status status;

    memset(&key, 0, sizeof(key));
    memset(&atom, 0, sizeof(atom));
    key.content_id = *entity_id;
    key.identity_preimage_fingerprint = *witness;
    materialization_require_active_perfcache(state, &pin);
    PG_TRY();
    {
        status = laplace_perfcache_unicode_identity_reverse_resolve_batch(
            &pin->native_pin, &key, 1u, &position, &reverse_found);
        if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace Unicode reverse materialization lookup failed"),
                     errdetail("status=%u", (unsigned int)status)));
        }
        if (reverse_found != 0u) {
            status = laplace_perfcache_unicode_tier0_resolve_batch(
                &pin->native_pin, &position, 1u, &atom, &atom_found);
            if (status != LAPLACE_PERFCACHE_REGISTRY_OK || atom_found == 0u ||
                memcmp(atom.value.content_id.bytes, entity_id->bytes,
                       sizeof(entity_id->bytes)) != 0 ||
                memcmp(atom.value.identity_preimage_fingerprint.bytes,
                       witness->bytes, sizeof(witness->bytes)) != 0) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace Unicode direct materialization lookup disagrees with reverse identity")));
            }
        }
        laplace_pg_perfcache_pin_release(&pin);
    }
    PG_CATCH();
    {
        laplace_pg_perfcache_pin_release(&pin);
        PG_RE_THROW();
    }
    PG_END_TRY();

    if (reverse_found == 0u) {
        return false;
    }
    memset(node, 0, sizeof(*node));
    node->entity_id = *entity_id;
    node->identity_witness = *witness;
    node->logical_count = 1u;
    node->atom = position;
    node->kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
    node->tier_floor = 0u;
    materialization_node_receipt(state, node, &node->node_receipt_id);
    return true;
}

static void materialization_resolve_composition(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id,
    const laplace_digest256* witness,
    laplace_cognition_materialization_node* node) {
    static const char sql[] =
        "SELECT physicality_id,trajectory_fingerprint,logical_count,vertex_count,trajectory "
        "FROM " LAPLACE_PG_SCHEMA ".physicality "
        "WHERE entity_id=$1 AND physicality_type=$2 AND geometry_epoch=$3 "
        "ORDER BY physicality_id";
    Oid types[3] = {BYTEAOID, INT4OID, BYTEAOID};
    Datum values[3];
    int result;
    HeapTuple tuple;
    TupleDesc descriptor;
    bytea* trajectory;
    uint64_t carrier_count;
    uint64_t logical_count;
    size_t trajectory_bytes;
    laplace_composition_occurrence* occurrences;
    uint64_t decoded_logical_count = 0u;
    uint8_t maximum_child_tier = 0u;
    size_t index;

    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        entity_id->bytes, sizeof(entity_id->bytes)));
    values[1] = Int32GetDatum((int32)LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION);
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes,
        sizeof(state->context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes)));
    result = SPI_execute_with_args(sql, 3, types, values, NULL, true, 2);
    ++state->database_operations;
    if (result != SPI_OK_SELECT || SPI_processed != 1u || SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization composition physicality is not unique in the pinned geometry epoch"),
                 errdetail("matches=%llu",
                           (unsigned long long)SPI_processed)));
    }

    tuple = SPI_tuptable->vals[0];
    descriptor = SPI_tuptable->tupdesc;
    memset(node, 0, sizeof(*node));
    node->entity_id = *entity_id;
    node->identity_witness = *witness;
    materialization_read_exact(
        materialization_required_value(tuple, descriptor, 1, "physicality id"),
        node->physicality_id.bytes, sizeof(node->physicality_id.bytes),
        "physicality id");
    materialization_read_exact(
        materialization_required_value(
            tuple, descriptor, 2, "trajectory fingerprint"),
        node->trajectory_fingerprint.bytes,
        sizeof(node->trajectory_fingerprint.bytes),
        "trajectory fingerprint");
    logical_count = materialization_numeric_u64(
        materialization_required_value(tuple, descriptor, 3, "logical count"),
        "materialization logical count");
    carrier_count = materialization_numeric_u64(
        materialization_required_value(tuple, descriptor, 4, "vertex count"),
        "materialization vertex count");
    trajectory = DatumGetByteaPP(
        materialization_required_value(tuple, descriptor, 5, "trajectory"));
    trajectory_bytes = (size_t)VARSIZE_ANY_EXHDR(trajectory);
    if (carrier_count == 0u || carrier_count > (uint64_t)SIZE_MAX ||
        carrier_count > (uint64_t)(SIZE_MAX / sizeof(laplace_trajectory_carrier)) ||
        trajectory_bytes != (size_t)carrier_count * sizeof(laplace_trajectory_carrier)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization composition trajectory size is invalid")));
    }
    occurrences = (laplace_composition_occurrence*)palloc0(
        sizeof(*occurrences) * (size_t)carrier_count);
    if (laplace_trajectory_composition_decode(
            (const laplace_trajectory_carrier*)VARDATA_ANY(trajectory),
            (size_t)carrier_count,
            occurrences,
            (size_t)carrier_count,
            &decoded_logical_count) != LAPLACE_TRAJECTORY_OK ||
        decoded_logical_count != logical_count || logical_count <= 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization composition trajectory cannot reconstruct its logical cardinality")));
    }
    for (index = 0u; index < (size_t)carrier_count; ++index) {
        if (occurrences[index].run_length == 0u ||
            occurrences[index].tier >= LAPLACE_COMPOSITION_TIER_MAXIMUM) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace materialization composition trajectory contains an invalid tier")));
        }
        if (occurrences[index].tier > maximum_child_tier) {
            maximum_child_tier = occurrences[index].tier;
        }
    }
    node->logical_count = logical_count;
    node->carrier_count = carrier_count;
    node->kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION;
    node->tier_floor = (uint8_t)(maximum_child_tier + 1u);
    materialization_node_receipt(state, node, &node->node_receipt_id);
    SPI_freetuptable(SPI_tuptable);
}

static int materialization_resolve_node_impl(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id,
    laplace_cognition_materialization_node* node) {
    laplace_digest256 witness;
    if (state == NULL || entity_id == NULL || node == NULL) {
        return 1;
    }
    memset(&witness, 0, sizeof(witness));
    memset(node, 0, sizeof(*node));
    materialization_read_entity_witness(state, entity_id, &witness);
    if (!materialization_resolve_atom(state, entity_id, &witness, node)) {
        materialization_resolve_composition(state, entity_id, &witness, node);
    }
    ++state->resolved_nodes;
    materialization_note_node(state, node);
    return 0;
}

static int materialization_resolve_node(
    void* opaque,
    const laplace_id128* entity_id,
    laplace_cognition_materialization_node* node) {
    laplace_pg_materialization_provider_state* state =
        (laplace_pg_materialization_provider_state*)opaque;
    volatile int status = 0;
    MemoryContext previous;
    if (node != NULL) {
        memset(node, 0, sizeof(*node));
    }
    if (state == NULL || state->error != NULL || state->scratch_context == NULL) {
        return 1;
    }
    previous = MemoryContextSwitchTo(state->scratch_context);
    PG_TRY();
    {
        status = materialization_resolve_node_impl(state, entity_id, node);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(state->caller_context);
        state->error = CopyErrorData();
        FlushErrorState();
        if (node != NULL) {
            memset(node, 0, sizeof(*node));
        }
        status = 1;
    }
    PG_END_TRY();
    MemoryContextSwitchTo(previous);
    MemoryContextReset(state->scratch_context);
    return (int)status;
}

static int materialization_read_trajectory_impl(
    laplace_pg_materialization_provider_state* state,
    const laplace_cognition_materialization_node* node,
    laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_digest256* read_receipt_id) {
    static const char sql[] =
        "SELECT trajectory FROM " LAPLACE_PG_SCHEMA ".physicality "
        "WHERE physicality_id=$1 AND entity_id=$2 AND trajectory_fingerprint=$3";
    static const char receipt_domain[] =
        "laplace-postgresql-materialization-trajectory-read-v1";
    Oid types[3] = {BYTEAOID, BYTEAOID, BYTEAOID};
    Datum values[3];
    int result;
    bytea* trajectory;
    size_t bytes;
    blake3_hasher hasher;

    if (state == NULL || node == NULL || carriers == NULL ||
        read_receipt_id == NULL || carrier_count == 0u ||
        node->kind != LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION ||
        node->carrier_count != (uint64_t)carrier_count ||
        carrier_count > SIZE_MAX / sizeof(*carriers)) {
        return 1;
    }
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        node->physicality_id.bytes, sizeof(node->physicality_id.bytes)));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        node->entity_id.bytes, sizeof(node->entity_id.bytes)));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        node->trajectory_fingerprint.bytes,
        sizeof(node->trajectory_fingerprint.bytes)));
    result = SPI_execute_with_args(sql, 3, types, values, NULL, true, 1);
    ++state->database_operations;
    if (result != SPI_OK_SELECT || SPI_processed != 1u || SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization trajectory is not uniquely persisted"),
                 errdetail("matches=%llu",
                           (unsigned long long)SPI_processed)));
    }
    trajectory = DatumGetByteaPP(materialization_required_value(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, "trajectory"));
    bytes = (size_t)VARSIZE_ANY_EXHDR(trajectory);
    if (bytes != carrier_count * sizeof(*carriers)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization trajectory byte count changed between node and payload reads")));
    }
    memcpy(carriers, VARDATA_ANY(trajectory), bytes);
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, receipt_domain, sizeof(receipt_domain) - 1u);
    blake3_hasher_update(
        &hasher,
        state->provider_fingerprint.bytes,
        sizeof(state->provider_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, node->node_receipt_id.bytes,
        sizeof(node->node_receipt_id.bytes));
    blake3_hasher_update(&hasher, VARDATA_ANY(trajectory), bytes);
    blake3_hasher_finalize(
        &hasher, read_receipt_id->bytes, sizeof(read_receipt_id->bytes));
    ++state->trajectory_reads;
    if (state->trajectory_bytes > UINT64_MAX - (uint64_t)bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace materialization trajectory accounting overflowed")));
    }
    state->trajectory_bytes += (uint64_t)bytes;
    materialization_note_trajectory(state, read_receipt_id);
    SPI_freetuptable(SPI_tuptable);
    return 0;
}

static int materialization_read_trajectory(
    void* opaque,
    const laplace_cognition_materialization_node* node,
    laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_digest256* read_receipt_id) {
    laplace_pg_materialization_provider_state* state =
        (laplace_pg_materialization_provider_state*)opaque;
    volatile int status = 0;
    MemoryContext previous;
    if (read_receipt_id != NULL) {
        memset(read_receipt_id, 0, sizeof(*read_receipt_id));
    }
    if (state == NULL || state->error != NULL || state->scratch_context == NULL) {
        return 1;
    }
    previous = MemoryContextSwitchTo(state->scratch_context);
    PG_TRY();
    {
        status = materialization_read_trajectory_impl(
            state, node, carriers, carrier_count, read_receipt_id);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(state->caller_context);
        state->error = CopyErrorData();
        FlushErrorState();
        if (read_receipt_id != NULL) {
            memset(read_receipt_id, 0, sizeof(*read_receipt_id));
        }
        status = 1;
    }
    PG_END_TRY();
    MemoryContextSwitchTo(previous);
    MemoryContextReset(state->scratch_context);
    return (int)status;
}

static void materialization_provider_release(void* opaque) {
    laplace_pg_materialization_provider_state* state =
        (laplace_pg_materialization_provider_state*)opaque;
    if (state == NULL) {
        return;
    }
    state->scratch_context = NULL;
    if (state->error != NULL) {
        FreeErrorData(state->error);
        state->error = NULL;
    }
}

void laplace_pg_materialization_provider_create(
    const laplace_framework_context* context,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider) {
    laplace_pg_materialization_provider_state* state;
    const uint64_t required_epochs =
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_GEOMETRY) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE);

    if (owner != NULL) {
        *owner = NULL;
    }
    if (provider != NULL) {
        memset(provider, 0, sizeof(*provider));
    }
    if (context == NULL || owner == NULL || provider == NULL ||
        laplace_framework_context_validate(context) != LAPLACE_FRAMEWORK_OK ||
        (context->epoch_mask & required_epochs) != required_epochs) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace materialization provider requires pinned geometry and perfcache epochs")));
    }

    state = (laplace_pg_materialization_provider_state*)palloc0(sizeof(*state));
    state->context = *context;
    state->caller_context = CurrentMemoryContext;
    state->scratch_context = AllocSetContextCreate(
        CurrentMemoryContext,
        "Laplace materialization read batch",
        ALLOCSET_DEFAULT_SIZES);
    state->cleanup.func = materialization_provider_release;
    state->cleanup.arg = state;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &state->cleanup);
    materialization_provider_identify(context, &state->provider_fingerprint);
    blake3_hasher_init(&state->readset);
    {
        static const char readset_domain[] =
            "laplace-postgresql-materialization-readset-v1";
        blake3_hasher_update(
            &state->readset, readset_domain, sizeof(readset_domain) - 1u);
        blake3_hasher_update(
            &state->readset,
            state->provider_fingerprint.bytes,
            sizeof(state->provider_fingerprint.bytes));
    }

    provider->state = state;
    provider->provider_fingerprint = state->provider_fingerprint;
    provider->resolve_node = materialization_resolve_node;
    provider->read_trajectory = materialization_read_trajectory;
    provider->abi_major = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_MATERIALIZATION_PROVIDER_ABI_MINOR;
    *owner = state;
}

void laplace_pg_materialization_provider_summary(
    const laplace_pg_materialization_provider_state* owner,
    laplace_pg_materialization_provider_report* report) {
    blake3_hasher copy;
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
    if (owner == NULL) {
        return;
    }
    report->provider_fingerprint = owner->provider_fingerprint;
    copy = owner->readset;
    blake3_hasher_finalize(
        &copy,
        report->readset_fingerprint.bytes,
        sizeof(report->readset_fingerprint.bytes));
    report->resolved_nodes = owner->resolved_nodes;
    report->trajectory_reads = owner->trajectory_reads;
    report->trajectory_bytes = owner->trajectory_bytes;
    report->database_operations = owner->database_operations;
}

ErrorData* laplace_pg_materialization_provider_take_error(
    laplace_pg_materialization_provider_state* owner) {
    ErrorData* error;
    if (owner == NULL) {
        return NULL;
    }
    error = owner->error;
    owner->error = NULL;
    return error;
}

void laplace_pg_materialization_provider_destroy(
    laplace_pg_materialization_provider_state** owner) {
    if (owner == NULL || *owner == NULL) {
        return;
    }
    if ((*owner)->scratch_context != NULL) {
        MemoryContextDelete((*owner)->scratch_context);
        (*owner)->scratch_context = NULL;
    }
    materialization_provider_release(*owner);
    *owner = NULL;
}
