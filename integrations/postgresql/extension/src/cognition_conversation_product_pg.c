#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "blake3.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "cognition_provider_pg.h"
#include "laplace/cognition_discourse_frame.h"
#include "laplace/cognition_firmware.h"
#include "laplace/cognition_prompt_admission.h"
#include "laplace/cognition_prompt_conversation.h"
#include "laplace/cognition_realization.h"
#include "laplace/decomposition_uax29.h"
#include "laplace/framework.h"
#include "laplace/identity.h"
#include "laplace/uax29.h"
#include "laplace_pg_internal.h"
#include "materialization_pg.h"
#include "persistence_pg.h"
#include "prompt_admission_pg.h"
#include "semantic_cognition_pg.h"
#include "uax29_active_pg.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_conversation_product_execute);

/* The discourse transport is already an extension-owned C entrypoint. Calling it
 * through DirectFunctionCall1 after cognition closes provider SPI gives the native
 * next-frame the same durable validation/readback law as every other discourse
 * deposit, including exact predecessor continuity. */
extern Datum laplace_pg_cognition_discourse_deposit(PG_FUNCTION_ARGS);

typedef struct laplace_pg_cognition_conversation_product_owners {
    laplace_cognition_prompt_admission* admission;
    laplace_pg_cognition_provider* physical;
    laplace_pg_semantic_provider_state* semantic;
    laplace_pg_materialization_provider_state* materialization;
    laplace_uax29_tables* uax29_tables;
    MemoryContextCallback cleanup;
} laplace_pg_cognition_conversation_product_owners;

static void conversation_product_release(void* opaque) {
    laplace_pg_cognition_conversation_product_owners* owners =
        (laplace_pg_cognition_conversation_product_owners*)opaque;
    if (owners == NULL) {
        return;
    }
    laplace_pg_cognition_provider_destroy(&owners->physical);
    laplace_pg_semantic_provider_destroy(&owners->semantic);
    laplace_pg_materialization_provider_destroy(&owners->materialization);
    laplace_cognition_prompt_admission_destroy(&owners->admission);
    laplace_uax29_tables_destroy(&owners->uax29_tables);
}

static void conversation_product_hash_u32(blake3_hasher* hasher, uint32_t value) {
    uint8_t bytes[4];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void conversation_product_hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void conversation_product_hash_digest(
    blake3_hasher* hasher, const laplace_digest256* value) {
    blake3_hasher_update(hasher, value->bytes, sizeof(value->bytes));
}

static void conversation_product_hash_id(
    blake3_hasher* hasher, const laplace_id128* value) {
    blake3_hasher_update(hasher, value->bytes, sizeof(value->bytes));
}

static void conversation_product_digest(
    const char* domain,
    const laplace_digest256* first,
    const laplace_digest256* second,
    const laplace_id128* id,
    uint32_t flags,
    laplace_digest256* output) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, strlen(domain));
    if (first != NULL) conversation_product_hash_digest(&hasher, first);
    if (second != NULL) conversation_product_hash_digest(&hasher, second);
    if (id != NULL) conversation_product_hash_id(&hasher, id);
    conversation_product_hash_u32(&hasher, flags);
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

static int conversation_product_digest_zero(const laplace_digest256* value) {
    uint8_t aggregate = 0u;
    size_t index;
    if (value == NULL) return 1;
    for (index = 0u; index < sizeof(value->bytes); ++index) {
        aggregate = (uint8_t)(aggregate | value->bytes[index]);
    }
    return aggregate == 0u;
}

static laplace_digest256 conversation_product_uax29_fingerprint(
    const laplace_pg_active_uax_authority* authority) {
    static const char domain[] = "laplace.decomposition.provider.uax29/v1";
    laplace_digest256 output;
    blake3_hasher hasher;
    memset(&output, 0, sizeof(output));
    blake3_hasher_init(&hasher);
    conversation_product_hash_u64(&hasher, (uint64_t)(sizeof(domain) - 1u));
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    conversation_product_hash_digest(&hasher, &authority->source_fingerprint);
    conversation_product_hash_digest(&hasher, &authority->recipe_fingerprint);
    blake3_hasher_finalize(&hasher, output.bytes, sizeof(output.bytes));
    return output;
}

