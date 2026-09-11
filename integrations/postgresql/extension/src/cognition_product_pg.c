#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "blake3.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "cognition_firmware_pg.h"
#include "laplace/cognition_firmware.h"
#include "laplace/cognition_prompt_admission.h"
#include "laplace/cognition_realization.h"
#include "laplace/decomposition_uax29.h"
#include "laplace/framework.h"
#include "laplace/uax29.h"
#include "laplace/unicode_root.h"
#include "laplace_pg_internal.h"
#include "materialization_pg.h"
#include "persistence_pg.h"
#include "prompt_admission_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_product_execute);

typedef struct laplace_pg_cognition_product_owners {
    laplace_cognition_firmware_image* image;
    laplace_cognition_prompt_admission* admission;
    laplace_cognition_firmware_result* result;
    laplace_pg_materialization_provider_state* materialization;
    laplace_unicode_source_bundle* unicode_bundle;
    laplace_uax29_tables* uax29_tables;
    MemoryContextCallback cleanup;
} laplace_pg_cognition_product_owners;

static void product_release(void* opaque) {
    laplace_pg_cognition_product_owners* owners =
        (laplace_pg_cognition_product_owners*)opaque;
    if (owners == NULL) {
        return;
    }
    laplace_cognition_firmware_result_destroy(&owners->result);
    laplace_cognition_prompt_admission_destroy(&owners->admission);
    laplace_cognition_firmware_image_destroy(&owners->image);
    laplace_pg_materialization_provider_destroy(&owners->materialization);
    laplace_uax29_tables_destroy(&owners->uax29_tables);
    laplace_unicode_source_bundle_close(&owners->unicode_bundle);
}

static void product_hash_u32(blake3_hasher* hasher, uint32_t value) {
    uint8_t bytes[4];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void product_hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void product_hash_digest(
    blake3_hasher* hasher, const laplace_digest256* value) {
    blake3_hasher_update(hasher, value->bytes, sizeof(value->bytes));
}

static void product_hash_id(
    blake3_hasher* hasher, const laplace_id128* value) {
    blake3_hasher_update(hasher, value->bytes, sizeof(value->bytes));
}

static void product_digest(
    const char* domain,
    const laplace_digest256* first,
    const laplace_digest256* second,
    const laplace_id128* id,
    uint32_t flags,
    laplace_digest256* output) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, strlen(domain));
    if (first != NULL) product_hash_digest(&hasher, first);
    if (second != NULL) product_hash_digest(&hasher, second);
    if (id != NULL) product_hash_id(&hasher, id);
    product_hash_u32(&hasher, flags);
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

static laplace_digest256 product_uax29_fingerprint(
    const laplace_unicode_source_receipt* receipt) {
    static const char domain[] = "laplace-uax29-r47-provider-v1";
    laplace_digest256 output;
    blake3_hasher hasher;
    memset(&output, 0, sizeof(output));
    blake3_hasher_init(&hasher);
    product_hash_u64(&hasher, (uint64_t)(sizeof(domain) - 1u));
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    product_hash_digest(&hasher, &receipt->receipt_id);
    product_hash_digest(&hasher, &receipt->source_fingerprint);
    product_hash_digest(&hasher, &receipt->verified_file_set_fingerprint);
    blake3_hasher_finalize(&hasher, output.bytes, sizeof(output.bytes));
    return output;
}

/* Production exact-reuse realization. A completed semantic act already names an
 * exact canonical answer entity. This provider does not write prose or infer a
 * language from prompt text; it offers that witnessed content as an exact whole
 * candidate. Explicit language/register obligations remain unsupported until an
 * independently admitted realization provider can satisfy them. */
