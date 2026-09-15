#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/hsearch.h"
#include "utils/memutils.h"

#include "blake3.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/perfcache_modules.h"
#include "laplace/persistence.h"
#include "laplace/trajectory.h"
#include "laplace_pg_internal.h"
#include "materialization_pg.h"
#include "perfcache_pg.h"
#include "physicality_entity_pg.h"

typedef struct materialization_cache_key {
    laplace_id128 entity_id;
    laplace_digest256 selected_physicality_id;
} materialization_cache_key;

typedef struct materialization_cache_entry {
    materialization_cache_key key;
    laplace_cognition_materialization_node node;
    laplace_persistence_physicality_record physicality;
    laplace_digest256 derivation_receipt;
    laplace_trajectory_carrier* trajectory;
    uint64_t carrier_count;
    size_t frontier_index;
    uint8_t ready;
    uint8_t resolving;
    uint8_t children_prefetched;
    uint8_t reserved8;
} materialization_cache_entry;

struct laplace_pg_materialization_provider_state {
    laplace_framework_context context;
    laplace_digest256 provider_fingerprint;
    uint64_t resolved_nodes;
    uint64_t trajectory_reads;
    uint64_t trajectory_bytes;
    uint64_t database_operations;
    uint64_t node_batch_count;
    uint64_t prefetched_node_count;
    blake3_hasher readset;
    MemoryContext caller_context;
    MemoryContext scratch_context;
    MemoryContext cache_context;
    MemoryContext binding_context;
    MemoryContext scope_context;
    HTAB* cache;
    laplace_pg_materialization_selection* selections;
    size_t selection_count;
    laplace_pg_materialization_selection* base_selections;
    size_t base_selection_count;
    laplace_digest256 scope_view_id;
    laplace_digest256 required_view_id;
    laplace_digest256 required_original_physicality;
    bool scope_historical;
    const laplace_composition_known_entity* scope_external;
    size_t scope_external_count;
    laplace_id128 scope_root;
    laplace_digest256 scope_root_physicality;
    laplace_digest256 scope_receipt;
    laplace_physicality_occurrence_binding* scope_occurrences;
    size_t scope_occurrence_count;
    ErrorData* error;
    MemoryContextCallback cleanup;
};

static bool materialization_digest_equal(
    const laplace_digest256* left,
    const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static bool materialization_id_equal(
    const laplace_id128* left,
    const laplace_id128* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static int materialization_selection_compare(const void* left, const void* right) {
    return memcmp(((const laplace_pg_materialization_selection*)left)->entity_id.bytes,
                  ((const laplace_pg_materialization_selection*)right)->entity_id.bytes,
                  sizeof(laplace_id128));
}

static const laplace_pg_materialization_selection* materialization_selection_find(
    const laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id) {
    laplace_pg_materialization_selection key;
    if (state->selection_count == 0u) return NULL;
    memset(&key, 0, sizeof(key));
    key.entity_id = *entity_id;
    return (const laplace_pg_materialization_selection*)bsearch(
        &key, state->selections, state->selection_count,
        sizeof(*state->selections), materialization_selection_compare);
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
    const laplace_digest256* selection_receipt,
    laplace_digest256* fingerprint) {
    static const char domain[] =
        "laplace-postgresql-cognition-materialization-provider-v2";
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
    if (selection_receipt != NULL) {
        /* The authority covers the complete verified binding set. Each node
         * receipt binds the physicality actually read; a subset of requested
         * roots must not change the receipt of an otherwise identical read. */
        static const char selected_domain[] =
            "laplace-postgresql-materialization-physicality-selection-v1";
        blake3_hasher_update(&hasher, selected_domain, sizeof(selected_domain) - 1u);
        blake3_hasher_update(&hasher, selection_receipt->bytes,
            sizeof(selection_receipt->bytes));
    }
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

static void materialization_note_prefetch_node(
    laplace_pg_materialization_provider_state* state,
    const laplace_cognition_materialization_node* node) {
    static const uint8_t tag = 3u;
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

static materialization_cache_key materialization_key(
    const laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id, const laplace_digest256* selected) {
    materialization_cache_key key;
    memset(&key, 0, sizeof(key));
    key.entity_id = *entity_id;
    if (selected != NULL) key.selected_physicality_id = *selected;
    else {
        const laplace_pg_materialization_selection* selection =
            materialization_selection_find(state, entity_id);
        if (selection != NULL) key.selected_physicality_id = selection->physicality_id;
    }
    return key;
}

static materialization_cache_entry* materialization_cache_find_selected(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id, const laplace_digest256* selected) {
    materialization_cache_key key = materialization_key(state, entity_id, selected);
    return (materialization_cache_entry*)hash_search(state->cache, &key, HASH_FIND, NULL);
}

static materialization_cache_entry* materialization_cache_find(
    laplace_pg_materialization_provider_state* state, const laplace_id128* entity_id) {
    return materialization_cache_find_selected(state, entity_id, NULL);
}

static materialization_cache_entry* materialization_cache_enter_selected(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id, const laplace_digest256* selected, bool* found) {
    materialization_cache_key key = materialization_key(state, entity_id, selected);
    materialization_cache_entry* entry =
        (materialization_cache_entry*)hash_search(state->cache, &key, HASH_ENTER, found);
    if (!*found) memset(((char*)entry) + sizeof(entry->key), 0,
        sizeof(*entry) - sizeof(entry->key));
    return entry;
}

static materialization_cache_entry* materialization_cache_enter(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id, bool* found) {
    return materialization_cache_enter_selected(state, entity_id, NULL, found);
}

static void materialization_reset_resolving(
    laplace_pg_materialization_provider_state* state) {
    HASH_SEQ_STATUS scan;
    materialization_cache_entry* entry;
    hash_seq_init(&scan, state->cache);
    while ((entry = (materialization_cache_entry*)hash_seq_search(&scan)) != NULL) {
        if (entry->resolving != 0u && entry->ready == 0u) {
            if (entry->trajectory != NULL) pfree(entry->trajectory);
            entry->trajectory = NULL;
            entry->carrier_count = 0u;
            memset(&entry->node, 0, sizeof(entry->node));
        }
        entry->resolving = 0u;
    }
}

static ArrayType* materialization_id_array(
    const laplace_id128* ids,
    size_t count) {
    Datum* values;
    size_t index;
    if (count == 0u || count > (size_t)INT_MAX ||
        count > MaxAllocSize / sizeof(*values)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace materialization frontier exceeds PostgreSQL array addressability")));
    }
    values = (Datum*)palloc(sizeof(*values) * count);
    for (index = 0u; index < count; ++index) {
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            ids[index].bytes, sizeof(ids[index].bytes)));
    }
    return construct_array(
        values, (int)count, BYTEAOID, -1, false, TYPALIGN_INT);
}

static ArrayType* materialization_selection_array(
    const laplace_digest256* selected, size_t count) {
    Datum* values = (Datum*)palloc0(sizeof(*values) * count);
    bool* nulls = (bool*)palloc0(sizeof(*nulls) * count);
    int dimensions[1] = {(int)count};
    int lower_bounds[1] = {1};
    size_t index;
    for (index = 0u; index < count; ++index) {
        static const laplace_digest256 zero = {{0}};
        nulls[index] = materialization_digest_equal(&selected[index], &zero);
        if (!nulls[index]) values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            selected[index].bytes, sizeof(selected[index].bytes)));
    }
    return construct_md_array(values, nulls, 1, dimensions, lower_bounds,
        BYTEAOID, -1, false, TYPALIGN_INT);
}

