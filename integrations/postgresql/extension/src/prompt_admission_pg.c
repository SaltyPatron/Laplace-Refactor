#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "blake3.h"
#include "composition_pg.h"
#include "content_admission_pg.h"
#include "fmgr.h"
#include "laplace/cognition_prompt_admission.h"
#include "laplace/framework.h"
#include "laplace_pg_internal.h"
#include "persistence_pg.h"
#include "prompt_admission_pg.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_prompt_admit);

void laplace_pg_prompt_atom_provider_create(
    const laplace_framework_context* context,
    laplace_pg_prompt_atom_provider_state* state,
    laplace_cognition_prompt_atom_provider_v1* provider) {
    laplace_content_atom_provider_v1 content_provider;

    if (provider == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace prompt atom provider output is null")));
    }

    memset(&content_provider, 0, sizeof(content_provider));
    laplace_pg_content_atom_provider_create(context, state, &content_provider);
    memset(provider, 0, sizeof(*provider));
    provider->state = content_provider.state;
    provider->provider_fingerprint = content_provider.provider_fingerprint;
    provider->resolve = content_provider.resolve;
    provider->abi_major = LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_PROMPT_ATOM_PROVIDER_ABI_MINOR;
}

void laplace_pg_prompt_admission_providers_create(
    const laplace_framework_context* context,
    laplace_pg_prompt_atom_provider_state* atom_state,
    laplace_cognition_prompt_atom_provider_v1* atom_provider,
    laplace_composition_presence_provider_v1* presence_provider) {
    if (presence_provider == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace prompt admission presence provider output is null")));
    }
    laplace_pg_prompt_atom_provider_create(context, atom_state, atom_provider);
    laplace_pg_composition_presence_provider(presence_provider);
}

/* Raw prompt admission does not invent a parser or language segmentation. This
 * provider authorizes the exact text root only; the canonical composition bridge
 * decodes the actual UTF-8 bytes into the already-active Unicode Tier-0 atoms.
 * UAX/grammar providers may later add witnessed structural spans without changing
 * the trunk identity produced here. */
static laplace_decomposition_status prompt_root_applicable(
    void* state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    int* applicable) {
    (void)state;
    (void)content;
    if (span == NULL || applicable == NULL) {
        return LAPLACE_DECOMPOSITION_INVALID_ARGUMENT;
    }
    *applicable = span->depth == 0u ? 1 : 0;
    return LAPLACE_DECOMPOSITION_OK;
}

static laplace_decomposition_status prompt_root_apply(
    void* state,
    const laplace_decomposition_content* content,
    const laplace_decomposition_span* span,
    laplace_decomposition_emit_fn emit,
    void* emit_state) {
    (void)state;
    (void)content;
    (void)span;
    (void)emit;
    (void)emit_state;
    return LAPLACE_DECOMPOSITION_OK;
}

static laplace_decomposition_provider_v1 prompt_root_provider(void) {
    static const char domain[] = "laplace-postgresql-exact-prompt-root-v1";
    laplace_decomposition_provider_v1 provider;
    blake3_hasher hasher;
    memset(&provider, 0, sizeof(provider));
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_finalize(
        &hasher,
        provider.provider_fingerprint.bytes,
        sizeof(provider.provider_fingerprint.bytes));
    provider.applicable = prompt_root_applicable;
    provider.apply = prompt_root_apply;
    provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return provider;
}

typedef struct prompt_admission_owner {
    laplace_cognition_prompt_admission* admission;
    MemoryContextCallback cleanup;
} prompt_admission_owner;

static void prompt_admission_release(void* opaque) {
    prompt_admission_owner* owner = (prompt_admission_owner*)opaque;
    if (owner != NULL) {
        laplace_cognition_prompt_admission_destroy(&owner->admission);
    }
}

static void prompt_read_scope(
    HeapTupleHeader tuple,
    laplace_cognition_prompt_admission_input* input) {
    int32 turn_flags;
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 1, "source_fingerprint"),
        &input->source_fingerprint, "source_fingerprint");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 2, "content_recipe_fingerprint"),
        &input->content_recipe_fingerprint, "content_recipe_fingerprint");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 3, "calculation_recipe_fingerprint"),
        &input->calculation_recipe_fingerprint, "calculation_recipe_fingerprint");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 4, "geometry_epoch"),
        &input->geometry_epoch, "geometry_epoch");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 5, "occurrence_context_fingerprint"),
        &input->occurrence_context_fingerprint, "occurrence_context_fingerprint");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 6, "principal_fingerprint"),
        &input->occurrence.principal_fingerprint, "principal_fingerprint");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 7, "session_fingerprint"),
        &input->occurrence.session_fingerprint, "session_fingerprint");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 8, "discourse_id"),
        &input->occurrence.discourse_id, "discourse_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 9, "world_id"),
        &input->occurrence.world_id, "world_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 10, "time_fingerprint"),
        &input->occurrence.time_fingerprint, "time_fingerprint");
    input->occurrence.turn_ordinal = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 11, "turn_ordinal"),
        "turn_ordinal");
    turn_flags = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, 12, "turn_flags"));
    if (turn_flags < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace prompt turn_flags cannot be negative")));
    }
    input->occurrence.turn_flags = (uint32_t)turn_flags;
    input->source_ordinal_base = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 13, "source_ordinal_base"),
        "source_ordinal_base");
    input->preferred_batch_bytes = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 14, "preferred_batch_bytes"),
        "preferred_batch_bytes");
}