static int product_realize_exact(
    void* state,
    const laplace_cognition_semantic_act* act,
    const laplace_cognition_realization_request* request,
    laplace_cognition_realization_candidate* candidates,
    size_t capacity,
    size_t* count,
    laplace_cognition_realization_usage* usage) {
    laplace_digest256* provider_fingerprint = (laplace_digest256*)state;
    static const char receipt_domain[] =
        "laplace-postgresql-product-exact-realization-receipt-v1";
    static const char candidate_domain[] =
        "laplace-postgresql-product-exact-realization-candidate-v1";
    static const char missing_domain[] =
        "laplace-postgresql-product-exact-realization-missing-v1";

    if (provider_fingerprint == NULL || act == NULL || request == NULL ||
        candidates == NULL || capacity == 0u || count == NULL || usage == NULL) {
        return 1;
    }
    *count = 0u;
    memset(usage, 0, sizeof(*usage));
    product_digest(
        receipt_domain,
        provider_fingerprint,
        &act->act_id,
        &act->primary_answer.entity_id,
        request->flags,
        &usage->provider_receipt_id);

    if ((request->flags &
         (LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT |
          LAPLACE_COGNITION_REALIZATION_REGISTER_PRESENT)) != 0u) {
        product_digest(
            missing_domain,
            &act->act_id,
            &request->realization_recipe_epoch,
            &act->primary_answer.entity_id,
            request->flags,
            &usage->missing_obligation_fingerprint);
        usage->missing_obligation_count = 1u;
        usage->disposition =
            LAPLACE_COGNITION_REALIZATION_DISPOSITION_UNSUPPORTED;
        return 0;
    }

    memset(&candidates[0], 0, sizeof(candidates[0]));
    candidates[0].content_id = act->primary_answer.entity_id;
    candidates[0].realization_recipe_id = request->realization_recipe_epoch;
    candidates[0].obligation_fingerprint = act->act_id;
    candidates[0].reused_subtree_count = 1u;
    candidates[0].match_class =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
    candidates[0].flags =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    product_digest(
        candidate_domain,
        &act->act_id,
        &request->realization_recipe_epoch,
        &act->primary_answer.entity_id,
        request->flags,
        &candidates[0].candidate_receipt_id);
    usage->rows_examined = 1u;
    usage->exact_whole_examined = 1u;
    usage->disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    *count = 1u;
    return 0;
}

static void product_realization_provider(
    laplace_digest256* fingerprint,
    laplace_cognition_realization_provider_v1* provider) {
    static const char domain[] =
        "laplace-postgresql-product-exact-realization-provider-v1";
    blake3_hasher hasher;
    memset(provider, 0, sizeof(*provider));
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_finalize(
        &hasher, fingerprint->bytes, sizeof(fingerprint->bytes));
    provider->state = fingerprint;
    provider->provider_fingerprint = *fingerprint;
    provider->maximum_candidate_records = 1u;
    provider->enumerate = product_realize_exact;
    provider->abi_major = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR;
}

/* The product prompt root remains exact bytes. This root provider intentionally
 * contributes no private token/topic semantics; the shared UAX29 provider below
 * independently adds exact grapheme/word/sentence structural observations. */