static void materialization_cache_trajectory(
    laplace_pg_materialization_provider_state* state,
    materialization_cache_entry* entry,
    const void* trajectory, size_t trajectory_bytes,
    uint64_t carrier_count,
    uint64_t logical_count) {
    MemoryContext prior;
    uint64_t ordinal = 1u;
    uint8_t maximum_child_tier = 0u;
    size_t bytes;
    size_t index;

    if (carrier_count == 0u || carrier_count > (uint64_t)(SIZE_MAX / sizeof(laplace_trajectory_carrier))) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization composition carrier count is invalid")));
    }
    bytes = (size_t)carrier_count * sizeof(laplace_trajectory_carrier);
    if (trajectory_bytes != bytes || logical_count == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization composition trajectory size is invalid")));
    }
    for (index = 0u; index < (size_t)carrier_count; ++index) {
        laplace_trajectory_carrier carrier;
        laplace_composition_occurrence occurrence;
        memcpy(
            &carrier,
            (const char*)trajectory + index * sizeof(carrier),
            sizeof(carrier));
        if (laplace_trajectory_composition_decode_one(
                &carrier, ordinal, &occurrence) != LAPLACE_TRAJECTORY_OK ||
            occurrence.run_length == 0u ||
            occurrence.tier >= LAPLACE_COMPOSITION_TIER_MAXIMUM ||
            UINT64_MAX - ordinal < (uint64_t)occurrence.run_length) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace materialization composition trajectory is invalid")));
        }
        ordinal += (uint64_t)occurrence.run_length;
        if (occurrence.tier > maximum_child_tier) {
            maximum_child_tier = occurrence.tier;
        }
    }
    if (ordinal - 1u != logical_count ||
        maximum_child_tier == UINT8_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization composition logical count is inconsistent")));
    }

    prior = MemoryContextSwitchTo(state->cache_context);
    entry->trajectory = (laplace_trajectory_carrier*)palloc(bytes);
    memcpy(entry->trajectory, trajectory, bytes);
    MemoryContextSwitchTo(prior);
    entry->carrier_count = carrier_count;
    entry->node.logical_count = logical_count;
    entry->node.carrier_count = carrier_count;
    entry->node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION;
    entry->node.tier_floor = logical_count == 1u
        ? maximum_child_tier : (uint8_t)(maximum_child_tier + 1u);
}

static void materialization_verify_physicality(
    HeapTuple tuple, TupleDesc descriptor,
    const laplace_cognition_materialization_node* node,
    uint64_t logical_count, uint64_t carrier_count,
    laplace_persistence_physicality_record* output) {
    laplace_persistence_physicality_record physicality;
    laplace_digest256 actual;
    unsigned index;
    memset(&physicality, 0, sizeof(physicality));
    physicality.physicality_id = node->physicality_id;
    physicality.trajectory_fingerprint = node->trajectory_fingerprint;
    physicality.logical_count = logical_count;
    physicality.vertex_count = carrier_count;
    materialization_read_exact(materialization_required_value(
        tuple, descriptor, 7, "physicality entity id"), physicality.entity_id.bytes,
        sizeof(physicality.entity_id.bytes), "physicality entity id");
    physicality.physicality_type = (uint32_t)DatumGetInt32(
        materialization_required_value(tuple, descriptor, 8, "physicality type"));
    physicality.vertex_class = (uint32_t)DatumGetInt32(
        materialization_required_value(tuple, descriptor, 9, "physicality vertex class"));
    physicality.recipe_version = (uint32_t)DatumGetInt32(
        materialization_required_value(tuple, descriptor, 10, "physicality recipe version"));
    physicality.structural_form = (uint32_t)DatumGetInt32(
        materialization_required_value(tuple, descriptor, 11, "physicality structural form"));
    physicality.dimension_count = (uint32_t)DatumGetInt32(
        materialization_required_value(tuple, descriptor, 12, "physicality dimension count"));
    physicality.flags = (uint32_t)DatumGetInt32(
        materialization_required_value(tuple, descriptor, 13, "physicality flags"));
    materialization_read_exact(materialization_required_value(
        tuple, descriptor, 14, "physicality recipe fingerprint"), physicality.recipe_fingerprint.bytes,
        sizeof(physicality.recipe_fingerprint.bytes), "physicality recipe fingerprint");
    materialization_read_exact(materialization_required_value(
        tuple, descriptor, 15, "physicality geometry epoch"), physicality.geometry_epoch.bytes,
        sizeof(physicality.geometry_epoch.bytes), "physicality geometry epoch");
    for (index = 0u; index < LAPLACE_GEOMETRY_COMPONENTS; ++index)
        physicality.centroid.component[index] = DatumGetFloat8(
            materialization_required_value(tuple, descriptor, (int)index + 16, "physicality centroid"));
    physicality.radius = DatumGetFloat8(
        materialization_required_value(tuple, descriptor, 20, "physicality radius"));
    if (!materialization_id_equal(&physicality.entity_id, &node->entity_id) ||
        laplace_persistence_physicality_identify(&physicality, &actual) != LAPLACE_PERSISTENCE_OK ||
        !materialization_digest_equal(&physicality.physicality_id, &actual))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace materialization physicality body differs from its native identity")));
    *output = physicality;
}

static uint64_t materialization_reflection_grant(
    const laplace_pg_materialization_provider_state* state) {
    uint64_t used = (uint64_t)MemoryContextMemAllocated(state->cache_context, true);
    const MemoryContext contexts[3] = {
        state->scratch_context, state->binding_context, state->scope_context};
    for (size_t index = 0u; index < 3u; ++index) {
        const uint64_t bytes = (uint64_t)MemoryContextMemAllocated(contexts[index], true);
        if (UINT64_MAX - used < bytes)
            ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                errmsg("Laplace materialization memory accounting overflowed")));
        used += bytes;
    }
    if (used >= state->context.resource_grant.memory_bytes)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace materialization has no memory grant for derived views")));
    /* Reserve equal space for the verified result's cache copy while native
     * reconstruction uses its own checked estimator within the other half. */
    return (state->context.resource_grant.memory_bytes - used) / 2u;
}

static void materialization_resolve_derived(
    laplace_pg_materialization_provider_state* state,
    const laplace_digest256* view_id, const laplace_id128* ids,
    const laplace_digest256* explicit_selected, size_t count,
    laplace_pg_physicality_entity_view** views, size_t* view_count) {
    laplace_digest256* selected;
    uint64_t operations = 0u;
    if (count > MaxAllocSize / sizeof(*selected) ||
        count > (UINT64_MAX - 1u) / 2u ||
        state->database_operations > UINT64_MAX - LAPLACE_PG_PHYSICALITY_ENTITY_READ_MAX_OPERATIONS)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace materialization derived frontier exceeds its finite boundary")));
    selected = (laplace_digest256*)palloc0(count * sizeof(*selected));
    for (size_t index = 0u; index < count; ++index) {
        const laplace_pg_materialization_selection* selection =
            materialization_selection_find(state, &ids[index]);
        if (explicit_selected != NULL) selected[index] = explicit_selected[index];
        else if (selection != NULL) selected[index] = selection->physicality_id;
    }
    laplace_pg_physicality_entity_resolve_scoped(&state->context, view_id, ids,
        selected, count, (uint64_t)count * 2u + 1u, materialization_reflection_grant(state),
        LAPLACE_PG_PHYSICALITY_ENTITY_READ_MAX_OPERATIONS, views, view_count, &operations);
    state->database_operations += operations;
}

static void materialization_resolve_view_owner(
    laplace_pg_materialization_provider_state* state,
    const laplace_digest256* source,
    laplace_pg_physicality_entity_view** views, size_t* view_count) {
    uint64_t operations = 0u;
    if (state->database_operations > UINT64_MAX - LAPLACE_PG_PHYSICALITY_ENTITY_READ_MAX_OPERATIONS)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace materialization owner read exceeds its operation boundary")));
    laplace_pg_physicality_entity_resolve_owner(&state->context, source,
        materialization_reflection_grant(state), LAPLACE_PG_PHYSICALITY_ENTITY_READ_MAX_OPERATIONS,
        views, view_count, &operations);
    state->database_operations += operations;
}

