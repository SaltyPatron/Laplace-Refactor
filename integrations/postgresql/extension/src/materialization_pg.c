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

typedef struct materialization_cache_entry {
    laplace_id128 entity_id;
    laplace_cognition_materialization_node node;
    laplace_trajectory_carrier* trajectory;
    uint64_t carrier_count;
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
    HTAB* cache;
    laplace_pg_materialization_selection* selections;
    size_t selection_count;
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

static materialization_cache_entry* materialization_cache_find(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id) {
    return (materialization_cache_entry*)hash_search(
        state->cache, entity_id, HASH_FIND, NULL);
}

static materialization_cache_entry* materialization_cache_enter(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id,
    bool* found) {
    materialization_cache_entry* entry =
        (materialization_cache_entry*)hash_search(
            state->cache, entity_id, HASH_ENTER, found);
    if (!*found) {
        memset(
            ((char*)entry) + sizeof(entry->entity_id),
            0,
            sizeof(*entry) - sizeof(entry->entity_id));
    }
    return entry;
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
    const laplace_pg_materialization_provider_state* state,
    const laplace_id128* ids, size_t count) {
    Datum* values = (Datum*)palloc0(sizeof(*values) * count);
    bool* nulls = (bool*)palloc0(sizeof(*nulls) * count);
    int dimensions[1] = {(int)count};
    int lower_bounds[1] = {1};
    size_t index;
    for (index = 0u; index < count; ++index) {
        const laplace_pg_materialization_selection* selection =
            materialization_selection_find(state, &ids[index]);
        nulls[index] = selection == NULL;
        if (selection != NULL) values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            selection->physicality_id.bytes, sizeof(selection->physicality_id.bytes)));
    }
    return construct_md_array(values, nulls, 1, dimensions, lower_bounds,
        BYTEAOID, -1, false, TYPALIGN_INT);
}