static laplace_decomposition_status product_prompt_applicable(
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

static laplace_decomposition_status product_prompt_apply(
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

static laplace_decomposition_provider_v1 product_prompt_provider(void) {
    static const char domain[] = "laplace-postgresql-product-prompt-root-v1";
    laplace_decomposition_provider_v1 provider;
    blake3_hasher hasher;
    memset(&provider, 0, sizeof(provider));
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_finalize(
        &hasher,
        provider.provider_fingerprint.bytes,
        sizeof(provider.provider_fingerprint.bytes));
    provider.applicable = product_prompt_applicable;
    provider.apply = product_prompt_apply;
    provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return provider;
}

static uint32_t product_read_u32(
    HeapTupleHeader tuple, int attribute, const char* field) {
    int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace %s cannot be negative", field)));
    }
    return (uint32_t)value;
}

static uint64_t product_read_u64(
    HeapTupleHeader tuple, int attribute, const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static void product_read_search_budget(
    HeapTupleHeader tuple, laplace_query_search_budget* budget) {
    memset(budget, 0, sizeof(*budget));
    budget->max_expanded_states = product_read_u64(tuple, 1, "max_expanded_states");
    budget->max_transition_records = product_read_u64(tuple, 2, "max_transition_records");
    budget->max_emitted_states = product_read_u64(tuple, 3, "max_emitted_states");
    budget->max_frontier_states = product_read_u64(tuple, 4, "max_frontier_states");
    budget->max_memory_bytes = product_read_u64(tuple, 5, "max_memory_bytes");
    budget->max_io_operations = product_read_u64(tuple, 6, "max_io_operations");
    budget->max_database_operations = product_read_u64(tuple, 7, "max_database_operations");
    budget->max_provider_calls = product_read_u64(tuple, 8, "max_provider_calls");
    budget->max_depth = product_read_u32(tuple, 9, "max_depth");
    budget->requested_path_count = product_read_u32(tuple, 10, "requested_path_count");
    budget->frontier_batch_width = product_read_u32(tuple, 11, "frontier_batch_width");
    budget->transition_batch_capacity = product_read_u32(tuple, 12, "transition_batch_capacity");
}

static void product_read_forward_limits(
    HeapTupleHeader tuple, laplace_cognition_observation_forward_limits* limits) {
    memset(limits, 0, sizeof(*limits));
    limits->max_layers = product_read_u64(tuple, 1, "max_layers");
    limits->max_provider_calls = product_read_u64(tuple, 2, "forward_max_provider_calls");
    limits->max_projected_queries = product_read_u64(tuple, 3, "max_projected_queries");
    limits->max_candidate_operations = product_read_u64(tuple, 4, "max_candidate_operations");
    limits->max_resolutions = product_read_u64(tuple, 5, "max_resolutions");
    limits->max_resource_cost = product_read_u64(tuple, 6, "max_resource_cost");
    limits->max_io_operations = product_read_u64(tuple, 7, "forward_max_io_operations");
    limits->max_database_operations = product_read_u64(tuple, 8, "forward_max_database_operations");
    limits->candidate_operation_capacity = product_read_u32(tuple, 9, "candidate_operation_capacity");
    limits->resolution_capacity = product_read_u32(tuple, 10, "resolution_capacity");
}

static void product_read_materialization(
    HeapTupleHeader tuple, laplace_cognition_materialization_request* request) {
    memset(request, 0, sizeof(*request));
    request->maximum_nodes = product_read_u64(tuple, 1, "maximum_nodes");
    request->maximum_trajectory_carriers = product_read_u64(
        tuple, 2, "maximum_trajectory_carriers");
    request->maximum_output_bytes = product_read_u64(
        tuple, 3, "materialization_maximum_output_bytes");
    request->maximum_depth = product_read_u32(tuple, 4, "maximum_depth");
    request->version = product_read_u32(tuple, 5, "materialization_version");
}

static void product_read_firmware_request(
    HeapTupleHeader tuple, laplace_cognition_firmware_request* request) {
    HeapTupleHeader search;
    HeapTupleHeader forward;
    HeapTupleHeader materialization;
    memset(request, 0, sizeof(*request));
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 1, "selected_program"),
        &request->selected_program, "selected_program");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 2, "evidence_boundary"),
        &request->evidence_boundary, "evidence_boundary");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 3, "result_contract_fingerprint"),
        &request->result_contract_fingerprint, "result_contract_fingerprint");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 4, "expected_previous_checkpoint"),
        &request->expected_previous_checkpoint, "expected_previous_checkpoint");
    request->previous_checkpoint_present = DatumGetBool(
        laplace_pg_required_composite_attribute(tuple, 5, "previous_checkpoint_present")) ? 1u : 0u;
    search = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(tuple, 6, "search_budget"));
    forward = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(tuple, 7, "forward_limits"));
    materialization = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(tuple, 8, "materialization"));
    product_read_search_budget(search, &request->search_budget);
    product_read_forward_limits(forward, &request->forward_limits);
    product_read_materialization(materialization, &request->materialization);
    request->maximum_output_bytes = product_read_u64(tuple, 9, "maximum_output_bytes");
    request->maximum_checkpoint_bytes = product_read_u64(tuple, 10, "maximum_checkpoint_bytes");
    request->boundary_flags = product_read_u32(tuple, 11, "boundary_flags");
    request->version = product_read_u32(tuple, 12, "firmware_version");
}