static bool materialization_same_physicality(
    const laplace_persistence_physicality_record* left,
    const laplace_persistence_physicality_record* right) {
    uint8_t a[8u + LAPLACE_PERSISTENCE_PHYSICALITY_PAYLOAD_BYTES], b[sizeof(a)];
    size_t a_bytes = 0u, b_bytes = 0u;
    if (laplace_persistence_frame_encode_physicality(left, a, sizeof(a), &a_bytes) !=
            LAPLACE_PERSISTENCE_OK ||
        laplace_persistence_frame_encode_physicality(right, b, sizeof(b), &b_bytes) !=
            LAPLACE_PERSISTENCE_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace materialization candidate body cannot be encoded")));
    return a_bytes == b_bytes && memcmp(a, b, a_bytes) == 0;
}

static void materialization_merge_derived(
    laplace_pg_materialization_provider_state* state,
    materialization_cache_entry* entry,
    const laplace_pg_physicality_entity_view* view, bool present) {
    static const char domain[] = "laplace-postgresql-derived-materialization-node-v1";
    blake3_hasher hash;
    static const laplace_digest256 zero = {{0}};
    if (!materialization_id_equal(&entry->key.entity_id, &view->physicality.entity_id) ||
        !materialization_digest_equal(&entry->node.identity_witness, &view->identity_witness) ||
        (!materialization_digest_equal(&entry->key.selected_physicality_id, &zero) &&
         !materialization_digest_equal(&entry->key.selected_physicality_id,
            &view->physicality.physicality_id)))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace derived materialization differs from its canonical identity or selection")));
    if (present) {
        if (!materialization_digest_equal(&entry->node.physicality_id,
                                         &view->physicality.physicality_id))
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace materialization composition is not unique in the pinned geometry epoch")));
        if (!materialization_same_physicality(&entry->physicality, &view->physicality) ||
            entry->carrier_count != view->carrier_count ||
            memcmp(entry->trajectory, view->carriers,
                   view->carrier_count * sizeof(*view->carriers)) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace stored and derived physicality bodies disagree")));
    } else {
        entry->physicality = view->physicality;
        entry->node.physicality_id = view->physicality.physicality_id;
        entry->node.trajectory_fingerprint = view->physicality.trajectory_fingerprint;
        materialization_cache_trajectory(state, entry, view->carriers,
            view->carrier_count * sizeof(*view->carriers), view->carrier_count,
            view->physicality.logical_count);
    }
    entry->derivation_receipt = view->derivation_receipt;
    materialization_node_receipt(state, &entry->node, &entry->node.node_receipt_id);
    blake3_hasher_init(&hash);
    blake3_hasher_update(&hash, domain, sizeof(domain)-1u);
    blake3_hasher_update(&hash, entry->node.node_receipt_id.bytes, 32u);
    blake3_hasher_update(&hash, view->derivation_receipt.bytes, 32u);
    blake3_hasher_finalize(&hash, entry->node.node_receipt_id.bytes, 32u);
}

static void materialization_resolve_batch(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* requested,
    const laplace_digest256* requested_selections,
    size_t requested_count) {
    static const char entity_sql[] =
        "WITH input(entity_id, selected_physicality_id, source_index) AS MATERIALIZED ("
        " SELECT entity_id, selected_physicality_id, ordinality - 1"
        " FROM unnest($1::bytea[],$2::bytea[]) WITH ORDINALITY"
        " AS u(entity_id, selected_physicality_id, ordinality))"
        " SELECT i.source_index, e.identity_witness, p.physicality_type"
        " FROM input AS i"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".entity AS e ON e.entity_id=i.entity_id"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".physicality AS p"
        " ON p.physicality_id=i.selected_physicality_id AND p.entity_id=i.entity_id"
        " AND p.geometry_epoch=$3"
        " ORDER BY i.source_index";
    static const char physicality_sql[] =
        "WITH input(entity_id, selected_physicality_id, source_index) AS MATERIALIZED ("
        " SELECT entity_id, selected_physicality_id, ordinality - 1"
        " FROM unnest($1::bytea[],$4::bytea[]) WITH ORDINALITY"
        " AS u(entity_id, selected_physicality_id, ordinality))"
        " SELECT i.source_index,p.physicality_id,p.trajectory_fingerprint,"
        "        p.logical_count,p.vertex_count,p.trajectory,p.entity_id,p.physicality_type,"
        "        p.vertex_class,p.recipe_version,p.structural_form,p.dimension_count,p.flags,"
        "        p.recipe_fingerprint,p.geometry_epoch,p.centroid_x,p.centroid_y,p.centroid_z,"
        "        p.centroid_m,p.radius"
        " FROM input AS i"
        " LEFT JOIN LATERAL ("
        "   SELECT physicality_id,trajectory_fingerprint,logical_count,vertex_count,trajectory,"
        "     entity_id,physicality_type,vertex_class,recipe_version,structural_form,dimension_count,flags,"
        "     recipe_fingerprint,geometry_epoch,centroid_x,centroid_y,centroid_z,centroid_m,radius"
        "   FROM " LAPLACE_PG_SCHEMA ".physicality"
        "   WHERE physicality_id=i.selected_physicality_id"
        "     AND entity_id=i.entity_id AND physicality_type=$2 AND geometry_epoch=$3"
        "   UNION ALL ("
        "     SELECT physicality_id,trajectory_fingerprint,logical_count,vertex_count,trajectory,"
        "       entity_id,physicality_type,vertex_class,recipe_version,structural_form,dimension_count,flags,"
        "       recipe_fingerprint,geometry_epoch,centroid_x,centroid_y,centroid_z,centroid_m,radius"
        "     FROM " LAPLACE_PG_SCHEMA ".physicality"
        "     WHERE i.selected_physicality_id IS NULL"
        "       AND entity_id=i.entity_id AND physicality_type=$2 AND geometry_epoch=$3"
        "     ORDER BY physicality_id LIMIT 2)"
        " ) AS p ON true"
        " ORDER BY i.source_index,p.physicality_id";
    /* Disjoint branches retain the primary-key lookup for a bound identity.
     * A bound identity has one exact physicality candidate. An unbound identity
     * still reads at most two candidates and rejects real ambiguity, including
     * nested compositions for which the caller has supplied no native choice. */
    laplace_id128* ids;
    laplace_digest256* selected_ids;
    uint32_t* selected_types;
    materialization_cache_entry** entries;
    laplace_digest256* witnesses;
    laplace_unicode_identity_key* keys;
    uint32_t* positions;
    uint8_t* reverse_found;
    laplace_unicode_atom_record_view* atoms;
    uint8_t* atom_found;
    laplace_id128* composition_ids;
    laplace_digest256* composition_selections;
    size_t* composition_source;
    size_t unique_count = 0u;
    volatile size_t composition_count = 0u;
    size_t index;
    Oid entity_types[3] = {BYTEAARRAYOID, BYTEAARRAYOID, BYTEAOID};
    Datum entity_values[3];
    int result;
    laplace_pg_perfcache_pin* pin = NULL;
    laplace_perfcache_registry_status cache_status;

    if (state == NULL || requested == NULL || requested_count == 0u ||
        requested_count > (size_t)INT_MAX ||
        requested_count > MaxAllocSize / sizeof(*ids)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace materialization node batch exceeds addressability")));
    }

    ids = (laplace_id128*)palloc(sizeof(*ids) * requested_count);
    entries = (materialization_cache_entry**)palloc(sizeof(*entries) * requested_count);
    for (index = 0u; index < requested_count; ++index) {
        bool found = false;
        materialization_cache_entry* entry =
            materialization_cache_enter_selected(state, &requested[index],
                requested_selections == NULL ? NULL : &requested_selections[index], &found);
        if (entry->ready != 0u || entry->resolving != 0u) {
            continue;
        }
        entry->resolving = 1u;
        ids[unique_count] = requested[index];
        entries[unique_count] = entry;
        ++unique_count;
    }
    if (unique_count == 0u) {
        return;
    }

    witnesses = (laplace_digest256*)palloc0(sizeof(*witnesses) * unique_count);
    selected_ids = (laplace_digest256*)palloc(sizeof(*selected_ids) * unique_count);
    selected_types = (uint32_t*)palloc0(sizeof(*selected_types) * unique_count);
    for (index = 0u; index < unique_count; ++index)
        selected_ids[index] = entries[index]->key.selected_physicality_id;
    keys = (laplace_unicode_identity_key*)palloc0(sizeof(*keys) * unique_count);
    positions = (uint32_t*)palloc0(sizeof(*positions) * unique_count);
    reverse_found = (uint8_t*)palloc0(sizeof(*reverse_found) * unique_count);
    atoms = (laplace_unicode_atom_record_view*)palloc0(sizeof(*atoms) * unique_count);
    atom_found = (uint8_t*)palloc0(sizeof(*atom_found) * unique_count);
    composition_ids = (laplace_id128*)palloc(sizeof(*composition_ids) * unique_count);
    composition_selections = (laplace_digest256*)palloc(sizeof(*composition_selections) * unique_count);
    composition_source = (size_t*)palloc(sizeof(*composition_source) * unique_count);

    entity_values[0] = PointerGetDatum(materialization_id_array(ids, unique_count));
    entity_values[1] = PointerGetDatum(materialization_selection_array(selected_ids, unique_count));
    entity_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes, 32u));
    result = SPI_execute_with_args(
        entity_sql, 3, entity_types, entity_values, NULL, true,
        (long)(unique_count + 1u));
    ++state->database_operations;
    ++state->node_batch_count;
    if (result != SPI_OK_SELECT || SPI_tuptable == NULL ||
        SPI_processed != unique_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization entity frontier is incomplete"),
                 errdetail("requested=%zu returned=%llu",
                           unique_count, (unsigned long long)SPI_processed)));
    }
    for (index = 0u; index < unique_count; ++index) {
        bool is_null = false;
        Datum source = SPI_getbinval(
            SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 1, &is_null);
        int64 source_index;
        if (is_null) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace materialization frontier lost source ordinality")));
        }
        source_index = DatumGetInt64(source);
        if (source_index < 0 || (uint64_t)source_index >= unique_count ||
            (size_t)source_index != index) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace materialization frontier reordered source identities")));
        }
        materialization_read_exact(
            materialization_required_value(
                SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 2,
                "identity witness"),
            witnesses[index].bytes, sizeof(witnesses[index].bytes),
            "identity witness");
        keys[index].content_id = ids[index];
        keys[index].identity_preimage_fingerprint = witnesses[index];
        {
            Datum physicality_type = SPI_getbinval(
                SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 3, &is_null);
            if (!is_null) selected_types[index] = (uint32_t)DatumGetInt32(physicality_type);
        }
    }
    SPI_freetuptable(SPI_tuptable);

    materialization_require_active_perfcache(state, &pin);
    PG_TRY();
    {
        cache_status = laplace_perfcache_unicode_identity_reverse_resolve_batch(
            &pin->native_pin, keys, unique_count, positions, reverse_found);
        if (cache_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace Unicode reverse materialization batch failed"),
                     errdetail("status=%u", (unsigned int)cache_status)));
        }
        cache_status = laplace_perfcache_unicode_tier0_resolve_batch(
            &pin->native_pin, positions, unique_count, atoms, atom_found);
        if (cache_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace Unicode direct materialization batch failed"),
                     errdetail("status=%u", (unsigned int)cache_status)));
        }
        laplace_pg_perfcache_pin_release(&pin);
    }
    PG_CATCH();
    {
        laplace_pg_perfcache_pin_release(&pin);
        PG_RE_THROW();
    }
    PG_END_TRY();

    for (index = 0u; index < unique_count; ++index) {
        materialization_cache_entry* entry = entries[index];
        memset(&entry->node, 0, sizeof(entry->node));
        entry->node.entity_id = ids[index];
        entry->node.identity_witness = witnesses[index];
        static const laplace_digest256 zero_selection = {{0}};
        /* Canonical Unicode identity does not classify a selected realization.
         * A singleton composition of that atom must traverse its actual body. */
        if (reverse_found[index] != 0u &&
            (materialization_digest_equal(&selected_ids[index], &zero_selection) ||
             selected_types[index] == LAPLACE_PERSISTENCE_PHYSICALITY_ATOMIC_POINT)) {
            const laplace_digest256* selection = &entry->key.selected_physicality_id;
            static const laplace_digest256 zero = {{0}};
            if (atom_found[index] == 0u ||
                memcmp(
                    atoms[index].value.content_id.bytes,
                    ids[index].bytes,
                    sizeof(ids[index].bytes)) != 0 ||
                memcmp(
                    atoms[index].value.identity_preimage_fingerprint.bytes,
                    witnesses[index].bytes,
                    sizeof(witnesses[index].bytes)) != 0) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace Unicode batch read disagrees with exact identity")));
            }
            if (!materialization_digest_equal(selection, &zero) &&
                !materialization_digest_equal(selection, &atoms[index].value.physicality_id)) {
                bool authenticated = false;
                for (size_t external = 0u; external < state->scope_external_count; ++external) {
                    const laplace_composition_known_entity* known = &state->scope_external[external];
                    if (materialization_id_equal(&known->entity_id, &ids[index]) &&
                        materialization_digest_equal(&known->physicality_id, selection) &&
                        materialization_digest_equal(&known->identity_witness, &witnesses[index]) &&
                        known->has_atom == 1u && known->atom == positions[index] && known->tier_floor == 0u) {
                        authenticated = true;
                        break;
                    }
                }
                if (!authenticated)
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                        errmsg("Laplace materialization selected atom physicality differs from the pinned Unicode generation")));
            }
            if (!materialization_digest_equal(selection, &zero))
                entry->node.physicality_id = *selection;
            entry->node.logical_count = 1u;
            entry->node.atom = positions[index];
            entry->node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_ATOM;
            entry->node.tier_floor = 0u;
            materialization_node_receipt(
                state, &entry->node, &entry->node.node_receipt_id);
            entry->ready = 1u;
            entry->resolving = 0u;
            ++state->prefetched_node_count;
            materialization_note_prefetch_node(state, &entry->node);
        } else {
            entry->frontier_index = composition_count;
            composition_ids[composition_count] = ids[index];
            composition_selections[composition_count] = entry->key.selected_physicality_id;
            composition_source[composition_count] = index;
            ++composition_count;
        }
    }

    if (composition_count != 0u) {
        Oid physicality_types[4] = {BYTEAARRAYOID, INT4OID, BYTEAOID, BYTEAARRAYOID};
        Datum physicality_values[4];
        size_t row;
        bool* seen = (bool*)palloc0(sizeof(*seen) * composition_count);
        long row_limit = composition_count > (size_t)((LONG_MAX - 1L) / 2L)
            ? LONG_MAX
            : (long)(composition_count * 2u + 1u);

        physicality_values[0] = PointerGetDatum(
            materialization_id_array(composition_ids, composition_count));
        physicality_values[1] = Int32GetDatum(
            (int32)LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION);
        physicality_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            state->context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes,
            sizeof(state->context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes)));
        physicality_values[3] = PointerGetDatum(materialization_selection_array(
            composition_selections, composition_count));
        result = SPI_execute_with_args(
            physicality_sql, 4, physicality_types, physicality_values,
            NULL, true, row_limit);
        ++state->database_operations;
        if (result != SPI_OK_SELECT || SPI_tuptable == NULL) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace materialization composition frontier read failed")));
        }
        for (row = 0u; row < (size_t)SPI_processed; ++row) {
            bool is_null = false;
            Datum source = SPI_getbinval(
                SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 1, &is_null);
            int64 source_index;
            size_t batch_index;
            size_t original_index;
            materialization_cache_entry* entry;
            bytea* trajectory;
            uint64_t logical_count;
            uint64_t carrier_count;

            if (is_null) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace materialization composition frontier lost ordinality")));
            }
            source_index = DatumGetInt64(source);
            if (source_index < 0 || (uint64_t)source_index >= composition_count) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace materialization composition frontier returned an invalid source index")));
            }
            batch_index = (size_t)source_index;
            (void)SPI_getbinval(SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 2, &is_null);
            if (is_null) continue; /* A verified indexed derived view may supply this node. */
            if (seen[batch_index]) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace materialization composition is not unique in the pinned geometry epoch")));
            }
            seen[batch_index] = true;
            original_index = composition_source[batch_index];
            entry = entries[original_index];
            materialization_read_exact(
                materialization_required_value(
                    SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 2,
                    "physicality id"),
                entry->node.physicality_id.bytes,
                sizeof(entry->node.physicality_id.bytes),
                "physicality id");
            {
                static const laplace_digest256 zero = {{0}};
                if (!materialization_digest_equal(&entry->key.selected_physicality_id, &zero) &&
                    !materialization_digest_equal(&entry->key.selected_physicality_id,
                        &entry->node.physicality_id)) {
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                        errmsg("Laplace materialization physicality differs from its verified selection")));
                }
            }
            materialization_read_exact(
                materialization_required_value(
                    SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 3,
                    "trajectory fingerprint"),
                entry->node.trajectory_fingerprint.bytes,
                sizeof(entry->node.trajectory_fingerprint.bytes),
                "trajectory fingerprint");
            logical_count = materialization_numeric_u64(
                materialization_required_value(
                    SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 4,
                    "logical count"),
                "materialization logical count");
            carrier_count = materialization_numeric_u64(
                materialization_required_value(
                    SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 5,
                    "vertex count"),
                "materialization vertex count");
            trajectory = DatumGetByteaPP(materialization_required_value(
                SPI_tuptable->vals[row], SPI_tuptable->tupdesc, 6,
                "trajectory"));
            materialization_verify_physicality(SPI_tuptable->vals[row], SPI_tuptable->tupdesc,
                &entry->node, logical_count, carrier_count, &entry->physicality);
            materialization_cache_trajectory(
                state, entry, VARDATA_ANY(trajectory), (size_t)VARSIZE_ANY_EXHDR(trajectory),
                carrier_count, logical_count);
            materialization_node_receipt(
                state, &entry->node, &entry->node.node_receipt_id);
        }
        SPI_freetuptable(SPI_tuptable);
        if (!state->scope_historical) {
            laplace_pg_physicality_entity_view* views = NULL;
            size_t view_count = 0u;
            materialization_resolve_derived(state, &state->scope_view_id,
                composition_ids, composition_selections, composition_count, &views, &view_count);
            for (size_t derived = 0u; derived < view_count; ++derived) {
                static const laplace_digest256 zero = {{0}};
                materialization_cache_entry* matching[2] = {
                    materialization_cache_find_selected(state, &views[derived].physicality.entity_id,
                        &views[derived].physicality.physicality_id),
                    materialization_cache_find_selected(state, &views[derived].physicality.entity_id, &zero)};
                bool matched = false;
                for (size_t choice = 0u; choice < 2u; ++choice) {
                    materialization_cache_entry* entry = matching[choice];
                    if (entry == NULL || entry->resolving == 0u ||
                        (choice != 0u && entry == matching[0])) continue;
                    if (entry->frontier_index >= composition_count ||
                        entries[composition_source[entry->frontier_index]] != entry)
                        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                            errmsg("Laplace derived materialization lost its exact frontier selection")));
                    materialization_merge_derived(state, entry,
                        &views[derived], seen[entry->frontier_index]);
                    seen[entry->frontier_index] = true;
                    matched = true;
                }
                if (!matched)
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                        errmsg("Laplace derived materialization returned an unrequested entity")));
            }
        }
        for (index = 0u; index < composition_count; ++index) {
            if (!seen[index]) {
                ereport(ERROR,
                        (errcode(ERRCODE_NO_DATA_FOUND),
                         errmsg("Laplace materialization composition is absent in the pinned geometry epoch")));
            }
        }
        /* Speculative prefetch may catch an error. Publish no composition from
         * this frontier until every candidate has passed uniqueness and native
         * identity checks, or a discarded ambiguity could leave its first row
         * cached as an authoritative choice for a later on-demand read. */
        for (index = 0u; index < composition_count; ++index) {
            materialization_cache_entry* entry = entries[composition_source[index]];
            entry->ready = 1u;
            entry->resolving = 0u;
            ++state->prefetched_node_count;
            materialization_note_prefetch_node(state, &entry->node);
        }
    }

    for (index = 0u; index < unique_count; ++index) {
        if (entries[index]->ready == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace materialization node batch did not resolve every requested identity")));
        }
        entries[index]->resolving = 0u;
    }
}

