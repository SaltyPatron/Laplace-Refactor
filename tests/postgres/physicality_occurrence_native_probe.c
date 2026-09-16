/* Execute the production PG adapter's native interval helpers. Section GC
 * excludes SQL/SPI entrypoints. Allocation/error/sort shims below are the only
 * PostgreSQL runtime substitutions; this is not PostgreSQL execution evidence. */
#ifndef REFLECTION_ADAPTER_SOURCE
#define REFLECTION_ADAPTER_SOURCE "../../integrations/postgresql/extension/src/physicality_entity_pg.c"
#endif
#include REFLECTION_ADAPTER_SOURCE
#include "../context_fixture.h"

#undef printf
#undef fprintf
#undef vfprintf
#undef snprintf
#undef vsnprintf
#undef qsort

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned assertions;
#define CHECK(condition) do { ++assertions; if (!(condition)) { \
    fprintf(stderr, "assertion %u at line %d failed: %s\n", assertions, __LINE__, #condition); exit(1); \
} } while (0)

void* palloc(Size bytes) { void* value = malloc(bytes == 0u ? 1u : bytes); CHECK(value != NULL); return value; }
void* palloc0(Size bytes) { void* value = calloc(1u, bytes == 0u ? 1u : bytes); CHECK(value != NULL); return value; }
void pfree(void* pointer) { free(pointer); }
void pg_qsort(void* values, size_t count, size_t width, int (*compare)(const void*, const void*)) {
    qsort(values, count, width, compare);
}
bool errstart(int level, const char* domain) { (void)level; (void)domain; return true; }
bool errstart_cold(int level, const char* domain) { return errstart(level, domain); }
int errcode(int code) { return code; }
int errmsg(const char* format, ...) {
    va_list arguments; va_start(arguments, format); vfprintf(stderr, format, arguments); va_end(arguments); return 0;
}
int errdetail(const char* format, ...) {
    va_list arguments; va_start(arguments, format); vfprintf(stderr, format, arguments); va_end(arguments); return 0;
}
void errfinish(const char* filename, int line, const char* function) {
    fprintf(stderr, "\nPG adapter error at %s:%d (%s)\n", filename, line, function); exit(2);
}

typedef struct proof_form {
    reflection_source source;
    laplace_composition_known_entity known;
    laplace_composition_working_set* working_set;
} proof_form;

static reflection_budget proof_budget(void) {
    reflection_budget result = {0};
    result.memory_limit = UINT64_C(512) * 1024u * 1024u;
    result.operation_limit = 0u; result.logical_limit = UINT64_C(10000000); result.read_only = true;
    return result;
}

static proof_form proof_atom(const laplace_framework_context* context, uint32_t position, double scale) {
    proof_form result = {0};
    CHECK(laplace_identity_codepoint_witness(position, &result.known.entity_id,
        &result.known.identity_witness) == LAPLACE_IDENTITY_OK);
    result.known.atom = position; result.known.has_atom = 1u;
    result.known.centroid.component[position % 4u] = scale;
    CHECK(laplace_persistence_atomic_point_physicality(&result.known.entity_id, 1u,
        &context->epochs[0], &context->epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY],
        &result.known.centroid, &result.source.physicality) == LAPLACE_PERSISTENCE_OK);
    result.known.physicality_id = result.source.physicality.physicality_id;
    result.source.witness = result.known.identity_witness;
    return result;
}

