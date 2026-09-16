#include "postgres.h"

#include <limits.h>
#include <stdint.h>
#include <string.h>
#include "access/detoast.h"
#include "catalog/pg_type.h"
#include "catalog/namespace.h"
#include "executor/spi.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "blake3.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/physicality_entity.h"
#include "laplace/physicality_occurrence_binding.h"
#include "laplace/content_reference_view.h"
#include "laplace_pg_internal.h"
#include "composition_pg.h"
#include "persistence_rows_pg.h"
#include "persistence_entities_pg.h"
#include "physicality_entity_pg.h"
#include "unicode_atoms_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_physicality_entity_admit_batch);
PG_FUNCTION_INFO_V1(laplace_pg_physicality_entity_varlena_bytes);

/* PostgreSQL's whole-row projection flattens toasted fields. Inspect the raw
 * field size before any such projection can allocate its expanded payload. */
Datum laplace_pg_physicality_entity_varlena_bytes(PG_FUNCTION_ARGS) {
    Oid type = get_fn_expr_argtype(fcinfo->flinfo, 0);
    if (!OidIsValid(type) || get_typlen(type) != -1 || get_typbyval(type))
        ereport(ERROR, (errcode(ERRCODE_DATATYPE_MISMATCH), errmsg("Laplace derivation byte inventory requires a variable-length value")));
    PG_RETURN_INT64((int64)toast_raw_datum_size(PG_GETARG_DATUM(0)));
}

typedef struct reflection_budget {
    uint64_t memory_limit;
    uint64_t memory_used;
    uint64_t operation_limit;
    uint64_t operations;
    uint64_t logical_limit;
    uint64_t logical_used;
    bool read_only;
} reflection_budget;

typedef struct reflection_source {
    laplace_persistence_physicality_record physicality;
    laplace_digest256 witness;
    laplace_trajectory_carrier* carriers;
    size_t carrier_count;
} reflection_source;

typedef struct reflection_calculation {
    laplace_physicality_entity_plan* plan;
    laplace_physicality_entity_plan_view plan_view;
    laplace_composition_working_set* working_set;
    laplace_composition_working_set_summary summary;
    const laplace_composition_result* results;
    size_t result_count;
    laplace_digest256 view_id;
    laplace_composition_known_entity* external_used;
    laplace_pg_physicality_entity_view* generated;
    size_t generated_count;
    laplace_digest256 source_geometry;
    uint64_t admitted_limits[4];
    laplace_digest256 binding_set_id;
    const laplace_physicality_occurrence_binding* occurrence_bindings;
    size_t occurrence_binding_count;
    const laplace_composition_known_entity* all_known;
    size_t all_known_count;
    laplace_physicality_occurrence_binding* generated_occurrence_bindings;
    size_t generated_occurrence_binding_count;
    laplace_digest256 generated_binding_receipt;
    struct reflection_bindings* binding_scope;
    laplace_content_reference_plan* reference_plan;
    laplace_content_reference_plan_view reference_view;
    laplace_composition_working_set* reference_working_set;
    laplace_composition_working_set_summary reference_summary;
    const laplace_composition_result* reference_results;
    size_t reference_result_count;
    laplace_composition_known_entity* canonical_external;
    laplace_composition_known_entity retained_source;
    laplace_composition_known_entity* canonical_known;
    size_t canonical_known_count;
    laplace_composition_known_entity* complete_known;
    size_t complete_known_count;
} reflection_calculation;

#define REFLECTION_RECORD_ROW \
    "ROW(p.physicality_id,p.entity_id,p.physicality_type,p.vertex_class," \
    "p.recipe_version,p.structural_form,p.dimension_count,p.flags," \
    "p.recipe_fingerprint,p.geometry_epoch,p.trajectory_fingerprint," \
    "p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m,p.radius," \
    "p.logical_count,p.vertex_count)::" LAPLACE_PG_SCHEMA ".physicality_record"

static void reflection_limit(const char* detail) {
    ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
        errmsg("Laplace physicality entity derivation exceeds its finite envelope"),
        errdetail("%s", detail)));
}

static void reflection_reserve(reflection_budget* budget, uint64_t count, uint64_t bytes) {
    if (bytes != 0u && count > UINT64_MAX / bytes)
        reflection_limit("memory estimate overflow");
    bytes *= count;
    if (budget->memory_used > budget->memory_limit ||
        bytes > budget->memory_limit - budget->memory_used)
        reflection_limit("memory grant exhausted before allocation or detoast");
    budget->memory_used += bytes;
}

static void reflection_query(reflection_budget* budget) {
    if (budget->operations >= budget->operation_limit)
        reflection_limit("database operation grant exhausted before SPI");
    ++budget->operations;
    CHECK_FOR_INTERRUPTS();
}

static void reflection_logical(reflection_budget* budget, uint64_t count) {
    if (budget->logical_used > budget->logical_limit ||
        count > budget->logical_limit - budget->logical_used)
        reflection_limit("logical occurrence work exhausted before native hashing");
    budget->logical_used += count;
}

static Datum reflection_required(HeapTuple tuple, TupleDesc descriptor, int column) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace physicality derivation lost a required stored field"),
            errdetail("column=%d", column)));
    return value;
}

static void reflection_bytes(Datum datum, void* result, size_t bytes) {
    bytea* value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != bytes)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace physicality derivation identity has invalid width")));
    memcpy(result, VARDATA_ANY(value), bytes);
}

static ArrayType* reflection_digests(const laplace_digest256* ids, size_t count) {
    Datum* values;
    if (count > INT_MAX || count > MaxAllocSize / sizeof(Datum))
        reflection_limit("identity set addressability");
    if (count == 0u) return construct_empty_array(BYTEAOID);
    values = palloc(count * sizeof(*values));
    for (size_t index = 0u; index < count; ++index)
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(ids[index].bytes, 32u));
    return construct_array(values, (int)count, BYTEAOID, -1, false, TYPALIGN_INT);
}

/* Metadata admission precedes the payload query. Both queries address exactly
 * the supplied primary keys; no whole-world or per-record SQL resolution. */