static int materialization_occurrence_compare(const void* left, const void* right) {
    const laplace_physicality_occurrence_binding* a = left;
    const laplace_physicality_occurrence_binding* b = right;
    int parent = memcmp(a->parent_physicality_id.bytes, b->parent_physicality_id.bytes, 32u);
    if (parent != 0) return parent;
    return (a->first_logical_ordinal > b->first_logical_ordinal) -
           (a->first_logical_ordinal < b->first_logical_ordinal);
}

static int materialization_reference_selection(
    const laplace_pg_materialization_provider_state* state,
    const laplace_cognition_materialization_reference* reference,
    laplace_cognition_materialization_selection* selection) {
    static const laplace_digest256 zero = {{0}};
    size_t lower = 0u, upper = state->scope_occurrence_count;
    laplace_physicality_occurrence_binding key;
    const laplace_physicality_occurrence_binding* binding;
    memset(selection, 0, sizeof(*selection));
    selection->contiguous_run_length = reference->occurrence.run_length;
    if (materialization_digest_equal(&state->scope_view_id, &zero)) return 0;
    if (materialization_digest_equal(&reference->parent_physicality_id, &zero)) {
        if (!materialization_id_equal(&state->scope_root, &reference->occurrence.entity_id) ||
            reference->occurrence.logical_ordinal != 0u || reference->occurrence.run_length != 1u)
            return 1;
        selection->physicality_id = state->scope_root_physicality;
        selection->binding_receipt_id = state->scope_receipt;
        return 0;
    }
    memset(&key, 0, sizeof(key));
    key.parent_physicality_id = reference->parent_physicality_id;
    key.first_logical_ordinal = reference->occurrence.logical_ordinal;
    while (lower < upper) {
        size_t middle = lower + (upper - lower) / 2u;
        if (materialization_occurrence_compare(&state->scope_occurrences[middle], &key) <= 0)
            lower = middle + 1u;
        else upper = middle;
    }
    if (lower == 0u || !materialization_digest_equal(
            &state->scope_occurrences[lower - 1u].parent_physicality_id,
            &reference->parent_physicality_id)) {
        /* A genuinely unbound external subtree retains its strict ordinary
         * resolution law. A declared parent may never have an uncovered gap. */
        if (lower < state->scope_occurrence_count && materialization_digest_equal(
                &state->scope_occurrences[lower].parent_physicality_id,
                &reference->parent_physicality_id)) return 2;
        return 0;
    }
    binding = &state->scope_occurrences[lower - 1u];
    if (reference->occurrence.logical_ordinal - binding->first_logical_ordinal >= binding->logical_count ||
        !materialization_id_equal(&binding->entity_id, &reference->occurrence.entity_id) ||
        binding->metadata != reference->occurrence.metadata) return 3;
    selection->contiguous_run_length = Min((uint64_t)reference->occurrence.run_length,
        binding->logical_count - (reference->occurrence.logical_ordinal - binding->first_logical_ordinal));
    selection->physicality_id = binding->selected_physicality_id;
    selection->binding_receipt_id = state->scope_receipt;
    return 0;
}