static laplace_id128 conversation_product_content_identity(const char* ascii) {
    laplace_id128 atoms[32];
    laplace_id128 result;
    size_t count;
    size_t index;
    memset(atoms, 0, sizeof(atoms));
    memset(&result, 0, sizeof(result));
    if (ascii == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace product modality identity is absent")));
    }
    count = strlen(ascii);
    if (count == 0u || count > sizeof(atoms) / sizeof(atoms[0])) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace product modality identity exceeds its fixed bound")));
    }
    for (index = 0u; index < count; ++index) {
        if (laplace_identity_codepoint(
                (uint32_t)(unsigned char)ascii[index], &atoms[index]) !=
            LAPLACE_IDENTITY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace product modality atom identity failed")));
        }
    }
    if (laplace_identity_composite(atoms, count, &result) != LAPLACE_IDENTITY_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace product modality composite identity failed")));
    }
    return result;
}

static laplace_digest256 conversation_product_recipe_epoch(void) {
    static const char domain[] =
        "laplace-stock-exact-witnessed-realization-v1";
    laplace_digest256 result;
    blake3_hasher hasher;
    memset(&result, 0, sizeof(result));
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_finalize(&hasher, result.bytes, sizeof(result.bytes));
    return result;
}

/* Production exact-reuse realization. The native semantic act already owns the
 * completed answer selection; this provider can only surface that exact witnessed
 * content. It cannot infer language or create prose outside persisted content. */
