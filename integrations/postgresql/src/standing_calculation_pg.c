#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/isa.h"
#include "laplace/standing_calculation.h"
#include "laplace_pg_internal.h"
#include "set_pg.h"

#ifndef LAPLACE_PG_STANDING_ENTRYPOINT
#define LAPLACE_PG_STANDING_ENTRYPOINT LAPLACE_PG_EVIDENCE_STANDING_SYMBOL
#endif

PG_FUNCTION_INFO_V1(LAPLACE_PG_STANDING_ENTRYPOINT);

static int compare_period_input(const void* left, const void* right) {
    const laplace_standing_period_input* first =
        (const laplace_standing_period_input*)left;
    const laplace_standing_period_input* second =
        (const laplace_standing_period_input*)right;
    return memcmp(first->event.event_id.bytes, second->event.event_id.bytes, 32u);
}

static int32 required_nonnegative_int32(
    HeapTupleHeader tuple, int attribute, const char* field) {
    const int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace standing %s cannot be negative", field)));
    }
    return value;
}

static void read_score_array(
    Datum datum, uint64_t output[LAPLACE_STANDING_OUTCOME_KIND_COUNT],
    const char* field) {
    ArrayType* array = DatumGetArrayTypeP(datum);
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    int index;
    if (ARR_NDIM(array) != 1 ||
        ARR_DIMS(array)[0] != (int)LAPLACE_STANDING_OUTCOME_KIND_COUNT ||
        ARR_LBOUND(array)[0] != 1 || ARR_ELEMTYPE(array) != NUMERICOID) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace standing %s must be an exact nine-element numeric array", field)));
    }
    get_typlenbyvalalign(
        NUMERICOID, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, NUMERICOID, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    for (index = 0; index < count; ++index) {
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace standing %s cannot contain nulls", field)));
        }
        output[index] = laplace_pg_uint64_from_numeric(values[index], field);
    }
}

static void read_recipe(
    HeapTupleHeader tuple, laplace_standing_recipe* recipe) {
    memset(recipe, 0, sizeof(*recipe));
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 1, "standing recipe_id"),
        &recipe->recipe_id, "standing recipe_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 2, "standing authority_receipt_id"),
        &recipe->authority_receipt_id, "standing authority_receipt_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 3, "standing evaluation_law_id"),
        &recipe->evaluation_law_id, "standing evaluation_law_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 4, "standing world_context_id"),
        &recipe->world_context_id, "standing world_context_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 5, "standing language_modality_id"),
        &recipe->language_modality_id, "standing language_modality_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 6, "standing valid_time_scope_id"),
        &recipe->valid_time_scope_id, "standing valid_time_scope_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 7, "standing evidence_boundary_id"),
        &recipe->evidence_boundary_id, "standing evidence_boundary_id");
    recipe->default_rating = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 8, "standing default_rating"));
    recipe->default_rating_deviation = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 9, "standing default_rating_deviation"));
    recipe->default_volatility = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 10, "standing default_volatility"));
    recipe->volatility_constraint = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 11, "standing volatility_constraint"));
    recipe->convergence_tolerance = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 12, "standing convergence_tolerance"));
    read_score_array(
        laplace_pg_required_composite_attribute(tuple, 13, "standing score_numerator"),
        recipe->score_numerator, "score_numerator");
    read_score_array(
        laplace_pg_required_composite_attribute(tuple, 14, "standing score_denominator"),
        recipe->score_denominator, "score_denominator");
    recipe->rateable_outcome_mask = (uint32_t)required_nonnegative_int32(
        tuple, 15, "rateable_outcome_mask");
    recipe->participant_role = (uint32_t)required_nonnegative_int32(
        tuple, 16, "participant_role");
    recipe->arena_kind = (uint32_t)required_nonnegative_int32(
        tuple, 17, "arena_kind");
    recipe->version = (uint32_t)required_nonnegative_int32(
        tuple, 18, "recipe version");
    recipe->flags = (uint32_t)required_nonnegative_int32(
        tuple, 19, "recipe flags");
}