static int materialization_key_compare(const void* left, const void* right) {
    return memcmp(left, right, sizeof(materialization_cache_key));
}

static void materialization_prefetch_children_impl(
    laplace_pg_materialization_provider_state* state,
    materialization_cache_entry* parent) {
    materialization_cache_key* keys;
    laplace_id128* children;
    laplace_digest256* selections;
    size_t child_count = 0u, unique = 0u, capacity;
    uint64_t ordinal = 1u;
    if (parent == NULL || parent->ready == 0u || parent->children_prefetched != 0u ||
        parent->node.kind != LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION ||
        parent->trajectory == NULL || parent->carrier_count == 0u) return;
    if (parent->carrier_count > SIZE_MAX - state->scope_occurrence_count) return;
    capacity = (size_t)parent->carrier_count + state->scope_occurrence_count;
    if (capacity > MaxAllocSize / sizeof(*keys) ||
        capacity > materialization_reflection_grant(state) /
            (sizeof(*keys) + sizeof(*children) + sizeof(*selections))) return;
    parent->children_prefetched = 1u;
    keys = palloc(capacity * sizeof(*keys));
    for (size_t index = 0u; index < (size_t)parent->carrier_count; ++index) {
        laplace_cognition_materialization_reference reference;
        uint64_t consumed = 0u;
        memset(&reference, 0, sizeof(reference));
        reference.parent_physicality_id = parent->node.physicality_id;
        if (laplace_trajectory_composition_decode_one(&parent->trajectory[index], ordinal,
                &reference.occurrence) != LAPLACE_TRAJECTORY_OK ||
            UINT64_MAX - ordinal < reference.occurrence.run_length) return;
        ordinal += reference.occurrence.run_length;
        while (reference.occurrence.run_length != 0u) {
            laplace_cognition_materialization_selection selected;
            static const laplace_digest256 zero = {{0}};
            if (materialization_reference_selection(state, &reference, &selected) != 0 ||
                selected.contiguous_run_length == 0u ||
                selected.contiguous_run_length > reference.occurrence.run_length) return;
            if (reference.occurrence.has_atom == 0u ||
                !materialization_digest_equal(&selected.physicality_id, &zero)) {
                materialization_cache_key key = materialization_key(state,
                    &reference.occurrence.entity_id,
                    materialization_digest_equal(&selected.physicality_id, &zero) ? NULL : &selected.physicality_id);
                materialization_cache_entry* cached = materialization_cache_find_selected(
                    state, &key.entity_id, &key.selected_physicality_id);
                if (cached == NULL || cached->ready == 0u) {
                    if (child_count >= capacity) return;
                    keys[child_count++] = key;
                }
            }
            consumed = selected.contiguous_run_length;
            reference.occurrence.logical_ordinal += consumed;
            reference.occurrence.run_length -= (uint16_t)consumed;
        }
    }
    if (child_count == 0u) return;
    qsort(keys, child_count, sizeof(*keys), materialization_key_compare);
    children = palloc(child_count * sizeof(*children));
    selections = palloc(child_count * sizeof(*selections));
    for (size_t index = 0u; index < child_count; ++index) {
        if (index != 0u && materialization_key_compare(&keys[index - 1u], &keys[index]) == 0) continue;
        children[unique] = keys[index].entity_id;
        selections[unique++] = keys[index].selected_physicality_id;
    }
    /* No speculative candidate becomes ready until the full bounded batch is
     * validated. A rejected prefetch is retried by actual native traversal. */
    materialization_resolve_batch(state, children, selections, unique);
}