static reflection_source* reflection_sources(
    const laplace_digest256* ids, size_t count, reflection_budget* budget) {
    static const char metadata_sql[] =
        "SELECT i.ordinality," REFLECTION_RECORD_ROW ",e.identity_witness,"
        "coalesce(octet_length(p.trajectory),0) FROM unnest($1::bytea[])"
        " WITH ORDINALITY i(id,ordinality) LEFT JOIN " LAPLACE_PG_SCHEMA
        ".physicality p ON p.physicality_id=i.id LEFT JOIN " LAPLACE_PG_SCHEMA
        ".entity e ON e.entity_id=p.entity_id ORDER BY i.ordinality";
    static const char payload_sql[] =
        "SELECT i.ordinality,p.trajectory FROM unnest($1::bytea[])"
        " WITH ORDINALITY i(id,ordinality) LEFT JOIN " LAPLACE_PG_SCHEMA
        ".physicality p ON p.physicality_id=i.id ORDER BY i.ordinality";
    Oid types[1] = {BYTEAARRAYOID};
    Datum values[1];
    reflection_source* sources;
    bool has_payload = false;
    if (count == 0u) return NULL;
    reflection_reserve(budget, count, sizeof(*sources) + 256u);
    if (count > MaxAllocSize / sizeof(*sources) || count >= LONG_MAX)
        reflection_limit("source set addressability");
    sources = palloc0(count * sizeof(*sources));
    values[0] = PointerGetDatum(reflection_digests(ids, count));
    reflection_query(budget);
    if (SPI_execute_with_args(metadata_sql, 1, types, values, NULL, budget->read_only, (long)count + 1) != SPI_OK_SELECT ||
        SPI_processed != count || SPI_tuptable == NULL)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace physicality descriptor source metadata set is incomplete")));
    for (size_t index = 0u; index < count; ++index) {
        HeapTuple row = SPI_tuptable->vals[index];
        TupleDesc descriptor = SPI_tuptable->tupdesc;
        int32 bytes;
        if (DatumGetInt64(reflection_required(row, descriptor, 1)) != (int64)index + 1)
            ereport(ERROR, (errmsg("Laplace physicality source order differs")));
        laplace_pg_physicality_read_record(reflection_required(row, descriptor, 2), &sources[index].physicality);
        reflection_bytes(reflection_required(row, descriptor, 3), &sources[index].witness, 32u);
        if (memcmp(sources[index].physicality.physicality_id.bytes, ids[index].bytes, 32u) != 0 ||
            memcmp(sources[index].physicality.entity_id.bytes, sources[index].witness.bytes, 16u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace descriptor source identity or witness differs")));
        bytes = DatumGetInt32(reflection_required(row, descriptor, 4));
        if (bytes < 0 || sources[index].physicality.vertex_count > SIZE_MAX / sizeof(laplace_trajectory_carrier) ||
            (uint64_t)bytes != sources[index].physicality.vertex_count * sizeof(laplace_trajectory_carrier))
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace descriptor source trajectory length differs")));
        sources[index].carrier_count = (size_t)sources[index].physicality.vertex_count;
        has_payload = has_payload || bytes != 0;
        reflection_reserve(budget, (uint64_t)bytes, 3u);
    }
    SPI_freetuptable(SPI_tuptable);
    if (!has_payload) return sources;
    reflection_query(budget);
    if (SPI_execute_with_args(payload_sql, 1, types, values, NULL, budget->read_only, (long)count + 1) != SPI_OK_SELECT ||
        SPI_processed != count || SPI_tuptable == NULL)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace physicality descriptor source payload set is incomplete")));
    for (size_t index = 0u; index < count; ++index) {
        bool is_null = false;
        Datum payload = SPI_getbinval(SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 2, &is_null);
        size_t bytes = sources[index].carrier_count * sizeof(laplace_trajectory_carrier);
        if (DatumGetInt64(reflection_required(SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 1)) != (int64)index + 1)
            ereport(ERROR, (errmsg("Laplace physicality payload order differs")));
        if (bytes == 0u) {
            if (!is_null && VARSIZE_ANY_EXHDR(DatumGetByteaPP(payload)) != 0)
                ereport(ERROR, (errmsg("Laplace atomic descriptor source unexpectedly has a trajectory")));
        } else {
            if (is_null) ereport(ERROR, (errmsg("Laplace descriptor source trajectory is absent")));
            sources[index].carriers = palloc(bytes);
            reflection_bytes(payload, sources[index].carriers, bytes);
        }
    }
    SPI_freetuptable(SPI_tuptable);
    return sources;
}

static void reflection_destroy(reflection_calculation* calculation) {
    laplace_composition_working_set_destroy(&calculation->working_set);
    laplace_physicality_entity_plan_destroy(&calculation->plan);
    laplace_composition_working_set_destroy(&calculation->reference_working_set);
    laplace_content_reference_plan_destroy(&calculation->reference_plan);
}

static int reflection_known_compare(const void* left, const void* right);

static reflection_source* reflection_known_validate(
    const laplace_framework_context* context,
    const laplace_composition_known_entity* known, size_t count,
    reflection_budget* budget) {
    laplace_digest256* selected;
    reflection_source* sources;
    uint32_t* atom_positions;
    laplace_composition_known_entity* resolved_atoms;
    size_t atom_count = 0u;
    size_t atom_cursor = 0u;
    laplace_composition_known_entity* tier_evidence;
    bool* independently_validated;
    size_t tier_evidence_count = 0u;
    reflection_reserve(budget, count, sizeof(*tier_evidence) + sizeof(*independently_validated));
    if (count > MaxAllocSize / sizeof(*tier_evidence)) reflection_limit("content tier evidence addressability");
    tier_evidence = palloc(count * sizeof(*tier_evidence));
    independently_validated = palloc0(count * sizeof(*independently_validated));
    if (count == 0u || count > MaxAllocSize / sizeof(*selected))
        reflection_limit("external selector count");
    reflection_reserve(budget, count, sizeof(*selected));
    selected = palloc(count * sizeof(*selected));
    for (size_t index = 0u; index < count; ++index) selected[index] = known[index].physicality_id;
    sources = reflection_sources(selected, count, budget);
    reflection_reserve(budget, count, sizeof(*atom_positions) + sizeof(*resolved_atoms));
    atom_positions = palloc(count * sizeof(*atom_positions));
    resolved_atoms = palloc0(count * sizeof(*resolved_atoms));
    for (size_t index = 0u; index < count; ++index) {
        if (sources[index].physicality.physicality_type == LAPLACE_PERSISTENCE_PHYSICALITY_ATOMIC_POINT &&
            known[index].has_atom != 1u)
                ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                    errmsg("A non-Unicode atomic view requires an independently resolved canonical content tier")));
        if (known[index].has_atom == 1u)
            atom_positions[atom_count++] = known[index].atom;
    }
    if (atom_count != 0u) {
        laplace_pg_active_unicode_root active;
        reflection_query(budget); /* mapped owner performs one durable-root SPI query */
        laplace_pg_resolve_active_unicode_atoms(context, atom_positions, atom_count, resolved_atoms, &active);
    }
    for (size_t index = 0u; index < count; ++index) {
        const reflection_source* source = &sources[index];
        laplace_physicality_entity_validation validation;
        uint64_t validation_bytes;
        uint8_t tier = 0u;
        bool independent = true;
        if (memcmp(source->physicality.entity_id.bytes, known[index].entity_id.bytes, 16u) != 0 ||
            memcmp(source->witness.bytes, known[index].identity_witness.bytes, 32u) != 0 ||
            memcmp(source->physicality.centroid.component, known[index].centroid.component, sizeof(laplace_point4d)) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace physicality descriptor external selector differs from its exact stored representation")));
        /* The immutable source epoch belongs to its authenticated physicality.
         * The current context selects the new view, not a rewrite of that source.
         * Validate through the same native body used by descriptor construction.
         * No second descriptor is built merely to validate a selected input. */
        reflection_logical(budget, source->physicality.logical_count);
        if (laplace_physicality_entity_validation_memory_bound(source->carrier_count, &validation_bytes) !=
                LAPLACE_PHYSICALITY_ENTITY_OK)
            reflection_limit("native validation memory estimate overflow");
        reflection_reserve(budget, 1u, validation_bytes);
        if (laplace_physicality_entity_record_validate(&source->physicality,
                source->carriers, source->carrier_count, source->carrier_count,
                source->physicality.logical_count, &validation) != LAPLACE_PHYSICALITY_ENTITY_OK) {
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace descriptor external source failed native validation")));
        }
        budget->memory_used -= validation_bytes;
        if (validation.witness_available != 0u &&
            memcmp(validation.realized_identity_witness.bytes, source->witness.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace descriptor selected source full witness differs from native content")));
        if (known[index].has_atom == 1u) {
            const laplace_composition_known_entity* atom = &resolved_atoms[atom_cursor++];
            if (memcmp(atom->entity_id.bytes, known[index].entity_id.bytes, 16u) != 0 ||
                memcmp(atom->identity_witness.bytes, known[index].identity_witness.bytes, 32u) != 0)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace descriptor atom differs from the pinned native Unicode input")));
        } else {
            independent = validation.tier_available != 0u && validation.witness_available != 0u;
            tier = independent ? validation.tier_floor : known[index].tier_floor;
            if (known[index].has_atom != 0u || known[index].atom != 0u)
                ereport(ERROR, (errmsg("Laplace descriptor composition cannot masquerade as an atom")));
        }
        if (known[index].tier_floor != tier)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace descriptor external content tier differs from its actual trajectory")));
        independently_validated[index] = independent;
        if (independent) tier_evidence[tier_evidence_count++] = known[index];
    }
    qsort(tier_evidence, tier_evidence_count, sizeof(*tier_evidence), reflection_known_compare);
    for (size_t index = 1u; index < tier_evidence_count; ++index)
        if (reflection_known_compare(&tier_evidence[index - 1u], &tier_evidence[index]) == 0 &&
            (tier_evidence[index - 1u].tier_floor != tier_evidence[index].tier_floor ||
             tier_evidence[index - 1u].has_atom != tier_evidence[index].has_atom ||
             tier_evidence[index - 1u].atom != tier_evidence[index].atom ||
             memcmp(tier_evidence[index - 1u].identity_witness.bytes, tier_evidence[index].identity_witness.bytes, 32u) != 0))
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace selected forms disagree on canonical content metadata")));
    for (size_t index = 0u; index < count; ++index) {
        if (independently_validated[index]) continue;
        const laplace_composition_known_entity* evidence = bsearch(&known[index], tier_evidence, tier_evidence_count,
            sizeof(*tier_evidence), reflection_known_compare);
        if (evidence == NULL || evidence->tier_floor != known[index].tier_floor ||
            evidence->has_atom != known[index].has_atom || evidence->atom != known[index].atom ||
            memcmp(evidence->identity_witness.bytes, known[index].identity_witness.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace singleton source needs independently authenticated canonical content witness and tier")));
    }
    return sources;
}

static int reflection_known_compare(const void* left, const void* right) {
    return memcmp(((const laplace_composition_known_entity*)left)->entity_id.bytes,
                  ((const laplace_composition_known_entity*)right)->entity_id.bytes, 16u);
}

static int reflection_known_selection_compare(const void* left, const void* right) {
    int order = reflection_known_compare(left, right);
    return order != 0 ? order : memcmp(((const laplace_composition_known_entity*)left)->physicality_id.bytes,
        ((const laplace_composition_known_entity*)right)->physicality_id.bytes, 32u);
}

static bool reflection_known_equal(
    const laplace_composition_known_entity* left,
    const laplace_composition_known_entity* right) {
    return memcmp(left->entity_id.bytes, right->entity_id.bytes, 16u) == 0 &&
        memcmp(left->identity_witness.bytes, right->identity_witness.bytes, 32u) == 0 &&
        memcmp(left->physicality_id.bytes, right->physicality_id.bytes, 32u) == 0 &&
        memcmp(left->centroid.component, right->centroid.component, sizeof(laplace_point4d)) == 0 &&
        left->atom == right->atom && left->tier_floor == right->tier_floor &&
        left->has_atom == right->has_atom;
}

static size_t reflection_known_sort(laplace_composition_known_entity* known, size_t count) {
    size_t unique = 0u;
    qsort(known, count, sizeof(*known), reflection_known_selection_compare);
    for (size_t index = 0u; index < count; ++index) {
        if (unique != 0u && reflection_known_selection_compare(&known[unique - 1u], &known[index]) == 0) {
            if (!reflection_known_equal(&known[unique - 1u], &known[index]))
                ereport(ERROR, (errcode(ERRCODE_AMBIGUOUS_PARAMETER),
                    errmsg("Laplace descriptor input disagrees on one exact external physicality")));
        } else known[unique++] = known[index];
    }
    return unique;
}

static int reflection_position_compare(const void* left, const void* right) {
    uint32_t a = *(const uint32_t*)left, b = *(const uint32_t*)right;
    return a < b ? -1 : a != b;
}

/* Collect one complete atom frontier for the entire admitted record batch.
 * The mapped provider is called once, never once per descriptor/scalar. */
static laplace_composition_known_entity* reflection_atoms(
    const laplace_framework_context* context, reflection_calculation* calculations,
    size_t count, const laplace_composition_known_entity* source_known, size_t source_known_count,
    uint32_t** positions_out, size_t* count_out, reflection_budget* budget) {
    uint64_t total = source_known_count;
    uint32_t* positions;
    laplace_composition_known_entity* atoms;
    size_t cursor = 0u, unique = 0u;
    laplace_pg_active_unicode_root active;
    for (size_t index = 0u; index < count; ++index) {
        if (UINT64_MAX - total < calculations[index].plan_view.atom_count)
            reflection_limit("atom frontier count overflow");
        total += calculations[index].plan_view.atom_count;
    }
    reflection_reserve(budget, total, sizeof(*positions) + sizeof(*atoms));
    if (total == 0u || total > MaxAllocSize / sizeof(*atoms))
        reflection_limit("descriptor atom frontier is empty or unaddressable");
    positions = palloc((size_t)total * sizeof(*positions));
    for (size_t index = 0u; index < count; ++index) {
        const laplace_physicality_entity_plan_view* view = &calculations[index].plan_view;
        memcpy(&positions[cursor], view->atom_positions, (size_t)view->atom_count * sizeof(*positions));
        cursor += (size_t)view->atom_count;
    }
    for (size_t index = 0u; index < source_known_count; ++index)
        if (source_known[index].has_atom == 1u) positions[cursor++] = source_known[index].atom;
    qsort(positions, cursor, sizeof(*positions), reflection_position_compare);
    for (size_t index = 0u; index < cursor; ++index)
        if (unique == 0u || positions[unique - 1u] != positions[index]) positions[unique++] = positions[index];
    atoms = palloc0(unique * sizeof(*atoms));
    reflection_query(budget);
    laplace_pg_resolve_active_unicode_atoms(context, positions, unique, atoms, &active);
    *positions_out = positions;
    *count_out = unique;
    return atoms;
}

static void reflection_prepare(
    const laplace_framework_context* context, const reflection_source* source,
    uint64_t maximum_requests, uint64_t maximum_operands, uint64_t maximum_carriers,
    uint64_t admitted_logical_limit,
    reflection_calculation* calculation, reflection_budget* budget) {
    laplace_physicality_entity_input input = {0};
    laplace_physicality_entity_status status;
    uint64_t plan_bytes;
    input.physicality = &source->physicality;
    input.carriers = source->carriers;
    input.carrier_count = source->carrier_count;
    input.view_geometry_epoch = context->epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY];
    input.maximum_requests = maximum_requests;
    input.maximum_operands = maximum_operands;
    input.maximum_carriers = maximum_carriers;
    calculation->admitted_limits[0] = maximum_requests;
    calculation->admitted_limits[1] = maximum_operands;
    calculation->admitted_limits[2] = maximum_carriers;
    calculation->admitted_limits[3] = admitted_logical_limit;
    input.maximum_logical_count = budget->logical_limit - budget->logical_used;
    calculation->source_geometry = source->physicality.geometry_epoch;
    reflection_logical(budget, source->physicality.logical_count);
    /* The native owner includes vector spare capacity, maps, and temporary
     * decoded occurrences. PG never guesses C++ object/container layout. */
    if (laplace_physicality_entity_plan_memory_bound(&input, &plan_bytes) != LAPLACE_PHYSICALITY_ENTITY_OK)
        reflection_limit("native descriptor memory estimate overflow");
    reflection_reserve(budget, 1u, plan_bytes);
    status = laplace_physicality_entity_plan_create(&input, &calculation->plan);
    if (status != LAPLACE_PHYSICALITY_ENTITY_OK)
        ereport(ERROR, (errcode(status == LAPLACE_PHYSICALITY_ENTITY_LIMIT
            ? ERRCODE_PROGRAM_LIMIT_EXCEEDED : ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace physicality descriptor native plan rejected its source"),
            errdetail("status=%u", (unsigned)status)));
    status = laplace_physicality_entity_plan_view_get(calculation->plan, &calculation->plan_view);
    if (status != LAPLACE_PHYSICALITY_ENTITY_OK ||
        memcmp(calculation->plan_view.physicality_record_id.bytes, source->physicality.physicality_id.bytes, 32u) != 0 ||
        memcmp(calculation->plan_view.realized_entity_id.bytes, source->physicality.entity_id.bytes, 16u) != 0)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace native physicality descriptor plan changed source identity")));
    if (calculation->plan_view.source_validation.witness_available != 0u &&
        memcmp(calculation->plan_view.source_validation.realized_identity_witness.bytes,
            source->witness.bytes, 32u) != 0)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace descriptor original source full witness differs from native content")));
}

static void reflection_calculate(
    const laplace_framework_context* semantic_context,
    const laplace_execution_grant* current_grant,
    const laplace_composition_known_entity* external, size_t external_count,
    const uint32_t* atom_positions, const laplace_composition_known_entity* atoms, size_t atom_count,
    uint64_t preferred_batch_bytes, reflection_calculation* calculation, reflection_budget* budget) {
    const laplace_physicality_entity_plan_view* view = &calculation->plan_view;
    laplace_composition_known_entity* known;
    laplace_composition_working_set_input input = {0};
    laplace_framework_context execution_context = *semantic_context;
    laplace_composition_status status;
    uint64_t count = view->external_entity_count + view->atom_count;
    if (calculation->canonical_external != NULL) {
        external = calculation->canonical_external;
        external_count = (size_t)view->external_entity_count;
    }
    if (count < view->atom_count || count > MaxAllocSize / sizeof(*known))
        reflection_limit("known descriptor frontier addressability");
    reflection_reserve(budget, count, sizeof(*known));
    known = palloc0((size_t)count * sizeof(*known));
    for (size_t index = 0u; index < view->external_entity_count; ++index) {
        laplace_composition_known_entity key = {0};
        const laplace_composition_known_entity* selected;
        key.entity_id = view->external_entity_ids[index];
        selected = bsearch(&key, external, external_count, sizeof(*external), reflection_known_compare);
        if (selected == NULL)
            ereport(ERROR, (errcode(ERRCODE_NO_DATA_FOUND),
                errmsg("Laplace physicality descriptor requires an exact external entity selector")));
        if (calculation->canonical_external == NULL &&
            memcmp(key.entity_id.bytes, view->realized_entity_id.bytes, 16u) == 0 &&
            memcmp(calculation->source_geometry.bytes, view->view_geometry_epoch.bytes, 32u) == 0) {
            key.physicality_id = view->physicality_record_id;
            selected = bsearch(&key, external, external_count, sizeof(*external), reflection_known_selection_compare);
            if (selected == NULL)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace descriptor target selector must retain its compatible original physicality")));
        } else if ((selected != external && reflection_known_compare(selected - 1, selected) == 0) ||
            (selected + 1 != external + external_count && reflection_known_compare(selected, selected + 1) == 0)) {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                errmsg("Laplace descriptor reference requires a canonical content view for multiply selected physicalities")));
        }
        known[index] = *selected;
        if (calculation->canonical_external == NULL &&
            memcmp(key.entity_id.bytes, view->realized_entity_id.bytes, 16u) == 0 &&
            memcmp(calculation->source_geometry.bytes, view->view_geometry_epoch.bytes, 32u) == 0 &&
            memcmp(selected->physicality_id.bytes, view->physicality_record_id.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace descriptor target selector must retain its compatible original physicality")));
    }
    calculation->external_used = known;
    for (size_t index = 0u; index < view->atom_count; ++index) {
        const uint32_t* found = bsearch(&view->atom_positions[index], atom_positions,
            atom_count, sizeof(*atom_positions), reflection_position_compare);
        if (found == NULL) ereport(ERROR, (errmsg("Laplace descriptor atom frontier is incomplete")));
        known[view->external_entity_count + index] = atoms[found - atom_positions];
    }
    execution_context.resource_grant = *current_grant;
    if (execution_context.resource_grant.memory_bytes > budget->memory_limit - budget->memory_used)
        execution_context.resource_grant.memory_bytes = budget->memory_limit - budget->memory_used;
    input.context = &execution_context;
    input.source_fingerprint = &view->physicality_record_id;
    input.calculation_recipe_fingerprint = &view->recipe_fingerprint;
    input.known_entities = known;
    input.known_entity_count = count;
    calculation->all_known = known;
    calculation->all_known_count = (size_t)count;
    input.operands = view->operands;
    input.operand_count = view->operand_count;
    input.requests = view->requests;
    input.request_count = view->request_count;
    input.preferred_batch_bytes = preferred_batch_bytes;
    status = laplace_composition_working_set_create(&input, &calculation->working_set);
    if (status != LAPLACE_COMPOSITION_OK)
        ereport(ERROR, (errcode(status == LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT
            ? ERRCODE_PROGRAM_LIMIT_EXCEEDED : ERRCODE_DATA_EXCEPTION),
            errmsg("Laplace physicality descriptor composition failed"), errdetail("status=%u", (unsigned)status)));
    if (laplace_composition_working_set_summary_get(calculation->working_set, &calculation->summary) != LAPLACE_COMPOSITION_OK)
        ereport(ERROR, (errmsg("Laplace descriptor composition summary is absent")));
    reflection_reserve(budget, 1u, calculation->summary.estimated_peak_working_bytes);
    calculation->results = laplace_composition_working_set_results(calculation->working_set, &calculation->result_count);
    if (calculation->results == NULL || calculation->result_count != view->request_count ||
        view->root_result_index >= calculation->result_count || calculation->summary.occurrence_count != 0u)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace descriptor composition result topology or zero-occurrence boundary differs")));
    {
        static const char domain[] = "laplace-postgresql-physicality-entity-view-v2";
        laplace_digest256 context_fingerprint;
        blake3_hasher hash;
        if (laplace_framework_context_fingerprint(semantic_context, &context_fingerprint) != LAPLACE_FRAMEWORK_OK)
            ereport(ERROR, (errmsg("Laplace descriptor admission context is invalid")));
        blake3_hasher_init(&hash);
        blake3_hasher_update(&hash, domain, sizeof(domain) - 1u);
        blake3_hasher_update(&hash, context_fingerprint.bytes, 32u);
        blake3_hasher_update(&hash, view->physicality_record_id.bytes, 32u);
        blake3_hasher_update(&hash, calculation->summary.input_fingerprint.bytes, 32u);
        blake3_hasher_update(&hash, calculation->binding_set_id.bytes, 32u);
        for (size_t limit = 0u; limit < 4u; ++limit) {
            uint8_t encoded[8];
            for (size_t byte = 0u; byte < sizeof(encoded); ++byte)
                encoded[byte] = (uint8_t)(calculation->admitted_limits[limit] >> (byte * 8u));
            blake3_hasher_update(&hash, encoded, sizeof(encoded));
        }
        blake3_hasher_finalize(&hash, calculation->view_id.bytes, 32u);
    }
}

typedef struct reflection_entity_owner {
    laplace_persistence_entity_record entity;
    size_t owner;
} reflection_entity_owner;

static int reflection_digest_compare(const void* left, const void* right);
static uint64_t reflection_positive_bigint(Datum value, bool zero);

static int reflection_entity_compare(const void* left, const void* right) {
    return memcmp(((const reflection_entity_owner*)left)->entity.entity_id.bytes,
                  ((const reflection_entity_owner*)right)->entity.entity_id.bytes, 16u);
}

/* One canonical entity stream through the common framework producer and sink.
 * Attribution of a newly inserted shared entity goes to its first input record;
 * that reporting choice never changes canonical identity or derivation owners. */
static void reflection_entities_publish(
    reflection_calculation* calculations, size_t count, uint64_t* inserted,
    const laplace_framework_context* context, uint64_t maximum_batch_bytes,
    laplace_digest256* deposit_receipt, bytea** deposit_snapshot, reflection_budget* budget, bool insert) {
    reflection_entity_owner* entities;
    laplace_pg_composite_binding binding;
    Datum* rows;
    Datum values[1];
    Oid types[1];
    size_t total = 0u, cursor = 0u, unique = 0u;
    for (size_t owner = 0u; owner < count; ++owner) {
        uint64_t current = calculations[owner].summary.unique_entity_count;
        if (current > SIZE_MAX - total) reflection_limit("entity count overflow");
        total += (size_t)current;
    }
    reflection_reserve(budget, total, sizeof(*entities) + 160u);
    if (total == 0u || total > MaxAllocSize / sizeof(*entities)) reflection_limit("entity set addressability");
    entities = palloc(total * sizeof(*entities));
    for (size_t owner = 0u; owner < count; ++owner) {
        size_t candidate_count;
        const laplace_composition_entity_candidate* candidates =
            laplace_composition_working_set_entity_candidates(calculations[owner].working_set, &candidate_count);
        if (candidates == NULL || candidate_count != calculations[owner].summary.unique_entity_count)
            ereport(ERROR, (errmsg("Laplace descriptor entity candidates are incomplete")));
        for (size_t index = 0u; index < candidate_count; ++index) {
            entities[cursor].entity = candidates[index].entity;
            entities[cursor++].owner = owner;
        }
    }
    qsort(entities, total, sizeof(*entities), reflection_entity_compare);
    for (size_t index = 0u; index < total; ++index) {
        if (unique != 0u && reflection_entity_compare(&entities[unique - 1u], &entities[index]) == 0) {
            if (memcmp(entities[unique - 1u].entity.identity_witness.bytes, entities[index].entity.identity_witness.bytes, 32u) != 0)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                    errmsg("Laplace descriptor batch contains a truncated identity collision")));
            if (entities[index].owner < entities[unique - 1u].owner) entities[unique - 1u].owner = entities[index].owner;
        } else entities[unique++] = entities[index];
    }
    rows = palloc(unique * sizeof(*rows));
    laplace_pg_entity_binding_open(&binding);
    for (size_t index = 0u; index < unique; ++index)
        rows[index] = laplace_pg_entity_record(&binding, &entities[index].entity);
    values[0] = PointerGetDatum(laplace_pg_composite_array(&binding, rows, unique));
    types[0] = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    if (insert) {
        static const char source_domain[] = "laplace-physicality-entity-deposit-sources-v1";
        laplace_persistence_entity_record* records;
        laplace_digest256* view_ids;
        laplace_pg_persistence_inserted_entities inserted_output = {0};
        laplace_pg_persistence_options options = {0};
        laplace_pg_persistence_producer_result result = {0};
        laplace_digest256 source;
        blake3_hasher hash;
        uint64_t adapter_bytes;
        reflection_reserve(budget, unique, sizeof(*records) + sizeof(*inserted_output.ids));
        reflection_reserve(budget, count, sizeof(*view_ids));
        records = palloc(unique * sizeof(*records));
        inserted_output.ids = palloc(unique * sizeof(*inserted_output.ids));
        inserted_output.capacity = unique;
        view_ids = palloc(count * sizeof(*view_ids));
        for (size_t index = 0u; index < unique; ++index) records[index] = entities[index].entity;
        for (size_t index = 0u; index < count; ++index) {
            view_ids[index] = calculations[index].view_id;
            if (memcmp(calculations[index].plan_view.recipe_fingerprint.bytes,
                    calculations[0].plan_view.recipe_fingerprint.bytes, 32u) != 0)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor batch mixes native recipes")));
        }
        qsort(view_ids, count, sizeof(*view_ids), reflection_digest_compare);
        blake3_hasher_init(&hash);
        blake3_hasher_update(&hash, source_domain, sizeof(source_domain) - 1u);
        for (size_t index = 0u; index < count; ++index)
            blake3_hasher_update(&hash, view_ids[index].bytes, 32u);
        blake3_hasher_finalize(&hash, source.bytes, 32u);
        adapter_bytes = laplace_pg_persistence_entities_memory_bytes(unique, maximum_batch_bytes);
        if (adapter_bytes == 0u) reflection_limit("entity producer buffer estimate overflow");
        options.reserved_memory_bytes = budget->memory_used;
        reflection_reserve(budget, 1u, adapter_bytes);
        options.maximum_database_operations = budget->operation_limit - budget->operations;
        options.inserted_entities = &inserted_output;
        laplace_pg_persistence_deposit_entities(context, &source,
            &calculations[0].plan_view.recipe_fingerprint, records, unique,
            maximum_batch_bytes, &options, &result);
        if (result.database_operations > options.maximum_database_operations ||
            result.summary.entity_count != unique || result.summary.physicality_count != 0u ||
            result.summary.trajectory_segment_count != 0u || result.summary.attestation_count != 0u ||
            result.summary.consensus_count != 0u || result.summary.logical_occurrence_count != 0u ||
            inserted_output.count != result.inserted[0] || inserted_output.count > unique)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor entity producer result differs")));
        budget->operations += result.database_operations;
        *deposit_receipt = result.producer.stream.receipt_id;
        {
            Oid receipt_type = BYTEAOID;
            Datum receipt_value = PointerGetDatum(laplace_pg_bytes_to_bytea(deposit_receipt->bytes, 32u));
            reflection_query(budget);
            if (SPI_execute_with_args("SELECT pg_catalog.record_send(r) FROM " LAPLACE_PG_SCHEMA
                    ".canonical_deposit_receipt r WHERE receipt_id=$1", 1, &receipt_type,
                    &receipt_value, NULL, false, 1) != SPI_OK_SELECT || SPI_processed != 1u)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor deposit receipt is absent")));
            Datum receipt_row = reflection_required(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1);
            reflection_reserve(budget, 1u, toast_raw_datum_size(receipt_row));
            *deposit_snapshot = DatumGetByteaPCopy(receipt_row);
            SPI_freetuptable(SPI_tuptable);
        }
        for (uint64_t index = 0u; index < inserted_output.count; ++index) {
            reflection_entity_owner key = {0};
            const reflection_entity_owner* owner;
            key.entity.entity_id = inserted_output.ids[index];
            owner = bsearch(&key, entities, unique, sizeof(*entities), reflection_entity_compare);
            if (owner == NULL) ereport(ERROR, (errmsg("Laplace descriptor producer returned an unrequested identity")));
            ++inserted[owner->owner];
        }
    }
    reflection_query(budget);
    if (SPI_execute_with_args(laplace_pg_entity_verify_sql(), 1, types, values, NULL, false, 1) != SPI_OK_SELECT ||
        laplace_pg_scalar_count("descriptor entity exact verification") != unique)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace descriptor entity identities collide with existing canonical witnesses")));
    SPI_freetuptable(SPI_tuptable);
}

static void reflection_view_binding(laplace_pg_composite_binding* binding) {
    Oid digest = laplace_pg_composite_type_oid("record_id_256");
    Oid content = laplace_pg_composite_type_oid("content_id_128");
    Oid types[13] = {digest, digest, content, digest,
        laplace_pg_composite_type_oid("execution_context"),
        laplace_pg_composite_array_oid("composition_known_entity_record"), digest, digest,
        INT8OID, INT8OID, INT8OID, NUMERICOID, digest};
    int32 mods[13] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,LAPLACE_PG_NUMERIC_TYPMOD(20,0),-1};
    laplace_pg_composite_binding_open("physicality_entity_view", types, mods, 13, binding);
}

static void reflection_node_binding(laplace_pg_composite_binding* binding) {
    Oid digest = laplace_pg_composite_type_oid("record_id_256");
    Oid types[7] = {digest, INT8OID, laplace_pg_composite_type_oid("content_id_128"),
        digest, digest, laplace_pg_composite_type_oid("physicality_record"), BYTEAARRAYOID};
    int32 mods[7] = {-1,-1,-1,-1,-1,-1,-1};
    laplace_pg_composite_binding_open("physicality_entity_node", types, mods, 7, binding);
}

static int reflection_id_compare(const void* left, const void* right) {
    return laplace_identity_compare((const laplace_id128*)left, (const laplace_id128*)right);
}

static ArrayType* reflection_child_ids(const laplace_trajectory_carrier* carriers, size_t count) {
    laplace_id128* ids;
    Datum* values;
    size_t unique = 0u;
    uint64_t ordinal = 1u;
    if (count == 0u || count > INT_MAX || count > MaxAllocSize / sizeof(*ids)) reflection_limit("derived child index addressability");
    ids = palloc(count * sizeof(*ids));
    values = palloc(count * sizeof(*values));
    for (size_t index = 0u; index < count; ++index) {
        laplace_composition_occurrence occurrence;
        if (laplace_trajectory_composition_decode_one(&carriers[index], ordinal, &occurrence) != LAPLACE_TRAJECTORY_OK ||
            UINT64_MAX - ordinal < occurrence.run_length)
            ereport(ERROR, (errmsg("Laplace derived carrier index cannot be decoded")));
        ids[index] = occurrence.entity_id;
        ordinal += occurrence.run_length;
    }
    /* The shared native identity order is bytewise, independent of endianness. */
    qsort(ids, count, sizeof(*ids), reflection_id_compare);
    for (size_t index = 0u; index < count; ++index)
        if (unique == 0u || memcmp(ids[unique - 1u].bytes, ids[index].bytes, 16u) != 0) ids[unique++] = ids[index];
    for (size_t index = 0u; index < unique; ++index)
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(ids[index].bytes, 16u));
    return construct_array(values, (int)unique, BYTEAOID, -1, false, TYPALIGN_INT);
}

static int reflection_generated_compare(const void* left, const void* right) {
    return memcmp(((const laplace_pg_physicality_entity_view*)left)->physicality.physicality_id.bytes,
                  ((const laplace_pg_physicality_entity_view*)right)->physicality.physicality_id.bytes, 32u);
}

static int reflection_occurrence_compare(const void* left, const void* right);


/* Generated descriptor edges are deterministic derivation, separate from the
 * retained historical intervals. Read the actual composer operand selections;
 * carrier coalescing cannot turn a second selected physicality into the first. */
static void reflection_generated_occurrences(reflection_calculation* calculation, reflection_budget* budget) {
    laplace_physicality_occurrence_binding* rows;
    laplace_physicality_occurrence_parent* parents;
    size_t cursor = 0u, unique = 0u;
    uint64_t total_carriers = 0u, logical_count = 0u, maximum_carriers = 0u, native_bytes;
    uint64_t operand_capacity = calculation->plan_view.operand_count + calculation->reference_view.operand_count;
    if (operand_capacity < calculation->plan_view.operand_count) reflection_limit("generated reference operand count overflow");
    reflection_reserve(budget, operand_capacity, sizeof(*rows));
    reflection_reserve(budget, calculation->generated_count, sizeof(*parents));
    if (operand_capacity > MaxAllocSize / sizeof(*rows) ||
        calculation->generated_count > MaxAllocSize / sizeof(*parents))
        reflection_limit("generated occurrence binding addressability");
    rows = palloc0((size_t)operand_capacity * sizeof(*rows));
    parents = palloc0(calculation->generated_count * sizeof(*parents));
    for (size_t index = 0u; index < calculation->generated_count; ++index) {
        const laplace_pg_physicality_entity_view* view = &calculation->generated[index];
        parents[index].physicality = &view->physicality;
        parents[index].carriers = view->carriers;
        parents[index].carrier_count = view->carrier_count;
        if (UINT64_MAX - total_carriers < view->carrier_count ||
            UINT64_MAX - logical_count < view->physicality.logical_count)
            reflection_limit("generated occurrence validation work overflow");
        total_carriers += view->carrier_count;
        logical_count += view->physicality.logical_count;
        if (view->carrier_count > maximum_carriers) maximum_carriers = view->carrier_count;
    }
    for (size_t program = 0u; program < 2u; ++program) {
      const laplace_composition_request* requests = program == 0u ? calculation->plan_view.requests : calculation->reference_view.requests;
      const laplace_composition_operand* operands = program == 0u ? calculation->plan_view.operands : calculation->reference_view.operands;
      const laplace_composition_known_entity* known = program == 0u ? calculation->all_known : calculation->reference_view.known_entities;
      const laplace_composition_result* results = program == 0u ? calculation->results : calculation->reference_results;
      size_t result_count = program == 0u ? calculation->result_count : calculation->reference_result_count;
      uint64_t operand_count = program == 0u ? calculation->plan_view.operand_count : calculation->reference_view.operand_count;
      uint64_t known_count = program == 0u ? calculation->all_known_count : calculation->reference_view.known_entity_count;
      for (size_t result_index = 0u; result_index < result_count; ++result_index) {
        const laplace_composition_request* request = &requests[result_index];
        const laplace_composition_result* result = &results[result_index];
        laplace_pg_physicality_entity_view key = {0};
        const laplace_pg_physicality_entity_view* parent;
        uint64_t covered = 0u, remaining = 0u;
        size_t carrier_index = 0u, parent_first = cursor;
        laplace_composition_occurrence decoded = {0};
        if (result->collapsed != 0u) continue;
        key.physicality.physicality_id = result->physicality_id;
        parent = bsearch(&key, calculation->generated, calculation->generated_count,
            sizeof(*calculation->generated), reflection_generated_compare);
        if (parent == NULL || request->first_operand > operand_count ||
            request->operand_count > operand_count - request->first_operand)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated occurrence request has no exact parent")));
        for (uint64_t operand_index = 0u; operand_index < request->operand_count; ++operand_index) {
            const laplace_composition_operand* operand =
                &operands[request->first_operand + operand_index];
            laplace_physicality_occurrence_binding current = {0};
            current.parent_physicality_id = result->physicality_id;
            current.first_logical_ordinal = covered + 1u;
            current.logical_count = operand->multiplicity;
            current.version = LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION;
            if (operand->reference_kind == LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY &&
                operand->reference_index < known_count) {
                current.entity_id = known[operand->reference_index].entity_id;
                current.selected_physicality_id = known[operand->reference_index].physicality_id;
            } else if (operand->reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT &&
                operand->reference_index < result_index) {
                current.entity_id = results[operand->reference_index].entity_id;
                current.selected_physicality_id = results[operand->reference_index].physicality_id;
            } else ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated occurrence selection is not a native operand")));
            uint64_t unbound = operand->multiplicity;
            bool first = true;
            while (unbound != 0u) {
                if (remaining == 0u) {
                    if (carrier_index >= parent->carrier_count ||
                        laplace_trajectory_composition_decode_one(&parent->carriers[carrier_index++], covered + 1u, &decoded) != LAPLACE_TRAJECTORY_OK)
                        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated occurrence carrier cannot be decoded")));
                    remaining = decoded.run_length;
                }
                if (first) { current.metadata = decoded.metadata; first = false; }
                if (memcmp(current.entity_id.bytes, decoded.entity_id.bytes, 16u) != 0 || current.metadata != decoded.metadata)
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated occurrence differs from its selected operand")));
                uint64_t part = unbound < remaining ? unbound : remaining;
                if (UINT64_MAX - covered < part) reflection_limit("generated occurrence ordinal overflow");
                covered += part; unbound -= part; remaining -= part;
            }
            if (current.logical_count == 0u)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated occurrence is empty")));
            if (cursor > parent_first &&
                memcmp(rows[cursor - 1u].entity_id.bytes, current.entity_id.bytes, 16u) == 0 &&
                memcmp(rows[cursor - 1u].selected_physicality_id.bytes, current.selected_physicality_id.bytes, 32u) == 0 &&
                rows[cursor - 1u].metadata == current.metadata) {
                if (UINT64_MAX - rows[cursor - 1u].logical_count < current.logical_count)
                    reflection_limit("generated occurrence run overflow");
                rows[cursor - 1u].logical_count += current.logical_count;
            } else {
                if (cursor >= operand_capacity) reflection_limit("generated occurrence output count overflow");
                rows[cursor++] = current;
            }
        }
        if (covered != parent->physicality.logical_count || remaining != 0u || carrier_index != parent->carrier_count)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated occurrence plan does not cover its parent")));
    }
    }
    qsort(rows, cursor, sizeof(*rows), reflection_occurrence_compare);
    for (size_t index = 0u; index < cursor; ++index) {
        if (unique != 0u && reflection_occurrence_compare(&rows[unique - 1u], &rows[index]) == 0) {
            const laplace_physicality_occurrence_binding* prior = &rows[unique - 1u];
            const laplace_physicality_occurrence_binding* current = &rows[index];
            if (prior->logical_count != current->logical_count || prior->metadata != current->metadata ||
                memcmp(prior->entity_id.bytes, current->entity_id.bytes, 16u) != 0 ||
                memcmp(prior->selected_physicality_id.bytes, current->selected_physicality_id.bytes, 32u) != 0)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated physicality has conflicting occurrence selections")));
        } else rows[unique++] = rows[index];
    }
    reflection_logical(budget, logical_count);
    if (laplace_physicality_occurrence_bindings_memory_bound(maximum_carriers, &native_bytes) != LAPLACE_PHYSICALITY_OCCURRENCE_OK)
        reflection_limit("generated occurrence native memory bound overflow");
    reflection_reserve(budget, 1u, native_bytes);
    if (laplace_physicality_occurrence_bindings_validate_batch(parents, calculation->generated_count,
        rows, unique, calculation->generated_count, total_carriers, unique, logical_count,
        &calculation->generated_binding_receipt, NULL, 0u) != LAPLACE_PHYSICALITY_OCCURRENCE_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated occurrence bindings failed native complete validation")));
    budget->memory_used -= native_bytes;
    calculation->generated_occurrence_bindings = rows;
    calculation->generated_occurrence_binding_count = unique;
}

static void reflection_generated_prepare(reflection_calculation* calculation, reflection_budget* budget) {
    uint64_t count = calculation->summary.unique_physicality_count + calculation->reference_summary.unique_physicality_count;
    size_t cursor = 0u, unique = 0u;
    if (count < calculation->summary.unique_physicality_count || count == 0u || count > MaxAllocSize / sizeof(*calculation->generated))
        reflection_limit("generated physicality addressability");
    reflection_reserve(budget, count, sizeof(*calculation->generated));
    calculation->generated = palloc0((size_t)count * sizeof(*calculation->generated));
    for (size_t program = 0u; program < 2u; ++program) {
        const laplace_composition_working_set* working_set = program == 0u ? calculation->working_set : calculation->reference_working_set;
        uint64_t candidates = program == 0u ? calculation->summary.unique_physicality_count : calculation->reference_summary.unique_physicality_count;
        for (uint64_t index = 0u; index < candidates; ++index) {
            laplace_pg_physicality_entity_view* item = &calculation->generated[cursor++];
            if (laplace_composition_working_set_physicality_candidate_get(working_set, index, &item->physicality) != LAPLACE_COMPOSITION_OK ||
                laplace_composition_working_set_trajectory_candidate_view_get(working_set, index, &item->carriers, &item->carrier_count) != LAPLACE_COMPOSITION_OK)
                ereport(ERROR, (errmsg("Laplace generated descriptor or reference physicality is unavailable")));
        }
    }
    qsort(calculation->generated, cursor, sizeof(*calculation->generated), reflection_generated_compare);
    for (size_t index = 0u; index < cursor; ++index) {
        if (unique != 0u && reflection_generated_compare(&calculation->generated[unique - 1u], &calculation->generated[index]) == 0) {
            const laplace_pg_physicality_entity_view* prior = &calculation->generated[unique - 1u];
            const laplace_pg_physicality_entity_view* current = &calculation->generated[index];
            if (memcmp(&prior->physicality, &current->physicality, sizeof(prior->physicality)) != 0 ||
                prior->carrier_count != current->carrier_count ||
                memcmp(prior->carriers, current->carriers, prior->carrier_count * sizeof(*prior->carriers)) != 0)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace generated physicality identity has conflicting bodies")));
        } else calculation->generated[unique++] = calculation->generated[index];
    }
    calculation->generated_count = unique;
    reflection_generated_occurrences(calculation, budget);
}

static size_t reflection_result_count(const reflection_calculation* calculation) {
    if (SIZE_MAX - calculation->result_count < calculation->reference_result_count)
        reflection_limit("generated node count overflow");
    return calculation->result_count + calculation->reference_result_count;
}

static bool reflection_result_view(
    const reflection_calculation* calculation, size_t result_index,
    laplace_pg_physicality_entity_view* output) {
    const laplace_composition_result* result;
    laplace_pg_physicality_entity_view key = {0};
    const laplace_pg_physicality_entity_view* found;
    if (result_index >= reflection_result_count(calculation))
        ereport(ERROR, (errmsg("Laplace descriptor owner node index is out of range")));
    result = result_index < calculation->result_count ? &calculation->results[result_index] :
        &calculation->reference_results[result_index - calculation->result_count];
    if (result->collapsed != 0u) return false;
    key.physicality.physicality_id = result->physicality_id;
    found = bsearch(&key, calculation->generated, calculation->generated_count,
        sizeof(*calculation->generated), reflection_generated_compare);
    if (found == NULL || memcmp(found->physicality.entity_id.bytes, result->entity_id.bytes, 16u) != 0 ||
        found->physicality.physicality_type != LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION ||
        found->carrier_count == 0u || found->carrier_count != result->trajectory_vertex_count)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace descriptor node differs from native generated physicality")));
    *output = *found;
    output->identity_witness = result->identity_witness;
    output->external_known = calculation->canonical_external != NULL ? &calculation->retained_source : calculation->external_used;
    output->external_count = calculation->canonical_external != NULL ? 1u : (size_t)calculation->plan_view.external_entity_count;
    output->view_id = calculation->view_id;
    output->binding_set_id = calculation->binding_set_id;
    output->occurrence_bindings = calculation->occurrence_bindings;
    output->occurrence_binding_count = calculation->occurrence_binding_count;
    output->generated_occurrence_bindings = calculation->generated_occurrence_bindings;
    output->generated_occurrence_binding_count = calculation->generated_occurrence_binding_count;
    output->generated_binding_receipt = calculation->generated_binding_receipt;
    output->canonical_known = calculation->canonical_known;
    output->canonical_known_count = calculation->canonical_known_count;
    output->all_known = calculation->complete_known != NULL ? calculation->complete_known : calculation->all_known;
    output->all_known_count = calculation->complete_known != NULL ? calculation->complete_known_count : calculation->all_known_count;
    output->owner_recipe_fingerprint = calculation->plan_view.recipe_fingerprint;
    output->original_source = calculation->retained_source;
    {
        static const char domain[] = "laplace-postgresql-physicality-entity-node-read-v1";
        uint8_t index_bytes[8];
        blake3_hasher hash;
        for (size_t index = 0u; index < sizeof(index_bytes); ++index)
            index_bytes[index] = (uint8_t)((uint64_t)result_index >> (index * 8u));
        blake3_hasher_init(&hash);
        blake3_hasher_update(&hash, domain, sizeof(domain) - 1u);
        blake3_hasher_update(&hash, calculation->view_id.bytes, 32u);
        blake3_hasher_update(&hash, calculation->plan_view.physicality_record_id.bytes, 32u);
        blake3_hasher_update(&hash, calculation->summary.input_fingerprint.bytes, 32u);
        blake3_hasher_update(&hash, index_bytes, sizeof(index_bytes));
        blake3_hasher_update(&hash, output->physicality.physicality_id.bytes, 32u);
        blake3_hasher_finalize(&hash, output->derivation_receipt.bytes, 32u);
    }
    return true;
}

static ArrayType* reflection_known_array(const laplace_composition_known_entity* known, size_t count) {
    Oid types[10] = {BYTEAOID,BYTEAOID,BYTEAOID,FLOAT8OID,FLOAT8OID,FLOAT8OID,FLOAT8OID,INT8OID,INT2OID,BOOLOID};
    int32 mods[10] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
    laplace_pg_composite_binding binding;
    Datum* rows;
    ArrayType* output;
    if (count > MaxAllocSize / sizeof(*rows)) reflection_limit("retained external selector addressability");
    rows = palloc(count * sizeof(*rows));
    laplace_pg_composite_binding_open("composition_known_entity_record", types, mods, 10, &binding);
    for (size_t index = 0u; index < count; ++index) {
        const laplace_composition_known_entity* value = &known[index];
        Datum fields[10];
        bool nulls[10] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(value->entity_id.bytes, 16u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(value->identity_witness.bytes, 32u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(value->physicality_id.bytes, 32u));
        for (size_t axis = 0u; axis < 4u; ++axis) fields[axis + 3u] = Float8GetDatum(value->centroid.component[axis]);
        fields[7] = Int64GetDatum(value->atom);
        fields[8] = Int16GetDatum(value->tier_floor);
        fields[9] = BoolGetDatum(value->has_atom != 0u);
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    output = laplace_pg_composite_array(&binding, rows, count);
    laplace_pg_composite_binding_close(&binding);
    return output;
}

static void reflection_owners_publish(
    Datum original_context, reflection_calculation* calculations, size_t count,
    uint64_t maximum_requests, uint64_t maximum_operands, uint64_t maximum_carriers,
    uint64_t maximum_logical_count, uint64_t* node_counts, reflection_budget* budget) {
    static const char view_insert[] = "INSERT INTO " LAPLACE_PG_SCHEMA ".physicality_entity_view SELECT (i).*"
        " FROM unnest($1::" LAPLACE_PG_SCHEMA ".physicality_entity_view[]) i ON CONFLICT DO NOTHING";
    static const char view_verify[] = "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".physicality_entity_view[]) i JOIN " LAPLACE_PG_SCHEMA
        ".physicality_entity_view v ON v.view_id=i.view_id WHERE pg_catalog.record_send(v)=pg_catalog.record_send(i)";
    static const char node_insert[] = "INSERT INTO " LAPLACE_PG_SCHEMA ".physicality_entity_node SELECT (i).*"
        " FROM unnest($1::" LAPLACE_PG_SCHEMA ".physicality_entity_node[]) i ON CONFLICT DO NOTHING";
    static const char node_verify[] = "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".physicality_entity_node[]) i JOIN " LAPLACE_PG_SCHEMA
        ".physicality_entity_node n ON n.view_id=i.view_id AND n.result_index=i.result_index"
        " WHERE pg_catalog.record_send(n)=pg_catalog.record_send(i)";
    static const char node_complete[] = "SELECT count(*) FROM " LAPLACE_PG_SCHEMA
        ".physicality_entity_node n JOIN (SELECT DISTINCT i.view_id FROM unnest($1::"
        LAPLACE_PG_SCHEMA ".physicality_entity_node[]) i) v USING(view_id)";
    laplace_pg_composite_binding views, nodes, physicalities;
    Datum* view_rows;
    Datum* node_rows;
    Datum values[1];
    Oid types[1];
    size_t total = 0u, cursor = 0u;
    for (size_t index = 0u; index < count; ++index) {
        if (SIZE_MAX - total < reflection_result_count(&calculations[index])) reflection_limit("derived node count overflow");
        total += reflection_result_count(&calculations[index]);
    }
    reflection_reserve(budget, total, 1024u);
    reflection_reserve(budget, count, 1024u);
    if (total > MaxAllocSize / sizeof(*node_rows) || count > MaxAllocSize / sizeof(*view_rows)) reflection_limit("owner set addressability");
    view_rows = palloc(count * sizeof(*view_rows));
    node_rows = palloc(total * sizeof(*node_rows));
    reflection_view_binding(&views);
    reflection_node_binding(&nodes);
    laplace_pg_physicality_binding_open(&physicalities);
    for (size_t owner = 0u; owner < count; ++owner) {
        reflection_calculation* calculation = &calculations[owner];
        const laplace_composition_result* root = &calculation->results[calculation->plan_view.root_result_index];
        Datum fields[13];
        bool nulls[13] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(calculation->view_id.bytes, 32u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(calculation->plan_view.physicality_record_id.bytes, 32u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(root->entity_id.bytes, 16u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(root->identity_witness.bytes, 32u));
        fields[4] = original_context;
        fields[5] = PointerGetDatum(reflection_known_array(calculation->canonical_external != NULL ?
            &calculation->retained_source : calculation->external_used,
            calculation->canonical_external != NULL ? 1u : (size_t)calculation->plan_view.external_entity_count));
        fields[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(calculation->summary.input_fingerprint.bytes, 32u));
        fields[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(calculation->plan_view.recipe_fingerprint.bytes, 32u));
        fields[8] = Int64GetDatum((int64)maximum_requests);
        fields[9] = Int64GetDatum((int64)maximum_operands);
        fields[10] = Int64GetDatum((int64)maximum_carriers);
        fields[11] = laplace_pg_numeric_from_uint64(maximum_logical_count);
        fields[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(calculation->binding_set_id.bytes, 32u));
        view_rows[owner] = laplace_pg_composite_record(&views, fields, nulls);
        for (size_t index = 0u; index < reflection_result_count(calculation); ++index) {
            laplace_pg_physicality_entity_view generated;
            Datum node[7];
            bool absent[7] = {false};
            if (!reflection_result_view(calculation, index, &generated)) continue;
            reflection_reserve(budget, generated.carrier_count, 96u);
            node[0] = fields[0];
            node[1] = Int64GetDatum((int64)index);
            node[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(generated.physicality.entity_id.bytes, 16u));
            node[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(generated.identity_witness.bytes, 32u));
            node[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(generated.physicality.physicality_id.bytes, 32u));
            node[5] = laplace_pg_physicality_record(&physicalities, &generated.physicality);
            node[6] = PointerGetDatum(reflection_child_ids(generated.carriers, generated.carrier_count));
            node_rows[cursor++] = laplace_pg_composite_record(&nodes, node, absent);
            ++node_counts[owner];
        }
    }
    types[0] = views.array_oid;
    values[0] = PointerGetDatum(laplace_pg_composite_array(&views, view_rows, count));
    reflection_query(budget);
    if (SPI_execute_with_args(view_insert, 1, types, values, NULL, false, 0) != SPI_OK_INSERT)
        ereport(ERROR, (errmsg("Laplace descriptor view owner insertion failed")));
    Datum view_values[1] = {values[0]};
    Oid view_types[1] = {types[0]};
    types[0] = nodes.array_oid;
    values[0] = PointerGetDatum(laplace_pg_composite_array(&nodes, node_rows, cursor));
    reflection_query(budget);
    if (SPI_execute_with_args(node_insert, 1, types, values, NULL, false, 0) != SPI_OK_INSERT)
        ereport(ERROR, (errmsg("Laplace descriptor node owner insertion failed")));
    reflection_query(budget);
    if (SPI_execute_with_args(node_verify, 1, types, values, NULL, false, 1) != SPI_OK_SELECT ||
        laplace_pg_scalar_count("descriptor exact node owner") != cursor)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor node owner changed")));
    SPI_freetuptable(SPI_tuptable);
    reflection_query(budget);
    if (SPI_execute_with_args(view_verify, 1, view_types, view_values, NULL, false, 1) != SPI_OK_SELECT ||
        laplace_pg_scalar_count("descriptor exact view owner") != count)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor view owner changed")));
    SPI_freetuptable(SPI_tuptable);
    reflection_query(budget);
    if (SPI_execute_with_args(node_complete, 1, types, values, NULL, false, 1) != SPI_OK_SELECT ||
        laplace_pg_scalar_count("descriptor complete node ownership") != cursor)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor has unexpected node owners")));
    SPI_freetuptable(SPI_tuptable);
    laplace_pg_composite_binding_close(&physicalities);
    laplace_pg_composite_binding_close(&nodes);
    laplace_pg_composite_binding_close(&views);
}

/* Record actual framework executions independently of immutable view identity.
 * The same view can be admitted alone or in a mixed batch without duplicating
 * its canonical nodes; both real execution receipts remain independently named. */
static void reflection_depositions_publish(
    const reflection_calculation* calculations, size_t count,
    const laplace_digest256* receipt, const bytea* snapshot, reflection_budget* budget) {
    static const char insert_sql[] = "INSERT INTO " LAPLACE_PG_SCHEMA
        ".physicality_entity_deposition(view_id,deposit_receipt_id)"
        " SELECT id,$2 FROM unnest($1::bytea[]) i(id) ON CONFLICT DO NOTHING";
    static const char verify_sql[] = "SELECT count(*),"
        "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".physicality_entity_deposition WHERE deposit_receipt_id=$2),"
        "(SELECT pg_catalog.record_send(r)=$3 FROM " LAPLACE_PG_SCHEMA
        ".canonical_deposit_receipt r WHERE receipt_id=$2)"
        " FROM unnest($1::bytea[]) i(id) JOIN " LAPLACE_PG_SCHEMA
        ".physicality_entity_deposition d ON d.view_id=i.id AND d.deposit_receipt_id=$2";
    Oid types[3] = {BYTEAARRAYOID,BYTEAOID,BYTEAOID};
    Datum values[3];
    laplace_digest256* ids;
    reflection_reserve(budget, count, sizeof(*ids) + 96u);
    ids = palloc(count * sizeof(*ids));
    for (size_t index = 0u; index < count; ++index) ids[index] = calculations[index].view_id;
    values[0] = PointerGetDatum(reflection_digests(ids, count));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipt->bytes, 32u));
    values[2] = PointerGetDatum(snapshot);
    reflection_query(budget);
    if (SPI_execute_with_args(insert_sql, 2, types, values, NULL, false, 0) != SPI_OK_INSERT)
        ereport(ERROR, (errmsg("Laplace descriptor deposition association insertion failed")));
    reflection_query(budget);
    if (SPI_execute_with_args(verify_sql, 3, types, values, NULL, false, 1) != SPI_OK_SELECT || SPI_processed != 1u ||
        reflection_positive_bigint(reflection_required(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1), true) != count ||
        reflection_positive_bigint(reflection_required(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2), true) != count ||
        !DatumGetBool(reflection_required(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3)))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor deposition receipt or complete source association changed")));
    SPI_freetuptable(SPI_tuptable);
}

typedef struct reflection_cleanup {
    MemoryContextCallback callback;
    reflection_calculation* calculations;
    size_t count;
} reflection_cleanup;

static void reflection_cleanup_all(void* opaque) {
    reflection_cleanup* cleanup = opaque;
    for (size_t index = 0u; index < cleanup->count; ++index)
        reflection_destroy(&cleanup->calculations[index]);
}

static reflection_calculation* reflection_calculations(size_t count, reflection_budget* budget) {
    reflection_cleanup* cleanup;
    reflection_reserve(budget, count, sizeof(reflection_calculation));
    reflection_reserve(budget, 1u, sizeof(*cleanup));
    if (count == 0u || count > MaxAllocSize / sizeof(reflection_calculation))
        reflection_limit("calculation set addressability");
    cleanup = palloc0(sizeof(*cleanup));
    cleanup->calculations = palloc0(count * sizeof(*cleanup->calculations));
    cleanup->count = count;
    cleanup->callback.func = reflection_cleanup_all;
    cleanup->callback.arg = cleanup;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &cleanup->callback);
    return cleanup->calculations;
}