static proof_form proof_composition(const laplace_framework_context* context,
    const proof_form* atom, uint64_t count) {
    proof_form result = {0};
    laplace_composition_operand operand = {0u, count, 0u, LAPLACE_COMPOSITION_REFERENCE_KNOWN_ENTITY, 0u};
    laplace_composition_request request = {0u, 1u, 1u, 1u, 0u, context->epochs[0],
        context->epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY], {{0}}};
    laplace_composition_working_set_input input = {0};
    input.context = context; input.source_fingerprint = &context->epochs[0];
    input.calculation_recipe_fingerprint = &context->epochs[0];
    input.known_entities = &atom->known; input.known_entity_count = 1u;
    input.operands = &operand; input.operand_count = 1u; input.requests = &request;
    input.request_count = 1u; input.preferred_batch_bytes = 4096u;
    CHECK(laplace_composition_working_set_create(&input, &result.working_set) == LAPLACE_COMPOSITION_OK);
    size_t result_count;
    const laplace_composition_result* calculated = laplace_composition_working_set_results(result.working_set, &result_count);
    CHECK(result_count == 1u);
    CHECK(laplace_composition_working_set_physicality_candidate_get(result.working_set, 0u,
        &result.source.physicality) == LAPLACE_COMPOSITION_OK);
    const laplace_trajectory_carrier* carriers;
    CHECK(laplace_composition_working_set_trajectory_candidate_view_get(result.working_set, 0u,
        &carriers, &result.source.carrier_count) == LAPLACE_COMPOSITION_OK);
    result.source.carriers = (laplace_trajectory_carrier*)carriers;
    result.source.witness = calculated[0].identity_witness;
    result.known.entity_id = calculated[0].entity_id; result.known.identity_witness = calculated[0].identity_witness;
    result.known.physicality_id = calculated[0].physicality_id; result.known.centroid = calculated[0].centroid;
    result.known.tier_floor = calculated[0].tier_floor;
    return result;
}

static proof_form proof_singleton(const proof_form* child) {
    proof_form result = *child; result.working_set = NULL;
    result.source.carriers = palloc(sizeof(*result.source.carriers)); result.source.carrier_count = 1u;
    CHECK(laplace_trajectory_composition_encode(&child->known.entity_id, 1u, 1u,
        (uint64_t)child->known.tier_floor << LAPLACE_TRAJECTORY_TIER_SHIFT,
        result.source.carriers) == LAPLACE_TRAJECTORY_OK);
    result.source.physicality.logical_count = 1u; result.source.physicality.vertex_count = 1u;
    result.source.physicality.radius = 0.0;
    CHECK(laplace_persistence_trajectory_fingerprint(result.source.carriers, 1u,
        &result.source.physicality.trajectory_fingerprint) == LAPLACE_PERSISTENCE_OK);
    CHECK(laplace_persistence_physicality_identify(&result.source.physicality,
        &result.source.physicality.physicality_id) == LAPLACE_PERSISTENCE_OK);
    result.known.physicality_id = result.source.physicality.physicality_id;
    /* The wrapper adds physical depth; canonical E content tier is unchanged. */
    laplace_physicality_entity_validation validation;
    CHECK(laplace_physicality_entity_record_validate(&result.source.physicality, result.source.carriers,
        1u, 1u, 1u, &validation) == LAPLACE_PHYSICALITY_ENTITY_OK);
    CHECK(validation.witness_available == 0u);
    return result;
}

/* Consume the actual native descriptor plan directly; database atom lookup and
 * reference-view admission remain outside this helper-only fixture. */
static reflection_calculation proof_descriptor(const laplace_framework_context* context,
    const proof_form* source, const proof_form* const* external, size_t external_count,
    const laplace_digest256* scope_id, reflection_budget* budget) {
    reflection_calculation result = {0};
    reflection_prepare(context, &source->source, 10000u, 100000u, 1000u, budget->logical_limit, &result, budget);
    result.binding_set_id = *scope_id;
    const laplace_physicality_entity_plan_view* view = &result.plan_view;
    size_t known_count = (size_t)(view->external_entity_count + view->atom_count);
    laplace_composition_known_entity* known = palloc0(known_count * sizeof(*known));
    for (size_t i = 0u; i < view->external_entity_count; ++i) {
        size_t found = 0u;
        for (; found < external_count; ++found)
            if (laplace_identity_equal(&view->external_entity_ids[i], &external[found]->known.entity_id)) break;
        CHECK(found < external_count); known[i] = external[found]->known;
    }
    for (size_t i = 0u; i < view->atom_count; ++i)
        known[view->external_entity_count + i] = proof_atom(context, view->atom_positions[i], 1.0).known;
    laplace_composition_working_set_input input = {0};
    input.context = context; input.source_fingerprint = &view->physicality_record_id;
    input.calculation_recipe_fingerprint = &view->recipe_fingerprint;
    input.known_entities = known; input.known_entity_count = known_count;
    input.operands = view->operands; input.operand_count = view->operand_count;
    input.requests = view->requests; input.request_count = view->request_count; input.preferred_batch_bytes = 4096u;
    CHECK(laplace_composition_working_set_create(&input, &result.working_set) == LAPLACE_COMPOSITION_OK);
    CHECK(laplace_composition_working_set_summary_get(result.working_set, &result.summary) == LAPLACE_COMPOSITION_OK);
    result.results = laplace_composition_working_set_results(result.working_set, &result.result_count);
    CHECK(result.result_count == view->request_count); CHECK(result.summary.occurrence_count == 0u);
    result.all_known = known; result.all_known_count = known_count;
    reflection_generated_prepare(&result, budget);
    return result;
}