static void materialization_prefetch_children(
    laplace_pg_materialization_provider_state* state,
    materialization_cache_entry* parent) {
    MemoryContext previous = CurrentMemoryContext;
    PG_TRY();
    {
        materialization_prefetch_children_impl(state, parent);
    }
    PG_CATCH();
    {
        /* Speculation includes planning/allocation as well as the read itself.
         * Restore the caller context before discarding any speculative error. */
        MemoryContextSwitchTo(previous);
        FlushErrorState();
        materialization_reset_resolving(state);
    }
    PG_END_TRY();
    MemoryContextSwitchTo(previous);
}

static int materialization_resolve_node_impl(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id,
    const laplace_digest256* selected,
    laplace_cognition_materialization_node* node) {
    materialization_cache_entry* entry;
    if (state == NULL || entity_id == NULL || node == NULL) {
        return 1;
    }
    entry = materialization_cache_find_selected(state, entity_id, selected);
    if (entry == NULL || entry->ready == 0u) {
        materialization_resolve_batch(state, entity_id, selected, 1u);
        entry = materialization_cache_find_selected(state, entity_id, selected);
    }
    if (entry == NULL || entry->ready == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_NO_DATA_FOUND),
                 errmsg("Laplace materialization node remained unresolved after its batch read")));
    }
    *node = entry->node;
    ++state->resolved_nodes;
    materialization_note_node(state, node);
    return 0;
}

static int materialization_resolve_selected(
    void* opaque,
    const laplace_id128* entity_id,
    const laplace_digest256* selected,
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
        status = materialization_resolve_node_impl(state, entity_id, selected, node);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(state->caller_context);
        state->error = CopyErrorData();
        FlushErrorState();
        materialization_reset_resolving(state);
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

static int materialization_resolve_node(void* opaque, const laplace_id128* entity_id,
    laplace_cognition_materialization_node* node) {
    return materialization_resolve_selected(opaque, entity_id, NULL, node);
}

static int materialization_select_reference(void* opaque,
    const laplace_cognition_materialization_reference* reference,
    laplace_cognition_materialization_selection* selection) {
    laplace_pg_materialization_provider_state* state = opaque;
    volatile int status = 1;
    MemoryContext previous = CurrentMemoryContext;
    if (state == NULL || reference == NULL || selection == NULL || state->error != NULL) return 1;
    PG_TRY();
    {
        status = materialization_reference_selection(state, reference, selection);
        if (status != 0) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace materialization occurrence binding disagrees with its exact parent and ordinal"),
            errdetail("binding status=%d", (int)status)));
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(state->caller_context);
        state->error = CopyErrorData();
        FlushErrorState();
        memset(selection, 0, sizeof(*selection));
        status = 1;
    }
    PG_END_TRY();
    MemoryContextSwitchTo(previous);
    return (int)status;
}