Datum laplace_pg_cognition_prompt_admit(PG_FUNCTION_ARGS) {
    static const char media_type[] = "text/plain";
    laplace_framework_context context;
    laplace_cognition_prompt_admission_input input;
    laplace_pg_prompt_atom_provider_state atom_state;
    laplace_cognition_prompt_atom_provider_v1 atom_provider;
    laplace_composition_presence_provider_v1 presence_provider;
    laplace_decomposition_provider_v1 root_provider;
    laplace_cognition_prompt_admission_view view;
    laplace_framework_producer_v1 producer;
    laplace_pg_persistence_producer_result persistence;
    laplace_cognition_prompt_admission_status admission_status;
    prompt_admission_owner* owner;
    text* prompt;
    HeapTupleHeader scope;
    Datum values[18];
    bool nulls[18];
    int publication_required = 0;

    memset(&input, 0, sizeof(input));
    memset(&view, 0, sizeof(view));
    memset(&producer, 0, sizeof(producer));
    memset(&persistence, 0, sizeof(persistence));
    memset(values, 0, sizeof(values));
    memset(nulls, 0, sizeof(nulls));

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    prompt = PG_GETARG_TEXT_PP(1);
    scope = DatumGetHeapTupleHeader(PG_GETARG_DATUM(2));
    if (VARSIZE_ANY_EXHDR(prompt) <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace prompt cannot be empty")));
    }

    prompt_read_scope(scope, &input);
    if (laplace_framework_context_fingerprint(
            &context, &input.occurrence.context_fingerprint) != LAPLACE_FRAMEWORK_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace prompt execution context cannot be fingerprinted")));
    }

    root_provider = prompt_root_provider();
    input.decomposition.content.bytes = (const uint8_t*)VARDATA_ANY(prompt);
    input.decomposition.content.byte_count = (uint64_t)VARSIZE_ANY_EXHDR(prompt);
    input.decomposition.content.media_type = media_type;
    input.decomposition.content.media_type_byte_count = sizeof(media_type) - 1u;
    input.decomposition.providers = &root_provider;
    input.decomposition.provider_count = 1u;
    input.decomposition.maximum_spans = 1u;
    input.decomposition.maximum_depth = 1u;
    input.framework_context = &context;
    input.version = LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION;

    laplace_pg_prompt_admission_providers_create(
        &context, &atom_state, &atom_provider, &presence_provider);

    owner = (prompt_admission_owner*)palloc0(sizeof(*owner));
    owner->cleanup.func = prompt_admission_release;
    owner->cleanup.arg = owner;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &owner->cleanup);

    admission_status = laplace_cognition_prompt_admission_create(
        &input, &atom_provider, &presence_provider, &owner->admission);
    if (admission_status != LAPLACE_COGNITION_PROMPT_ADMISSION_OK ||
        owner->admission == NULL ||
        laplace_cognition_prompt_admission_view_get(owner->admission, &view) !=
            LAPLACE_COGNITION_PROMPT_ADMISSION_OK) {
        prompt_admission_release(owner);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace canonical prompt admission failed"),
                 errdetail("status=%u", (unsigned int)admission_status)));
    }

    admission_status = laplace_cognition_prompt_admission_producer(
        owner->admission, &producer);
    if (admission_status == LAPLACE_COGNITION_PROMPT_ADMISSION_OK) {
        publication_required = 1;
        LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL(
            &context, &input.source_fingerprint,
            &input.calculation_recipe_fingerprint, &producer, &persistence);
    } else if (admission_status !=
               LAPLACE_COGNITION_PROMPT_ADMISSION_NO_PUBLICATION_REQUIRED) {
        prompt_admission_release(owner);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace prompt publication stream failed"),
                 errdetail("status=%u", (unsigned int)admission_status)));
    }

    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.trunk_entity_id.bytes, sizeof(view.trunk_entity_id.bytes)));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.trunk_identity_witness.bytes, sizeof(view.trunk_identity_witness.bytes)));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.trunk_physicality_id.bytes, sizeof(view.trunk_physicality_id.bytes)));
    values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.occurrence_id.bytes, sizeof(view.occurrence_id.bytes)));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.admission_receipt_id.bytes, sizeof(view.admission_receipt_id.bytes)));
    values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.exact_bytes_fingerprint.bytes, sizeof(view.exact_bytes_fingerprint.bytes)));
    values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.decomposition_trace_fingerprint.bytes,
        sizeof(view.decomposition_trace_fingerprint.bytes)));
    values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.atom_provider_fingerprint.bytes, sizeof(view.atom_provider_fingerprint.bytes)));
    values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.atom_provider_receipt_id.bytes, sizeof(view.atom_provider_receipt_id.bytes)));
    values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        view.presence_receipt_id.bytes, sizeof(view.presence_receipt_id.bytes)));
    if (publication_required != 0) {
        values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            persistence.producer.receipt_id.bytes,
            sizeof(persistence.producer.receipt_id.bytes)));
    } else {
        nulls[10] = true;
    }
    values[11] = laplace_pg_numeric_from_uint64(persistence.summary.entity_count);
    values[12] = laplace_pg_numeric_from_uint64(persistence.summary.physicality_count);
    values[13] = laplace_pg_numeric_from_uint64(persistence.summary.trajectory_segment_count);
    values[14] = laplace_pg_numeric_from_uint64(persistence.summary.logical_occurrence_count);
    values[15] = BoolGetDatum(publication_required != 0);
    values[16] = Int32GetDatum((int32)view.status);
    values[17] = Int32GetDatum((int32)view.version);

    prompt_admission_release(owner);
    PG_RETURN_DATUM(HeapTupleGetDatum(
        laplace_pg_form_result_tuple(fcinfo, values, nulls, 18)));
}