static void materialization_cache_trajectory(
    laplace_pg_materialization_provider_state* state,
    materialization_cache_entry* entry,
    const bytea* trajectory,
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
    if ((size_t)VARSIZE_ANY_EXHDR(trajectory) != bytes || logical_count <= 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace materialization composition trajectory size is invalid")));
    }
    for (index = 0u; index < (size_t)carrier_count; ++index) {
        laplace_trajectory_carrier carrier;
        laplace_composition_occurrence occurrence;
        memcpy(
            &carrier,
            VARDATA_ANY(trajectory) + index * sizeof(carrier),
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
    memcpy(entry->trajectory, VARDATA_ANY(trajectory), bytes);
    MemoryContextSwitchTo(prior);
    entry->carrier_count = carrier_count;
    entry->node.logical_count = logical_count;
    entry->node.carrier_count = carrier_count;
    entry->node.kind = LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION;
    entry->node.tier_floor = (uint8_t)(maximum_child_tier + 1u);
}

static void materialization_verify_physicality(
    HeapTuple tuple, TupleDesc descriptor,
    const laplace_cognition_materialization_node* node,
    uint64_t logical_count, uint64_t carrier_count) {
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
}

static void materialization_resolve_batch(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* requested,
    size_t requested_count) {
    static const char entity_sql[] =
        "WITH input(entity_id, source_index) AS MATERIALIZED ("
        " SELECT entity_id, ordinality - 1"
        " FROM unnest($1::bytea[]) WITH ORDINALITY AS u(entity_id, ordinality))"
        " SELECT i.source_index, e.identity_witness"
        " FROM input AS i"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".entity AS e ON e.entity_id=i.entity_id"
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
    materialization_cache_entry** entries;
    laplace_digest256* witnesses;
    laplace_unicode_identity_key* keys;
    uint32_t* positions;
    uint8_t* reverse_found;
    laplace_unicode_atom_record_view* atoms;
    uint8_t* atom_found;
    laplace_id128* composition_ids;
    size_t* composition_source;
    size_t unique_count = 0u;
    volatile size_t composition_count = 0u;
    size_t index;
    Oid entity_types[1] = {BYTEAARRAYOID};
    Datum entity_values[1];
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
            materialization_cache_enter(state, &requested[index], &found);
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
    keys = (laplace_unicode_identity_key*)palloc0(sizeof(*keys) * unique_count);
    positions = (uint32_t*)palloc0(sizeof(*positions) * unique_count);
    reverse_found = (uint8_t*)palloc0(sizeof(*reverse_found) * unique_count);
    atoms = (laplace_unicode_atom_record_view*)palloc0(sizeof(*atoms) * unique_count);
    atom_found = (uint8_t*)palloc0(sizeof(*atom_found) * unique_count);
    composition_ids = (laplace_id128*)palloc(sizeof(*composition_ids) * unique_count);
    composition_source = (size_t*)palloc(sizeof(*composition_source) * unique_count);

    entity_values[0] = PointerGetDatum(materialization_id_array(ids, unique_count));
    result = SPI_execute_with_args(
        entity_sql, 1, entity_types, entity_values, NULL, true,
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
        if (reverse_found[index] != 0u) {
            const laplace_pg_materialization_selection* selection =
                materialization_selection_find(state, &ids[index]);
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
            if (selection != NULL && !materialization_digest_equal(
                    &selection->physicality_id, &atoms[index].value.physicality_id)) {
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace materialization selected atom physicality differs from the pinned Unicode generation")));
            }
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
            composition_ids[composition_count] = ids[index];
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
            state, composition_ids, composition_count));
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
                const laplace_pg_materialization_selection* selection =
                    materialization_selection_find(state, &entry->node.entity_id);
                if (selection != NULL && !materialization_digest_equal(
                        &selection->physicality_id, &entry->node.physicality_id)) {
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
                &entry->node, logical_count, carrier_count);
            materialization_cache_trajectory(
                state, entry, trajectory, carrier_count, logical_count);
            materialization_node_receipt(
                state, &entry->node, &entry->node.node_receipt_id);
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
        SPI_freetuptable(SPI_tuptable);
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

static void materialization_prefetch_children(
    laplace_pg_materialization_provider_state* state,
    materialization_cache_entry* parent) {
    laplace_id128* children;
    size_t child_count = 0u;
    size_t index;
    uint64_t ordinal = 1u;
    volatile bool failed = false;
    ErrorData* volatile ignored = NULL;

    if (parent == NULL || parent->ready == 0u ||
        parent->node.kind != LAPLACE_COGNITION_MATERIALIZATION_NODE_COMPOSITION ||
        parent->children_prefetched != 0u || parent->trajectory == NULL ||
        parent->carrier_count == 0u ||
        parent->carrier_count > (uint64_t)(MaxAllocSize / sizeof(*children))) {
        return;
    }
    parent->children_prefetched = 1u;
    children = (laplace_id128*)palloc(
        sizeof(*children) * (size_t)parent->carrier_count);
    for (index = 0u; index < (size_t)parent->carrier_count; ++index) {
        laplace_composition_occurrence occurrence;
        size_t prior;
        bool duplicate = false;
        if (laplace_trajectory_composition_decode_one(
                &parent->trajectory[index], ordinal, &occurrence) !=
                LAPLACE_TRAJECTORY_OK ||
            UINT64_MAX - ordinal < (uint64_t)occurrence.run_length) {
            return;
        }
        ordinal += (uint64_t)occurrence.run_length;
        if (materialization_cache_find(state, &occurrence.entity_id) != NULL) {
            continue;
        }
        for (prior = 0u; prior < child_count; ++prior) {
            if (materialization_id_equal(
                    &children[prior], &occurrence.entity_id)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            children[child_count++] = occurrence.entity_id;
        }
    }
    if (child_count == 0u) {
        return;
    }

    /* Prefetch is a physical acceleration only. A child-level corruption or
     * provider fault must not change a parent's logical read into an earlier
     * failure. Discard the speculative diagnostic and let an on-demand resolve
     * reproduce it if native execution actually reaches that child. */
    PG_TRY();
    {
        materialization_resolve_batch(state, children, child_count);
    }
    PG_CATCH();
    {
        ignored = CopyErrorData();
        FlushErrorState();
        materialization_reset_resolving(state);
        failed = true;
    }
    PG_END_TRY();
    if (ignored != NULL) {
        FreeErrorData(ignored);
    }
    (void)failed;
}

static int materialization_resolve_node_impl(
    laplace_pg_materialization_provider_state* state,
    const laplace_id128* entity_id,
    laplace_cognition_materialization_node* node) {
    materialization_cache_entry* entry;
    if (state == NULL || entity_id == NULL || node == NULL) {
        return 1;
    }
    entry = materialization_cache_find(state, entity_id);
    if (entry == NULL || entry->ready == 0u) {
        materialization_resolve_batch(state, entity_id, 1u);
        entry = materialization_cache_find(state, entity_id);
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
    entry = materialization_cache_find(state, &node->entity_id);
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

static void materialization_provider_release(void* opaque) {
    laplace_pg_materialization_provider_state* state =
        (laplace_pg_materialization_provider_state*)opaque;
    if (state == NULL) {
        return;
    }
    state->scratch_context = NULL;
    state->cache_context = NULL;
    state->cache = NULL;
    state->selections = NULL;
    state->selection_count = 0u;
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
    memset(&control, 0, sizeof(control));
    control.keysize = sizeof(laplace_id128);
    control.entrysize = sizeof(materialization_cache_entry);
    control.hcxt = state->cache_context;
    state->cache = hash_create(
        "Laplace materialization exact-node cache",
        128,
        &control,
        HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
    if (selection_count != 0u) {
        size_t index;
        MemoryContext previous = MemoryContextSwitchTo(state->cache_context);
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
    materialization_provider_release(*owner);
    *owner = NULL;
}