static int materialization_read_trajectory_impl(
    laplace_pg_materialization_provider_state* state,
    const laplace_cognition_materialization_node* node,
    laplace_trajectory_carrier* carriers,
    size_t carrier_count,
    laplace_digest256* read_receipt_id) {
    static const char receipt_domain[] =
        "laplace-postgresql-materialization-trajectory-read-v2";
    materialization_cache_entry* entry;
    size_t bytes;
    blake3_hasher hasher;

    if (state == NULL || node == NULL || carriers == NULL ||
        read_receipt_id == NULL || carrier_count == 0u ||
        node->kind != LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION ||
        node->carrier_count != (uint64_t)carrier_count ||
        carrier_count > SIZE_MAX / sizeof(*carriers)) {
        return 1;
    }
    entry = materialization_cache_find_selected(state, &node->entity_id, &node->physicality_id);
    if (entry == NULL) entry = materialization_cache_find(state, &node->entity_id);
    if (entry == NULL || entry->ready == 0u || entry->trajectory == NULL ||
        entry->carrier_count != (uint64_t)carrier_count ||
        !materialization_digest_equal(
            &entry->node.physicality_id, &node->physicality_id) ||
        !materialization_digest_equal(
            &entry->node.trajectory_fingerprint,
            &node->trajectory_fingerprint)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization trajectory cache disagrees with its resolved node")));
    }

    materialization_prefetch_children(state, entry);
    bytes = carrier_count * sizeof(*carriers);
    memcpy(carriers, entry->trajectory, bytes);
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, receipt_domain, sizeof(receipt_domain) - 1u);
    blake3_hasher_update(
        &hasher,
        state->provider_fingerprint.bytes,
        sizeof(state->provider_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, node->node_receipt_id.bytes,
        sizeof(node->node_receipt_id.bytes));
    blake3_hasher_update(&hasher, entry->trajectory, bytes);
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
        materialization_reset_resolving(state);
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

static void materialization_clear_cache(laplace_pg_materialization_provider_state* state) {
    HASHCTL control;
    MemoryContextReset(state->cache_context);
    memset(&control, 0, sizeof(control));
    control.keysize = sizeof(materialization_cache_key);
    control.entrysize = sizeof(materialization_cache_entry);
    control.hcxt = state->cache_context;
    state->cache = hash_create("Laplace materialization exact-node cache", 128,
        &control, HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
}

static void materialization_begin_read_impl(
    laplace_pg_materialization_provider_state* state, const laplace_id128* root,
    const laplace_digest256* source, const laplace_digest256* recipe,
    laplace_digest256* receipt) {
    static const laplace_digest256 zero = {{0}};
    laplace_pg_physicality_entity_view* views = NULL;
    size_t view_count = 0u, capacity;
    const laplace_physicality_occurrence_binding* selected_bindings;
    const bool historical = !materialization_digest_equal(&state->required_original_physicality, &zero);
    bool canonical_atom = false;
    laplace_composition_known_entity canonical_root = {0};
    laplace_digest256 selected_root_physicality;
    MemoryContext previous;
    bool found = false;
    materialization_cache_entry* entry;
    if (!materialization_digest_equal(&state->scope_view_id, &zero))
        materialization_clear_cache(state);
    MemoryContextReset(state->scope_context);
    state->scope_view_id = zero;
    state->scope_historical = false;
    state->scope_external = NULL;
    state->scope_external_count = 0u;
    state->scope_occurrences = NULL;
    state->scope_occurrence_count = 0u;
    memset(&state->scope_root, 0, sizeof(state->scope_root));
    state->scope_root_physicality = zero;
    state->scope_receipt = zero;
    state->selections = state->base_selections;
    state->selection_count = state->base_selection_count;

    if (!materialization_digest_equal(&state->required_view_id, &zero) &&
        !materialization_digest_equal(source, &state->required_view_id))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace materialization source differs from its required retained view")));

    /* A source receipt identifies a reflection owner only by its exact key.
     * Ordinary content receipts produce no owner and preserve the unbound law. */
    if (historical) materialization_resolve_view_owner(state, source, &views, &view_count);
    else materialization_resolve_derived(state, source, root, NULL, 1u, &views, &view_count);
    if (!historical && view_count == 0u &&
        !materialization_digest_equal(&state->required_view_id, &zero)) {
        /* Atom references collapse to known inputs and have no generated node
         * row. Authenticate exact owner membership before choosing its pinned
         * canonical atom, never a historical all-known tuple for the same E. */
        materialization_resolve_view_owner(state, source, &views, &view_count);
        if (view_count == 1u) {
            for (size_t index = 0u; index < views[0].canonical_known_count; ++index) {
                const laplace_composition_known_entity* candidate = &views[0].canonical_known[index];
                if (!materialization_id_equal(root, &candidate->entity_id)) continue;
                if (candidate->has_atom != 1u || candidate->tier_floor != 0u ||
                    (canonical_atom && (!materialization_digest_equal(&canonical_root.physicality_id,
                         &candidate->physicality_id) ||
                     !materialization_digest_equal(&canonical_root.identity_witness,
                         &candidate->identity_witness))))
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                        errmsg("Laplace materialization owner does not bind one exact canonical atom root")));
                canonical_root = *candidate;
                canonical_atom = true;
            }
            if (!canonical_atom)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace materialization requested entity is outside the retained generated owner")));
        }
    }
    if (view_count == 0u) {
        if (!materialization_digest_equal(&state->required_view_id, &zero))
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace materialization required retained view does not own the requested entity")));
        return;
    }
    if (view_count != 1u || !materialization_digest_equal(source, &views[0].view_id) ||
        !materialization_digest_equal(recipe, &views[0].owner_recipe_fingerprint))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace materialization source does not bind one exact descriptor view and recipe")));
    if (historical && (!materialization_id_equal(root, &views[0].original_source.entity_id) ||
        !materialization_digest_equal(&state->required_original_physicality,
            &views[0].original_source.physicality_id)))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace materialization selected root is not the retained observation's original source")));
    selected_root_physicality = historical ? views[0].original_source.physicality_id
        : canonical_atom ? canonical_root.physicality_id : views[0].physicality.physicality_id;
    if (state->base_selection_count == SIZE_MAX)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace materialization root scope overflows addressability")));
    capacity = state->base_selection_count + 1u;
    state->scope_occurrence_count = historical ? views[0].occurrence_binding_count
        : views[0].generated_occurrence_binding_count;
    selected_bindings = historical ? views[0].occurrence_bindings
        : views[0].generated_occurrence_bindings;
    if (capacity > MaxAllocSize / sizeof(*state->selections) ||
        views[0].all_known_count > MaxAllocSize / sizeof(*state->scope_external) ||
        state->scope_occurrence_count > MaxAllocSize / sizeof(*state->scope_occurrences))
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace materialization root scope exceeds PostgreSQL addressability")));
    {
        uint64_t copy_bytes = (uint64_t)capacity * sizeof(*state->selections) +
            (uint64_t)views[0].all_known_count * sizeof(*state->scope_external) +
            (uint64_t)state->scope_occurrence_count * sizeof(*state->scope_occurrences);
        if (copy_bytes > materialization_reflection_grant(state))
            ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                errmsg("Laplace materialization root scope exceeds its memory grant")));
    }
    materialization_clear_cache(state);
    previous = MemoryContextSwitchTo(state->scope_context);
    state->selections = palloc(capacity * sizeof(*state->selections));
    if (state->base_selection_count != 0u)
        memcpy(state->selections, state->base_selections,
            state->base_selection_count * sizeof(*state->selections));
    state->selection_count = state->base_selection_count;
    state->scope_external_count = views[0].all_known_count;
    if (views[0].all_known_count != 0u) {
        laplace_composition_known_entity* known = palloc(
            views[0].all_known_count * sizeof(*known));
        memcpy(known, views[0].all_known, views[0].all_known_count * sizeof(*known));
        state->scope_external = known;
    }
    if (state->scope_occurrence_count != 0u) {
        size_t used = 0u;
        state->scope_occurrences = palloc(state->scope_occurrence_count * sizeof(*state->scope_occurrences));
        memcpy(state->scope_occurrences, selected_bindings,
            state->scope_occurrence_count * sizeof(*state->scope_occurrences));
        qsort(state->scope_occurrences, state->scope_occurrence_count,
            sizeof(*state->scope_occurrences), materialization_occurrence_compare);
        for (size_t index = 0u; index < state->scope_occurrence_count; ++index) {
            const laplace_physicality_occurrence_binding value = state->scope_occurrences[index];
            if (used != 0u && materialization_occurrence_compare(
                    &state->scope_occurrences[used - 1u], &value) == 0) {
                if (memcmp(&state->scope_occurrences[used - 1u], &value, sizeof(value)) != 0)
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                        errmsg("Laplace materialization selected occurrence context has conflicting intervals")));
                continue;
            }
            if (used != 0u && materialization_digest_equal(
                    &state->scope_occurrences[used - 1u].parent_physicality_id, &value.parent_physicality_id)) {
                const laplace_physicality_occurrence_binding* prior = &state->scope_occurrences[used - 1u];
                if (prior->logical_count > UINT64_MAX - prior->first_logical_ordinal ||
                    value.first_logical_ordinal != prior->first_logical_ordinal + prior->logical_count)
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                        errmsg("Laplace materialization occurrence scope has a gap or overlap")));
            } else if (value.first_logical_ordinal != 1u)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace materialization occurrence scope does not begin at its parent origin")));
            state->scope_occurrences[used++] = value;
        }
        state->scope_occurrence_count = used;
    }
    /* The base map is a caller's explicit ordinary selection. Historical and
     * generated child references are occurrence-scoped and never compressed
     * into one entity-to-physicality map. */
    state->selections[state->selection_count].entity_id = *root;
    state->selections[state->selection_count++].physicality_id = selected_root_physicality;
    MemoryContextSwitchTo(previous);
    qsort(state->selections, state->selection_count, sizeof(*state->selections),
        materialization_selection_compare);
    state->selection_count = 0u;
    for (size_t index = 0u; index < capacity; ++index) {
        const laplace_pg_materialization_selection selected = state->selections[index];
        if (state->selection_count != 0u && materialization_id_equal(
                &state->selections[state->selection_count-1u].entity_id, &selected.entity_id)) {
            if (!materialization_digest_equal(
                    &state->selections[state->selection_count-1u].physicality_id, &selected.physicality_id))
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace materialization root conflicts with the caller physicality selection")));
        } else state->selections[state->selection_count++] = selected;
    }
    state->scope_root = *root;
    state->scope_root_physicality = selected_root_physicality;
    state->scope_receipt = views[0].derivation_receipt;
    state->scope_view_id = views[0].view_id;
    state->scope_historical = historical;
    if (historical || canonical_atom) {
        const char* domain = historical
            ? "laplace-postgresql-original-physicality-observation-read-v1"
            : "laplace-postgresql-canonical-atom-owner-read-v1";
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, domain, strlen(domain));
        blake3_hasher_update(&hasher, views[0].derivation_receipt.bytes, 32u);
        blake3_hasher_update(&hasher, root->bytes, 16u);
        blake3_hasher_update(&hasher, selected_root_physicality.bytes, 32u);
        blake3_hasher_finalize(&hasher, state->scope_receipt.bytes, 32u);
    } else {
        entry = materialization_cache_enter(state, root, &found);
        entry->node.entity_id = *root;
        entry->node.identity_witness = views[0].identity_witness;
        materialization_merge_derived(state, entry, &views[0], false);
        entry->ready = 1u;
    }
    *receipt = state->scope_receipt;
}

