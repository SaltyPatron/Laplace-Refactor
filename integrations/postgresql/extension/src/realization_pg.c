#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "utils/memutils.h"

#include "blake3.h"
#include "laplace_pg_internal.h"
#include "realization_pg.h"

#define LAPLACE_PG_EXACT_REALIZATION_PROVIDER_DOMAIN \
    "laplace-postgresql-exact-realization-provider-v1"
#define LAPLACE_PG_EXACT_REALIZATION_CANDIDATE_DOMAIN \
    "laplace-postgresql-exact-realization-candidate-v1"
#define LAPLACE_PG_EXACT_REALIZATION_RECEIPT_DOMAIN \
    "laplace-postgresql-exact-realization-receipt-v1"
#define LAPLACE_PG_EXACT_REALIZATION_MISSING_DOMAIN \
    "laplace-postgresql-exact-realization-missing-v1"

struct laplace_pg_exact_realization_provider_state {
    laplace_framework_context context;
    laplace_digest256 provider_fingerprint;
    uint64_t rows_examined;
    uint64_t database_operations;
    uint64_t provider_calls;
    MemoryContext caller_context;
    MemoryContext scratch_context;
    ErrorData* error;
    MemoryContextCallback cleanup;
};

static bool realization_same_digest(
    const laplace_digest256* left,
    const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static void realization_hash_u32(blake3_hasher* hasher, uint32_t value) {
    uint8_t bytes[4];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void realization_hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t bytes[8];
    size_t index;
    for (index = 0u; index < sizeof(bytes); ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, bytes, sizeof(bytes));
}

static void realization_hash_id(
    blake3_hasher* hasher,
    const laplace_id128* value) {
    blake3_hasher_update(hasher, value->bytes, sizeof(value->bytes));
}

static void realization_hash_digest(
    blake3_hasher* hasher,
    const laplace_digest256* value) {
    blake3_hasher_update(hasher, value->bytes, sizeof(value->bytes));
}

static void realization_identify_provider(
    const laplace_framework_context* context,
    laplace_digest256* output) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher,
        LAPLACE_PG_EXACT_REALIZATION_PROVIDER_DOMAIN,
        sizeof(LAPLACE_PG_EXACT_REALIZATION_PROVIDER_DOMAIN) - 1u);
    realization_hash_digest(&hasher, &context->authority_fingerprint);
    realization_hash_digest(
        &hasher, &context->epochs[LAPLACE_FRAMEWORK_EPOCH_EVIDENCE]);
    realization_hash_digest(
        &hasher, &context->epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY]);
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

static void realization_missing_obligation(
    const laplace_pg_exact_realization_provider_state* state,
    const laplace_cognition_semantic_act* act,
    const laplace_cognition_realization_request* request,
    uint32_t reason,
    laplace_digest256* output) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher,
        LAPLACE_PG_EXACT_REALIZATION_MISSING_DOMAIN,
        sizeof(LAPLACE_PG_EXACT_REALIZATION_MISSING_DOMAIN) - 1u);
    realization_hash_digest(&hasher, &state->provider_fingerprint);
    realization_hash_digest(&hasher, &act->act_id);
    realization_hash_id(&hasher, &act->primary_answer.entity_id);
    realization_hash_id(&hasher, &request->modality_id);
    realization_hash_id(&hasher, &request->language_id);
    realization_hash_id(&hasher, &request->register_id);
    realization_hash_digest(&hasher, &request->evidence_epoch);
    realization_hash_digest(&hasher, &request->realization_recipe_epoch);
    realization_hash_digest(&hasher, &request->context_fingerprint);
    realization_hash_u32(&hasher, reason);
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