static laplace_physicality_occurrence_binding proof_interval(const proof_form* parent,
    const proof_form* child, uint64_t first, uint64_t count) {
    laplace_composition_occurrence occurrence;
    CHECK(laplace_trajectory_composition_decode_one(parent->source.carriers, 1u, &occurrence) == LAPLACE_TRAJECTORY_OK);
    laplace_physicality_occurrence_binding result = {0};
    result.parent_physicality_id = parent->known.physicality_id;
    result.entity_id = child->known.entity_id; result.selected_physicality_id = child->known.physicality_id;
    result.first_logical_ordinal = first; result.logical_count = count; result.metadata = occurrence.metadata;
    result.version = LAPLACE_PHYSICALITY_OCCURRENCE_BINDING_VERSION;
    return result;
}

static reflection_bindings proof_complete(laplace_physicality_occurrence_binding* rows, size_t row_count,
    const proof_form* const* forms, size_t form_count, reflection_budget* budget) {
    reflection_bindings result = {0}; result.rows = rows; result.count = row_count;
    const reflection_source** sources = palloc(form_count * sizeof(*sources));
    laplace_composition_known_entity* known = palloc(form_count * sizeof(*known));
    for (size_t i = 0u; i < form_count; ++i) { sources[i] = &forms[i]->source; known[i] = forms[i]->known; }
    qsort(rows, row_count, sizeof(*rows), reflection_occurrence_compare);
    qsort(sources, form_count, sizeof(*sources), reflection_source_pointer_compare);
    reflection_occurrences_select_sources(&result, known, form_count, false, budget);
    reflection_occurrences_validate(&result, sources, form_count, budget);
    return result;
}

/* Execute the actual complete reference/descriptor helpers with authenticated
 * native bodies. Only the mapped atom fetch is supplied as an explicit fixture;
 * no database call is reachable from this path. */