static void reflection_sources_recheck(
    const reflection_source* expected, size_t count, reflection_budget* budget) {
    laplace_digest256* ids;
    reflection_source* observed;
    reflection_reserve(budget, count, sizeof(*ids));
    ids = palloc(count * sizeof(*ids));
    for (size_t index = 0u; index < count; ++index) ids[index] = expected[index].physicality.physicality_id;
    observed = reflection_sources(ids, count, budget);
    for (size_t index = 0u; index < count; ++index) {
        laplace_digest256 observed_identity;
        if (laplace_persistence_physicality_identify(&observed[index].physicality, &observed_identity) != LAPLACE_PERSISTENCE_OK ||
            memcmp(observed_identity.bytes, observed[index].physicality.physicality_id.bytes, 32u) != 0 ||
            memcmp(expected[index].physicality.physicality_id.bytes, observed[index].physicality.physicality_id.bytes, 32u) != 0 ||
            memcmp(expected[index].witness.bytes, observed[index].witness.bytes, 32u) != 0 ||
            expected[index].carrier_count != observed[index].carrier_count ||
            (expected[index].carrier_count != 0u && memcmp(expected[index].carriers, observed[index].carriers,
                expected[index].carrier_count * sizeof(laplace_trajectory_carrier)) != 0))
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace descriptor immutable source changed during admission")));
    }
}