static void realization_candidate_receipt(
    const laplace_pg_exact_realization_provider_state* state,
    const laplace_cognition_semantic_act* act,
    const laplace_cognition_realization_request* request,
    const laplace_digest256* identity_witness,
    laplace_digest256* output) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher,
        LAPLACE_PG_EXACT_REALIZATION_CANDIDATE_DOMAIN,
        sizeof(LAPLACE_PG_EXACT_REALIZATION_CANDIDATE_DOMAIN) - 1u);
    realization_hash_digest(&hasher, &state->provider_fingerprint);
    realization_hash_digest(&hasher, &act->act_id);
    realization_hash_digest(&hasher, &act->forward_receipt_id);
    realization_hash_id(&hasher, &act->primary_answer.entity_id);
    realization_hash_digest(&hasher, identity_witness);
    realization_hash_id(&hasher, &request->modality_id);
    realization_hash_digest(&hasher, &request->evidence_epoch);
    realization_hash_digest(&hasher, &request->realization_recipe_epoch);
    realization_hash_digest(&hasher, &request->context_fingerprint);
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

static void realization_provider_receipt(
    const laplace_pg_exact_realization_provider_state* state,
    const laplace_cognition_semantic_act* act,
    const laplace_cognition_realization_request* request,
    const laplace_digest256* candidate_receipt,
    const laplace_digest256* missing,
    uint64_t rows,
    uint32_t disposition,
    laplace_digest256* output) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher,
        LAPLACE_PG_EXACT_REALIZATION_RECEIPT_DOMAIN,
        sizeof(LAPLACE_PG_EXACT_REALIZATION_RECEIPT_DOMAIN) - 1u);
    realization_hash_digest(&hasher, &state->provider_fingerprint);
    realization_hash_digest(&hasher, &act->act_id);
    realization_hash_id(&hasher, &request->modality_id);
    realization_hash_id(&hasher, &request->language_id);
    realization_hash_id(&hasher, &request->register_id);
    realization_hash_digest(&hasher, &request->evidence_epoch);
    realization_hash_digest(&hasher, &request->realization_recipe_epoch);
    realization_hash_digest(&hasher, &request->context_fingerprint);
    realization_hash_digest(&hasher, candidate_receipt);
    realization_hash_digest(&hasher, missing);
    realization_hash_u64(&hasher, rows);
    realization_hash_u32(&hasher, disposition);
    blake3_hasher_finalize(&hasher, output->bytes, sizeof(output->bytes));
}

static void realization_read_digest(
    Datum datum,
    laplace_digest256* output,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(output->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace exact realization %s has invalid width", field)));
    }
    memcpy(output->bytes, VARDATA_ANY(value), sizeof(output->bytes));
}