static int materialization_begin_read(void* opaque, const laplace_id128* root,
    const laplace_digest256* source, const laplace_digest256* recipe,
    laplace_digest256* receipt) {
    laplace_pg_materialization_provider_state* state = opaque;
    MemoryContext previous;
    volatile int status = 0;
    if (state == NULL || root == NULL || source == NULL || recipe == NULL ||
        receipt == NULL || state->error != NULL || state->scratch_context == NULL) return 1;
    memset(receipt, 0, sizeof(*receipt));
    previous = MemoryContextSwitchTo(state->scratch_context);
    PG_TRY();
    {
        materialization_begin_read_impl(state, root, source, recipe, receipt);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(state->caller_context);
        state->error = CopyErrorData();
        FlushErrorState();
        materialization_reset_resolving(state);
        memset(receipt, 0, sizeof(*receipt));
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
    state->cache_context = NULL;
    state->binding_context = NULL;
    state->scope_context = NULL;
    state->cache = NULL;
    state->selections = NULL;
    state->selection_count = 0u;
    state->base_selections = NULL;
    state->base_selection_count = 0u;
    state->scope_external = NULL;
    state->scope_external_count = 0u;
    if (state->error != NULL) {
        FreeErrorData(state->error);
        state->error = NULL;
    }
}

void laplace_pg_materialization_provider_create(
    const laplace_framework_context* context,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider) {
    laplace_pg_materialization_provider_create_selected(
        context, NULL, NULL, 0u, owner, provider);
}

void laplace_pg_materialization_provider_create_for_view(
    const laplace_framework_context* context,
    const laplace_digest256* required_view_id,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider) {
    static const laplace_digest256 zero = {{0}};
    if (owner != NULL) *owner = NULL;
    if (provider != NULL) memset(provider, 0, sizeof(*provider));
    if (required_view_id == NULL || materialization_digest_equal(required_view_id, &zero))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace materialization requires a nonzero retained view identity")));
    laplace_pg_materialization_provider_create(context, owner, provider);
    (*owner)->required_view_id = *required_view_id;
}

void laplace_pg_materialization_provider_create_for_view_selected(
    const laplace_framework_context* context,
    const laplace_digest256* required_view_id,
    const laplace_digest256* original_root_physicality_id,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider) {
    static const laplace_digest256 zero = {{0}};
    if (owner != NULL) *owner = NULL;
    if (provider != NULL) memset(provider, 0, sizeof(*provider));
    if (original_root_physicality_id == NULL ||
        materialization_digest_equal(original_root_physicality_id, &zero))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace original content read requires an exact physicality identity")));
    laplace_pg_materialization_provider_create_for_view(context, required_view_id, owner, provider);
    (*owner)->required_original_physicality = *original_root_physicality_id;
}

void laplace_pg_materialization_provider_create_selected(
    const laplace_framework_context* context,
    const laplace_digest256* selection_receipt,
    const laplace_pg_materialization_selection* selections,
    size_t selection_count,
    laplace_pg_materialization_provider_state** owner,
    laplace_cognition_materialization_provider_v1* provider) {
    laplace_pg_materialization_provider_state* state;
    HASHCTL control;
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
    if ((selections == NULL) != (selection_count == 0u) ||
        (selection_receipt == NULL) != (selection_count == 0u) || selection_count > 4096u) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace materialization physicality selections exceed their finite boundary")));
    }
    if (selection_receipt != NULL) {
        static const laplace_digest256 zero_digest = {{0}};
        if (materialization_digest_equal(selection_receipt, &zero_digest))
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace materialization selected physicalities require their verified receipt")));
    }

    state = (laplace_pg_materialization_provider_state*)palloc0(sizeof(*state));
    state->context = *context;
    state->caller_context = CurrentMemoryContext;
    state->scratch_context = AllocSetContextCreate(
        CurrentMemoryContext,
        "Laplace materialization frontier scratch",
        ALLOCSET_DEFAULT_SIZES);
    state->cache_context = AllocSetContextCreate(
        CurrentMemoryContext,
        "Laplace materialization frontier cache",
        ALLOCSET_DEFAULT_SIZES);
    state->binding_context = AllocSetContextCreate(CurrentMemoryContext,
        "Laplace materialization immutable bindings", ALLOCSET_SMALL_SIZES);
    state->scope_context = AllocSetContextCreate(CurrentMemoryContext,
        "Laplace materialization root scope", ALLOCSET_SMALL_SIZES);
    memset(&control, 0, sizeof(control));
    control.keysize = sizeof(materialization_cache_key);
    control.entrysize = sizeof(materialization_cache_entry);
    control.hcxt = state->cache_context;
    state->cache = hash_create(
        "Laplace materialization exact-node cache",
        128,
        &control,
        HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
    if (selection_count != 0u) {
        size_t index;
        MemoryContext previous = MemoryContextSwitchTo(state->binding_context);
        state->selections = (laplace_pg_materialization_selection*)palloc(
            selection_count * sizeof(*selections));
        memcpy(state->selections, selections, selection_count * sizeof(*selections));
        MemoryContextSwitchTo(previous);
        qsort(state->selections, selection_count, sizeof(*selections),
            materialization_selection_compare);
        for (index = 0u; index < selection_count; ++index) {
            static const laplace_digest256 zero_digest = {{0}};
            static const laplace_id128 zero_id = {{0}};
            const laplace_pg_materialization_selection candidate = state->selections[index];
            if (materialization_id_equal(&candidate.entity_id, &zero_id) ||
                materialization_digest_equal(&candidate.physicality_id, &zero_digest)) {
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace materialization physicality selection has an absent identity")));
            }
            if (state->selection_count != 0u && materialization_id_equal(
                    &state->selections[state->selection_count - 1u].entity_id,
                    &candidate.entity_id)) {
                if (!materialization_digest_equal(
                        &state->selections[state->selection_count - 1u].physicality_id,
                        &candidate.physicality_id)) {
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                        errmsg("Laplace materialization has conflicting selected physicalities for one entity")));
                }
            } else state->selections[state->selection_count++] = candidate;
        }
    }
    state->base_selections = state->selections;
    state->base_selection_count = state->selection_count;
    state->cleanup.func = materialization_provider_release;
    state->cleanup.arg = state;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &state->cleanup);
    materialization_provider_identify(context, selection_receipt, &state->provider_fingerprint);
    blake3_hasher_init(&state->readset);
    {
        static const char readset_domain[] =
            "laplace-postgresql-materialization-readset-v2";
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
    provider->begin_read = materialization_begin_read;
    provider->select_reference = materialization_select_reference;
    provider->resolve_selected = materialization_resolve_selected;
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
    if ((*owner)->cache_context != NULL) {
        MemoryContextDelete((*owner)->cache_context);
        (*owner)->cache_context = NULL;
        (*owner)->cache = NULL;
    }
    if ((*owner)->binding_context != NULL) {
        MemoryContextDelete((*owner)->binding_context);
        (*owner)->binding_context = NULL;
    }
    if ((*owner)->scope_context != NULL) {
        MemoryContextDelete((*owner)->scope_context);
        (*owner)->scope_context = NULL;
    }
    materialization_provider_release(*owner);
    *owner = NULL;
}