static int reflection_digest_compare(const void* left, const void* right) {
    return memcmp(((const laplace_digest256*)left)->bytes, ((const laplace_digest256*)right)->bytes, 32u);
}

static laplace_digest256* reflection_read_ids(ArrayType* array, size_t* count, reflection_budget* budget) {
    Datum* values;
    bool* nulls;
    int items;
    laplace_digest256* ids;
    laplace_digest256* sorted;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != BYTEAOID || ARR_LBOUND(array)[0] != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Laplace descriptor requires a nonempty one-dimensional record ID array")));
    items = ArrayGetNItems(ARR_NDIM(array), ARR_DIMS(array));
    reflection_reserve(budget, (uint64_t)items, 2u * sizeof(*ids) + sizeof(Datum) + 64u);
    if (items <= 0 || (size_t)items > MaxAllocSize / sizeof(*ids)) reflection_limit("record ID array addressability");
    deconstruct_array(array, BYTEAOID, -1, false, TYPALIGN_INT, &values, &nulls, &items);
    ids = palloc((size_t)items * sizeof(*ids));
    sorted = palloc((size_t)items * sizeof(*sorted));
    for (int index = 0; index < items; ++index) {
        if (nulls[index]) ereport(ERROR, (errmsg("Laplace descriptor record ID cannot be null")));
        reflection_bytes(values[index], &ids[index], 32u);
    }
    memcpy(sorted, ids, (size_t)items * sizeof(*ids));
    qsort(sorted, (size_t)items, sizeof(*sorted), reflection_digest_compare);
    for (int index = 1; index < items; ++index)
        if (reflection_digest_compare(&sorted[index - 1], &sorted[index]) == 0)
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Laplace descriptor batch repeats a record ID")));
    *count = (size_t)items;
    return ids;
}

static laplace_composition_known_entity* reflection_read_known(
    ArrayType* array, uint64_t* count, reflection_budget* budget) {
    int items = ArrayGetNItems(ARR_NDIM(array), ARR_DIMS(array));
    if (items <= 0) reflection_limit("external selector frontier is empty");
    reflection_reserve(budget, (uint64_t)items,
        sizeof(laplace_composition_known_entity) + sizeof(Datum) + sizeof(bool) + 128u);
    laplace_composition_known_entity* known = laplace_pg_composition_read_known_entities(array, count);
    if (*count != (uint64_t)items)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor selector count changed during decode")));
    return known;
}


typedef struct reflection_bindings {
    laplace_digest256 id;
    laplace_physicality_occurrence_binding* rows;
    size_t count;
    size_t parent_count;
    uint64_t logical_count;
    laplace_composition_known_entity* exact_sources;
    size_t exact_source_count;
    laplace_digest256* parent_ids;
    laplace_digest256* parent_receipts;
    uint64_t* retained_parent_binding_counts;
    uint64_t* retained_parent_logical_counts;
} reflection_bindings;

static int reflection_known_physicality_compare(const void* left, const void* right) {
    return memcmp(((const laplace_composition_known_entity*)left)->physicality_id.bytes,
        ((const laplace_composition_known_entity*)right)->physicality_id.bytes, 32u);
}

/* Retain one source frontier for the complete interval set. Unrelated input
 * atoms do not alter an empty set or duplicate its retained metadata. */
static void reflection_occurrences_select_sources(
    reflection_bindings* bindings, const laplace_composition_known_entity* external,
    size_t external_count, bool already_sorted, reflection_budget* budget) {
    const laplace_composition_known_entity* sorted;
    laplace_digest256* ids;
    size_t count = 0u;
    if (bindings->count > SIZE_MAX / 2u) reflection_limit("occurrence source identity count overflow");
    reflection_reserve(budget, 2u * bindings->count, sizeof(*ids) + sizeof(*bindings->exact_sources));
    if (2u * bindings->count > MaxAllocSize / sizeof(*bindings->exact_sources)) reflection_limit("occurrence source frontier addressability");
    ids = palloc(2u * bindings->count * sizeof(*ids));
    sorted = external;
    if (!already_sorted) {
        laplace_composition_known_entity* copy;
        reflection_reserve(budget, external_count, sizeof(*sorted));
        copy = palloc(external_count * sizeof(*copy));
        memcpy(copy, external, external_count * sizeof(*copy));
        qsort(copy, external_count, sizeof(*copy), reflection_known_physicality_compare);
        sorted = copy;
    }
    for (size_t index = 0u; index < bindings->count; ++index) {
        ids[2u * index] = bindings->rows[index].parent_physicality_id;
        ids[2u * index + 1u] = bindings->rows[index].selected_physicality_id;
    }
    qsort(ids, 2u * bindings->count, sizeof(*ids), reflection_digest_compare);
    bindings->exact_sources = palloc(2u * bindings->count * sizeof(*bindings->exact_sources));
    for (size_t index = 0u; index < 2u * bindings->count; ++index) {
        laplace_composition_known_entity key = {0};
        const laplace_composition_known_entity* found;
        if (index != 0u && reflection_digest_compare(&ids[index - 1u], &ids[index]) == 0) continue;
        key.physicality_id = ids[index];
        found = bsearch(&key, sorted, external_count, sizeof(*sorted), reflection_known_physicality_compare);
        if (found == NULL)
            ereport(ERROR, (errcode(ERRCODE_NO_DATA_FOUND), errmsg("Laplace occurrence set lacks an exact selected source witness")));
        bindings->exact_sources[count++] = *found;
    }
    bindings->exact_source_count = count;
}

static int reflection_occurrence_compare(const void* left, const void* right) {
    const laplace_physicality_occurrence_binding* a = left;
    const laplace_physicality_occurrence_binding* b = right;
    int order = memcmp(a->parent_physicality_id.bytes, b->parent_physicality_id.bytes, 32u);
    return order != 0 ? order : (a->first_logical_ordinal < b->first_logical_ordinal ? -1 :
        a->first_logical_ordinal != b->first_logical_ordinal);
}

static int reflection_source_pointer_compare(const void* left, const void* right) {
    const reflection_source* const* a = left;
    const reflection_source* const* b = right;
    return memcmp((*a)->physicality.physicality_id.bytes, (*b)->physicality.physicality_id.bytes, 32u);
}

static int reflection_source_entity_compare(const void* left, const void* right) {
    const reflection_source* const* a = left;
    const reflection_source* const* b = right;
    return memcmp((*a)->physicality.entity_id.bytes, (*b)->physicality.entity_id.bytes, 16u);
}

static const reflection_source** reflection_source_union(
    const reflection_source* sources, size_t source_count,
    const reflection_source* selected, size_t selected_count,
    size_t* count, reflection_budget* budget) {
    const reflection_source** result;
    size_t total, unique = 0u;
    if (source_count > SIZE_MAX - selected_count) reflection_limit("occurrence source count overflow");
    total = source_count + selected_count;
    reflection_reserve(budget, total, sizeof(*result));
    if (total > MaxAllocSize / sizeof(*result)) reflection_limit("occurrence source addressability");
    result = palloc(total * sizeof(*result));
    for (size_t index = 0u; index < source_count; ++index) result[index] = &sources[index];
    for (size_t index = 0u; index < selected_count; ++index) result[source_count + index] = &selected[index];
    qsort(result, total, sizeof(*result), reflection_source_pointer_compare);
    for (size_t index = 0u; index < total; ++index)
        if (unique == 0u || reflection_source_pointer_compare(&result[unique - 1u], &result[index]) != 0)
            result[unique++] = result[index];
    *count = unique;
    return result;
}

static void reflection_occurrence_binding_open(laplace_pg_composite_binding* binding, bool stored) {
    Oid digest = laplace_pg_composite_type_oid("record_id_256");
    Oid content = laplace_pg_composite_type_oid("content_id_128");
    Oid stored_types[8] = {digest,digest,NUMERICOID,content,digest,NUMERICOID,NUMERICOID,INT4OID};
    int32 stored_mods[8] = {-1,-1,LAPLACE_PG_NUMERIC_TYPMOD(20,0),-1,-1,
        LAPLACE_PG_NUMERIC_TYPMOD(20,0),LAPLACE_PG_NUMERIC_TYPMOD(20,0),-1};
    Oid input_types[7] = {BYTEAOID,BYTEAOID,BYTEAOID,NUMERICOID,NUMERICOID,NUMERICOID,INT4OID};
    int32 input_mods[7] = {-1,-1,-1,LAPLACE_PG_NUMERIC_TYPMOD(20,0),
        LAPLACE_PG_NUMERIC_TYPMOD(20,0),LAPLACE_PG_NUMERIC_TYPMOD(20,0),-1};
    laplace_pg_composite_binding_open(stored ? "physicality_occurrence_binding" :
        "physicality_occurrence_selection", stored ? stored_types : input_types,
        stored ? stored_mods : input_mods, stored ? 8 : 7, binding);
}

static reflection_bindings reflection_occurrences_read(ArrayType* array, reflection_budget* budget) {
    reflection_bindings result = {0};
    laplace_pg_composite_binding binding;
    Datum* values;
    bool* nulls;
    int items;
    int16 length;
    bool by_value;
    char alignment;
    reflection_occurrence_binding_open(&binding, false);
    if (ARR_ELEMTYPE(array) != binding.type_oid ||
        (ARR_NDIM(array) != 0 && (ARR_NDIM(array) != 1 || ARR_LBOUND(array)[0] != 1)))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace occurrence selection requires an exact one-dimensional record array")));
    items = ArrayGetNItems(ARR_NDIM(array), ARR_DIMS(array));
    reflection_reserve(budget, (uint64_t)items, sizeof(*result.rows) + sizeof(Datum) + sizeof(bool) + 128u);
    if ((uint64_t)items > budget->logical_limit || (size_t)items > MaxAllocSize / sizeof(*result.rows))
        reflection_limit("occurrence selection addressability");
    if (items == 0) {
        laplace_pg_composite_binding_close(&binding);
        return result;
    }
    get_typlenbyvalalign(binding.type_oid, &length, &by_value, &alignment);
    deconstruct_array(array, binding.type_oid, length, by_value, alignment, &values, &nulls, &items);
    result.rows = palloc0((size_t)items * sizeof(*result.rows));
    result.count = (size_t)items;
    for (size_t index = 0u; index < result.count; ++index) {
        HeapTupleHeader row;
        laplace_physicality_occurrence_binding* out = &result.rows[index];
        if (nulls[index]) ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
            errmsg("Laplace occurrence selection cannot contain a null row")));
        row = DatumGetHeapTupleHeader(values[index]);
        reflection_bytes(laplace_pg_required_composite_attribute(row, 1, "occurrence parent"),
            &out->parent_physicality_id, 32u);
        reflection_bytes(laplace_pg_required_composite_attribute(row, 2, "occurrence entity"),
            &out->entity_id, 16u);
        reflection_bytes(laplace_pg_required_composite_attribute(row, 3, "occurrence selected physicality"),
            &out->selected_physicality_id, 32u);
        out->first_logical_ordinal = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(row, 4, "occurrence first ordinal"), "occurrence first ordinal");
        out->logical_count = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(row, 5, "occurrence logical count"), "occurrence logical count");
        out->metadata = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(row, 6, "occurrence metadata"), "occurrence metadata");
        int32 version = DatumGetInt32(laplace_pg_required_composite_attribute(row, 7, "occurrence version"));
        if (version != LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION)
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Laplace occurrence selection version is unsupported")));
        out->version = (uint32_t)version;
    }
    qsort(result.rows, result.count, sizeof(*result.rows), reflection_occurrence_compare);
    laplace_pg_composite_binding_close(&binding);
    return result;
}

/* An omitted selection is derived only when the admitted source frontier names
 * exactly one physicality for each child. No lexical/minimum/first choice may
 * resolve a genuinely ambiguous occurrence. */
static void reflection_occurrences_derive(
    reflection_bindings* result, const reflection_source* const* sources, size_t count,
    reflection_budget* budget) {
    size_t total = 0u, cursor = 0u;
    const reflection_source** by_entity;
    reflection_reserve(budget, count, sizeof(*by_entity));
    by_entity = palloc(count * sizeof(*by_entity));
    memcpy(by_entity, sources, count * sizeof(*by_entity));
    qsort(by_entity, count, sizeof(*by_entity), reflection_source_entity_compare);
    for (size_t index = 0u; index < count; ++index) {
        if (SIZE_MAX - total < sources[index]->carrier_count) reflection_limit("occurrence carrier frontier overflow");
        total += sources[index]->carrier_count;
    }
    reflection_reserve(budget, total, sizeof(*result->rows));
    if (total > MaxAllocSize / sizeof(*result->rows)) reflection_limit("derived occurrence frontier addressability");
    result->rows = palloc0(total * sizeof(*result->rows));
    result->count = total;
    for (size_t parent = 0u; parent < count; ++parent) {
        uint64_t ordinal = 1u;
        const reflection_source* source = sources[parent];
        for (size_t index = 0u; index < source->carrier_count; ++index) {
            laplace_composition_occurrence occurrence;
            reflection_source key = {0};
            const reflection_source* key_pointer = &key;
            const reflection_source* const* found;
            const reflection_source* chosen;
            if (laplace_trajectory_composition_decode_one(&source->carriers[index], ordinal, &occurrence) != LAPLACE_TRAJECTORY_OK)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence source cannot be decoded")));
            key.physicality.entity_id = occurrence.entity_id;
            found = bsearch(&key_pointer, by_entity, count, sizeof(*by_entity), reflection_source_entity_compare);
            if (found == NULL)
                ereport(ERROR, (errcode(ERRCODE_NO_DATA_FOUND),
                    errmsg("Laplace occurrence selection is incomplete for an admitted child")));
            if ((found != by_entity && reflection_source_entity_compare(found - 1, found) == 0) ||
                (found + 1 != by_entity + count && reflection_source_entity_compare(found, found + 1) == 0))
                ereport(ERROR, (errcode(ERRCODE_AMBIGUOUS_PARAMETER),
                    errmsg("Laplace requires explicit intervals for a child with multiple selected physicalities")));
            chosen = *found;
            laplace_physicality_occurrence_binding* out = &result->rows[cursor++];
            out->parent_physicality_id = source->physicality.physicality_id;
            out->entity_id = occurrence.entity_id;
            out->selected_physicality_id = chosen->physicality.physicality_id;
            out->first_logical_ordinal = ordinal;
            out->logical_count = occurrence.run_length;
            out->metadata = occurrence.metadata;
            out->version = LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION;
            if (UINT64_MAX - ordinal < occurrence.run_length && index + 1u != source->carrier_count)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence ordinal overflow")));
            ordinal += occurrence.run_length;
        }
    }
}

/* Every source is already authenticated by the shared physicality body owner.
 * This pass binds complete parent intervals and checks every selected child,
 * including the geometry epoch, before the native receipt is accepted. */