static int conversation_product_realize_exact(
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
    conversation_product_digest(
        receipt_domain,
        provider_fingerprint,
        &act->act_id,
        &act->primary_answer.entity_id,
        request->flags,
        &usage->provider_receipt_id);

    if ((request->flags &
         (LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT |
          LAPLACE_COGNITION_REALIZATION_REGISTER_PRESENT)) != 0u) {
        conversation_product_digest(
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
    conversation_product_digest(
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

static void conversation_product_realization_provider(
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
    provider->enumerate = conversation_product_realize_exact;
    provider->abi_major = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR;
}

/* The whole exact prompt remains the only entry root. UAX29 contributes typed
 * structural boundaries independently; neither transport chooses a token/topic. */
static laplace_decomposition_status conversation_product_prompt_applicable(
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

static laplace_decomposition_status conversation_product_prompt_apply(
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

static laplace_decomposition_provider_v1 conversation_product_prompt_provider(void) {
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
    provider.applicable = conversation_product_prompt_applicable;
    provider.apply = conversation_product_prompt_apply;
    provider.abi_major = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MAJOR;
    provider.abi_minor = LAPLACE_DECOMPOSITION_PROVIDER_ABI_MINOR;
    return provider;
}

static uint32_t conversation_product_read_u32(
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

static uint64_t conversation_product_read_u64(
    HeapTupleHeader tuple, int attribute, const char* field) {
    return laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, attribute, field), field);
}

static void conversation_product_read_search_budget(
    HeapTupleHeader tuple, laplace_query_search_budget* budget) {
    memset(budget, 0, sizeof(*budget));
    budget->max_expanded_states = conversation_product_read_u64(tuple, 1, "max_expanded_states");
    budget->max_transition_records = conversation_product_read_u64(tuple, 2, "max_transition_records");
    budget->max_emitted_states = conversation_product_read_u64(tuple, 3, "max_emitted_states");
    budget->max_frontier_states = conversation_product_read_u64(tuple, 4, "max_frontier_states");
    budget->max_memory_bytes = conversation_product_read_u64(tuple, 5, "max_memory_bytes");
    budget->max_io_operations = conversation_product_read_u64(tuple, 6, "max_io_operations");
    budget->max_database_operations = conversation_product_read_u64(tuple, 7, "max_database_operations");
    budget->max_provider_calls = conversation_product_read_u64(tuple, 8, "max_provider_calls");
    budget->max_depth = conversation_product_read_u32(tuple, 9, "max_depth");
    budget->requested_path_count = conversation_product_read_u32(tuple, 10, "requested_path_count");
    budget->frontier_batch_width = conversation_product_read_u32(tuple, 11, "frontier_batch_width");
    budget->transition_batch_capacity = conversation_product_read_u32(tuple, 12, "transition_batch_capacity");
}

static void conversation_product_read_forward_limits(
    HeapTupleHeader tuple, laplace_cognition_observation_forward_limits* limits) {
    memset(limits, 0, sizeof(*limits));
    limits->max_layers = conversation_product_read_u64(tuple, 1, "max_layers");
    limits->max_provider_calls = conversation_product_read_u64(tuple, 2, "forward_max_provider_calls");
    limits->max_projected_queries = conversation_product_read_u64(tuple, 3, "max_projected_queries");
    limits->max_candidate_operations = conversation_product_read_u64(tuple, 4, "max_candidate_operations");
    limits->max_resolutions = conversation_product_read_u64(tuple, 5, "max_resolutions");
    limits->max_resource_cost = conversation_product_read_u64(tuple, 6, "max_resource_cost");
    limits->max_io_operations = conversation_product_read_u64(tuple, 7, "forward_max_io_operations");
    limits->max_database_operations = conversation_product_read_u64(tuple, 8, "forward_max_database_operations");
    limits->candidate_operation_capacity = conversation_product_read_u32(tuple, 9, "candidate_operation_capacity");
    limits->resolution_capacity = conversation_product_read_u32(tuple, 10, "resolution_capacity");
}

static void conversation_product_read_materialization(
    HeapTupleHeader tuple, laplace_cognition_materialization_request* request) {
    memset(request, 0, sizeof(*request));
    request->maximum_nodes = conversation_product_read_u64(tuple, 1, "maximum_nodes");
    request->maximum_trajectory_carriers = conversation_product_read_u64(
        tuple, 2, "maximum_trajectory_carriers");
    request->maximum_output_bytes = conversation_product_read_u64(
        tuple, 3, "materialization_maximum_output_bytes");
    request->maximum_depth = conversation_product_read_u32(tuple, 4, "maximum_depth");
    request->version = conversation_product_read_u32(tuple, 5, "materialization_version");
}

static void conversation_product_read_request(
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
    conversation_product_read_search_budget(search, &request->search_budget);
    conversation_product_read_forward_limits(forward, &request->forward_limits);
    conversation_product_read_materialization(materialization, &request->materialization);
    request->maximum_output_bytes = conversation_product_read_u64(tuple, 9, "maximum_output_bytes");
    request->maximum_checkpoint_bytes = conversation_product_read_u64(tuple, 10, "maximum_checkpoint_bytes");
    request->boundary_flags = conversation_product_read_u32(tuple, 11, "boundary_flags");
    request->version = conversation_product_read_u32(tuple, 12, "firmware_version");
}

static void conversation_product_read_prompt_scope(
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
    input->occurrence.turn_ordinal = conversation_product_read_u64(tuple, 11, "turn_ordinal");
    turn_flags = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, 12, "turn_flags"));
    if (turn_flags < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace turn_flags cannot be negative")));
    }
    input->occurrence.turn_flags = (uint32_t)turn_flags;
    input->source_ordinal_base = conversation_product_read_u64(tuple, 13, "source_ordinal_base");
    input->preferred_batch_bytes = conversation_product_read_u64(tuple, 14, "preferred_batch_bytes");
}

static void conversation_product_validate_previous(
    const laplace_cognition_firmware_request* request,
    const bytea* previous) {
    const size_t previous_bytes = (size_t)VARSIZE_ANY_EXHDR(previous);
    const int present = previous_bytes != 0u;
    if ((request->previous_checkpoint_present != 0u) != present) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace previous discourse presence does not match supplied bytes")));
    }
    if (!present) {
        if (!conversation_product_digest_zero(&request->expected_previous_checkpoint)) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace absent previous discourse has a nonzero expected fingerprint")));
        }
        return;
    }
    {
        laplace_cognition_discourse_state state;
        laplace_cognition_discourse_frame_receipt receipt;
        const laplace_cognition_discourse_frame_status status =
            laplace_cognition_discourse_frame_decode(
                (const uint8_t*)VARDATA_ANY(previous), previous_bytes,
                &state, &receipt);
        (void)state;
        if (status != LAPLACE_COGNITION_DISCOURSE_FRAME_OK ||
            memcmp(
                receipt.frame_fingerprint.bytes,
                request->expected_previous_checkpoint.bytes,
                sizeof(receipt.frame_fingerprint.bytes)) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                     errmsg("Laplace previous discourse frame does not match its expected fingerprint"),
                     errdetail("native discourse frame status=%d", (int)status)));
        }
    }
}