static int realization_enumerate_impl(
    laplace_pg_exact_realization_provider_state* state,
    const laplace_cognition_semantic_act* act,
    const laplace_cognition_realization_request* request,
    laplace_cognition_realization_candidate* candidates,
    size_t candidate_capacity,
    size_t* candidate_count,
    laplace_cognition_realization_usage* usage) {
    static const char query[] =
        "SELECT identity_witness FROM " LAPLACE_PG_SCHEMA ".entity "
        "WHERE entity_id=$1";
    Oid types[1] = {BYTEAOID};
    Datum values[1];
    laplace_digest256 witness;
    laplace_digest256 candidate_receipt;
    laplace_digest256 missing;
    int result;
    uint32_t disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;

    memset(&witness, 0, sizeof(witness));
    memset(&candidate_receipt, 0, sizeof(candidate_receipt));
    memset(&missing, 0, sizeof(missing));
    *candidate_count = 0u;
    memset(usage, 0, sizeof(*usage));

    if (!realization_same_digest(
            &request->evidence_epoch,
            &state->context.epochs[LAPLACE_FRAMEWORK_EPOCH_EVIDENCE])) {
        realization_missing_obligation(state, act, request, 1u, &missing);
        disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_INCOMPLETE;
        usage->missing_obligation_fingerprint = missing;
        usage->missing_obligation_count = 1u;
        usage->disposition = disposition;
        realization_provider_receipt(
            state, act, request, &candidate_receipt, &missing, 0u,
            disposition, &usage->provider_receipt_id);
        return 0;
    }

    /* Language and register require independently admitted realization state.
     * Exact content reuse cannot certify either one from content identity alone. */
    if ((request->flags &
         (LAPLACE_COGNITION_REALIZATION_LANGUAGE_PRESENT |
          LAPLACE_COGNITION_REALIZATION_REGISTER_PRESENT)) != 0u) {
        realization_missing_obligation(state, act, request, 2u, &missing);
        disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_UNSUPPORTED;
        usage->missing_obligation_fingerprint = missing;
        usage->missing_obligation_count = 1u;
        usage->disposition = disposition;
        realization_provider_receipt(
            state, act, request, &candidate_receipt, &missing, 0u,
            disposition, &usage->provider_receipt_id);
        return 0;
    }

    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        act->primary_answer.entity_id.bytes,
        sizeof(act->primary_answer.entity_id.bytes)));
    result = SPI_execute_with_args(query, 1, types, values, NULL, true, 1);
    ++state->database_operations;
    ++state->provider_calls;
    if (result != SPI_OK_SELECT || SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace exact realization canonical content lookup failed")));
    }
    state->rows_examined += (uint64_t)SPI_processed;
    usage->rows_examined = (uint64_t)SPI_processed;
    usage->exact_whole_examined = (uint64_t)SPI_processed;

    if (SPI_processed == 0u) {
        realization_missing_obligation(state, act, request, 3u, &missing);
        disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_INCOMPLETE;
        usage->missing_obligation_fingerprint = missing;
        usage->missing_obligation_count = 1u;
        usage->disposition = disposition;
        realization_provider_receipt(
            state, act, request, &candidate_receipt, &missing, 0u,
            disposition, &usage->provider_receipt_id);
        SPI_freetuptable(SPI_tuptable);
        return 0;
    }

    realization_read_digest(
        SPI_getbinval(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, NULL),
        &witness, "identity witness");
    if (memcmp(
            witness.bytes, act->primary_answer.entity_id.bytes,
            sizeof(act->primary_answer.entity_id.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace exact realization entity witness disagrees with content identity")));
    }
    if (candidate_capacity == 0u || candidates == NULL) {
        realization_missing_obligation(state, act, request, 4u, &missing);
        disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_INCOMPLETE;
        usage->missing_obligation_fingerprint = missing;
        usage->missing_obligation_count = 1u;
        usage->disposition = disposition;
        realization_provider_receipt(
            state, act, request, &candidate_receipt, &missing,
            (uint64_t)SPI_processed, disposition, &usage->provider_receipt_id);
        SPI_freetuptable(SPI_tuptable);
        return 0;
    }

    realization_candidate_receipt(
        state, act, request, &witness, &candidate_receipt);
    memset(&candidates[0], 0, sizeof(candidates[0]));
    candidates[0].content_id = act->primary_answer.entity_id;
    candidates[0].candidate_receipt_id = candidate_receipt;
    candidates[0].realization_recipe_id = request->realization_recipe_epoch;
    candidates[0].obligation_fingerprint = act->act_id;
    candidates[0].preference_rank = 0u;
    candidates[0].reused_subtree_count = 1u;
    candidates[0].generated_composition_count = 0u;
    candidates[0].structural_tier = 0u;
    candidates[0].match_class =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_EXACT_WHOLE;
    candidates[0].missing_obligation_count = 0u;
    candidates[0].flags =
        LAPLACE_COGNITION_REALIZATION_CANDIDATE_REUSED_WITNESSED;
    *candidate_count = 1u;
    usage->disposition = LAPLACE_COGNITION_REALIZATION_DISPOSITION_COMPLETE;
    realization_provider_receipt(
        state, act, request, &candidate_receipt, &missing,
        (uint64_t)SPI_processed, usage->disposition,
        &usage->provider_receipt_id);
    SPI_freetuptable(SPI_tuptable);
    return 0;
}

static int realization_enumerate(
    void* opaque,
    const laplace_cognition_semantic_act* act,
    const laplace_cognition_realization_request* request,
    laplace_cognition_realization_candidate* candidates,
    size_t candidate_capacity,
    size_t* candidate_count,
    laplace_cognition_realization_usage* usage) {
    laplace_pg_exact_realization_provider_state* state =
        (laplace_pg_exact_realization_provider_state*)opaque;
    volatile int status = 0;
    MemoryContext previous;

    if (candidate_count != NULL) {
        *candidate_count = 0u;
    }
    if (usage != NULL) {
        memset(usage, 0, sizeof(*usage));
    }
    if (state == NULL || act == NULL || request == NULL ||
        candidate_count == NULL || usage == NULL || state->error != NULL ||
        state->scratch_context == NULL ||
        (candidate_capacity != 0u && candidates == NULL)) {
        return 1;
    }

    previous = MemoryContextSwitchTo(state->scratch_context);
    PG_TRY();
    {
        status = realization_enumerate_impl(
            state, act, request, candidates, candidate_capacity,
            candidate_count, usage);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(state->caller_context);
        state->error = CopyErrorData();
        FlushErrorState();
        *candidate_count = 0u;
        memset(usage, 0, sizeof(*usage));
        status = 1;
    }
    PG_END_TRY();
    MemoryContextSwitchTo(previous);
    MemoryContextReset(state->scratch_context);
    return (int)status;
}

static void realization_provider_release(void* opaque) {
    laplace_pg_exact_realization_provider_state* state =
        (laplace_pg_exact_realization_provider_state*)opaque;
    if (state == NULL) {
        return;
    }
    state->scratch_context = NULL;
    if (state->error != NULL) {
        FreeErrorData(state->error);
        state->error = NULL;
    }
}

void laplace_pg_exact_realization_provider_create(
    const laplace_framework_context* context,
    laplace_pg_exact_realization_provider_state** owner,
    laplace_cognition_realization_provider_v1* provider) {
    laplace_pg_exact_realization_provider_state* state;
    const uint64_t required_epochs =
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_EVIDENCE) |
        (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_GEOMETRY);

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
                 errmsg("Laplace exact realization provider requires pinned evidence and geometry epochs")));
    }

    state = (laplace_pg_exact_realization_provider_state*)palloc0(sizeof(*state));
    state->context = *context;
    state->caller_context = CurrentMemoryContext;
    state->scratch_context = AllocSetContextCreate(
        CurrentMemoryContext,
        "Laplace exact realization read batch",
        ALLOCSET_DEFAULT_SIZES);
    state->cleanup.func = realization_provider_release;
    state->cleanup.arg = state;
    MemoryContextRegisterResetCallback(CurrentMemoryContext, &state->cleanup);
    realization_identify_provider(context, &state->provider_fingerprint);

    provider->state = state;
    provider->provider_fingerprint = state->provider_fingerprint;
    provider->maximum_candidate_records = 1u;
    provider->enumerate = realization_enumerate;
    provider->abi_major = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MAJOR;
    provider->abi_minor = LAPLACE_COGNITION_REALIZATION_PROVIDER_ABI_MINOR;
    *owner = state;
}