static void read_state(
    HeapTupleHeader tuple,
    laplace_standing_state* state,
    const char* field_prefix) {
    memset(state, 0, sizeof(*state));
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 1, "standing state_id"),
        &state->state_id, field_prefix);
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 2, "standing coordinate_id"),
        &state->coordinate_id, field_prefix);
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 3, "standing arena_scope_id"),
        &state->arena_scope_id, field_prefix);
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 4, "standing prior_state_id"),
        &state->prior_state_id, field_prefix);
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 5, "standing epoch_id"),
        &state->epoch_id, field_prefix);
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 6, "standing rating_recipe_id"),
        &state->rating_recipe_id, field_prefix);
    state->rating = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 7, "standing rating"));
    state->rating_deviation = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 8, "standing rating_deviation"));
    state->volatility = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 9, "standing volatility"));
    state->eligible_match_count = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 10, "standing eligible_match_count"),
        "standing eligible_match_count");
    state->period_ordinal = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 11, "standing period_ordinal"),
        "standing period_ordinal");
    state->rating_recipe_version = (uint32_t)required_nonnegative_int32(
        tuple, 12, "rating_recipe_version");
    state->flags = (uint32_t)required_nonnegative_int32(tuple, 13, "flags");
}

static void read_event(
    HeapTupleHeader tuple,
    laplace_standing_event* event) {
    HeapTupleHeader opponent;
    memset(event, 0, sizeof(*event));
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 1, "standing event_id"),
        &event->event_id, "standing event_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 2, "standing participant_coordinate_id"),
        &event->participant_coordinate_id, "standing participant_coordinate_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 3, "standing participant_prior_state_id"),
        &event->participant_prior_state_id, "standing participant_prior_state_id");
    opponent = DatumGetHeapTupleHeader(
        laplace_pg_required_composite_attribute(tuple, 4, "standing opponent_prior_state"));
    read_state(opponent, &event->opponent_prior_state, "standing opponent_prior_state");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 5, "standing period_id"),
        &event->period_id, "standing period_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 6, "standing eligible_root_id"),
        &event->eligible_root_id, "standing eligible_root_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 7, "standing outcome_mapping_id"),
        &event->outcome_mapping_id, "standing outcome_mapping_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 8, "standing context_id"),
        &event->context_id, "standing context_id");
    laplace_pg_read_digest(
        laplace_pg_required_composite_attribute(tuple, 9, "standing valid_time_id"),
        &event->valid_time_id, "standing valid_time_id");
    event->score_numerator = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 10, "standing score_numerator"),
        "standing score_numerator");
    event->score_denominator = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 11, "standing score_denominator"),
        "standing score_denominator");
    event->outcome_kind = (uint32_t)required_nonnegative_int32(
        tuple, 12, "outcome_kind");
    event->flags = (uint32_t)required_nonnegative_int32(tuple, 13, "event flags");
}

static laplace_standing_period_input* read_inputs(
    ArrayType* array,
    size_t* input_count) {
    const Oid type_oid = laplace_pg_composite_type_oid("standing_period_input");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_standing_period_input* inputs;
    int index;
    if (ARR_NDIM(array) != 1 || ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace standing input must be a one-dimensional exact standing_period_input array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace standing period cannot be empty")));
    }
    inputs = (laplace_standing_period_input*)palloc0(
        sizeof(*inputs) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader input;
        HeapTupleHeader recipe;
        HeapTupleHeader prior;
        HeapTupleHeader event;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace standing input cannot contain null records")));
        }
        input = DatumGetHeapTupleHeader(values[index]);
        recipe = DatumGetHeapTupleHeader(
            laplace_pg_required_composite_attribute(input, 1, "standing recipe"));
        prior = DatumGetHeapTupleHeader(
            laplace_pg_required_composite_attribute(input, 2, "standing prior_state"));
        event = DatumGetHeapTupleHeader(
            laplace_pg_required_composite_attribute(input, 3, "standing event"));
        read_recipe(recipe, &inputs[index].recipe);
        read_state(prior, &inputs[index].prior_state, "standing prior_state");
        read_event(event, &inputs[index].event);
    }
    *input_count = (size_t)count;
    return inputs;
}