static reflection_calculation proof_integrated_descriptor(
    const laplace_framework_context* context, const proof_form* source,
    const proof_form* const* forms, size_t form_count, reflection_bindings* scope,
    reflection_budget* budget) {
    reflection_calculation result = {0};
    reflection_prepare(context, &source->source, 10000u, 100000u, 1000u, budget->logical_limit, &result, budget);
    result.binding_scope = scope; result.binding_set_id = scope->id;
    result.occurrence_bindings = scope->rows; result.occurrence_binding_count = scope->count;
    const reflection_source** sources = palloc(form_count * sizeof(*sources));
    laplace_composition_known_entity* external = palloc(form_count * sizeof(*external));
    size_t capacity = (size_t)result.plan_view.atom_count + form_count;
    uint32_t* positions = palloc(capacity * sizeof(*positions));
    size_t position_count = (size_t)result.plan_view.atom_count;
    memcpy(positions, result.plan_view.atom_positions, position_count * sizeof(*positions));
    for (size_t index = 0u; index < form_count; ++index) {
        sources[index] = &forms[index]->source; external[index] = forms[index]->known;
        if (forms[index]->known.has_atom == 1u) positions[position_count++] = forms[index]->known.atom;
    }
    qsort(sources, form_count, sizeof(*sources), reflection_source_pointer_compare);
    size_t external_count = reflection_known_sort(external, form_count);
    qsort(positions, position_count, sizeof(*positions), reflection_position_compare);
    size_t unique = 0u;
    for (size_t index = 0u; index < position_count; ++index)
        if (unique == 0u || positions[unique - 1u] != positions[index]) positions[unique++] = positions[index];
    position_count = unique;
    reflection_source* bodies = palloc(position_count * sizeof(*bodies));
    laplace_composition_known_entity* atoms = palloc(position_count * sizeof(*atoms));
    for (size_t index = 0u; index < position_count; ++index) {
        proof_form atom = proof_atom(context, positions[index], 1.0);
        atoms[index] = atom.known; bodies[index] = atom.source;
    }
    reflection_reference_prepare(context, &context->resource_grant, &source->source, sources, form_count,
        external, external_count, positions, atoms, bodies, position_count, 4096u, &result, budget);
    reflection_calculate(context, &context->resource_grant, external, external_count,
        positions, atoms, position_count, 4096u, &result, budget);
    reflection_complete_known(&result, budget);
    reflection_generated_prepare(&result, budget);
    CHECK(result.canonical_external != NULL);
    CHECK(memcmp(result.retained_source.physicality_id.bytes, source->known.physicality_id.bytes, 32u) == 0);
    return result;
}