static void conversation_product_deposit_frame(
    const uint8_t* frame,
    size_t frame_bytes,
    const laplace_digest256* expected_state_id) {
    bytea* value;
    bytea* deposited;
    Datum result;
    if (frame == NULL || frame_bytes == 0u || expected_state_id == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace next discourse frame is absent")));
    }
    value = laplace_pg_bytes_to_bytea(frame, frame_bytes);
    result = DirectFunctionCall1(
        laplace_pg_cognition_discourse_deposit, PointerGetDatum(value));
    deposited = DatumGetByteaPP(result);
    if (VARSIZE_ANY_EXHDR(deposited) != (int)sizeof(expected_state_id->bytes) ||
        memcmp(
            VARDATA_ANY(deposited), expected_state_id->bytes,
            sizeof(expected_state_id->bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace durable discourse deposit returned a different state identity")));
    }
}

Datum laplace_pg_cognition_conversation_product_execute(PG_FUNCTION_ARGS) {
    static const char media_type[] = "text/plain";
    const uint32_t semantic_mask = LAPLACE_OBSERVATION_QUERY_SEMANTIC;
    const uint32_t structural_mask =
        LAPLACE_OBSERVATION_QUERY_RELATION_MASK & ~semantic_mask;
    laplace_framework_context context;
    laplace_cognition_firmware_request request;
    laplace_cognition_prompt_admission_input prompt_input;
    laplace_cognition_prompt_admission_view prompt_view;
    laplace_pg_prompt_atom_provider_state atom_state;
    laplace_cognition_prompt_atom_provider_v1 atoms;
    laplace_composition_presence_provider_v1 presence;
    laplace_decomposition_provider_v1 prompt_provider;
    laplace_decomposition_provider_v1 prompt_providers[2];
    laplace_decomposition_uax29_provider uax29_provider;
    laplace_pg_active_uax_authority uax_authority;
    laplace_digest256 uax29_fingerprint;
    laplace_framework_producer_v1 prompt_producer;
    laplace_pg_persistence_producer_result prompt_persistence;
    laplace_cognition_observation_request provider_scope;
    laplace_cognition_observation_request semantic_scope;
    laplace_cognition_observation_candidate_provider_v1 persistent_providers[2];
    laplace_cognition_realization_provider_v1 realization;
    laplace_digest256 realization_fingerprint;
    laplace_cognition_materialization_provider_v1 materialization;
    laplace_pg_cognition_provider_report physical_report;
    laplace_pg_semantic_provider_report semantic_report;
    laplace_pg_materialization_provider_report materialization_report;
    laplace_cognition_prompt_conversation_request conversation_request;
    laplace_cognition_prompt_conversation_result conversation_result;
    laplace_cognition_prompt_conversation_status conversation_status;
    laplace_cognition_prompt_admission_status prompt_status;
    ErrorData* physical_error = NULL;
    ErrorData* semantic_error = NULL;
    ErrorData* materialization_error = NULL;
    laplace_pg_cognition_conversation_product_owners* owners;
    HeapTupleHeader scope;
    HeapTupleHeader request_tuple;
    bytea* previous;
    text* prompt;
    uint8_t* output;
    uint8_t* next_frame;
    size_t output_bytes = 0u;
    size_t next_frame_bytes = 0u;
    uint64_t prompt_bytes;
    uint64_t prompt_span_capacity;
    uint64_t provider_workspace;
    uint64_t output_capacity64;
    size_t output_capacity;
    int64 workspace;
    int publication_required = 0;
    int spi_connected = 0;
    Datum values[30];
    bool nulls[30];

    memset(&request, 0, sizeof(request));
    memset(&prompt_input, 0, sizeof(prompt_input));
    memset(&prompt_view, 0, sizeof(prompt_view));
    memset(&uax29_provider, 0, sizeof(uax29_provider));
    memset(&uax_authority, 0, sizeof(uax_authority));
    memset(&uax29_fingerprint, 0, sizeof(uax29_fingerprint));
    memset(prompt_providers, 0, sizeof(prompt_providers));
    memset(&prompt_producer, 0, sizeof(prompt_producer));
    memset(&prompt_persistence, 0, sizeof(prompt_persistence));
    memset(&provider_scope, 0, sizeof(provider_scope));
    memset(&semantic_scope, 0, sizeof(semantic_scope));
    memset(persistent_providers, 0, sizeof(persistent_providers));
    memset(&physical_report, 0, sizeof(physical_report));
    memset(&semantic_report, 0, sizeof(semantic_report));
    memset(&materialization_report, 0, sizeof(materialization_report));
    memset(&conversation_request, 0, sizeof(conversation_request));
    memset(&conversation_result, 0, sizeof(conversation_result));
    memset(values, 0, sizeof(values));
    memset(nulls, 0, sizeof(nulls));

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    prompt = PG_GETARG_TEXT_PP(1);
    scope = DatumGetHeapTupleHeader(PG_GETARG_DATUM(2));
    request_tuple = DatumGetHeapTupleHeader(PG_GETARG_DATUM(3));
    previous = PG_GETARG_BYTEA_PP(4);
    workspace = PG_GETARG_INT64(5);

    if (VARSIZE_ANY_EXHDR(prompt) <= 0 || workspace <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace conversation cognition requires a prompt and positive workspace")));
    }
    conversation_product_read_request(request_tuple, &request);
    conversation_product_validate_previous(&request, previous);
    if (request.version != LAPLACE_COGNITION_FIRMWARE_VERSION ||
        request.previous_checkpoint_present > 1u ||
        (request.boundary_flags &
         ~LAPLACE_COGNITION_OBSERVATION_REQUEST_BOUNDARY_COMPLETE) != 0u ||
        request.search_budget.requested_path_count == 0u ||
        request.maximum_output_bytes == 0u ||
        request.maximum_checkpoint_bytes < LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES ||
        conversation_product_digest_zero(&request.selected_program) ||
        (context.epoch_mask &
         (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_FIRMWARE)) == 0u ||
        memcmp(
            context.epochs[LAPLACE_FRAMEWORK_EPOCH_FIRMWARE].bytes,
            request.selected_program.bytes,
            sizeof(request.selected_program.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace conversation cognition request is invalid")));
    }

    output_capacity64 = request.maximum_output_bytes;
    if (request.materialization.maximum_output_bytes < output_capacity64) {
        output_capacity64 = request.materialization.maximum_output_bytes;
    }
    if (output_capacity64 == 0u || output_capacity64 > (uint64_t)MaxAllocSize) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace conversation output capacity exceeds PostgreSQL addressability")));
    }
    output_capacity = (size_t)output_capacity64;
    output = (uint8_t*)palloc(output_capacity);
    next_frame = (uint8_t*)palloc(LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES);

    owners = (laplace_pg_cognition_conversation_product_owners*)palloc0(sizeof(*owners));
    owners->cleanup.func = conversation_product_release;
    owners->cleanup.arg = owners;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &owners->cleanup);

    conversation_product_read_prompt_scope(scope, &prompt_input);
    if (laplace_framework_context_fingerprint(
            &context, &prompt_input.occurrence.context_fingerprint) !=
        LAPLACE_FRAMEWORK_OK) {
        conversation_product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace product prompt context cannot be fingerprinted")));
    }

    laplace_pg_uax29_tables_from_active_unicode(
        &owners->uax29_tables, &uax_authority);
    if ((context.epoch_mask &
         (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE)) == 0u ||
        memcmp(
            context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes,
            uax_authority.activation_epoch_fingerprint.bytes,
            sizeof(uax_authority.activation_epoch_fingerprint.bytes)) != 0) {
        conversation_product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace product prompt Unicode authority does not match the execution context")));
    }
    uax29_fingerprint = conversation_product_uax29_fingerprint(&uax_authority);
    if (laplace_decomposition_uax29_provider_init(
            &uax29_provider, owners->uax29_tables, &uax29_fingerprint) !=
        LAPLACE_DECOMPOSITION_OK) {
        conversation_product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace product prompt UAX29 provider cannot be initialized")));
    }

    prompt_provider = conversation_product_prompt_provider();
    prompt_providers[0] = prompt_provider;
    prompt_providers[1] = uax29_provider.provider;
    prompt_bytes = (uint64_t)VARSIZE_ANY_EXHDR(prompt);
    if (prompt_bytes > (UINT64_MAX - UINT64_C(1)) / UINT64_C(3)) {
        conversation_product_release(owners);
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
        conversation_product_release(owners);
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
        conversation_product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace product prompt persistence stream failed"),
                 errdetail("status=%u", (unsigned int)prompt_status)));
    }

    provider_workspace = (uint64_t)workspace;
    if (request.search_budget.max_memory_bytes < provider_workspace) {
        provider_workspace = request.search_budget.max_memory_bytes;
    }
    if (provider_workspace < UINT64_C(2048) ||
        provider_workspace >= context.resource_grant.memory_bytes) {
        conversation_product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace conversation provider workspace is outside its finite grant")));
    }

    provider_scope.anchor_entity_id = prompt_view.trunk_entity_id;
    provider_scope.world_id = prompt_view.turn.world_id;
    provider_scope.time_fingerprint = prompt_view.turn.time_fingerprint;
    provider_scope.context_fingerprint = prompt_view.turn.context_fingerprint;
    provider_scope.evidence_boundary = request.evidence_boundary;
    provider_scope.evidence_epoch = context.epochs[LAPLACE_FRAMEWORK_EPOCH_EVIDENCE];
    provider_scope.authority_id = context.authority_fingerprint;
    provider_scope.result_contract_fingerprint = request.result_contract_fingerprint;
    provider_scope.search_budget = request.search_budget;
    provider_scope.forward_limits = request.forward_limits;
    provider_scope.relation_mask = structural_mask;
    provider_scope.maximum_results = request.search_budget.requested_path_count;
    provider_scope.flags = request.boundary_flags |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    provider_scope.version = LAPLACE_COGNITION_OBSERVATION_REQUEST_VERSION;
    if (laplace_cognition_observation_request_identify(
            &provider_scope, &physical_report.readset_fingerprint) !=
        LAPLACE_COGNITION_OBSERVATION_REQUEST_OK) {
        conversation_product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace conversation physical provider scope is invalid")));
    }
    memset(&physical_report, 0, sizeof(physical_report));
    laplace_pg_cognition_provider_create(
        &provider_scope,
        provider_workspace,
        &owners->physical,
        &persistent_providers[0]);

    semantic_scope = provider_scope;
    semantic_scope.relation_mask = semantic_mask;
    laplace_pg_semantic_provider_create(
        &semantic_scope,
        (uint64_t)semantic_scope.search_budget.transition_batch_capacity,
        &owners->semantic,
        &persistent_providers[1]);

    conversation_product_realization_provider(
        &realization_fingerprint, &realization);
    laplace_pg_materialization_provider_create(
        &context, &owners->materialization, &materialization);

    conversation_request.cognition_policy.evidence_boundary = request.evidence_boundary;
    conversation_request.cognition_policy.evidence_epoch =
        context.epochs[LAPLACE_FRAMEWORK_EPOCH_EVIDENCE];
    conversation_request.cognition_policy.authority_id = context.authority_fingerprint;
    conversation_request.cognition_policy.result_contract_fingerprint =
        request.result_contract_fingerprint;
    conversation_request.cognition_policy.search_budget = request.search_budget;
    conversation_request.cognition_policy.forward_limits = request.forward_limits;
    conversation_request.cognition_policy.relation_mask =
        LAPLACE_OBSERVATION_QUERY_RELATION_MASK;
    conversation_request.cognition_policy.maximum_results =
        request.search_budget.requested_path_count;
    conversation_request.cognition_policy.request_flags = request.boundary_flags |
        LAPLACE_COGNITION_OBSERVATION_REQUEST_TERMINAL_RESULTS;
    conversation_request.cognition_policy.version = LAPLACE_COGNITION_TURN_POLICY_VERSION;
    conversation_request.realization_policy.modality_id =
        conversation_product_content_identity(media_type);
    conversation_request.realization_policy.realization_recipe_epoch =
        conversation_product_recipe_epoch();
    conversation_request.realization_policy.maximum_candidates = 1u;
    conversation_request.realization_policy.version =
        LAPLACE_COGNITION_CONVERSATION_REALIZATION_POLICY_VERSION;
    conversation_request.materialization = request.materialization;
    conversation_request.discourse_roots.version =
        LAPLACE_COGNITION_CONVERSATION_DISCOURSE_ROOTS_VERSION;
    conversation_request.version = LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION;

    if (SPI_connect() != SPI_OK_CONNECT) {
        conversation_product_release(owners);
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace conversation providers could not connect to PostgreSQL")));
    }
    spi_connected = 1;

    conversation_status = laplace_cognition_prompt_conversation_execute(
        owners->admission,
        &conversation_request,
        VARSIZE_ANY_EXHDR(previous) != 0
            ? (const uint8_t*)VARDATA_ANY(previous)
            : NULL,
        (size_t)VARSIZE_ANY_EXHDR(previous),
        persistent_providers,
        2u,
        &realization,
        &materialization,
        output,
        output_capacity,
        &output_bytes,
        next_frame,
        LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES,
        &next_frame_bytes,
        &conversation_result);

    PG_TRY();
    {
        if (spi_connected != 0 && SPI_finish() != SPI_OK_FINISH) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Laplace conversation provider SPI cleanup failed")));
        }
        spi_connected = 0;
        if (owners->physical != NULL) {
            laplace_pg_cognition_provider_summary(owners->physical, &physical_report);
            physical_error = laplace_pg_cognition_provider_take_error(owners->physical);
            laplace_pg_cognition_provider_destroy(&owners->physical);
        }
        if (owners->semantic != NULL) {
            laplace_pg_semantic_provider_summary(owners->semantic, &semantic_report);
            semantic_error = laplace_pg_semantic_provider_take_error(owners->semantic);
            laplace_pg_semantic_provider_destroy(&owners->semantic);
        }
        if (owners->materialization != NULL) {
            laplace_pg_materialization_provider_summary(
                owners->materialization, &materialization_report);
            materialization_error =
                laplace_pg_materialization_provider_take_error(owners->materialization);
        }
    }
    PG_CATCH();
    {
        conversation_product_release(owners);
        PG_RE_THROW();
    }
    PG_END_TRY();

    if (physical_error != NULL || semantic_error != NULL || materialization_error != NULL) {
        ErrorData* rethrow = physical_error != NULL
            ? physical_error
            : (semantic_error != NULL ? semantic_error : materialization_error);
        if (physical_error != NULL && semantic_error != NULL) FreeErrorData(semantic_error);
        if (physical_error != NULL && materialization_error != NULL) FreeErrorData(materialization_error);
        if (physical_error == NULL && semantic_error != NULL && materialization_error != NULL)
            FreeErrorData(materialization_error);
        conversation_product_release(owners);
        ReThrowError(rethrow);
    }

    if (conversation_status == LAPLACE_COGNITION_PROMPT_CONVERSATION_OK) {
        if (next_frame_bytes != LAPLACE_COGNITION_DISCOURSE_FRAME_BYTES ||
            output_bytes > output_capacity) {
            conversation_product_release(owners);
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace conversation published an invalid output/frame size")));
        }
        conversation_product_deposit_frame(
            next_frame,
            next_frame_bytes,
            &conversation_result.conversation.discourse_state.state_id);
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
        request.selected_program.bytes, sizeof(request.selected_program.bytes)));
    values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(output, output_bytes));

    if (conversation_status == LAPLACE_COGNITION_PROMPT_CONVERSATION_OK) {
        const laplace_cognition_conversation_result* result =
            &conversation_result.conversation;
        if (result->forward_receipt.layer_count > (uint64_t)INT_MAX) {
            conversation_product_release(owners);
            ereport(ERROR,
                    (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                     errmsg("Laplace conversation layer count exceeds SQL integer range")));
        }
        values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            result->materialization_receipt.output_fingerprint.bytes,
            sizeof(result->materialization_receipt.output_fingerprint.bytes)));
        values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            next_frame, next_frame_bytes));
        values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            result->discourse_frame_receipt.frame_fingerprint.bytes,
            sizeof(result->discourse_frame_receipt.frame_fingerprint.bytes)));
        values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            result->conversation_id.bytes, sizeof(result->conversation_id.bytes)));
        values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            result->forward_receipt.layer_trace_fingerprint.bytes,
            sizeof(result->forward_receipt.layer_trace_fingerprint.bytes)));
        values[11] = Int32GetDatum((int32)result->forward_receipt.layer_count);
        values[12] = Int32GetDatum(1);
        values[13] = laplace_pg_numeric_from_uint64(result->forward_receipt.layer_count);
        values[14] = laplace_pg_numeric_from_uint64(result->forward_receipt.provider_call_count);
        values[15] = laplace_pg_numeric_from_uint64(result->forward_receipt.resource_cost);
        values[16] = laplace_pg_numeric_from_uint64(result->forward_receipt.io_operations);
        values[17] = laplace_pg_numeric_from_uint64(result->forward_receipt.database_operations);
    } else {
        nulls[6] = true;
        values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(NULL, 0u));
        nulls[8] = true;
        nulls[9] = true;
        nulls[10] = true;
        values[11] = Int32GetDatum(0);
        values[12] = Int32GetDatum(0);
        values[13] = laplace_pg_numeric_from_uint64(0u);
        values[14] = laplace_pg_numeric_from_uint64(0u);
        values[15] = laplace_pg_numeric_from_uint64(0u);
        values[16] = laplace_pg_numeric_from_uint64(0u);
        values[17] = laplace_pg_numeric_from_uint64(0u);
    }

    values[18] = laplace_pg_numeric_from_uint64(physical_report.rows_fetched);
    values[19] = laplace_pg_numeric_from_uint64(physical_report.batch_count);
    values[20] = laplace_pg_numeric_from_uint64(semantic_report.rows_examined);
    values[21] = laplace_pg_numeric_from_uint64(semantic_report.database_operations);
    values[22] = laplace_pg_numeric_from_uint64(materialization_report.resolved_nodes);
    values[23] = laplace_pg_numeric_from_uint64(materialization_report.trajectory_reads);
    values[24] = laplace_pg_numeric_from_uint64(materialization_report.trajectory_bytes);
    values[25] = laplace_pg_numeric_from_uint64(materialization_report.database_operations);
    values[26] = Int32GetDatum((int32)conversation_status);
    values[27] = Int32GetDatum(-1);
    values[28] = Int32GetDatum((int32)conversation_status);
    values[29] = Int32GetDatum(LAPLACE_COGNITION_PROMPT_CONVERSATION_VERSION);

    conversation_product_release(owners);
    PG_RETURN_DATUM(HeapTupleGetDatum(
        laplace_pg_form_result_tuple(fcinfo, values, nulls, 30)));
}