static void require_recipe_admission(
    Oid input_array_type,
    Datum input_array,
    const laplace_framework_context* context) {
#if !defined(LAPLACE_TEST_STANDING_ADMISSION_BYPASS)
    static const char admission_sql[] =
        "WITH input AS MATERIALIZED (SELECT DISTINCT ON ((i.recipe).recipe_id) (i.recipe).*"
        " FROM unnest($1::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i"
        " ORDER BY (i.recipe).recipe_id)"
        " SELECT NOT EXISTS (SELECT FROM input i"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".standing_recipe_admission a USING(recipe_id)"
        " WHERE a.recipe_id IS NULL OR a.authority_receipt_id<>i.authority_receipt_id"
        " OR a.authority_fingerprint<>$2 OR a.evidence_epoch<>$3)";
    static const char history_sql[] =
        "WITH input AS MATERIALIZED (SELECT DISTINCT ON ((i.recipe).recipe_id) (i.recipe).*"
        " FROM unnest($1::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i"
        " ORDER BY (i.recipe).recipe_id)"
        " SELECT NOT EXISTS (SELECT FROM input i"
        " LEFT JOIN " LAPLACE_PG_SCHEMA ".standing_recipe_history r USING(recipe_id)"
        " WHERE r.recipe_id IS NULL OR r.authority_receipt_id<>i.authority_receipt_id"
        " OR r.evaluation_law_id<>i.evaluation_law_id OR r.world_context_id<>i.world_context_id"
        " OR r.language_modality_id<>i.language_modality_id OR r.valid_time_scope_id<>i.valid_time_scope_id"
        " OR r.evidence_boundary_id<>i.evidence_boundary_id OR r.default_rating IS DISTINCT FROM i.default_rating"
        " OR r.default_rating_deviation IS DISTINCT FROM i.default_rating_deviation"
        " OR r.default_volatility IS DISTINCT FROM i.default_volatility"
        " OR r.volatility_constraint IS DISTINCT FROM i.volatility_constraint"
        " OR r.convergence_tolerance IS DISTINCT FROM i.convergence_tolerance"
        " OR r.score_numerator<>i.score_numerator OR r.score_denominator<>i.score_denominator"
        " OR r.rateable_outcome_mask<>i.rateable_outcome_mask OR r.participant_role<>i.participant_role"
        " OR r.arena_kind<>i.arena_kind OR r.version<>i.version OR r.flags<>i.flags)";
    Oid types[3] = {input_array_type, BYTEAOID, BYTEAOID};
    Datum values[3];
    int status;
    bool is_null = false;
    Datum admitted;
    values[0] = input_array;
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        context->authority_fingerprint.bytes, 32u));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        context->epochs[LAPLACE_FRAMEWORK_EPOCH_EVIDENCE].bytes, 32u));
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,(errcode(ERRCODE_CONNECTION_FAILURE),
                       errmsg("Laplace standing admission could not connect to SPI")));
    }
    status = SPI_execute_with_args(admission_sql, 3, types, values, NULL, true, 1);
    if (status != SPI_OK_SELECT || SPI_processed != 1u) {
        ereport(ERROR,(errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Laplace standing admission readback failed")));
    }
    admitted = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null || !DatumGetBool(admitted)) {
        ereport(ERROR,(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                       errmsg("Laplace standing recipe is not admitted for the active authority and evidence epoch")));
    }
    status = SPI_execute_with_args(history_sql, 1, types, values, NULL, true, 1);
    if (status != SPI_OK_SELECT || SPI_processed != 1u) {
        ereport(ERROR,(errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Laplace standing recipe history readback failed")));
    }
    admitted = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null || !DatumGetBool(admitted)) {
        ereport(ERROR,(errcode(ERRCODE_DATA_CORRUPTED),
                       errmsg("Laplace standing admitted recipe history is corrupt")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,(errcode(ERRCODE_INTERNAL_ERROR),
                       errmsg("Laplace standing admission could not close SPI")));
    }
#else
    (void)input_array_type;
    (void)input_array;
    (void)context;
#endif
}

static void persist_standing(
    Oid input_array_type,
    Datum input_array,
    const laplace_standing_period_result* result,
    const laplace_isa_receipt* isa_receipt) {
    static const char initial_write_sql[] =
        "WITH raw AS ("
        " SELECT (i.prior_state).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i"
        " UNION ALL"
        " SELECT ((i.event).opponent_prior_state).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i"
        "), initial AS (SELECT DISTINCT ON (state_id) * FROM raw WHERE period_ordinal=0 ORDER BY state_id)"
        " INSERT INTO " LAPLACE_PG_SCHEMA ".standing_state_history(state_id,coordinate_id,arena_scope_id,prior_state_id,epoch_id,rating_recipe_id,rating,rating_deviation,volatility,eligible_match_count,period_ordinal,rating_recipe_version,flags)"
        " SELECT state_id,coordinate_id,arena_scope_id,NULL,epoch_id,rating_recipe_id,rating,rating_deviation,volatility,eligible_match_count,period_ordinal,rating_recipe_version,flags FROM initial ON CONFLICT DO NOTHING";
#if !defined(LAPLACE_TEST_STANDING_REPLAY_VERIFY_BYPASS)
    static const char states_verify_sql[] =
        "WITH raw AS ("
        " SELECT (i.prior_state).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i"
        " UNION ALL"
        " SELECT ((i.event).opponent_prior_state).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i"
        "), input AS (SELECT DISTINCT ON (state_id) * FROM raw ORDER BY state_id)"
        " SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".standing_state_history s USING(state_id)"
        " WHERE s.state_id IS NULL OR s.coordinate_id<>i.coordinate_id OR s.arena_scope_id<>i.arena_scope_id"
        " OR s.prior_state_id IS DISTINCT FROM CASE WHEN i.period_ordinal=0 THEN NULL ELSE i.prior_state_id END"
        " OR s.epoch_id<>i.epoch_id OR s.rating_recipe_id<>i.rating_recipe_id OR s.rating IS DISTINCT FROM i.rating"
        " OR s.rating_deviation IS DISTINCT FROM i.rating_deviation OR s.volatility IS DISTINCT FROM i.volatility"
        " OR s.eligible_match_count<>i.eligible_match_count OR s.period_ordinal<>i.period_ordinal"
        " OR s.rating_recipe_version<>i.rating_recipe_version OR s.flags<>i.flags)";
#endif
    static const char event_write_sql[] =
        "WITH input AS (SELECT (i.event).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i)"
        " INSERT INTO " LAPLACE_PG_SCHEMA ".standing_match_event(event_id,participant_coordinate_id,participant_prior_state_id,opponent_prior_state_id,period_id,eligible_root_id,outcome_mapping_id,context_id,valid_time_id,score_numerator,score_denominator,outcome_kind,flags)"
        " SELECT event_id,participant_coordinate_id,participant_prior_state_id,(opponent_prior_state).state_id,period_id,eligible_root_id,outcome_mapping_id,context_id,valid_time_id,score_numerator,score_denominator,outcome_kind,flags FROM input ON CONFLICT DO NOTHING";
    static const char event_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT (i.event).* FROM unnest($1::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i)"
        " SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".standing_match_event e USING(event_id)"
        " WHERE e.event_id IS NULL OR e.participant_coordinate_id<>i.participant_coordinate_id OR e.participant_prior_state_id<>i.participant_prior_state_id"
        " OR e.opponent_prior_state_id<>(i.opponent_prior_state).state_id OR e.period_id<>i.period_id OR e.eligible_root_id<>i.eligible_root_id"
        " OR e.outcome_mapping_id<>i.outcome_mapping_id OR e.context_id<>i.context_id OR e.valid_time_id<>i.valid_time_id"
        " OR e.score_numerator<>i.score_numerator OR e.score_denominator<>i.score_denominator OR e.outcome_kind<>i.outcome_kind OR e.flags<>i.flags)";
    static const char state_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".standing_state_history(state_id,coordinate_id,arena_scope_id,prior_state_id,epoch_id,rating_recipe_id,rating,rating_deviation,volatility,eligible_match_count,period_ordinal,rating_recipe_version,flags)"
        " VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13) ON CONFLICT DO NOTHING";
    static const char state_verify_sql[] =
        "SELECT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".standing_state_history WHERE state_id=$1 AND coordinate_id=$2 AND arena_scope_id=$3 AND prior_state_id=$4 AND epoch_id=$5 AND rating_recipe_id=$6 AND rating IS NOT DISTINCT FROM $7 AND rating_deviation IS NOT DISTINCT FROM $8 AND volatility IS NOT DISTINCT FROM $9 AND eligible_match_count=$10 AND period_ordinal=$11 AND rating_recipe_version=$12 AND flags=$13)";
    static const char receipt_write_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".standing_period_receipt(receipt_id,prior_state_id,successor_state_id,period_id,input_fingerprint,output_fingerprint,isa_receipt_id,eligible_event_count,prior_match_count,successor_match_count,volatility_iterations,version,flags)"
        " VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13) ON CONFLICT DO NOTHING";
    static const char receipt_verify_sql[] =
        "SELECT EXISTS (SELECT FROM " LAPLACE_PG_SCHEMA ".standing_period_receipt WHERE receipt_id IS NOT DISTINCT FROM $1 AND prior_state_id IS NOT DISTINCT FROM $2 AND successor_state_id IS NOT DISTINCT FROM $3 AND period_id IS NOT DISTINCT FROM $4 AND input_fingerprint IS NOT DISTINCT FROM $5 AND output_fingerprint IS NOT DISTINCT FROM $6 AND isa_receipt_id IS NOT DISTINCT FROM $7 AND eligible_event_count IS NOT DISTINCT FROM $8 AND prior_match_count IS NOT DISTINCT FROM $9 AND successor_match_count IS NOT DISTINCT FROM $10 AND volatility_iterations IS NOT DISTINCT FROM $11 AND version IS NOT DISTINCT FROM $12 AND flags IS NOT DISTINCT FROM $13)";
    static const char members_write_sql[] =
        "WITH input AS (SELECT $1::bytea receipt_id,(i.event).event_id,row_number() OVER (ORDER BY (i.event).event_id)::numeric member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i)"
        " INSERT INTO " LAPLACE_PG_SCHEMA ".standing_period_receipt_member(receipt_id,event_id,member_ordinal) SELECT receipt_id,event_id,member_ordinal FROM input ON CONFLICT DO NOTHING";
    static const char members_verify_sql[] =
        "WITH input AS MATERIALIZED (SELECT $1::bytea receipt_id,(i.event).event_id,row_number() OVER (ORDER BY (i.event).event_id)::numeric member_ordinal FROM unnest($2::" LAPLACE_PG_SCHEMA ".standing_period_input[]) i)"
        " SELECT NOT EXISTS (SELECT FROM input i LEFT JOIN " LAPLACE_PG_SCHEMA ".standing_period_receipt_member m USING(receipt_id,event_id) WHERE m.receipt_id IS NULL OR m.member_ordinal<>i.member_ordinal)"
        " AND (SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".standing_period_receipt_member WHERE receipt_id=$1)=(SELECT count(*) FROM input)";
    Oid input_types[1] = {input_array_type};
    Datum input_values[1] = {input_array};
    Oid state_types[13] = {BYTEAOID,BYTEAOID,BYTEAOID,BYTEAOID,BYTEAOID,BYTEAOID,FLOAT8OID,FLOAT8OID,FLOAT8OID,INT8OID,INT8OID,INT4OID,INT4OID};
    Datum state_values[13];
    Oid receipt_types[13] = {BYTEAOID,BYTEAOID,BYTEAOID,BYTEAOID,BYTEAOID,BYTEAOID,BYTEAOID,INT8OID,INT8OID,INT8OID,INT4OID,INT4OID,INT4OID};
    Datum receipt_values[13];
    Oid member_types[2] = {BYTEAOID, input_array_type};
    Datum member_values[2];
    const laplace_standing_state* state = &result->successor_state;
    const laplace_standing_period_receipt* receipt = &result->receipt;
#define DIGEST_DATUM(value) PointerGetDatum(laplace_pg_bytes_to_bytea((value).bytes, 32u))
    state_values[0]=DIGEST_DATUM(state->state_id); state_values[1]=DIGEST_DATUM(state->coordinate_id);
    state_values[2]=DIGEST_DATUM(state->arena_scope_id); state_values[3]=DIGEST_DATUM(state->prior_state_id);
    state_values[4]=DIGEST_DATUM(state->epoch_id); state_values[5]=DIGEST_DATUM(state->rating_recipe_id);
    state_values[6]=Float8GetDatum(state->rating); state_values[7]=Float8GetDatum(state->rating_deviation);
    state_values[8]=Float8GetDatum(state->volatility);
    state_values[9]=Int64GetDatum(laplace_pg_checked_int64(state->eligible_match_count,"standing eligible match count"));
    state_values[10]=Int64GetDatum(laplace_pg_checked_int64(state->period_ordinal,"standing period ordinal"));
    state_values[11]=Int32GetDatum((int32)state->rating_recipe_version); state_values[12]=Int32GetDatum((int32)state->flags);
    receipt_values[0]=DIGEST_DATUM(receipt->receipt_id); receipt_values[1]=DIGEST_DATUM(receipt->prior_state_id);
    receipt_values[2]=DIGEST_DATUM(receipt->successor_state_id); receipt_values[3]=DIGEST_DATUM(receipt->period_id);
    receipt_values[4]=DIGEST_DATUM(receipt->input_fingerprint); receipt_values[5]=DIGEST_DATUM(receipt->output_fingerprint);
    receipt_values[6]=DIGEST_DATUM(isa_receipt->receipt_id);
    receipt_values[7]=Int64GetDatum(laplace_pg_checked_int64(receipt->eligible_event_count,"standing eligible event count"));
    receipt_values[8]=Int64GetDatum(laplace_pg_checked_int64(receipt->prior_match_count,"standing prior match count"));
    receipt_values[9]=Int64GetDatum(laplace_pg_checked_int64(receipt->successor_match_count,"standing successor match count"));
    receipt_values[10]=Int32GetDatum((int32)receipt->volatility_iterations);
    receipt_values[11]=Int32GetDatum((int32)receipt->version); receipt_values[12]=Int32GetDatum((int32)receipt->flags);
    member_values[0]=receipt_values[0]; member_values[1]=input_array;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,(errcode(ERRCODE_CONNECTION_FAILURE),errmsg("Laplace standing persistence could not connect to SPI")));
    }