void laplace_pg_exact_realization_provider_summary(
    const laplace_pg_exact_realization_provider_state* owner,
    laplace_pg_exact_realization_provider_report* report) {
    if (report == NULL) {
        return;
    }
    memset(report, 0, sizeof(*report));
    if (owner == NULL) {
        return;
    }
    report->provider_fingerprint = owner->provider_fingerprint;
    report->rows_examined = owner->rows_examined;
    report->database_operations = owner->database_operations;
    report->provider_calls = owner->provider_calls;
}

ErrorData* laplace_pg_exact_realization_provider_take_error(
    laplace_pg_exact_realization_provider_state* owner) {
    ErrorData* error;
    if (owner == NULL) {
        return NULL;
    }
    error = owner->error;
    owner->error = NULL;
    return error;
}

void laplace_pg_exact_realization_provider_destroy(
    laplace_pg_exact_realization_provider_state** owner) {
    laplace_pg_exact_realization_provider_state* state;
    if (owner == NULL || *owner == NULL) {
        return;
    }
    state = *owner;
    *owner = NULL;
    state->cleanup.func = NULL;
    state->cleanup.arg = NULL;
    if (state->scratch_context != NULL) {
        MemoryContextDelete(state->scratch_context);
        state->scratch_context = NULL;
    }
    if (state->error != NULL) {
        FreeErrorData(state->error);
        state->error = NULL;
    }
    pfree(state);
}