static void product_read_prompt_scope(
    HeapTupleHeader tuple, laplace_cognition_prompt_admission_input* input) {
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
    input->occurrence.turn_ordinal = product_read_u64(tuple, 11, "turn_ordinal");
    turn_flags = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, 12, "turn_flags"));
    if (turn_flags < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace turn_flags cannot be negative")));
    }
    input->occurrence.turn_flags = (uint32_t)turn_flags;
    input->source_ordinal_base = product_read_u64(tuple, 13, "source_ordinal_base");
    input->preferred_batch_bytes = product_read_u64(tuple, 14, "preferred_batch_bytes");
}

Datum laplace_pg_cognition_product_execute(PG_FUNCTION_ARGS) {
    static const char media_type[] = "text/plain";
    laplace_framework_context context;
    laplace_cognition_firmware_request request;
    laplace_cognition_firmware_program program;
    laplace_cognition_prompt_admission_input prompt_input;
    laplace_cognition_prompt_admission_view prompt_view;
    laplace_pg_prompt_atom_provider_state atom_state;
    laplace_cognition_prompt_atom_provider_v1 atoms;
    laplace_composition_presence_provider_v1 presence;
    laplace_decomposition_provider_v1 prompt_provider;
    laplace_decomposition_provider_v1 prompt_providers[2];
    laplace_decomposition_uax29_provider uax29_provider;
    laplace_unicode_source_receipt unicode_receipt;
    laplace_digest256 uax29_fingerprint;
    laplace_framework_producer_v1 prompt_producer;
    laplace_pg_persistence_producer_result prompt_persistence;
    laplace_cognition_realization_provider_v1 realization;
    laplace_digest256 realization_fingerprint;
    laplace_cognition_materialization_provider_v1 materialization;
    laplace_pg_materialization_provider_report materialization_report;
    laplace_pg_cognition_provider_report cognition_report;
    laplace_cognition_firmware_receipt receipt;
    laplace_cognition_firmware_error firmware_error;
    laplace_cognition_firmware_status status;
    laplace_cognition_prompt_admission_status prompt_status;
    laplace_unicode_status unicode_status;
    laplace_uax29_status uax29_status;
    ErrorData* database_error = NULL;
    ErrorData* materialization_error = NULL;
    laplace_pg_cognition_product_owners* owners;
    HeapTupleHeader scope;
    HeapTupleHeader request_tuple;
    bytea* image_bytes;
    bytea* previous;
    text* prompt;
    const uint8_t* image_view = NULL;
    const uint8_t* output = NULL;
    const uint8_t* checkpoint = NULL;
    size_t image_view_bytes = 0u;
    size_t output_bytes = 0u;
    size_t checkpoint_bytes = 0u;
    uint64_t prompt_bytes;
    uint64_t prompt_span_capacity;
    laplace_digest256 program_id;
    int64 workspace;
    int publication_required = 0;
    Datum values[30];
    bool nulls[30];

    memset(&request, 0, sizeof(request));
    memset(&program, 0, sizeof(program));
    memset(&prompt_input, 0, sizeof(prompt_input));
    memset(&prompt_view, 0, sizeof(prompt_view));
    memset(&uax29_provider, 0, sizeof(uax29_provider));
    memset(&unicode_receipt, 0, sizeof(unicode_receipt));
    memset(&uax29_fingerprint, 0, sizeof(uax29_fingerprint));
    memset(prompt_providers, 0, sizeof(prompt_providers));
    memset(&prompt_producer, 0, sizeof(prompt_producer));
    memset(&prompt_persistence, 0, sizeof(prompt_persistence));
    memset(&materialization_report, 0, sizeof(materialization_report));
    memset(&cognition_report, 0, sizeof(cognition_report));
    memset(&receipt, 0, sizeof(receipt));
    memset(&firmware_error, 0, sizeof(firmware_error));
    memset(values, 0, sizeof(values));
    memset(nulls, 0, sizeof(nulls));

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    image_bytes = PG_GETARG_BYTEA_PP(1);
    prompt = PG_GETARG_TEXT_PP(2);
    scope = DatumGetHeapTupleHeader(PG_GETARG_DATUM(3));
    request_tuple = DatumGetHeapTupleHeader(PG_GETARG_DATUM(4));
    previous = PG_GETARG_BYTEA_PP(5);
    workspace = PG_GETARG_INT64(6);

    if (VARSIZE_ANY_EXHDR(image_bytes) <= 0 || VARSIZE_ANY_EXHDR(prompt) <= 0 ||
        workspace < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace product cognition requires firmware, prompt and a nonnegative workspace")));
    }
    product_read_firmware_request(request_tuple, &request);
    if ((request.previous_checkpoint_present != 0u) !=
        (VARSIZE_ANY_EXHDR(previous) != 0)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace previous checkpoint presence does not match supplied bytes")));
    }

    owners = (laplace_pg_cognition_product_owners*)palloc0(sizeof(*owners));
    owners->cleanup.func = product_release;
    owners->cleanup.arg = owners;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &owners->cleanup);

    status = laplace_cognition_firmware_image_load(
        (const uint8_t*)VARDATA_ANY(image_bytes),
        (size_t)VARSIZE_ANY_EXHDR(image_bytes),
        &request.selected_program,
        UINT64_C(1048576),
        &owners->image);
    if (status != LAPLACE_COGNITION_FIRMWARE_OK || owners->image == NULL ||
        laplace_cognition_firmware_image_view(
            owners->image, &program, &image_view, &image_view_bytes, &program_id) !=
            LAPLACE_COGNITION_FIRMWARE_OK) {
        product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace selected firmware image is invalid"),
                 errdetail("status=%u", (unsigned int)status)));
    }
    (void)image_view;
    (void)image_view_bytes;

    product_read_prompt_scope(scope, &prompt_input);
    if (laplace_framework_context_fingerprint(
            &context, &prompt_input.occurrence.context_fingerprint) !=
        LAPLACE_FRAMEWORK_OK) {
        product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace product prompt context cannot be fingerprinted")));
    }

    unicode_status = laplace_unicode_source_bundle_open(
        LAPLACE_UNICODE_SOURCE_ROOT,
        &owners->unicode_bundle,
        &unicode_receipt);
    if (unicode_status != LAPLACE_UNICODE_OK || owners->unicode_bundle == NULL) {
        product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace product prompt Unicode source cannot be opened"),
                 errdetail("status=%u", (unsigned int)unicode_status)));
    }
    uax29_status = laplace_uax29_tables_create(
        owners->unicode_bundle, &owners->uax29_tables);
    if (uax29_status != LAPLACE_UAX29_OK || owners->uax29_tables == NULL) {
        product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace product prompt UAX29 tables cannot be constructed"),
                 errdetail("status=%u", (unsigned int)uax29_status)));
    }
    laplace_unicode_source_bundle_close(&owners->unicode_bundle);
    uax29_fingerprint = product_uax29_fingerprint(&unicode_receipt);
    if (laplace_decomposition_uax29_provider_init(
            &uax29_provider,
            owners->uax29_tables,
            &uax29_fingerprint) != LAPLACE_DECOMPOSITION_OK) {
        product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace product prompt UAX29 provider cannot be initialized")));
    }

    prompt_provider = product_prompt_provider();
    prompt_providers[0] = prompt_provider;
    prompt_providers[1] = uax29_provider.provider;
    prompt_bytes = (uint64_t)VARSIZE_ANY_EXHDR(prompt);
    if (prompt_bytes > (UINT64_MAX - UINT64_C(1)) / UINT64_C(3)) {
        product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace product prompt exceeds the structural span bound")));
    }
    prompt_span_capacity = prompt_bytes * UINT64_C(3) + UINT64_C(1);
    prompt_input.decomposition.content.bytes = (const uint8_t*)VARDATA_ANY(prompt);
    prompt_input.decomposition.content.byte_count = prompt_bytes;
    prompt_input.decomposition.content.media_type = media_type;
    prompt_input.decomposition.content.media_type_byte_count = sizeof(media_type) - 1u;
    prompt_input.decomposition.providers = prompt_providers;
    prompt_input.decomposition.provider_count = 2u;
    prompt_input.decomposition.maximum_spans = prompt_span_capacity;
    prompt_input.decomposition.maximum_depth = 1u;
    prompt_input.framework_context = &context;
    prompt_input.version = LAPLACE_COGNITION_PROMPT_ADMISSION_VERSION;

    laplace_pg_prompt_admission_providers_create(
        &context, &atom_state, &atoms, &presence);
    prompt_status = laplace_cognition_prompt_admission_create(
        &prompt_input, &atoms, &presence, &owners->admission);
    if (prompt_status != LAPLACE_COGNITION_PROMPT_ADMISSION_OK ||
        owners->admission == NULL ||
        laplace_cognition_prompt_admission_view_get(
            owners->admission, &prompt_view) !=
            LAPLACE_COGNITION_PROMPT_ADMISSION_OK) {
        product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace product prompt admission failed"),
                 errdetail("status=%u", (unsigned int)prompt_status)));
    }
    laplace_uax29_tables_destroy(&owners->uax29_tables);

    prompt_status = laplace_cognition_prompt_admission_producer(
        owners->admission, &prompt_producer);
    if (prompt_status == LAPLACE_COGNITION_PROMPT_ADMISSION_OK) {
        publication_required = 1;
        LAPLACE_PG_PERSISTENCE_RUN_PRODUCER_SYMBOL(
            &context,
            &prompt_input.source_fingerprint,
            &prompt_input.calculation_recipe_fingerprint,
            &prompt_producer,
            &prompt_persistence);
    } else if (prompt_status !=
               LAPLACE_COGNITION_PROMPT_ADMISSION_NO_PUBLICATION_REQUIRED) {
        product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace product prompt persistence stream failed"),
                 errdetail("status=%u", (unsigned int)prompt_status)));
    }

    product_realization_provider(&realization_fingerprint, &realization);
    laplace_pg_materialization_provider_create(
        &context, &owners->materialization, &materialization);

    status = laplace_pg_cognition_firmware_execute_indexed(
        &program,
        &request,
        &context,
        owners->admission,
        VARSIZE_ANY_EXHDR(previous) != 0
            ? (const uint8_t*)VARDATA_ANY(previous)
            : NULL,
        (size_t)VARSIZE_ANY_EXHDR(previous),
        &realization,
        &materialization,
        (uint64_t)workspace,
        NULL,
        NULL,
        &owners->result,
        &firmware_error,
        &cognition_report,
        &database_error);

    laplace_pg_materialization_provider_summary(
        owners->materialization, &materialization_report);
    materialization_error =
        laplace_pg_materialization_provider_take_error(owners->materialization);

    if (database_error != NULL || materialization_error != NULL) {
        ErrorData* rethrow = database_error != NULL
            ? database_error
            : materialization_error;
        if (database_error != NULL && materialization_error != NULL) {
            FreeErrorData(materialization_error);
        }
        product_release(owners);
        ReThrowError(rethrow);
    }

    if (status == LAPLACE_COGNITION_FIRMWARE_OK && owners->result != NULL) {
        if (laplace_cognition_firmware_result_receipt(owners->result, &receipt) !=
                LAPLACE_COGNITION_FIRMWARE_OK ||
            laplace_cognition_firmware_result_output(
                owners->result, &output, &output_bytes) !=
                LAPLACE_COGNITION_FIRMWARE_OK ||
            laplace_cognition_firmware_result_checkpoint(
                owners->result, &checkpoint, &checkpoint_bytes) !=
                LAPLACE_COGNITION_FIRMWARE_OK) {
            product_release(owners);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace product cognition result cannot be read")));
        }
    }

    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        prompt_view.trunk_entity_id.bytes,
        sizeof(prompt_view.trunk_entity_id.bytes)));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        prompt_view.occurrence_id.bytes,
        sizeof(prompt_view.occurrence_id.bytes)));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        prompt_view.admission_receipt_id.bytes,
        sizeof(prompt_view.admission_receipt_id.bytes)));
    if (publication_required != 0) {
        values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            prompt_persistence.producer.receipt_id.bytes,
            sizeof(prompt_persistence.producer.receipt_id.bytes)));
    } else {
        nulls[3] = true;
    }
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        program_id.bytes, sizeof(program_id.bytes)));
    values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(output, output_bytes));
    if (status == LAPLACE_COGNITION_FIRMWARE_OK) {
        values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.output_fingerprint.bytes,
            sizeof(receipt.output_fingerprint.bytes)));
        values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            checkpoint, checkpoint_bytes));
        values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.next_checkpoint_fingerprint.bytes,
            sizeof(receipt.next_checkpoint_fingerprint.bytes)));
        values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.receipt_id.bytes, sizeof(receipt.receipt_id.bytes)));
        values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            receipt.trace_fingerprint.bytes,
            sizeof(receipt.trace_fingerprint.bytes)));
    } else {
        nulls[6] = true;
        values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(NULL, 0u));
        nulls[8] = true;
        nulls[9] = true;
        nulls[10] = true;
    }
    values[11] = Int32GetDatum((int32)receipt.completed_steps);
    values[12] = Int32GetDatum((int32)receipt.emitted_parts);
    values[13] = laplace_pg_numeric_from_uint64(receipt.layer_count);
    values[14] = laplace_pg_numeric_from_uint64(receipt.provider_call_count);
    values[15] = laplace_pg_numeric_from_uint64(receipt.resource_cost);
    values[16] = laplace_pg_numeric_from_uint64(receipt.io_operations);
    values[17] = laplace_pg_numeric_from_uint64(receipt.database_operations);
    values[18] = laplace_pg_numeric_from_uint64(cognition_report.rows_fetched);
    values[19] = laplace_pg_numeric_from_uint64(cognition_report.batch_count);
    values[20] = laplace_pg_numeric_from_uint64(cognition_report.semantic_rows_examined);
    values[21] = laplace_pg_numeric_from_uint64(cognition_report.semantic_database_operations);
    values[22] = laplace_pg_numeric_from_uint64(materialization_report.resolved_nodes);
    values[23] = laplace_pg_numeric_from_uint64(materialization_report.trajectory_reads);
    values[24] = laplace_pg_numeric_from_uint64(materialization_report.trajectory_bytes);
    values[25] = laplace_pg_numeric_from_uint64(materialization_report.database_operations);
    values[26] = Int32GetDatum((int32)status);
    values[27] = Int32GetDatum((int32)firmware_error.step_index);
    values[28] = Int32GetDatum((int32)firmware_error.native_status);
    values[29] = Int32GetDatum((int32)receipt.version);

    product_release(owners);
    PG_RETURN_DATUM(HeapTupleGetDatum(
        laplace_pg_form_result_tuple(fcinfo, values, nulls, 30)));
}