#if defined(LAPLACE_TEST_STANDING_REPLAY_VERIFY_BYPASS)
    if (SPI_execute_with_args(initial_write_sql,1,input_types,input_values,NULL,false,0) != SPI_OK_INSERT) {
        ereport(ERROR,(errcode(ERRCODE_INTERNAL_ERROR),errmsg("Laplace standing initial-state write failed")));
    }
#else
    laplace_pg_execute_set_write_verify(initial_write_sql,states_verify_sql,1,input_types,input_values,"standing prior-state replay");
#endif
    laplace_pg_execute_set_write_verify(event_write_sql,event_verify_sql,1,input_types,input_values,"standing event replay");
    laplace_pg_execute_set_write_verify(state_write_sql,state_verify_sql,13,state_types,state_values,"standing successor replay");
    laplace_pg_execute_set_write_verify(receipt_write_sql,receipt_verify_sql,13,receipt_types,receipt_values,"standing receipt replay");
    laplace_pg_execute_set_write_verify(members_write_sql,members_verify_sql,2,member_types,member_values,"standing receipt membership");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,(errcode(ERRCODE_INTERNAL_ERROR),errmsg("Laplace standing persistence could not close SPI")));
    }
#undef DIGEST_DATUM
}

Datum LAPLACE_PG_STANDING_ENTRYPOINT(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    ArrayType* input_array = PG_GETARG_ARRAYTYPE_P(1);
    const Oid input_array_type = get_array_type(ARR_ELEMTYPE(input_array));
    laplace_standing_period_input* inputs;
    size_t input_count = 0u;
    laplace_standing_period_result isa_output;
    laplace_standing_period_result semantic_output;
    laplace_standing_error semantic_error;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_receipt isa_receipt;
    laplace_isa_error isa_error;
    Datum result_values[23];
    bool result_nulls[23] = {false};
    HeapTuple result_tuple;
    const laplace_standing_state* state;
    const laplace_standing_period_receipt* receipt;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    require_recipe_admission(
        input_array_type, PointerGetDatum(input_array), &context);
    inputs = read_inputs(input_array, &input_count);
    qsort(inputs, input_count, sizeof(*inputs), compare_period_input);
    memset(&isa_output, 0, sizeof(isa_output));
    memset(values, 0, sizeof(values));
    values[0].data=inputs; values[0].count=(uint64_t)input_count; values[0].capacity=(uint64_t)input_count;
    values[0].stride_bytes=sizeof(*inputs); values[0].type=LAPLACE_ISA_VALUE_STANDING_PERIOD_INPUT_VECTOR;
    values[1].data=&isa_output; values[1].capacity=1u; values[1].stride_bytes=sizeof(isa_output);
    values[1].type=LAPLACE_ISA_VALUE_STANDING_PERIOD_RESULT_VECTOR;
    memset(&instruction,0,sizeof(instruction));
    instruction.opcode=LAPLACE_ISA_OPCODE_EVIDENCE_CALCULATE_STANDING_BATCH;
    instruction.version=LAPLACE_ISA_INSTRUCTION_VERSION_EVIDENCE_CALCULATE_STANDING_BATCH;
    instruction.output_value=1u;
    memset(&program,0,sizeof(program));
    program.instructions=&instruction; program.values=values; program.context=&context;
    program.instruction_count=1u; program.value_count=2u; program.major=LAPLACE_ISA_MAJOR;
    program.minor=LAPLACE_ISA_MINOR; program.receipt_detail=LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    memset(&isa_receipt,0,sizeof(isa_receipt)); memset(&isa_error,0,sizeof(isa_error));
    if (laplace_isa_execute(&program,&isa_receipt,&isa_error) != LAPLACE_ISA_OK) {
        ereport(ERROR,(errcode(ERRCODE_DATA_EXCEPTION),errmsg("Laplace standing ISA execution failed"),
                       errdetail("status=%d instruction=%llu",(int)isa_error.status,(unsigned long long)isa_error.instruction_index)));
    }
    memset(&semantic_output,0,sizeof(semantic_output)); memset(&semantic_error,0,sizeof(semantic_error));
    if (laplace_standing_calculate_period_batch(inputs,input_count,&semantic_output,&semantic_error) != LAPLACE_STANDING_OK ||
        memcmp(&semantic_output,&isa_output,sizeof(semantic_output)) != 0) {
        ereport(ERROR,(errcode(ERRCODE_DATA_EXCEPTION),errmsg("Laplace standing semantic receipt reconstruction failed")));
    }
    laplace_pg_persist_execution_receipt(&isa_receipt,input_count,instruction.opcode);
    persist_standing(input_array_type,PointerGetDatum(input_array),&semantic_output,&isa_receipt);
    state=&semantic_output.successor_state; receipt=&semantic_output.receipt;
#define RESULT_DIGEST(index,value) result_values[index]=PointerGetDatum(laplace_pg_bytes_to_bytea((value).bytes,32u))
    RESULT_DIGEST(0,state->state_id); RESULT_DIGEST(1,state->coordinate_id); RESULT_DIGEST(2,state->arena_scope_id);
    RESULT_DIGEST(3,state->prior_state_id); RESULT_DIGEST(4,state->epoch_id); RESULT_DIGEST(5,state->rating_recipe_id);
    result_values[6]=Float8GetDatum(state->rating); result_values[7]=Float8GetDatum(state->rating_deviation);
    result_values[8]=Float8GetDatum(state->volatility); result_values[9]=laplace_pg_numeric_from_uint64(state->eligible_match_count);
    result_values[10]=laplace_pg_numeric_from_uint64(state->period_ordinal); result_values[11]=Int32GetDatum((int32)state->rating_recipe_version);
    result_values[12]=Int32GetDatum((int32)state->flags); RESULT_DIGEST(13,receipt->receipt_id);
    RESULT_DIGEST(14,receipt->input_fingerprint); RESULT_DIGEST(15,receipt->output_fingerprint); RESULT_DIGEST(16,isa_receipt.receipt_id);
    result_values[17]=laplace_pg_numeric_from_uint64(receipt->eligible_event_count);
    result_values[18]=laplace_pg_numeric_from_uint64(receipt->prior_match_count);
    result_values[19]=laplace_pg_numeric_from_uint64(receipt->successor_match_count);
    result_values[20]=Int32GetDatum((int32)receipt->volatility_iterations);
    result_values[21]=Int32GetDatum((int32)receipt->version); result_values[22]=Int32GetDatum((int32)receipt->flags);
    result_tuple=laplace_pg_form_result_tuple(fcinfo,result_values,result_nulls,23);
#undef RESULT_DIGEST
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