int main(void) {
    laplace_framework_context context = laplace_test_context(0u);
    context.resource_grant.memory_bytes = UINT64_C(64) * 1024u * 1024u;
    reflection_budget budget = proof_budget();
    proof_form zero = proof_atom(&context, '0', 0.25), canonical_zero = proof_atom(&context, '0', 1.0);
    const proof_form* external_zero[] = {&zero}; laplace_digest256 empty_scope = {{0}};
    reflection_calculation descriptor = proof_descriptor(&context, &zero, external_zero, 1u, &empty_scope, &budget);
    size_t original_edges = 0u, canonical_edges = 0u;
    for (size_t i = 0u; i < descriptor.generated_occurrence_binding_count; ++i) {
        const laplace_physicality_occurrence_binding* row = &descriptor.generated_occurrence_bindings[i];
        if (!laplace_identity_equal(&row->entity_id, &zero.known.entity_id)) continue;
        if (memcmp(row->selected_physicality_id.bytes, zero.known.physicality_id.bytes, 32u) == 0) ++original_edges;
        if (memcmp(row->selected_physicality_id.bytes, canonical_zero.known.physicality_id.bytes, 32u) == 0) ++canonical_edges;
    }
    CHECK(original_edges > 0u); CHECK(canonical_edges > 0u);

    /* Ordinary composition keeps the exact older-epoch atom as its input;
     * only the new parent is placed under the current request epoch. */
    laplace_framework_context old_context = context;
    old_context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes[0] ^= 1u;
    proof_form old_atom = proof_atom(&old_context, 'x', 1.0);
    proof_form new_parent = proof_composition(&context, &old_atom, 2u);
    CHECK(memcmp(old_atom.source.physicality.geometry_epoch.bytes,
        new_parent.source.physicality.geometry_epoch.bytes, 32u) != 0);
    laplace_physicality_occurrence_binding historical_rows[] = {
        proof_interval(&new_parent, &old_atom, 1u, 2u)};
    const proof_form* historical_forms[] = {&new_parent, &old_atom};
    reflection_bindings cross_epoch = proof_complete(historical_rows, 1u,
        historical_forms, 2u, &budget);
    CHECK(cross_epoch.parent_count == 1u && cross_epoch.count == 1u);
    CHECK(memcmp(cross_epoch.rows[0].selected_physicality_id.bytes,
        old_atom.known.physicality_id.bytes, 32u) == 0);
    laplace_composition_working_set_destroy(&new_parent.working_set);

    proof_form a = proof_atom(&context, 'a', 1.0), b = proof_atom(&context, 'b', 1.0);
    proof_form aa = proof_composition(&context, &a, 2u), bb = proof_composition(&context, &b, 2u);
    proof_form wrapper = proof_singleton(&aa);
    const proof_form* small_forms[] = {&a, &aa, &wrapper};
    const proof_form* mixed_forms[] = {&a, &aa, &wrapper, &b, &bb};
    laplace_physicality_occurrence_binding small_rows[] = {proof_interval(&wrapper, &aa, 1u, 1u), proof_interval(&aa, &a, 1u, 2u)};
    laplace_physicality_occurrence_binding mixed_rows[] = {small_rows[0], small_rows[1], proof_interval(&bb, &b, 1u, 2u)};
    laplace_physicality_occurrence_binding split_rows[] = {proof_interval(&wrapper, &aa, 1u, 1u),
        proof_interval(&aa, &a, 1u, 1u), proof_interval(&aa, &a, 2u, 1u), proof_interval(&bb, &b, 1u, 2u)};
    reflection_bindings small = proof_complete(small_rows, 2u, small_forms, 3u, &budget);
    reflection_bindings mixed = proof_complete(mixed_rows, 3u, mixed_forms, 5u, &budget);
    reflection_bindings split = proof_complete(split_rows, 4u, mixed_forms, 5u, &budget);
    const reflection_source* all_sources[] = {&a.source, &aa.source, &wrapper.source, &b.source, &bb.source};
    qsort(all_sources, 5u, sizeof(*all_sources), reflection_source_pointer_compare);
    reflection_scope_index small_index = reflection_occurrence_scope_index(&small, &budget);
    reflection_scope_index mixed_index = reflection_occurrence_scope_index(&mixed, &budget);
    reflection_scope_index split_index = reflection_occurrence_scope_index(&split, &budget);
    reflection_bindings* small_scope = reflection_occurrence_scope(&small_index, &wrapper.source, all_sources, 5u, &budget);
    reflection_bindings* mixed_scope = reflection_occurrence_scope(&mixed_index, &wrapper.source, all_sources, 5u, &budget);
    reflection_bindings* split_scope = reflection_occurrence_scope(&split_index, &wrapper.source, all_sources, 5u, &budget);
    CHECK(small_scope->parent_count == 2u && small_scope->count == 2u);
    CHECK(mixed_scope->parent_count == 2u && mixed_scope->count == 2u);
    CHECK(split_scope->parent_count == 2u && split_scope->count == 3u);
    CHECK(memcmp(small_scope->id.bytes, mixed_scope->id.bytes, 32u) == 0);
    CHECK(memcmp(mixed_scope->id.bytes, split_scope->id.bytes, 32u) != 0);
    reflection_bindings* child_scope = reflection_occurrence_scope(&mixed_index, &aa.source, all_sources, 5u, &budget);
    CHECK(child_scope->parent_count == 1u && child_scope->count == 1u);
    reflection_bindings* repeated = reflection_occurrence_scope(&mixed_index, &wrapper.source, all_sources, 5u, &budget);
    CHECK(memcmp(repeated->id.bytes, mixed_scope->id.bytes, 32u) == 0);
    reflection_bindings* atomic_scope = reflection_occurrence_scope(&mixed_index, &a.source, all_sources, 5u, &budget);
    CHECK(atomic_scope->parent_count == 0u && atomic_scope->count == 0u);
    const proof_form* external_aa[] = {&aa, &a};
    reflection_calculation whole_descriptor = proof_descriptor(&context, &aa, external_aa, 2u, &mixed_scope->id, &budget);
    reflection_calculation split_descriptor = proof_descriptor(&context, &aa, external_aa, 2u, &split_scope->id, &budget);
    const laplace_composition_result* whole_root = &whole_descriptor.results[whole_descriptor.plan_view.root_result_index];
    const laplace_composition_result* split_root = &split_descriptor.results[split_descriptor.plan_view.root_result_index];
    CHECK(laplace_identity_equal(&whole_root->entity_id, &split_root->entity_id));
    CHECK(memcmp(whole_root->identity_witness.bytes, split_root->identity_witness.bytes, 32u) == 0);
    CHECK(memcmp(whole_root->physicality_id.bytes, split_root->physicality_id.bytes, 32u) == 0);
    reflection_calculation canonical_descriptor = proof_integrated_descriptor(&context, &zero,
        external_zero, 1u, atomic_scope, &budget);
    size_t exact_canonical_edges = 0u;
    for (size_t index = 0u; index < canonical_descriptor.generated_occurrence_binding_count; ++index) {
        const laplace_physicality_occurrence_binding* row = &canonical_descriptor.generated_occurrence_bindings[index];
        if (!laplace_identity_equal(&row->entity_id, &zero.known.entity_id)) continue;
        CHECK(memcmp(row->selected_physicality_id.bytes, canonical_zero.known.physicality_id.bytes, 32u) == 0);
        ++exact_canonical_edges;
    }
    CHECK(exact_canonical_edges > 0u);
    CHECK(canonical_descriptor.reference_result_count == 0u);
    CHECK(canonical_descriptor.canonical_known_count > 0u);
    size_t canonical_atom_tuples = 0u;
    for (size_t index = 0u; index < canonical_descriptor.canonical_known_count; ++index) {
        const laplace_composition_known_entity* known = &canonical_descriptor.canonical_known[index];
        if (!laplace_identity_equal(&known->entity_id, &zero.known.entity_id)) continue;
        CHECK(known->has_atom == 1u && known->tier_floor == 0u && known->atom == '0');
        CHECK(memcmp(known->physicality_id.bytes, canonical_zero.known.physicality_id.bytes, 32u) == 0);
        ++canonical_atom_tuples;
    }
    CHECK(canonical_atom_tuples == 1u);
    reflection_calculation canonical_small = proof_integrated_descriptor(&context, &wrapper, small_forms, 3u, small_scope, &budget);
    reflection_calculation canonical_mixed = proof_integrated_descriptor(&context, &wrapper, mixed_forms, 5u, mixed_scope, &budget);
    reflection_calculation canonical_split = proof_integrated_descriptor(&context, &wrapper, mixed_forms, 5u, split_scope, &budget);
    CHECK(canonical_small.reference_result_count > 0u);
    CHECK(memcmp(canonical_small.view_id.bytes, canonical_mixed.view_id.bytes, 32u) == 0);
    CHECK(memcmp(canonical_mixed.view_id.bytes, canonical_split.view_id.bytes, 32u) != 0);
    const laplace_composition_result* reference_root = &canonical_small.results[canonical_small.plan_view.root_result_index];
    const laplace_composition_result* split_reference_root = &canonical_split.results[canonical_split.plan_view.root_result_index];
    CHECK(laplace_identity_equal(&reference_root->entity_id, &split_reference_root->entity_id));
    CHECK(memcmp(reference_root->identity_witness.bytes, split_reference_root->identity_witness.bytes, 32u) == 0);
    CHECK(memcmp(reference_root->physicality_id.bytes, split_reference_root->physicality_id.bytes, 32u) == 0);
    size_t reference_nodes = 0u;
    for (size_t index = canonical_small.result_count; index < reflection_result_count(&canonical_small); ++index) {
        laplace_pg_physicality_entity_view node = {0};
        CHECK(reflection_result_view(&canonical_small, index, &node));
        CHECK(memcmp(node.physicality.recipe_fingerprint.bytes, canonical_small.reference_view.recipe_fingerprint.bytes, 32u) == 0);
        CHECK(memcmp(node.owner_recipe_fingerprint.bytes, canonical_small.plan_view.recipe_fingerprint.bytes, 32u) == 0);
        CHECK(memcmp(node.physicality.geometry_epoch.bytes, context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY].bytes, 32u) == 0);
        CHECK(node.generated_occurrence_binding_count > 0u);
        CHECK(node.all_known_count == canonical_small.complete_known_count);
        CHECK(node.canonical_known_count == canonical_small.canonical_known_count);
        ++reference_nodes;
    }
    CHECK(reference_nodes == canonical_small.reference_result_count);
    reflection_destroy(&canonical_small);
    reflection_calculation reconstructed = proof_integrated_descriptor(&context, &wrapper, small_forms, 3u, small_scope, &budget);
    CHECK(memcmp(reconstructed.view_id.bytes, canonical_mixed.view_id.bytes, 32u) == 0);
    CHECK(memcmp(reconstructed.generated_binding_receipt.bytes, canonical_mixed.generated_binding_receipt.bytes, 32u) == 0);
    /* P hashes the stored E carriers and geometry. A selected child can have
     * the same centroid and a different physicality recipe, so the historical
     * and generated parent bodies legitimately coincide while selections do not. */
    proof_form alternate_a = a;
    alternate_a.source.physicality.recipe_fingerprint.bytes[0] ^= 0x80u;
    CHECK(laplace_persistence_physicality_identify(&alternate_a.source.physicality,
        &alternate_a.source.physicality.physicality_id) == LAPLACE_PERSISTENCE_OK);
    alternate_a.known.physicality_id = alternate_a.source.physicality.physicality_id;
    laplace_pg_physicality_entity_view reference_body = {0};
    CHECK(reflection_result_view(&canonical_mixed, canonical_mixed.result_count, &reference_body));
    proof_form coincident_parent = {0};
    coincident_parent.source.physicality = reference_body.physicality;
    coincident_parent.source.carriers = (laplace_trajectory_carrier*)reference_body.carriers;
    coincident_parent.source.carrier_count = reference_body.carrier_count;
    coincident_parent.source.witness = reference_body.identity_witness;
    coincident_parent.known.entity_id = reference_body.physicality.entity_id;
    coincident_parent.known.identity_witness = reference_body.identity_witness;
    coincident_parent.known.physicality_id = reference_body.physicality.physicality_id;
    coincident_parent.known.centroid = reference_body.physicality.centroid;
    coincident_parent.known.tier_floor = canonical_mixed.reference_results[0].tier_floor;
    const proof_form* coincident_forms[] = {&alternate_a, &coincident_parent};
    laplace_physicality_occurrence_binding coincident_rows[] = {proof_interval(&coincident_parent, &alternate_a, 1u, 2u)};
    reflection_bindings coincident_scope = proof_complete(coincident_rows, 1u, coincident_forms, 2u, &budget);
    reflection_calculation coincident = proof_integrated_descriptor(&context, &coincident_parent,
        coincident_forms, 2u, &coincident_scope, &budget);
    size_t coincident_distinct_selections = 0u;
    for (size_t index = 0u; index < coincident.generated_occurrence_binding_count; ++index) {
        const laplace_physicality_occurrence_binding* generated = &coincident.generated_occurrence_bindings[index];
        if (memcmp(generated->parent_physicality_id.bytes, coincident_parent.known.physicality_id.bytes, 32u) != 0) continue;
        CHECK(laplace_identity_equal(&generated->entity_id, &coincident_rows[0].entity_id));
        CHECK(memcmp(generated->selected_physicality_id.bytes, a.known.physicality_id.bytes, 32u) == 0);
        CHECK(memcmp(generated->selected_physicality_id.bytes, coincident_rows[0].selected_physicality_id.bytes, 32u) != 0);
        ++coincident_distinct_selections;
    }
    CHECK(coincident_distinct_selections == 1u);
    CHECK(memcmp(coincident.generated_binding_receipt.bytes, coincident.binding_set_id.bytes, 32u) != 0);
    CHECK(budget.operations == 0u);
    printf("{\"status\":\"passed\",\"assertions\":%u,\"original_zero_edges\":%zu,\"canonical_zero_edges\":%zu,"
        "\"sparse_scope_preserves_singleton_and_neighbor_independence\":true,\"split_intervals_change_scope_only\":true,"
        "\"same_parent_body_distinct_selection_contexts\":true,\"canonical_reference_integration\":true,\"singleton_content_tier_preserved\":true,\"reconstructed_reference_nodes_verified\":true,"
        "\"postgresql_execution\":false,\"spi_operations\":0}\n", assertions, original_edges, canonical_edges);
    return 0;
}