static void reflection_occurrences_validate(
    reflection_bindings* bindings, const reflection_source* const* sources, size_t count,
    reflection_budget* budget) {
    laplace_physicality_occurrence_parent* parents;
    size_t parent_count = 0u;
    uint64_t carrier_count = 0u, maximum_parent_carriers = 0u, logical_count = 0u, native_bytes;
    reflection_reserve(budget, count, sizeof(*parents));
    if (count > MaxAllocSize / sizeof(*parents)) reflection_limit("occurrence parent addressability");
    parents = palloc0(count * sizeof(*parents));
    for (size_t index = 0u; index < count; ++index) {
        const reflection_source* source = sources[index];
        if (source->physicality.physicality_type != LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION) continue;
        if (UINT64_MAX - carrier_count < source->carrier_count ||
            UINT64_MAX - logical_count < source->physicality.logical_count)
            reflection_limit("occurrence parent work overflow");
        carrier_count += source->carrier_count;
        logical_count += source->physicality.logical_count;
        if (source->carrier_count > maximum_parent_carriers) maximum_parent_carriers = source->carrier_count;
        parents[parent_count].physicality = &source->physicality;
        parents[parent_count].carriers = source->carriers;
        parents[parent_count++].carrier_count = source->carrier_count;
    }
    for (size_t index = 0u; index < bindings->count; ++index) {
        const laplace_physicality_occurrence_binding* row = &bindings->rows[index];
        reflection_source parent_key = {0}, selected_key = {0};
        const reflection_source* parent_pointer = &parent_key;
        const reflection_source* selected_pointer = &selected_key;
        const reflection_source* const* parent;
        const reflection_source* const* selected;
        parent_key.physicality.physicality_id = row->parent_physicality_id;
        selected_key.physicality.physicality_id = row->selected_physicality_id;
        parent = bsearch(&parent_pointer, sources, count, sizeof(*sources), reflection_source_pointer_compare);
        selected = bsearch(&selected_pointer, sources, count, sizeof(*sources), reflection_source_pointer_compare);
        if (parent == NULL || selected == NULL ||
            memcmp((*selected)->physicality.entity_id.bytes, row->entity_id.bytes, 16u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace occurrence selection differs from its exact admitted child")));
    }
    reflection_logical(budget, logical_count);
    reflection_reserve(budget, parent_count, sizeof(*bindings->parent_ids) + sizeof(*bindings->parent_receipts));
    bindings->parent_ids = palloc(parent_count * sizeof(*bindings->parent_ids));
    bindings->parent_receipts = palloc(parent_count * sizeof(*bindings->parent_receipts));
    for (size_t index = 0u; index < parent_count; ++index)
        bindings->parent_ids[index] = parents[index].physicality->physicality_id;
    if (laplace_physicality_occurrence_bindings_memory_bound(maximum_parent_carriers, &native_bytes) !=
        LAPLACE_PHYSICALITY_OCCURRENCE_OK) reflection_limit("native occurrence validation memory overflow");
    reflection_reserve(budget, 1u, native_bytes);
    laplace_physicality_occurrence_status status = laplace_physicality_occurrence_bindings_validate_batch(
        parents, parent_count, bindings->rows, bindings->count, parent_count, carrier_count,
        bindings->count, logical_count, &bindings->id, bindings->parent_receipts, parent_count);
    budget->memory_used -= native_bytes;
    if (status != LAPLACE_PHYSICALITY_OCCURRENCE_OK)
        ereport(ERROR, (errcode(status == LAPLACE_PHYSICALITY_OCCURRENCE_LIMIT ?
            ERRCODE_PROGRAM_LIMIT_EXCEEDED : ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace occurrence intervals failed complete native validation"), errdetail("status=%u", (unsigned)status)));
    bindings->parent_count = parent_count;
    bindings->logical_count = logical_count;
}


typedef struct reflection_scope_index {
    const reflection_bindings* complete;
    size_t* first;
    size_t* count;
    size_t* queue;
    uint64_t* visited;
    uint64_t generation;
} reflection_scope_index;

static reflection_scope_index reflection_occurrence_scope_index(
    const reflection_bindings* complete, reflection_budget* budget) {
    reflection_scope_index index = {0};
    size_t cursor = 0u;
    index.complete = complete;
    reflection_reserve(budget, complete->parent_count, 3u * sizeof(size_t) + sizeof(uint64_t));
    if (complete->parent_count > MaxAllocSize / sizeof(size_t)) reflection_limit("occurrence scope index addressability");
    index.first = palloc(complete->parent_count * sizeof(*index.first));
    index.count = palloc(complete->parent_count * sizeof(*index.count));
    index.queue = palloc(complete->parent_count * sizeof(*index.queue));
    index.visited = palloc0(complete->parent_count * sizeof(*index.visited));
    for (size_t parent = 0u; parent < complete->parent_count; ++parent) {
        index.first[parent] = cursor;
        while (cursor < complete->count && memcmp(complete->rows[cursor].parent_physicality_id.bytes,
            complete->parent_ids[parent].bytes, 32u) == 0) ++cursor;
        index.count[parent] = cursor - index.first[parent];
        if (index.count[parent] == 0u)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace complete occurrence parent has no intervals")));
    }
    if (cursor != complete->count)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace complete occurrence index omitted a parent")));
    return index;
}

static int reflection_size_compare(const void* left, const void* right) {
    size_t a = *(const size_t*)left, b = *(const size_t*)right;
    return a < b ? -1 : a != b;
}

/* One sparse reachable historical scope per source. The shared index is built
 * once for the whole admission; generation marks avoid clearing a batch-sized
 * bitmap or scanning unrelated parents for every descriptor. */
static reflection_bindings* reflection_occurrence_scope(
    reflection_scope_index* index, const reflection_source* source,
    const reflection_source* const* all_sources, size_t all_source_count,
    reflection_budget* budget) {
    const reflection_bindings* complete = index->complete;
    reflection_bindings* scope;
    size_t queued = 0u, cursor = 0u, output_count = 0u, output_cursor = 0u;
    const laplace_digest256* root;
    const reflection_source** selected;
    reflection_reserve(budget, 1u, sizeof(*scope));
    scope = palloc0(sizeof(*scope));
    if (++index->generation == 0u) reflection_limit("occurrence scope generation overflow");
    root = bsearch(&source->physicality.physicality_id, complete->parent_ids, complete->parent_count,
        sizeof(*complete->parent_ids), reflection_digest_compare);
    if (root != NULL) {
        size_t parent = (size_t)(root - complete->parent_ids);
        index->visited[parent] = index->generation;
        index->queue[queued++] = parent;
    } else if (source->physicality.physicality_type == LAPLACE_PERSISTENCE_PHYSICALITY_COMPOSITION)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace source has no complete occurrence parent")));
    while (cursor < queued) {
        size_t parent = index->queue[cursor++];
        if (SIZE_MAX - output_count < index->count[parent]) reflection_limit("occurrence scope count overflow");
        output_count += index->count[parent];
        for (size_t row = index->first[parent]; row < index->first[parent] + index->count[parent]; ++row) {
            const laplace_digest256* child = bsearch(&complete->rows[row].selected_physicality_id,
                complete->parent_ids, complete->parent_count, sizeof(*complete->parent_ids), reflection_digest_compare);
            if (child != NULL) {
                size_t child_index = (size_t)(child - complete->parent_ids);
                if (index->visited[child_index] != index->generation) {
                    if (queued >= complete->parent_count) reflection_limit("occurrence scope frontier overflow");
                    index->visited[child_index] = index->generation;
                    index->queue[queued++] = child_index;
                }
            }
        }
    }
    qsort(index->queue, queued, sizeof(*index->queue), reflection_size_compare);
    reflection_reserve(budget, output_count, sizeof(*scope->rows));
    if (output_count > MaxAllocSize / sizeof(*scope->rows)) reflection_limit("occurrence scope output addressability");
    scope->rows = palloc(output_count * sizeof(*scope->rows));
    scope->count = output_count;
    for (size_t parent = 0u; parent < queued; ++parent) {
        size_t source_index = index->queue[parent];
        memcpy(&scope->rows[output_cursor], &complete->rows[index->first[source_index]],
            index->count[source_index] * sizeof(*scope->rows));
        output_cursor += index->count[source_index];
    }
    reflection_occurrences_select_sources(scope, complete->exact_sources, complete->exact_source_count, true, budget);
    reflection_reserve(budget, scope->exact_source_count, sizeof(*selected));
    selected = palloc(scope->exact_source_count * sizeof(*selected));
    for (size_t item = 0u; item < scope->exact_source_count; ++item) {
        reflection_source key = {0}; const reflection_source* pointer = &key;
        const reflection_source* const* found;
        key.physicality.physicality_id = scope->exact_sources[item].physicality_id;
        found = bsearch(&pointer, all_sources, all_source_count, sizeof(*all_sources), reflection_source_pointer_compare);
        if (found == NULL) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace scope source was not authenticated")));
        selected[item] = *found;
    }
    reflection_occurrences_validate(scope, selected, scope->exact_source_count, budget);
    return scope;
}

/* Pinned atom bodies are fetched once for the complete admitted batch. The
 * mapped provider supplies the tuple; the shared native planner verifies its
 * full witness, body identity and epoch before it owns any reference geometry. */
static reflection_source* reflection_atom_bodies(
    const laplace_composition_known_entity* atoms, size_t count, reflection_budget* budget) {
    laplace_digest256* ids;
    reflection_reserve(budget, count, sizeof(*ids));
    if (count > MaxAllocSize / sizeof(*ids)) reflection_limit("pinned atom body frontier addressability");
    ids = palloc(count * sizeof(*ids));
    for (size_t index = 0u; index < count; ++index) ids[index] = atoms[index].physicality_id;
    return reflection_sources(ids, count, budget);
}

static void reflection_reference_prepare(
    const laplace_framework_context* semantic_context, const laplace_execution_grant* grant,
    const reflection_source* source, const reflection_source* const* sources, size_t source_count,
    const laplace_composition_known_entity* external, size_t external_count,
    const uint32_t* positions, const laplace_composition_known_entity* atoms,
    const reflection_source* atom_bodies, size_t atom_count, uint64_t preferred_bytes,
    reflection_calculation* calculation, reflection_budget* budget) {
    const reflection_bindings* scope = calculation->binding_scope;
    laplace_composition_known_entity key = {0};
    const laplace_composition_known_entity* root;
    laplace_composition_known_entity* selected;
    laplace_composition_known_entity* canonical;
    laplace_content_reference_source* views;
    laplace_content_reference_atom* pinned;
    laplace_id128* roots;
    size_t selected_count, root_count = 0u, pinned_count = 0u;
    uint64_t carriers = 0u, logical = 0u, bytes, nodes;
    laplace_framework_context execution = *semantic_context;
    laplace_content_reference_input input = {0};
    laplace_content_reference_status status;
    if (scope == NULL || scope->exact_source_count == SIZE_MAX)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace reference view has no authenticated source scope")));
    key.entity_id = source->physicality.entity_id;
    key.physicality_id = source->physicality.physicality_id;
    root = bsearch(&key, external, external_count, sizeof(*external), reflection_known_selection_compare);
    if (root == NULL)
        ereport(ERROR, (errcode(ERRCODE_NO_DATA_FOUND), errmsg("Laplace reference view requires its exact original source tuple")));
    calculation->retained_source = *root;
    selected_count = scope->exact_source_count + 1u;
    reflection_reserve(budget, selected_count, sizeof(*selected) + sizeof(*views) + sizeof(*pinned));
    if (selected_count > MaxAllocSize / sizeof(*selected) || selected_count > MaxAllocSize / sizeof(*views) ||
        selected_count > MaxAllocSize / sizeof(*pinned)) reflection_limit("canonical source frontier addressability");
    selected = palloc(selected_count * sizeof(*selected));
    selected[0] = *root;
    if (scope->exact_source_count != 0u)
        memcpy(selected + 1u, scope->exact_sources, scope->exact_source_count * sizeof(*selected));
    selected_count = reflection_known_sort(selected, selected_count);
    views = palloc0(selected_count * sizeof(*views));
    pinned = palloc0(selected_count * sizeof(*pinned));
    if (SIZE_MAX - selected_count < calculation->plan_view.external_entity_count)
        reflection_limit("canonical reference root count overflow");
    size_t root_capacity = selected_count + (size_t)calculation->plan_view.external_entity_count;
    reflection_reserve(budget, root_capacity, sizeof(*roots) + sizeof(*canonical));
    if (root_capacity > MaxAllocSize / sizeof(*canonical)) reflection_limit("canonical reference roots addressability");
    roots = palloc(root_capacity * sizeof(*roots));
    for (size_t index = 0u; index < selected_count; ++index) {
        reflection_source body_key = {0}; const reflection_source* pointer = &body_key;
        const reflection_source* const* found;
        body_key.physicality.physicality_id = selected[index].physicality_id;
        found = bsearch(&pointer, sources, source_count, sizeof(*sources), reflection_source_pointer_compare);
        if (found == NULL) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace canonical source body is absent")));
        views[index].physicality = &(*found)->physicality;
        views[index].carriers = (*found)->carriers;
        views[index].carrier_count = (*found)->carrier_count;
        views[index].identity_witness = (*found)->witness;
        if (UINT64_MAX - carriers < views[index].carrier_count ||
            UINT64_MAX - logical < views[index].physicality->logical_count)
            reflection_limit("canonical reference work overflow");
        carriers += views[index].carrier_count;
        logical += views[index].physicality->logical_count;
        roots[root_count++] = selected[index].entity_id;
        if (selected[index].has_atom == 1u) {
            const uint32_t* position = bsearch(&selected[index].atom, positions, atom_count,
                sizeof(*positions), reflection_position_compare);
            if (position == NULL) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace canonical atom frontier is incomplete")));
            bool repeated = index != 0u && reflection_known_compare(&selected[index - 1u], &selected[index]) == 0;
            if (!repeated) {
                size_t atom_index = (size_t)(position - positions);
                pinned[pinned_count].known = atoms[atom_index];
                pinned[pinned_count++].physicality = &atom_bodies[atom_index].physicality;
            }
        }
    }
    memcpy(roots + root_count, calculation->plan_view.external_entity_ids,
        (size_t)calculation->plan_view.external_entity_count * sizeof(*roots));
    root_count += (size_t)calculation->plan_view.external_entity_count;
    qsort(roots, root_count, sizeof(*roots), reflection_id_compare);
    size_t unique = 0u;
    for (size_t index = 0u; index < root_count; ++index)
        if (unique == 0u || reflection_id_compare(&roots[unique - 1u], &roots[index]) != 0) roots[unique++] = roots[index];
    root_count = unique;
    if (UINT64_MAX - logical < pinned_count || UINT64_MAX - carriers < root_count ||
        UINT64_MAX - carriers - root_count < selected_count ||
        UINT64_MAX - carriers - root_count - selected_count < pinned_count)
        reflection_limit("canonical reference finite envelope overflow");
    logical += pinned_count;
    nodes = carriers + root_count + selected_count + pinned_count;
    execution.resource_grant = *grant;
    if (execution.resource_grant.memory_bytes > budget->memory_limit - budget->memory_used)
        execution.resource_grant.memory_bytes = budget->memory_limit - budget->memory_used;
    input.context = &execution;
    input.roots = roots; input.root_count = root_count;
    input.sources = views; input.source_count = selected_count;
    input.atoms = pinned; input.atom_count = pinned_count;
    input.limits.maximum_roots = root_count;
    input.limits.maximum_nodes = nodes;
    input.limits.maximum_physicalities = selected_count + pinned_count;
    input.limits.maximum_carriers = carriers;
    input.limits.maximum_logical_count = logical;
    input.limits.maximum_depth = nodes;
    input.limits.maximum_memory_bytes = execution.resource_grant.memory_bytes;
    if (laplace_content_reference_plan_memory_bound(&input, &bytes) != LAPLACE_CONTENT_REFERENCE_OK)
        reflection_limit("canonical reference native memory bound rejected its envelope");
    reflection_logical(budget, logical);
    reflection_reserve(budget, 1u, bytes);
    status = laplace_content_reference_plan_create(&input, &calculation->reference_plan);
    if (status != LAPLACE_CONTENT_REFERENCE_OK)
        ereport(ERROR, (errcode(status == LAPLACE_CONTENT_REFERENCE_LIMIT ? ERRCODE_PROGRAM_LIMIT_EXCEEDED :
            status == LAPLACE_CONTENT_REFERENCE_INCOMPLETE ? ERRCODE_NO_DATA_FOUND : ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace canonical content reference plan rejected its authenticated source closure"),
            errdetail("native_status=%u", (unsigned)status)));
    if (laplace_content_reference_plan_view_get(calculation->reference_plan, &calculation->reference_view) != LAPLACE_CONTENT_REFERENCE_OK ||
        calculation->reference_view.root_count != root_count)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace canonical reference plan lost its root mapping")));
    const laplace_content_reference_plan_view* view = &calculation->reference_view;
    if (calculation->plan_view.request_count > calculation->admitted_limits[0] ||
        calculation->plan_view.operand_count > calculation->admitted_limits[1] ||
        view->request_count > calculation->admitted_limits[0] - calculation->plan_view.request_count ||
        view->operand_count > calculation->admitted_limits[1] - calculation->plan_view.operand_count)
        reflection_limit("descriptor and canonical reference plans exceed their combined request or operand grant");
    if (view->request_count != 0u) {
        laplace_composition_working_set_input composition = {0};
        if (execution.resource_grant.memory_bytes > budget->memory_limit - budget->memory_used)
        execution.resource_grant.memory_bytes = budget->memory_limit - budget->memory_used;
        composition.context = &execution;
        composition.source_fingerprint = &view->source_fingerprint;
        composition.calculation_recipe_fingerprint = &view->recipe_fingerprint;
        composition.known_entities = view->known_entities; composition.known_entity_count = view->known_entity_count;
        composition.operands = view->operands; composition.operand_count = view->operand_count;
        composition.requests = view->requests; composition.request_count = view->request_count;
        composition.preferred_batch_bytes = preferred_bytes;
        laplace_composition_status composed = laplace_composition_working_set_create(&composition, &calculation->reference_working_set);
        if (composed != LAPLACE_COMPOSITION_OK)
            ereport(ERROR, (errcode(composed == LAPLACE_COMPOSITION_RESOURCE_INSUFFICIENT ? ERRCODE_PROGRAM_LIMIT_EXCEEDED : ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace canonical reference composition failed"), errdetail("native_status=%u", (unsigned)composed)));
        if (laplace_composition_working_set_summary_get(calculation->reference_working_set, &calculation->reference_summary) != LAPLACE_COMPOSITION_OK ||
            calculation->reference_summary.occurrence_count != 0u)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace canonical reference crossed the zero-occurrence boundary")));
        reflection_reserve(budget, 1u, calculation->reference_summary.estimated_peak_working_bytes);
        calculation->reference_results = laplace_composition_working_set_results(calculation->reference_working_set, &calculation->reference_result_count);
    }
    if (laplace_content_reference_plan_verify_results(calculation->reference_plan, calculation->reference_results,
        calculation->reference_result_count) != LAPLACE_CONTENT_REFERENCE_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace canonical reference changed an entity or its full witness")));
    canonical = palloc0(root_count * sizeof(*canonical));
    for (size_t index = 0u; index < root_count; ++index) {
        const laplace_content_reference_root* mapping = &view->roots[index];
        if (mapping->reference_kind == LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY && mapping->reference_index < view->known_entity_count)
            canonical[index] = view->known_entities[mapping->reference_index];
        else if (mapping->reference_kind == LAPLACE_COMPOSITION_REFERENCE_PRIOR_RESULT && mapping->reference_index < calculation->reference_result_count) {
            const laplace_composition_result* result = &calculation->reference_results[mapping->reference_index];
            canonical[index].entity_id = result->entity_id;
            canonical[index].identity_witness = result->identity_witness;
            canonical[index].physicality_id = result->physicality_id;
            canonical[index].centroid = result->centroid;
            canonical[index].tier_floor = result->tier_floor;
        } else ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace canonical reference root is not an authenticated result")));
        if (memcmp(canonical[index].entity_id.bytes, mapping->entity_id.bytes, 16u) != 0 ||
            memcmp(canonical[index].identity_witness.bytes, mapping->identity_witness.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace canonical reference root mapping changed identity")));
    }
    qsort(canonical, root_count, sizeof(*canonical), reflection_known_compare);
    for (size_t index = 0u; index < selected_count; ++index) {
        const laplace_composition_known_entity* actual = bsearch(&selected[index], canonical, root_count, sizeof(*canonical), reflection_known_compare);
        if (actual == NULL || actual->has_atom != selected[index].has_atom || actual->atom != selected[index].atom ||
            actual->tier_floor != selected[index].tier_floor ||
            memcmp(actual->identity_witness.bytes, selected[index].identity_witness.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace selected source content metadata differs from independent canonical reconstruction")));
    }
    size_t external_roots = (size_t)calculation->plan_view.external_entity_count;
    reflection_reserve(budget, external_roots, sizeof(*calculation->canonical_external));
    calculation->canonical_external = palloc(external_roots * sizeof(*calculation->canonical_external));
    for (size_t index = 0u; index < external_roots; ++index) {
        key.entity_id = calculation->plan_view.external_entity_ids[index];
        const laplace_composition_known_entity* actual = bsearch(&key, canonical, root_count, sizeof(*canonical), reflection_known_compare);
        if (actual == NULL) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor lost a canonical external reference")));
        calculation->canonical_external[index] = *actual;
    }
    qsort(calculation->canonical_external, external_roots, sizeof(*calculation->canonical_external), reflection_known_compare);
}

static void reflection_complete_known(reflection_calculation* calculation, reflection_budget* budget) {
    size_t reference_count = (size_t)calculation->reference_view.known_entity_count;
    size_t historical_count = calculation->binding_scope->exact_source_count;
    if (SIZE_MAX - calculation->all_known_count < reference_count ||
        SIZE_MAX - calculation->all_known_count - reference_count <= historical_count)
        reflection_limit("complete selected input frontier overflow");
    size_t canonical_count = calculation->all_known_count + reference_count;
    reflection_reserve(budget, canonical_count, sizeof(*calculation->canonical_known));
    if (canonical_count > MaxAllocSize / sizeof(*calculation->canonical_known)) reflection_limit("canonical known input frontier addressability");
    calculation->canonical_known = palloc(canonical_count * sizeof(*calculation->canonical_known));
    memcpy(calculation->canonical_known, calculation->all_known,
        calculation->all_known_count * sizeof(*calculation->canonical_known));
    if (reference_count != 0u) memcpy(calculation->canonical_known + calculation->all_known_count,
        calculation->reference_view.known_entities, reference_count * sizeof(*calculation->canonical_known));
    calculation->canonical_known_count = reflection_known_sort(calculation->canonical_known, canonical_count);
    size_t count = calculation->canonical_known_count + historical_count + 1u;
    reflection_reserve(budget, count, sizeof(*calculation->complete_known));
    if (count > MaxAllocSize / sizeof(*calculation->complete_known)) reflection_limit("complete selected input frontier addressability");
    calculation->complete_known = palloc(count * sizeof(*calculation->complete_known));
    size_t cursor = calculation->canonical_known_count;
    memcpy(calculation->complete_known, calculation->canonical_known, cursor * sizeof(*calculation->complete_known));
    if (historical_count != 0u) memcpy(calculation->complete_known + cursor,
        calculation->binding_scope->exact_sources, historical_count * sizeof(*calculation->complete_known));
    calculation->complete_known[cursor + historical_count] = calculation->retained_source;
    calculation->complete_known_count = reflection_known_sort(calculation->complete_known, count);
}

static int reflection_scope_pointer_compare(const void* left, const void* right) {
    const reflection_bindings* const* a = left;
    const reflection_bindings* const* b = right;
    return memcmp((*a)->id.bytes, (*b)->id.bytes, 32u);
}

/* Parent interval bodies are stored once under their native parent receipt.
 * Complete reachable scopes store only memberships and exact source selectors.
 * All writes and verification are set-wise, independent of descriptor count. */
static void reflection_occurrences_publish(
    const reflection_bindings* complete, reflection_calculation* calculations,
    size_t calculation_count, reflection_budget* budget) {
    static const char parent_insert[] = "INSERT INTO " LAPLACE_PG_SCHEMA
        ".physicality_occurrence_parent SELECT (i).* FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".physicality_occurrence_parent[]) i ON CONFLICT DO NOTHING";
    static const char rows_insert[] = "INSERT INTO " LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding SELECT (i).* FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding[]) i ON CONFLICT DO NOTHING";
    static const char scope_insert[] = "INSERT INTO " LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding_set SELECT (i).* FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding_set[]) i ON CONFLICT DO NOTHING";
    static const char member_insert[] = "INSERT INTO " LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding_member SELECT (i).* FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding_member[]) i ON CONFLICT DO NOTHING";
    static const char verify[] = "SELECT "
        "(SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA ".physicality_occurrence_parent[]) i JOIN "
        LAPLACE_PG_SCHEMA ".physicality_occurrence_parent p USING(parent_receipt_id) WHERE pg_catalog.record_send(p)=pg_catalog.record_send(i)),"
        "(SELECT count(*) FROM unnest($2::" LAPLACE_PG_SCHEMA ".physicality_occurrence_binding[]) i JOIN "
        LAPLACE_PG_SCHEMA ".physicality_occurrence_binding b USING(parent_receipt_id,parent_physicality_id,first_logical_ordinal)"
        " WHERE pg_catalog.record_send(b)=pg_catalog.record_send(i)),"
        "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".physicality_occurrence_binding b JOIN unnest($1::"
        LAPLACE_PG_SCHEMA ".physicality_occurrence_parent[]) i USING(parent_receipt_id)),"
        "(SELECT count(*) FROM unnest($3::" LAPLACE_PG_SCHEMA ".physicality_occurrence_binding_set[]) i JOIN "
        LAPLACE_PG_SCHEMA ".physicality_occurrence_binding_set s USING(binding_set_id) WHERE pg_catalog.record_send(s)=pg_catalog.record_send(i)),"
        "(SELECT count(*) FROM unnest($4::" LAPLACE_PG_SCHEMA ".physicality_occurrence_binding_member[]) i JOIN "
        LAPLACE_PG_SCHEMA ".physicality_occurrence_binding_member m USING(binding_set_id,parent_receipt_id,parent_physicality_id)"
        " WHERE pg_catalog.record_send(m)=pg_catalog.record_send(i)),"
        "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".physicality_occurrence_binding_member m JOIN unnest($3::"
        LAPLACE_PG_SCHEMA ".physicality_occurrence_binding_set[]) i USING(binding_set_id))";
    laplace_pg_composite_binding parent_type, row_type, scope_type, member_type;
    Oid digest = laplace_pg_composite_type_oid("record_id_256");
    Oid parent_types[4] = {digest,digest,INT8OID,NUMERICOID};
    int32 parent_mods[4] = {-1,-1,-1,LAPLACE_PG_NUMERIC_TYPMOD(20,0)};
    Oid scope_types[5] = {digest,INT8OID,INT8OID,NUMERICOID,laplace_pg_composite_array_oid("composition_known_entity_record")};
    int32 scope_mods[5] = {-1,-1,-1,LAPLACE_PG_NUMERIC_TYPMOD(20,0),-1};
    Oid member_types[3] = {digest,digest,digest}; int32 member_mods[3] = {-1,-1,-1};
    const reflection_bindings** scopes;
    size_t scope_count = 0u, member_count = 0u, row_cursor = 0u, member_cursor = 0u;
    Datum *parent_rows, *rows, *scope_rows, *member_rows;
    Oid types[4]; Datum values[4];
    reflection_reserve(budget, calculation_count, sizeof(*scopes));
    if (calculation_count > MaxAllocSize / sizeof(*scopes)) reflection_limit("occurrence scope publication addressability");
    scopes = palloc(calculation_count * sizeof(*scopes));
    for (size_t index = 0u; index < calculation_count; ++index) scopes[index] = calculations[index].binding_scope;
    qsort(scopes, calculation_count, sizeof(*scopes), reflection_scope_pointer_compare);
    for (size_t index = 0u; index < calculation_count; ++index)
        if (scope_count == 0u || reflection_scope_pointer_compare(&scopes[scope_count - 1u], &scopes[index]) != 0)
            scopes[scope_count++] = scopes[index];
    for (size_t index = 0u; index < scope_count; ++index) {
        if (SIZE_MAX - member_count < scopes[index]->parent_count) reflection_limit("occurrence scope membership overflow");
        member_count += scopes[index]->parent_count;
        reflection_reserve(budget, scopes[index]->exact_source_count, 640u);
    }
    reflection_reserve(budget, complete->parent_count, 384u);
    reflection_reserve(budget, complete->count, 768u);
    reflection_reserve(budget, scope_count, 768u);
    reflection_reserve(budget, member_count, 384u);
    if (complete->parent_count > MaxAllocSize / sizeof(Datum) || complete->count > MaxAllocSize / sizeof(Datum) ||
        scope_count > MaxAllocSize / sizeof(Datum) || member_count > MaxAllocSize / sizeof(Datum))
        reflection_limit("occurrence publication row addressability");
    parent_rows = palloc(complete->parent_count * sizeof(*parent_rows));
    rows = palloc(complete->count * sizeof(*rows));
    scope_rows = palloc(scope_count * sizeof(*scope_rows));
    member_rows = palloc(member_count * sizeof(*member_rows));
    laplace_pg_composite_binding_open("physicality_occurrence_parent", parent_types, parent_mods, 4, &parent_type);
    reflection_occurrence_binding_open(&row_type, true);
    laplace_pg_composite_binding_open("physicality_occurrence_binding_set", scope_types, scope_mods, 5, &scope_type);
    laplace_pg_composite_binding_open("physicality_occurrence_binding_member", member_types, member_mods, 3, &member_type);
    for (size_t parent = 0u; parent < complete->parent_count; ++parent) {
        size_t first = row_cursor; uint64_t logical = 0u;
        Datum parent_fields[4]; bool parent_nulls[4] = {false};
        parent_fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(complete->parent_receipts[parent].bytes, 32u));
        parent_fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(complete->parent_ids[parent].bytes, 32u));
        while (row_cursor < complete->count &&
            memcmp(complete->rows[row_cursor].parent_physicality_id.bytes, complete->parent_ids[parent].bytes, 32u) == 0) {
            const laplace_physicality_occurrence_binding* row = &complete->rows[row_cursor];
            Datum fields[8]; bool nulls[8] = {false};
            fields[0] = parent_fields[0]; fields[1] = parent_fields[1];
            fields[2] = laplace_pg_numeric_from_uint64(row->first_logical_ordinal);
            fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(row->entity_id.bytes, 16u));
            fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(row->selected_physicality_id.bytes, 32u));
            fields[5] = laplace_pg_numeric_from_uint64(row->logical_count);
            fields[6] = laplace_pg_numeric_from_uint64(row->metadata);
            fields[7] = Int32GetDatum((int32)row->version);
            rows[row_cursor++] = laplace_pg_composite_record(&row_type, fields, nulls);
            if (UINT64_MAX - logical < row->logical_count) reflection_limit("occurrence parent logical count overflow");
            logical += row->logical_count;
        }
        parent_fields[2] = Int64GetDatum(laplace_pg_checked_int64(row_cursor - first, "parent binding count"));
        parent_fields[3] = laplace_pg_numeric_from_uint64(logical);
        parent_rows[parent] = laplace_pg_composite_record(&parent_type, parent_fields, parent_nulls);
    }
    if (row_cursor != complete->count) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence publication lost a parent")));
    for (size_t index = 0u; index < scope_count; ++index) {
        const reflection_bindings* scope = scopes[index];
        Datum fields[5]; bool nulls[5] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(scope->id.bytes, 32u));
        fields[1] = Int64GetDatum(laplace_pg_checked_int64(scope->parent_count, "scope parent count"));
        fields[2] = Int64GetDatum(laplace_pg_checked_int64(scope->count, "scope binding count"));
        fields[3] = laplace_pg_numeric_from_uint64(scope->logical_count);
        fields[4] = PointerGetDatum(reflection_known_array(scope->exact_sources, scope->exact_source_count));
        scope_rows[index] = laplace_pg_composite_record(&scope_type, fields, nulls);
        for (size_t parent = 0u; parent < scope->parent_count; ++parent) {
            Datum membership[3]; bool absent[3] = {false};
            membership[0] = fields[0];
            membership[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(scope->parent_receipts[parent].bytes, 32u));
            membership[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(scope->parent_ids[parent].bytes, 32u));
            member_rows[member_cursor++] = laplace_pg_composite_record(&member_type, membership, absent);
        }
    }
    laplace_pg_composite_binding* bindings[4] = {&parent_type,&row_type,&scope_type,&member_type};
    Datum* inputs[4] = {parent_rows,rows,scope_rows,member_rows};
    size_t counts[4] = {complete->parent_count,complete->count,scope_count,member_count};
    const char* statements[4] = {parent_insert,rows_insert,scope_insert,member_insert};
    for (size_t index = 0u; index < 4u; ++index) {
        types[index] = bindings[index]->array_oid;
        values[index] = PointerGetDatum(laplace_pg_composite_array(bindings[index], inputs[index], counts[index]));
        reflection_query(budget);
        if (SPI_execute_with_args(statements[index], 1, &types[index], &values[index], NULL, false, 0) != SPI_OK_INSERT)
            ereport(ERROR, (errmsg("Laplace normalized occurrence publication failed"), errdetail("family=%zu", index)));
    }
    reflection_query(budget);
    if (SPI_execute_with_args(verify, 4, types, values, NULL, false, 1) != SPI_OK_SELECT || SPI_processed != 1u)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace normalized occurrence verification is absent")));
    size_t expected[6] = {complete->parent_count,complete->count,complete->count,scope_count,member_count,member_count};
    for (size_t index = 0u; index < 6u; ++index)
        if (reflection_positive_bigint(reflection_required(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, (int)index + 1), true) != expected[index])
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace normalized complete occurrence owner changed")));
    SPI_freetuptable(SPI_tuptable);
    for (size_t index = 0u; index < 4u; ++index) laplace_pg_composite_binding_close(bindings[index]);
}

Datum laplace_pg_physicality_entity_admit_batch(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    reflection_budget budget = {0};
    MemoryContext caller = CurrentMemoryContext;
    ReturnSetInfo* returned = (ReturnSetInfo*)fcinfo->resultinfo;
    reflection_calculation* calculations;
    reflection_source* sources;
    reflection_source* selected_sources;
    const reflection_source** occurrence_sources;
    size_t occurrence_source_count;
    reflection_bindings bindings;
    reflection_scope_index scope_index;
    laplace_digest256* ids;
    laplace_composition_known_entity* external;
    laplace_composition_known_entity* atoms;
    reflection_source* atom_bodies;
    uint32_t* positions;
    uint64_t external_count;
    size_t count, atom_count;
    uint64_t* inserted;
    uint64_t* nodes;
    laplace_digest256 deposit_receipt;
    bytea* deposit_snapshot;
    int64 maximum_requests = PG_GETARG_INT64(3);
    int64 maximum_operands = PG_GETARG_INT64(4);
    int64 maximum_carriers = PG_GETARG_INT64(5);
    int64 preferred_bytes = PG_GETARG_INT64(7);
    int64 operations = PG_GETARG_INT64(8);
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if ((context.flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0u)
        ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
            errmsg("Read-only Laplace context cannot admit physicality entity state")));
    if (laplace_framework_context_validate(&context) != LAPLACE_FRAMEWORK_OK ||
        (context.epoch_mask & (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_GEOMETRY)) == 0u ||
        maximum_requests <= 0 || maximum_operands <= 0 || maximum_carriers < 0 || preferred_bytes <= 0 || operations <= 0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Laplace descriptor admission requires valid pinned context and finite positive work grants")));
    budget.memory_limit = context.resource_grant.memory_bytes;
    budget.operation_limit = (uint64_t)operations;
    budget.logical_limit = laplace_pg_uint64_from_numeric(PG_GETARG_DATUM(6), "descriptor maximum logical work");
    if (budget.logical_limit == 0u) reflection_limit("logical work grant is zero");
    InitMaterializedSRF(fcinfo, MAT_SRF_USE_EXPECTED_DESC);
    reflection_reserve(&budget, 1u, toast_raw_datum_size(PG_GETARG_DATUM(1)));
    reflection_reserve(&budget, 1u, toast_raw_datum_size(PG_GETARG_DATUM(2)));
    reflection_reserve(&budget, 1u, toast_raw_datum_size(PG_GETARG_DATUM(9)));
    ids = reflection_read_ids(PG_GETARG_ARRAYTYPE_P(1), &count, &budget);
    external = reflection_read_known(PG_GETARG_ARRAYTYPE_P(2), &external_count, &budget);
    external_count = reflection_known_sort(external, (size_t)external_count);
    bindings = reflection_occurrences_read(PG_GETARG_ARRAYTYPE_P(9), &budget);
    calculations = reflection_calculations(count, &budget);
    reflection_reserve(&budget, count, 2u * sizeof(uint64_t));
    inserted = palloc0(count * sizeof(*inserted));
    nodes = palloc0(count * sizeof(*nodes));
    if (SPI_connect() != SPI_OK_CONNECT) ereport(ERROR, (errmsg("Laplace descriptor admission could not connect")));
    MemoryContextSwitchTo(caller);
    sources = reflection_sources(ids, count, &budget);
    selected_sources = reflection_known_validate(&context, external, (size_t)external_count, &budget);
    occurrence_sources = reflection_source_union(sources, count, selected_sources,
        (size_t)external_count, &occurrence_source_count, &budget);
    if (bindings.count == 0u) reflection_occurrences_derive(&bindings, occurrence_sources, occurrence_source_count, &budget);
    reflection_occurrences_validate(&bindings, occurrence_sources, occurrence_source_count, &budget);
    reflection_occurrences_select_sources(&bindings, external, (size_t)external_count, false, &budget);
    scope_index = reflection_occurrence_scope_index(&bindings, &budget);
    for (size_t index = 0u; index < count; ++index)
        reflection_prepare(&context, &sources[index], (uint64_t)maximum_requests,
            (uint64_t)maximum_operands, (uint64_t)maximum_carriers, budget.logical_limit, &calculations[index], &budget);
    atoms = reflection_atoms(&context, calculations, count, external, (size_t)external_count, &positions, &atom_count, &budget);
    atom_bodies = reflection_atom_bodies(atoms, atom_count, &budget);
    for (size_t index = 0u; index < count; ++index) {
        calculations[index].binding_scope = reflection_occurrence_scope(&scope_index, &sources[index],
            occurrence_sources, occurrence_source_count, &budget);
        calculations[index].binding_set_id = calculations[index].binding_scope->id;
        calculations[index].occurrence_bindings = calculations[index].binding_scope->rows;
        calculations[index].occurrence_binding_count = calculations[index].binding_scope->count;
        reflection_reference_prepare(&context, &context.resource_grant, &sources[index], occurrence_sources,
            occurrence_source_count, external, (size_t)external_count, positions, atoms, atom_bodies, atom_count,
            (uint64_t)preferred_bytes, &calculations[index], &budget);
        reflection_calculate(&context, &context.resource_grant, external, (size_t)external_count,
            positions, atoms, atom_count, (uint64_t)preferred_bytes, &calculations[index], &budget);
        reflection_complete_known(&calculations[index], &budget);
        reflection_generated_prepare(&calculations[index], &budget);
    }
    reflection_entities_publish(calculations, count, inserted, &context, (uint64_t)preferred_bytes,
        &deposit_receipt, &deposit_snapshot, &budget, true);
    reflection_occurrences_publish(&bindings, calculations, count, &budget);
    reflection_owners_publish(PG_GETARG_DATUM(0), calculations, count, (uint64_t)maximum_requests,
        (uint64_t)maximum_operands, (uint64_t)maximum_carriers, budget.logical_limit, nodes, &budget);
    reflection_depositions_publish(calculations, count, &deposit_receipt, deposit_snapshot, &budget);
    reflection_sources_recheck(sources, count, &budget);
    reflection_sources_recheck(selected_sources, (size_t)external_count, &budget);
    reflection_sources_recheck(atom_bodies, atom_count, &budget);
    reflection_entities_publish(calculations, count, NULL, &context, 0u, NULL, NULL, &budget, false);
    for (size_t index = 0u; index < count; ++index) {
        const reflection_calculation* calculation = &calculations[index];
        const laplace_composition_result* root = &calculation->results[calculation->plan_view.root_result_index];
        Datum fields[9];
        bool nulls[9] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(ids[index].bytes, 32u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(calculation->view_id.bytes, 32u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(root->entity_id.bytes, 16u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(root->identity_witness.bytes, 32u));
        fields[4] = Int64GetDatum(laplace_pg_checked_int64(calculation->summary.unique_entity_count, "descriptor entity candidates"));
        fields[5] = Int64GetDatum(laplace_pg_checked_int64(inserted[index], "descriptor inserted entities"));
        fields[6] = Int64GetDatum(laplace_pg_checked_int64(nodes[index], "descriptor derived nodes"));
        fields[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(deposit_receipt.bytes, 32u));
        fields[8] = Int64GetDatum(laplace_pg_checked_int64(budget.operations, "descriptor batch database operations"));
        tuplestore_putvalues(returned->setResult, returned->setDesc, fields, nulls);
        reflection_destroy(&calculations[index]);
    }
    if (SPI_finish() != SPI_OK_FINISH) ereport(ERROR, (errmsg("Laplace descriptor admission could not finish")));
    return (Datum)0;
}

typedef struct reflection_retained_owner {
    laplace_digest256 view_id;
    laplace_digest256 source_id;
    laplace_id128 root_id;
    laplace_digest256 root_witness;
    laplace_framework_context context;
    laplace_digest256 input_fingerprint;
    laplace_digest256 recipe;
    uint64_t maximum_requests, maximum_operands, maximum_carriers, maximum_logical_count;
    laplace_composition_known_entity* external;
    size_t external_count;
    laplace_digest256 binding_set_id;
    reflection_bindings* bindings;
} reflection_retained_owner;

typedef struct reflection_retained_node {
    laplace_digest256 view_id;
    uint64_t index;
    laplace_id128 entity_id;
    laplace_digest256 witness;
    laplace_persistence_physicality_record physicality;
    ArrayType* child_ids;
} reflection_retained_node;


static int reflection_binding_set_compare(const void* left, const void* right) {
    return memcmp(((const reflection_bindings*)left)->id.bytes,
        ((const reflection_bindings*)right)->id.bytes, 32u);
}

/* Inventory and fetch each selected set once. Headers and interval rows use
 * separate tagged rows in one payload query, so a large source frontier is
 * never copied once per interval or descriptor node. */
static reflection_bindings* reflection_occurrences_retain(
    const laplace_digest256* ids, size_t count, reflection_budget* budget) {
    static const char inventory_sql[] = "SELECT i.id,s.parent_count,s.binding_count,s.logical_count,"
        LAPLACE_PG_SCHEMA ".physicality_entity_varlena_bytes(s.exact_sources),"
        "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".physicality_occurrence_binding_member m WHERE m.binding_set_id=i.id)"
        " FROM unnest($1::bytea[]) i(id) LEFT JOIN " LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding_set s ON s.binding_set_id=i.id ORDER BY i.id";
    static const char payload_sql[] = "SELECT s.binding_set_id,0 AS kind,NULL::bytea AS parent_id,"
        "0::numeric AS ordinal,NULL::" LAPLACE_PG_SCHEMA ".physicality_occurrence_binding AS binding,s.exact_sources,"
        "NULL::" LAPLACE_PG_SCHEMA ".physicality_occurrence_parent AS parent_owner"
        " FROM unnest($1::bytea[]) i(id) JOIN " LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding_set s ON s.binding_set_id=i.id"
        " UNION ALL SELECT m.binding_set_id,1,b.parent_physicality_id,b.first_logical_ordinal,b,"
        "NULL::" LAPLACE_PG_SCHEMA ".composition_known_entity_record[],p"
        " FROM unnest($1::bytea[]) i(id) JOIN " LAPLACE_PG_SCHEMA
        ".physicality_occurrence_binding_member m ON m.binding_set_id=i.id"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".physicality_occurrence_parent p USING(parent_receipt_id,parent_physicality_id)"
        " JOIN " LAPLACE_PG_SCHEMA ".physicality_occurrence_binding b USING(parent_receipt_id,parent_physicality_id)"
        " ORDER BY 1,2,3,4";
    reflection_bindings* sets;
    Oid type = BYTEAARRAYOID;
    Datum value;
    uint64_t row_count = count, cursor = 0u;
    size_t* row_cursors;
    size_t* parent_cursors;
    bool* headers;
    reflection_reserve(budget, count, sizeof(*sets) + sizeof(*row_cursors) + sizeof(*parent_cursors) + sizeof(*headers) + 128u);
    if (count == 0u || count > MaxAllocSize / sizeof(*sets) || count >= LONG_MAX)
        reflection_limit("retained occurrence set addressability");
    sets = palloc0(count * sizeof(*sets));
    row_cursors = palloc0(count * sizeof(*row_cursors));
    parent_cursors = palloc0(count * sizeof(*parent_cursors));
    headers = palloc0(count * sizeof(*headers));
    value = PointerGetDatum(reflection_digests(ids, count));
    reflection_query(budget);
    if (SPI_execute_with_args(inventory_sql, 1, &type, &value, NULL, true, (long)count + 1) != SPI_OK_SELECT ||
        SPI_processed != count || SPI_tuptable == NULL)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained occurrence set inventory is incomplete")));
    for (size_t index = 0u; index < count; ++index) {
        HeapTuple row = SPI_tuptable->vals[index];
        TupleDesc descriptor = SPI_tuptable->tupdesc;
        uint64_t parents = reflection_positive_bigint(reflection_required(row, descriptor, 2), true);
        uint64_t bindings = reflection_positive_bigint(reflection_required(row, descriptor, 3), true);
        uint64_t logical = laplace_pg_uint64_from_numeric(reflection_required(row, descriptor, 4), "retained occurrence logical count");
        uint64_t bytes = reflection_positive_bigint(reflection_required(row, descriptor, 5), false);
        reflection_bytes(reflection_required(row, descriptor, 1), &sets[index].id, 32u);
        if (memcmp(sets[index].id.bytes, ids[index].bytes, 32u) != 0 ||
            ((parents == 0u) != (bindings == 0u)) || ((parents == 0u) != (logical == 0u)) ||
            parents > bindings || bindings > logical ||
            reflection_positive_bigint(reflection_required(row, descriptor, 6), true) != parents)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained occurrence set counts are inconsistent")));
        if (bindings > budget->logical_limit || parents > SIZE_MAX ||
            bindings > MaxAllocSize / sizeof(*sets[index].rows) || UINT64_MAX - row_count < bindings)
            reflection_limit("retained occurrence frontier exceeds its finite envelope");
        row_count += bindings;
        reflection_reserve(budget, bindings, sizeof(*sets[index].rows) + 768u);
        reflection_reserve(budget, bytes, 3u);
        sets[index].parent_count = (size_t)parents;
        sets[index].count = (size_t)bindings;
        sets[index].logical_count = logical;
        sets[index].rows = palloc0((size_t)bindings * sizeof(*sets[index].rows));
        reflection_reserve(budget, parents, sizeof(*sets[index].parent_ids) + sizeof(*sets[index].parent_receipts) + 2u * sizeof(uint64_t));
        sets[index].parent_ids = palloc((size_t)parents * sizeof(*sets[index].parent_ids));
        sets[index].parent_receipts = palloc((size_t)parents * sizeof(*sets[index].parent_receipts));
        sets[index].retained_parent_binding_counts = palloc((size_t)parents * sizeof(uint64_t));
        sets[index].retained_parent_logical_counts = palloc((size_t)parents * sizeof(uint64_t));
    }
    SPI_freetuptable(SPI_tuptable);
    if (row_count >= LONG_MAX) reflection_limit("retained occurrence payload row addressability");
    reflection_query(budget);
    if (SPI_execute_with_args(payload_sql, 1, &type, &value, NULL, true, (long)row_count + 1) != SPI_OK_SELECT ||
        SPI_processed != row_count || SPI_tuptable == NULL)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained occurrence payload is incomplete")));
    for (uint64_t index = 0u; index < row_count; ++index) {
        HeapTuple row = SPI_tuptable->vals[index];
        TupleDesc descriptor = SPI_tuptable->tupdesc;
        reflection_bindings key = {0};
        reflection_bindings* set;
        reflection_bytes(reflection_required(row, descriptor, 1), &key.id, 32u);
        set = bsearch(&key, sets, count, sizeof(*sets), reflection_binding_set_compare);
        if (set == NULL) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence payload has an unexpected owner")));
        size_t owner = (size_t)(set - sets);
        int32 kind = DatumGetInt32(reflection_required(row, descriptor, 2));
        if (kind == 0) {
            Datum source_array = reflection_required(row, descriptor, 6);
            ArrayType* array = DatumGetArrayTypeP(source_array);
            if (headers[owner]) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence payload repeats its header")));
            headers[owner] = true;
            if (ArrayGetNItems(ARR_NDIM(array), ARR_DIMS(array)) != 0) {
                uint64_t source_count;
                set->exact_sources = reflection_read_known(array, &source_count, budget);
                set->exact_source_count = (size_t)source_count;
                for (size_t source = 1u; source < set->exact_source_count; ++source)
                    if (reflection_known_physicality_compare(&set->exact_sources[source - 1u], &set->exact_sources[source]) >= 0)
                        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence sources are not a unique exact ordered frontier")));
            } else if (set->count != 0u)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence source frontier is absent")));
        } else if (kind == 1 && headers[owner] && row_cursors[owner] < set->count) {
            HeapTupleHeader binding = DatumGetHeapTupleHeader(reflection_required(row, descriptor, 5));
            laplace_physicality_occurrence_binding* out = &set->rows[row_cursors[owner]++];
            laplace_digest256 parent_receipt;
            reflection_bytes(laplace_pg_required_composite_attribute(binding, 1, "occurrence parent receipt"), &parent_receipt, 32u);
            reflection_bytes(laplace_pg_required_composite_attribute(binding, 2, "occurrence parent"), &out->parent_physicality_id, 32u);
            out->first_logical_ordinal = laplace_pg_uint64_from_numeric(
                laplace_pg_required_composite_attribute(binding, 3, "occurrence first ordinal"), "occurrence first ordinal");
            reflection_bytes(laplace_pg_required_composite_attribute(binding, 4, "occurrence entity"), &out->entity_id, 16u);
            reflection_bytes(laplace_pg_required_composite_attribute(binding, 5, "occurrence selected physicality"), &out->selected_physicality_id, 32u);
            out->logical_count = laplace_pg_uint64_from_numeric(
                laplace_pg_required_composite_attribute(binding, 6, "occurrence logical count"), "occurrence logical count");
            out->metadata = laplace_pg_uint64_from_numeric(
                laplace_pg_required_composite_attribute(binding, 7, "occurrence metadata"), "occurrence metadata");
            int32 version = DatumGetInt32(laplace_pg_required_composite_attribute(binding, 8, "occurrence version"));
            if (version != LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence payload version or ownership differs")));
            out->version = (uint32_t)version;
            HeapTupleHeader parent = DatumGetHeapTupleHeader(reflection_required(row, descriptor, 7));
            laplace_digest256 owner_receipt, owner_physicality;
            reflection_bytes(laplace_pg_required_composite_attribute(parent, 1, "occurrence parent receipt"), &owner_receipt, 32u);
            reflection_bytes(laplace_pg_required_composite_attribute(parent, 2, "occurrence parent physicality"), &owner_physicality, 32u);
            if (memcmp(owner_receipt.bytes, parent_receipt.bytes, 32u) != 0 ||
                memcmp(owner_physicality.bytes, out->parent_physicality_id.bytes, 32u) != 0 ||
                reflection_positive_bigint(laplace_pg_required_composite_attribute(parent, 3, "parent binding count"), false) > set->count ||
                laplace_pg_uint64_from_numeric(laplace_pg_required_composite_attribute(parent, 4, "parent logical count"), "parent logical count") > set->logical_count)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence parent owner differs from its scope")));
            if (parent_cursors[owner] == 0u || memcmp(set->parent_ids[parent_cursors[owner] - 1u].bytes,
                out->parent_physicality_id.bytes, 32u) != 0) {
                if (parent_cursors[owner] >= set->parent_count)
                    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence scope contains extra parents")));
                set->parent_ids[parent_cursors[owner]] = out->parent_physicality_id;
                set->retained_parent_binding_counts[parent_cursors[owner]] = reflection_positive_bigint(
                    laplace_pg_required_composite_attribute(parent, 3, "parent binding count"), false);
                set->retained_parent_logical_counts[parent_cursors[owner]] = laplace_pg_uint64_from_numeric(
                    laplace_pg_required_composite_attribute(parent, 4, "parent logical count"), "parent logical count");
                set->parent_receipts[parent_cursors[owner]++] = parent_receipt;
            } else if (memcmp(set->parent_receipts[parent_cursors[owner] - 1u].bytes, parent_receipt.bytes, 32u) != 0)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence scope changes a selected parent receipt")));
            ++cursor;
        } else ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace occurrence payload order or count differs")));
    }
    SPI_freetuptable(SPI_tuptable);
    if (cursor != row_count - count)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained occurrence payload count differs")));
    for (size_t index = 0u; index < count; ++index)
        if (!headers[index] || row_cursors[index] != sets[index].count || parent_cursors[index] != sets[index].parent_count)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained occurrence set is incomplete")));
    return sets;
}

static void reflection_occurrences_revalidate(
    reflection_bindings* bindings, const reflection_source* const* sources, size_t source_count,
    reflection_budget* budget) {
    const reflection_source** selected;
    laplace_digest256 expected = bindings->id;
    size_t expected_parents = bindings->parent_count;
    uint64_t expected_logical = bindings->logical_count;
    const laplace_digest256* expected_parent_ids = bindings->parent_ids;
    const laplace_digest256* expected_parent_receipts = bindings->parent_receipts;
    reflection_reserve(budget, bindings->exact_source_count, sizeof(*selected));
    if (bindings->exact_source_count > MaxAllocSize / sizeof(*selected))
        reflection_limit("retained selected occurrence source addressability");
    selected = palloc(bindings->exact_source_count * sizeof(*selected));
    for (size_t index = 0u; index < bindings->exact_source_count; ++index) {
        reflection_source key = {0};
        const reflection_source* pointer = &key;
        const reflection_source* const* found;
        key.physicality.physicality_id = bindings->exact_sources[index].physicality_id;
        found = bsearch(&pointer, sources, source_count, sizeof(*sources), reflection_source_pointer_compare);
        if (found == NULL) ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained occurrence source was not authenticated")));
        selected[index] = *found;
    }
    reflection_occurrences_validate(bindings, selected, bindings->exact_source_count, budget);
    if (memcmp(expected.bytes, bindings->id.bytes, 32u) != 0 ||
        expected_parents != bindings->parent_count || expected_logical != bindings->logical_count)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained occurrence receipt differs from complete native reconstruction")));
    size_t row_cursor = 0u;
    for (size_t index = 0u; index < expected_parents; ++index) {
        if (memcmp(expected_parent_ids[index].bytes, bindings->parent_ids[index].bytes, 32u) != 0 ||
            memcmp(expected_parent_receipts[index].bytes, bindings->parent_receipts[index].bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained parent occurrence receipt differs from native reconstruction")));
        size_t first = row_cursor; uint64_t logical = 0u;
        while (row_cursor < bindings->count && memcmp(bindings->rows[row_cursor].parent_physicality_id.bytes,
            bindings->parent_ids[index].bytes, 32u) == 0) {
            if (UINT64_MAX - logical < bindings->rows[row_cursor].logical_count)
                ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained parent occurrence count overflows")));
            logical += bindings->rows[row_cursor++].logical_count;
        }
        if (row_cursor - first != bindings->retained_parent_binding_counts[index] ||
            logical != bindings->retained_parent_logical_counts[index])
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained parent occurrence inventory differs from its exact intervals")));
    }
    /* Refuse hidden unused source metadata under an otherwise valid set ID. */
    reflection_bindings exact = *bindings;
    reflection_occurrences_select_sources(&exact, bindings->exact_sources, bindings->exact_source_count, true, budget);
    if (exact.exact_source_count != bindings->exact_source_count)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained occurrence set contains unused source selectors")));
}

static int reflection_owner_compare(const void* left, const void* right) {
    return memcmp(((const reflection_retained_owner*)left)->view_id.bytes,
                  ((const reflection_retained_owner*)right)->view_id.bytes, 32u);
}

static Datum reflection_attribute(HeapTupleHeader row, int index) {
    return laplace_pg_required_composite_attribute(row, index, "physicality derivation owner");
}

static uint64_t reflection_positive_bigint(Datum value, bool zero) {
    int64 number = DatumGetInt64(value);
    if (number < 0 || (!zero && number == 0))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained descriptor work grant is invalid")));
    return (uint64_t)number;
}

static void reflection_owner_read(Datum value, reflection_retained_owner* owner, reflection_budget* budget) {
    HeapTupleHeader row = DatumGetHeapTupleHeader(value);
    Datum external = reflection_attribute(row, 6);
    uint64_t count;
    reflection_bytes(reflection_attribute(row, 1), &owner->view_id, 32u);
    reflection_bytes(reflection_attribute(row, 2), &owner->source_id, 32u);
    reflection_bytes(reflection_attribute(row, 3), &owner->root_id, 16u);
    reflection_bytes(reflection_attribute(row, 4), &owner->root_witness, 32u);
    laplace_pg_read_execution_context(reflection_attribute(row, 5), &owner->context);
    if (laplace_framework_context_validate(&owner->context) != LAPLACE_FRAMEWORK_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained descriptor context is invalid")));
    reflection_reserve(budget, 2u, toast_raw_datum_size(external));
    owner->external = reflection_read_known(DatumGetArrayTypeP(external), &count, budget);
    owner->external_count = reflection_known_sort(owner->external, (size_t)count);
    if (owner->external_count != count)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained descriptor repeats an external selector")));
    reflection_bytes(reflection_attribute(row, 7), &owner->input_fingerprint, 32u);
    reflection_bytes(reflection_attribute(row, 8), &owner->recipe, 32u);
    owner->maximum_requests = reflection_positive_bigint(reflection_attribute(row, 9), false);
    owner->maximum_operands = reflection_positive_bigint(reflection_attribute(row, 10), false);
    owner->maximum_carriers = reflection_positive_bigint(reflection_attribute(row, 11), true);
    owner->maximum_logical_count = laplace_pg_uint64_from_numeric(reflection_attribute(row, 12), "retained logical work");
    reflection_bytes(reflection_attribute(row, 13), &owner->binding_set_id, 32u);
    if (owner->maximum_logical_count == 0u)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace retained descriptor logical grant is zero")));
}

static ArrayType* reflection_entities_array(const laplace_id128* ids, size_t count) {
    Datum* values;
    if (count == 0u || count > INT_MAX || count > MaxAllocSize / sizeof(*values))
        reflection_limit("candidate entity frontier addressability");
    values = palloc(count * sizeof(*values));
    for (size_t index = 0u; index < count; ++index)
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(ids[index].bytes, 16u));
    return construct_array(values, (int)count, BYTEAOID, -1, false, TYPALIGN_INT);
}

/* Two indexed selection queries precede any native work. The selected owner is
 * a deterministic witness for one distinct physicality, not a claim that every
 * unused historical derivation of that same body has been inspected. */
static void reflection_read_views_connected(
    const laplace_framework_context* context, const laplace_digest256* required_view_id, const laplace_id128* entity_ids,
    const laplace_digest256* selected, size_t entity_count, uint32_t relation_mask,
    uint64_t maximum_rows, uint64_t maximum_memory_bytes, uint64_t maximum_operations,
    laplace_pg_physicality_entity_view** output, size_t* output_count, uint64_t* operations) {
    static const char inventory_sql[] =
        "WITH matched AS MATERIALIZED (SELECT DISTINCT ON(n.physicality_id) n.view_id,n.result_index,n.physicality_id"
        " FROM " LAPLACE_PG_SCHEMA ".physicality_entity_node n JOIN " LAPLACE_PG_SCHEMA
        ".physicality_entity_view v USING(view_id) WHERE "
        "((($3 & 2)<>0 AND EXISTS(SELECT 1 FROM unnest($1::bytea[],$2::bytea[]) i(id,selected)"
        " WHERE n.entity_id=i.id AND (i.selected IS NULL OR n.physicality_id=i.selected)))"
        " OR (($3 & 29)<>0 AND n.child_ids && $1::bytea[])"
        " OR (($3 & 32)<>0 AND $11::bytea IS NOT NULL AND n.entity_id=v.root_entity_id))"
        " AND ($11::bytea IS NULL OR v.view_id=$11)"
        " AND (v.admission_context).epochs=$4 AND (v.admission_context).authority_fingerprint=$5"
        " AND (v.admission_context).epoch_mask=$6 AND (v.admission_context).framework_major=$7"
        " AND (v.admission_context).framework_minor=$8 AND ((v.admission_context).flags & $12)=($9 & $12)"
        " ORDER BY n.physicality_id,n.view_id,n.result_index LIMIT $10)"
        " SELECT m.view_id,m.result_index,2048::bigint+"
        LAPLACE_PG_SCHEMA ".physicality_entity_varlena_bytes(v.external_known)+"
        LAPLACE_PG_SCHEMA ".physicality_entity_varlena_bytes(v.admission_context)+"
        LAPLACE_PG_SCHEMA ".physicality_entity_varlena_bytes(n.physicality_metadata)+"
        LAPLACE_PG_SCHEMA ".physicality_entity_varlena_bytes(n.child_ids) FROM matched m"
        " JOIN " LAPLACE_PG_SCHEMA ".physicality_entity_view v USING(view_id)"
        " JOIN " LAPLACE_PG_SCHEMA ".physicality_entity_node n ON n.view_id=m.view_id AND n.result_index=m.result_index"
        " ORDER BY m.physicality_id";
    static const char payload_sql[] =
        "SELECT i.ordinality,v,n,e.identity_witness,r.identity_witness"
        " FROM unnest($1::bytea[],$2::bigint[]) WITH ORDINALITY i(view_id,result_index,ordinality)"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".physicality_entity_view v ON v.view_id=i.view_id"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".physicality_entity_node n ON n.view_id=i.view_id AND n.result_index=i.result_index"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".entity e ON e.entity_id=n.entity_id"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".entity r ON r.entity_id=v.root_entity_id ORDER BY i.ordinality";
    reflection_budget budget = {0};
    Oid schema;
    Oid types[12] = {BYTEAARRAYOID,BYTEAARRAYOID,INT4OID,BYTEAARRAYOID,BYTEAOID,INT8OID,INT2OID,INT2OID,INT4OID,INT8OID,BYTEAOID,INT4OID};
    Datum values[12];
    char nulls[12] = {' ',' ',' ',' ',' ',' ',' ',' ',' ',' ','n',' '};
    Datum* selections;
    bool* selection_nulls;
    int dims[1], lower[1] = {1};
    Datum* view_ids;
    Datum* result_indices;
    reflection_retained_owner* owners;
    reflection_retained_node* nodes;
    laplace_digest256* source_ids;
    reflection_source* sources;
    reflection_calculation* calculations;
    laplace_digest256* binding_ids;
    reflection_bindings* binding_sets;
    size_t binding_set_count = 0u, occurrence_source_count;
    reflection_source* selected_sources;
    const reflection_source** occurrence_sources;
    laplace_composition_known_entity* external;
    laplace_composition_known_entity* atoms;
    uint32_t* atom_positions;
    size_t count, owner_count = 0u, external_count = 0u, atom_count;
    *output = NULL; *output_count = 0u; *operations = 0u;
    if (entity_count == 0u || (relation_mask & 63u) == 0u) return;
    schema = get_namespace_oid(LAPLACE_PG_SCHEMA, true);
    if (!OidIsValid(schema) || !OidIsValid(get_relname_relid("physicality_entity_node", schema))) return;
    if (!OidIsValid(get_relname_relid("physicality_entity_view", schema)))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor node index has no derivation owner")));
    if (context == NULL || entity_ids == NULL || laplace_framework_context_validate(context) != LAPLACE_FRAMEWORK_OK ||
        maximum_rows == 0u || maximum_rows >= INT_MAX || maximum_operations == 0u || maximum_memory_bytes == 0u)
        reflection_limit("candidate read grants are absent or unaddressable");
    budget.memory_limit = maximum_memory_bytes < context->resource_grant.memory_bytes ? maximum_memory_bytes : context->resource_grant.memory_bytes;
    budget.logical_limit = budget.memory_limit * LAPLACE_PG_PHYSICALITY_ENTITY_READ_LOGICAL_STEPS_PER_GRANT_BYTE;
    budget.operation_limit = maximum_operations;
    budget.read_only = true;
    reflection_reserve(&budget, entity_count, sizeof(Datum) + sizeof(bool) + 128u);
    reflection_reserve(&budget, maximum_rows + 1u, 256u);
    if (entity_count > INT_MAX || entity_count > MaxAllocSize / sizeof(*selections)) reflection_limit("selector frontier addressability");
    selections = palloc0(entity_count * sizeof(*selections));
    selection_nulls = palloc(entity_count * sizeof(*selection_nulls));
    for (size_t index = 0u; index < entity_count; ++index) {
        static const laplace_digest256 zero = {{0}};
        selection_nulls[index] = selected == NULL || memcmp(selected[index].bytes, zero.bytes, 32u) == 0;
        if (!selection_nulls[index]) selections[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(selected[index].bytes, 32u));
    }
    dims[0] = (int)entity_count;
    values[0] = PointerGetDatum(reflection_entities_array(entity_ids, entity_count));
    values[1] = PointerGetDatum(construct_md_array(selections, selection_nulls, 1, dims, lower, BYTEAOID, -1, false, TYPALIGN_INT));
    values[2] = Int32GetDatum((int32)relation_mask);
    values[3] = PointerGetDatum(reflection_digests(context->epochs, LAPLACE_FRAMEWORK_EPOCH_COUNT));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(context->authority_fingerprint.bytes, 32u));
    values[5] = Int64GetDatum((int64)context->epoch_mask);
    values[6] = Int16GetDatum((int16)context->major);
    values[7] = Int16GetDatum((int16)context->minor);
    values[8] = Int32GetDatum((int32)context->flags);
    values[9] = Int64GetDatum((int64)maximum_rows + 1);
    values[10] = (Datum)0;
    values[11] = Int32GetDatum((int32)~(uint32_t)LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY);
    if (required_view_id != NULL) {
        static const laplace_digest256 zero = {{0}};
        if (memcmp(required_view_id->bytes, zero.bytes, 32u) != 0) {
            values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(required_view_id->bytes, 32u));
            nulls[10] = ' ';
        }
    }
    reflection_query(&budget);
    if (SPI_execute_with_args(inventory_sql, 12, types, values, nulls, true, (long)maximum_rows + 1) != SPI_OK_SELECT || SPI_tuptable == NULL)
        ereport(ERROR, (errmsg("Laplace descriptor candidate inventory failed")));
    if (SPI_processed > maximum_rows) reflection_limit("distinct physicality candidate row grant exhausted");
    count = (size_t)SPI_processed;
    if (count == 0u) {
        SPI_freetuptable(SPI_tuptable);
        *operations = budget.operations;
        return;
    }
    reflection_reserve(&budget, count, sizeof(*owners) + sizeof(*nodes) + sizeof(*view_ids) + sizeof(*result_indices));
    owners = palloc0(count * sizeof(*owners));
    nodes = palloc0(count * sizeof(*nodes));
    view_ids = palloc(count * sizeof(*view_ids));
    result_indices = palloc(count * sizeof(*result_indices));
    for (size_t index = 0u; index < count; ++index) {
        HeapTuple row = SPI_tuptable->vals[index];
        reflection_bytes(reflection_required(row, SPI_tuptable->tupdesc, 1), &nodes[index].view_id, 32u);
        nodes[index].index = reflection_positive_bigint(reflection_required(row, SPI_tuptable->tupdesc, 2), true);
        int64 bytes = DatumGetInt64(reflection_required(row, SPI_tuptable->tupdesc, 3));
        if (bytes <= 0) ereport(ERROR, (errmsg("Laplace descriptor candidate byte inventory is invalid")));
        reflection_reserve(&budget, (uint64_t)bytes, 3u);
        view_ids[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(nodes[index].view_id.bytes, 32u));
        result_indices[index] = Int64GetDatum((int64)nodes[index].index);
    }
    SPI_freetuptable(SPI_tuptable);
    {
        Oid payload_types[2] = {BYTEAARRAYOID,INT8ARRAYOID};
        Datum payload_values[2] = {
            PointerGetDatum(construct_array(view_ids, (int)count, BYTEAOID, -1, false, TYPALIGN_INT)),
            PointerGetDatum(construct_array(result_indices, (int)count, INT8OID, sizeof(int64), FLOAT8PASSBYVAL, TYPALIGN_DOUBLE))};
        reflection_query(&budget);
        if (SPI_execute_with_args(payload_sql, 2, payload_types, payload_values, NULL, true, (long)count + 1) != SPI_OK_SELECT ||
            SPI_processed != count || SPI_tuptable == NULL)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace selected descriptor owner set is incomplete")));
    }
    for (size_t index = 0u; index < count; ++index) {
        HeapTuple row = SPI_tuptable->vals[index];
        TupleDesc descriptor = SPI_tuptable->tupdesc;
        HeapTupleHeader node = DatumGetHeapTupleHeader(reflection_required(row, descriptor, 3));
        laplace_digest256 node_witness, root_witness, recorded_view, physicality_id;
        if (DatumGetInt64(reflection_required(row, descriptor, 1)) != (int64)index + 1)
            ereport(ERROR, (errmsg("Laplace descriptor candidate order differs")));
        reflection_owner_read(reflection_required(row, descriptor, 2), &owners[index], &budget);
        reflection_bytes(reflection_attribute(node, 1), &recorded_view, 32u);
        if (memcmp(recorded_view.bytes, nodes[index].view_id.bytes, 32u) != 0 ||
            memcmp(recorded_view.bytes, owners[index].view_id.bytes, 32u) != 0 ||
            reflection_positive_bigint(reflection_attribute(node, 2), true) != nodes[index].index)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor candidate ownership differs")));
        reflection_bytes(reflection_attribute(node, 3), &nodes[index].entity_id, 16u);
        reflection_bytes(reflection_attribute(node, 4), &nodes[index].witness, 32u);
        reflection_bytes(reflection_attribute(node, 5), &physicality_id, 32u);
        laplace_pg_physicality_read_record(reflection_attribute(node, 6), &nodes[index].physicality);
        reflection_reserve(&budget, 2u, toast_raw_datum_size(reflection_attribute(node, 7)));
        nodes[index].child_ids = DatumGetArrayTypePCopy(reflection_attribute(node, 7));
        reflection_bytes(reflection_required(row, descriptor, 4), &node_witness, 32u);
        reflection_bytes(reflection_required(row, descriptor, 5), &root_witness, 32u);
        if (memcmp(physicality_id.bytes, nodes[index].physicality.physicality_id.bytes, 32u) != 0 ||
            memcmp(nodes[index].entity_id.bytes, nodes[index].physicality.entity_id.bytes, 16u) != 0 ||
            memcmp(node_witness.bytes, nodes[index].witness.bytes, 32u) != 0 ||
            memcmp(root_witness.bytes, owners[index].root_witness.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor indexed identity differs from canonical entity or physicality")));
    }
    SPI_freetuptable(SPI_tuptable);
    qsort(owners, count, sizeof(*owners), reflection_owner_compare);
    for (size_t index = 0u; index < count; ++index)
        if (owner_count == 0u || reflection_owner_compare(&owners[owner_count - 1u], &owners[index]) != 0)
            owners[owner_count++] = owners[index];
    reflection_reserve(&budget, owner_count, sizeof(*source_ids));
    source_ids = palloc(owner_count * sizeof(*source_ids));
    reflection_reserve(&budget, owner_count, sizeof(*binding_ids));
    binding_ids = palloc(owner_count * sizeof(*binding_ids));
    for (size_t index = 0u; index < owner_count; ++index) {
        source_ids[index] = owners[index].source_id;
        binding_ids[index] = owners[index].binding_set_id;
        if (SIZE_MAX - external_count < owners[index].external_count) reflection_limit("retained external frontier overflow");
        external_count += owners[index].external_count;
    }
    qsort(binding_ids, owner_count, sizeof(*binding_ids), reflection_digest_compare);
    for (size_t index = 0u; index < owner_count; ++index)
        if (binding_set_count == 0u || reflection_digest_compare(&binding_ids[binding_set_count - 1u], &binding_ids[index]) != 0)
            binding_ids[binding_set_count++] = binding_ids[index];
    binding_sets = reflection_occurrences_retain(binding_ids, binding_set_count, &budget);
    for (size_t index = 0u; index < binding_set_count; ++index) {
        if (SIZE_MAX - external_count < binding_sets[index].exact_source_count)
            reflection_limit("retained occurrence source frontier overflow");
        external_count += binding_sets[index].exact_source_count;
    }
    for (size_t index = 0u; index < owner_count; ++index) {
        reflection_bindings key = {0};
        key.id = owners[index].binding_set_id;
        owners[index].bindings = bsearch(&key, binding_sets, binding_set_count,
            sizeof(*binding_sets), reflection_binding_set_compare);
        if (owners[index].bindings == NULL)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor occurrence set owner is absent")));
    }
    sources = reflection_sources(source_ids, owner_count, &budget);
    reflection_reserve(&budget, external_count, sizeof(*external));
    if (external_count == 0u || external_count > MaxAllocSize / sizeof(*external)) reflection_limit("retained external frontier addressability");
    external = palloc(external_count * sizeof(*external));
    size_t cursor = 0u;
    for (size_t index = 0u; index < owner_count; ++index) {
        memcpy(&external[cursor], owners[index].external, owners[index].external_count * sizeof(*external));
        cursor += owners[index].external_count;
    }
    for (size_t index = 0u; index < binding_set_count; ++index) {
        memcpy(&external[cursor], binding_sets[index].exact_sources,
            binding_sets[index].exact_source_count * sizeof(*external));
        cursor += binding_sets[index].exact_source_count;
    }
    external_count = reflection_known_sort(external, external_count);
    selected_sources = reflection_known_validate(context, external, external_count, &budget);
    occurrence_sources = reflection_source_union(sources, owner_count, selected_sources,
        external_count, &occurrence_source_count, &budget);
    for (size_t index = 0u; index < binding_set_count; ++index)
        reflection_occurrences_revalidate(&binding_sets[index], occurrence_sources, occurrence_source_count, &budget);
    calculations = reflection_calculations(owner_count, &budget);
    for (size_t index = 0u; index < owner_count; ++index) {
        const reflection_retained_owner* owner = &owners[index];
        if (sources[index].physicality.logical_count > owner->maximum_logical_count)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor source exceeds its original admitted work")));
        reflection_prepare(&owner->context, &sources[index], owner->maximum_requests,
            owner->maximum_operands, owner->maximum_carriers, owner->maximum_logical_count, &calculations[index], &budget);
    }
    atoms = reflection_atoms(context, calculations, owner_count, external, external_count, &atom_positions, &atom_count, &budget);
    reflection_source* atom_bodies = reflection_atom_bodies(atoms, atom_count, &budget);
    for (size_t index = 0u; index < owner_count; ++index) {
        const reflection_retained_owner* owner = &owners[index];
        reflection_calculation* calculation = &calculations[index];
        calculation->binding_scope = owner->bindings;
        calculation->binding_set_id = owner->binding_set_id;
        calculation->occurrence_bindings = owner->bindings->rows;
        calculation->occurrence_binding_count = owner->bindings->count;
        reflection_reference_prepare(&owner->context, &context->resource_grant, &sources[index], occurrence_sources,
            occurrence_source_count, external, external_count, atom_positions, atoms, atom_bodies, atom_count,
            UINT64_C(65536), calculation, &budget);
        reflection_calculate(&owner->context, &context->resource_grant, owner->external, owner->external_count,
            atom_positions, atoms, atom_count, UINT64_C(65536), calculation, &budget);
        const laplace_composition_result* root = &calculation->results[calculation->plan_view.root_result_index];
        if (memcmp(calculation->view_id.bytes, owner->view_id.bytes, 32u) != 0 ||
            memcmp(calculation->summary.input_fingerprint.bytes, owner->input_fingerprint.bytes, 32u) != 0 ||
            memcmp(calculation->plan_view.recipe_fingerprint.bytes, owner->recipe.bytes, 32u) != 0 ||
            memcmp(root->entity_id.bytes, owner->root_id.bytes, 16u) != 0 ||
            memcmp(root->identity_witness.bytes, owner->root_witness.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor original derivation receipt differs from native reconstruction")));
        reflection_complete_known(calculation, &budget);
        reflection_generated_prepare(calculation, &budget);
    }
    reflection_reserve(&budget, count, sizeof(**output));
    *output = palloc0(count * sizeof(**output));
    for (size_t index = 0u; index < count; ++index) {
        reflection_retained_owner key = {0};
        const reflection_retained_owner* owner;
        laplace_pg_physicality_entity_view generated;
        ArrayType* children;
        key.view_id = nodes[index].view_id;
        owner = bsearch(&key, owners, owner_count, sizeof(*owners), reflection_owner_compare);
        if (owner == NULL || nodes[index].index > SIZE_MAX ||
            !reflection_result_view(&calculations[owner - owners], (size_t)nodes[index].index, &generated))
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor node no longer has a native view")));
        if (memcmp(generated.physicality.physicality_id.bytes, nodes[index].physicality.physicality_id.bytes, 32u) != 0 ||
            memcmp(generated.identity_witness.bytes, nodes[index].witness.bytes, 32u) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor node body or witness differs from its native derivation")));
        reflection_reserve(&budget, generated.carrier_count, 128u);
        children = reflection_child_ids(generated.carriers, generated.carrier_count);
        if (VARSIZE(children) != VARSIZE(nodes[index].child_ids) ||
            memcmp(children, nodes[index].child_ids, (size_t)VARSIZE(children)) != 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED), errmsg("Laplace descriptor child index differs from its native ordered trajectory")));
        laplace_trajectory_carrier* carriers = palloc(generated.carrier_count * sizeof(*carriers));
        memcpy(carriers, generated.carriers, generated.carrier_count * sizeof(*carriers));
        generated.carriers = carriers;
        /* The retained selector array is already PG-owned and independently
         * authenticated; it outlives the native working set below. */
        generated.external_known = owner->external;
        generated.external_count = owner->external_count;
        (*output)[index] = generated;
    }
    for (size_t index = 0u; index < owner_count; ++index) reflection_destroy(&calculations[index]);
    *output_count = count;
    *operations = budget.operations;
    if (budget.operations > LAPLACE_PG_PHYSICALITY_ENTITY_READ_MAX_OPERATIONS)
        ereport(ERROR, (errmsg("Laplace descriptor fixed database-operation contract drifted")));
}

static void reflection_read_views(
    const laplace_framework_context* context, const laplace_digest256* required_view_id, const laplace_id128* entity_ids,
    const laplace_digest256* selected, size_t entity_count, uint32_t relation_mask,
    uint64_t maximum_rows, uint64_t maximum_memory_bytes, uint64_t maximum_operations,
    laplace_pg_physicality_entity_view** output, size_t* output_count, uint64_t* operations) {
    MemoryContext caller = CurrentMemoryContext;
    if (output == NULL || output_count == NULL || operations == NULL)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Laplace descriptor reader requires output slots")));
    if (SPI_connect() != SPI_OK_CONNECT) ereport(ERROR, (errmsg("Laplace descriptor reader could not connect")));
    MemoryContextSwitchTo(caller);
    PG_TRY();
    {
        reflection_read_views_connected(context, required_view_id, entity_ids, selected, entity_count, relation_mask,
            maximum_rows, maximum_memory_bytes, maximum_operations, output, output_count, operations);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(caller);
        SPI_finish();
        PG_RE_THROW();
    }
    PG_END_TRY();
    if (SPI_finish() != SPI_OK_FINISH) ereport(ERROR, (errmsg("Laplace descriptor read could not finish")));
}

void laplace_pg_physicality_entity_candidates(
    const laplace_framework_context* context, const laplace_id128* source_ids, size_t source_count,
    uint32_t relation_mask, uint64_t maximum_rows, uint64_t maximum_memory_bytes,
    uint64_t maximum_database_operations, laplace_pg_physicality_entity_view** views,
    size_t* view_count, uint64_t* database_operations) {
    reflection_read_views(context, NULL, source_ids, NULL, source_count, relation_mask, maximum_rows,
        maximum_memory_bytes, maximum_database_operations, views, view_count, database_operations);
}

void laplace_pg_physicality_entity_resolve(
    const laplace_framework_context* context, const laplace_id128* entity_ids,
    const laplace_digest256* selected_physicality_ids, size_t entity_count,
    uint64_t maximum_rows, uint64_t maximum_memory_bytes, uint64_t maximum_database_operations,
    laplace_pg_physicality_entity_view** views, size_t* view_count, uint64_t* database_operations) {
    reflection_read_views(context, NULL, entity_ids, selected_physicality_ids, entity_count, 2u, maximum_rows,
        maximum_memory_bytes, maximum_database_operations, views, view_count, database_operations);
}

void laplace_pg_physicality_entity_resolve_scoped(
    const laplace_framework_context* context, const laplace_digest256* required_view_id,
    const laplace_id128* entity_ids, const laplace_digest256* selected_physicality_ids, size_t entity_count,
    uint64_t maximum_rows, uint64_t maximum_memory_bytes, uint64_t maximum_database_operations,
    laplace_pg_physicality_entity_view** views, size_t* view_count, uint64_t* database_operations) {
    reflection_read_views(context, required_view_id, entity_ids, selected_physicality_ids, entity_count, 2u, maximum_rows,
        maximum_memory_bytes, maximum_database_operations, views, view_count, database_operations);
}

void laplace_pg_physicality_entity_resolve_owner(
    const laplace_framework_context* context, const laplace_digest256* exact_view_id,
    uint64_t maximum_memory_bytes, uint64_t maximum_database_operations,
    laplace_pg_physicality_entity_view** views, size_t* view_count, uint64_t* database_operations) {
    static const laplace_digest256 zero = {{0}};
    static const laplace_id128 unused = {{0}};
    if (exact_view_id == NULL || memcmp(exact_view_id->bytes, zero.bytes, 32u) == 0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE), errmsg("Laplace original source lookup requires an exact retained view owner")));
    reflection_read_views(context, exact_view_id, &unused, NULL, 1u, 32u, 1u,
        maximum_memory_bytes, maximum_database_operations, views, view_count, database_operations);
}
