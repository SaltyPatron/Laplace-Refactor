#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "utils/builtins.h"

#include "laplace/cognition_discourse.h"
#include "laplace/cognition_discourse_frame.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace_pg_internal.h"

PG_FUNCTION_INFO_V1(laplace_pg_cognition_discourse_deposit);
PG_FUNCTION_INFO_V1(laplace_pg_cognition_discourse_read);
PG_FUNCTION_INFO_V1(laplace_pg_cognition_discourse_latest);

static void discourse_error(
    const char* message,
    laplace_cognition_discourse_frame_status status) {
    ereport(ERROR,
            (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
             errmsg("%s", message),
             errdetail("native discourse frame status=%d", (int)status)));
}

static int bytea_equals_bytes(
    const bytea* value,
    const uint8_t* bytes,
    size_t byte_count) {
    return value != NULL &&
        (size_t)VARSIZE_ANY_EXHDR(value) == byte_count &&
        memcmp(VARDATA_ANY(value), bytes, byte_count) == 0;
}

static void require_digest_argument(
    const bytea* value,
    const char* field,
    laplace_digest256* digest) {
    if (value == NULL || digest == NULL ||
        VARSIZE_ANY_EXHDR(value) != (int)sizeof(digest->bytes)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace %s must be exactly 32 bytes", field)));
    }
    memcpy(digest->bytes, VARDATA_ANY(value), sizeof(digest->bytes));
}

static void decode_frame_or_error(
    const bytea* frame,
    laplace_cognition_discourse_state* state,
    laplace_cognition_discourse_frame_receipt* receipt) {
    laplace_cognition_discourse_frame_status status;
    size_t byte_count;
    if (frame == NULL || state == NULL || receipt == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace discourse frame arguments are invalid")));
    }
    byte_count = (size_t)VARSIZE_ANY_EXHDR(frame);
    status = laplace_cognition_discourse_frame_decode(
        (const uint8_t*)VARDATA_ANY(frame), byte_count, state, receipt);
    if (status != LAPLACE_COGNITION_DISCOURSE_FRAME_OK) {
        discourse_error("Laplace discourse frame failed native validation", status);
    }
}

static bytea* copy_spi_bytea(Datum datum) {
    bytea* value = DatumGetByteaPP(datum);
    const size_t byte_count = (size_t)VARSIZE_ANY_EXHDR(value);
    bytea* copy = (bytea*)SPI_palloc(VARHDRSZ + byte_count);
    SET_VARSIZE(copy, VARHDRSZ + byte_count);
    if (byte_count != 0u) {
        memcpy(VARDATA(copy), VARDATA_ANY(value), byte_count);
    }
    return copy;
}

static void validate_predecessor(
    const laplace_cognition_discourse_state* state) {
    const int has_previous =
        (state->flags & LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE) != 0;
    if (!has_previous) {
        if (state->turn_ordinal != 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace discourse root must have turn ordinal zero")));
        }
        return;
    }

    {
        static const char sql[] =
            "SELECT discourse_id, turn_ordinal FROM " LAPLACE_PG_SCHEMA
            ".cognition_discourse_state WHERE state_id = $1";
        Oid types[1] = {BYTEAOID};
        Datum values[1];
        bytea* previous = laplace_pg_bytes_to_bytea(
            state->previous_state_id.bytes, sizeof(state->previous_state_id.bytes));
        int spi_status;
        bool is_null = false;
        Datum discourse_datum;
        Datum ordinal_datum;
        bytea* discourse;
        int64 previous_ordinal;

        values[0] = PointerGetDatum(previous);
        spi_status = SPI_execute_with_args(
            sql, 1, types, values, NULL, true, 1);
        if (spi_status != SPI_OK_SELECT || SPI_processed != 1u) {
            ereport(ERROR,
                    (errcode(ERRCODE_FOREIGN_KEY_VIOLATION),
                     errmsg("Laplace discourse predecessor is not durably present")));
        }
        discourse_datum = SPI_getbinval(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
        if (is_null) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace discourse predecessor has no discourse identity")));
        }
        discourse = DatumGetByteaPP(discourse_datum);
        if (!bytea_equals_bytes(
                discourse, state->discourse_id.bytes,
                sizeof(state->discourse_id.bytes))) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace discourse predecessor belongs to another discourse")));
        }
        ordinal_datum = SPI_getbinval(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2, &is_null);
        if (is_null) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace discourse predecessor has no turn ordinal")));
        }
        previous_ordinal = DatumGetInt64(ordinal_datum);
        if (previous_ordinal < 0 ||
            state->turn_ordinal != (uint64_t)previous_ordinal + UINT64_C(1)) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Laplace discourse successor is not contiguous with its predecessor")));
        }
    }
}

static void verify_exact_stored_frame(
    const laplace_cognition_discourse_state* expected_state,
    const laplace_cognition_discourse_frame_receipt* expected_receipt,
    const bytea* expected_frame) {
    static const char sql[] =
        "SELECT frame FROM " LAPLACE_PG_SCHEMA
        ".cognition_discourse_state WHERE state_id = $1";
    Oid types[1] = {BYTEAOID};
    Datum values[1];
    bytea* state_id = laplace_pg_bytes_to_bytea(
        expected_state->state_id.bytes, sizeof(expected_state->state_id.bytes));
    int spi_status;
    bool is_null = false;
    Datum frame_datum;
    bytea* stored_frame;
    laplace_cognition_discourse_state stored_state;
    laplace_cognition_discourse_frame_receipt stored_receipt;

    values[0] = PointerGetDatum(state_id);
    /* Refresh the command snapshot to include this call's preceding insert. */
    spi_status = SPI_execute_with_args(sql, 1, types, values, NULL, false, 1);
    if (spi_status != SPI_OK_SELECT || SPI_processed != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace discourse deposit did not publish exactly one state")));
    }
    frame_datum = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace discourse stored frame is null")));
    }
    stored_frame = DatumGetByteaPP(frame_datum);
    if (VARSIZE_ANY_EXHDR(stored_frame) != VARSIZE_ANY_EXHDR(expected_frame) ||
        memcmp(
            VARDATA_ANY(stored_frame), VARDATA_ANY(expected_frame),
            (size_t)VARSIZE_ANY_EXHDR(expected_frame)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace discourse state identity conflicts with different durable bytes")));
    }
    decode_frame_or_error(stored_frame, &stored_state, &stored_receipt);
    if (memcmp(
            stored_state.state_id.bytes, expected_state->state_id.bytes,
            sizeof(stored_state.state_id.bytes)) != 0 ||
        memcmp(
            stored_receipt.frame_fingerprint.bytes,
            expected_receipt->frame_fingerprint.bytes,
            sizeof(stored_receipt.frame_fingerprint.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace discourse readback changed native identity")));
    }
}

Datum laplace_pg_cognition_discourse_deposit(PG_FUNCTION_ARGS) {
    bytea* frame = PG_GETARG_BYTEA_PP(0);
    laplace_cognition_discourse_state state;
    laplace_cognition_discourse_frame_receipt receipt;
    static const char insert_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".cognition_discourse_state ("
        "state_id, discourse_id, previous_state_id, observation_entity_id, "
        "observation_occurrence_id, semantic_act_id, turn_ordinal, state_flags, "
        "state_version, frame_fingerprint, frame) "
        "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11) "
        "ON CONFLICT (state_id) DO NOTHING";
    Oid types[11] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT4OID, INT4OID, BYTEAOID, BYTEAOID};
    Datum values[11];
    char nulls[11] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    int spi_status;
    bytea* state_id_result;

    decode_frame_or_error(frame, &state, &receipt);

    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace discourse persistence could not connect to SPI")));
    }

    validate_predecessor(&state);

    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state.state_id.bytes, sizeof(state.state_id.bytes)));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state.discourse_id.bytes, sizeof(state.discourse_id.bytes)));
    if ((state.flags & LAPLACE_COGNITION_DISCOURSE_HAS_PREVIOUS_STATE) != 0) {
        values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            state.previous_state_id.bytes, sizeof(state.previous_state_id.bytes)));
    } else {
        values[2] = (Datum)0;
        nulls[2] = 'n';
    }
    values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state.observation_entity_id.bytes, sizeof(state.observation_entity_id.bytes)));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state.observation_occurrence_id.bytes,
        sizeof(state.observation_occurrence_id.bytes)));
    values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state.semantic_act_id.bytes, sizeof(state.semantic_act_id.bytes)));
    values[6] = Int64GetDatum(laplace_pg_checked_int64(
        state.turn_ordinal, "discourse turn ordinal"));
    values[7] = Int32GetDatum((int32)state.flags);
    values[8] = Int32GetDatum((int32)state.version);
    values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        receipt.frame_fingerprint.bytes, sizeof(receipt.frame_fingerprint.bytes)));
    values[10] = PointerGetDatum(frame);

    spi_status = SPI_execute_with_args(
        insert_sql, 11, types, values, nulls, false, 0);
    if (spi_status != SPI_OK_INSERT) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace discourse persistence insert failed")));
    }

    verify_exact_stored_frame(&state, &receipt, frame);
    state_id_result = (bytea*)SPI_palloc(VARHDRSZ + sizeof(state.state_id.bytes));
    SET_VARSIZE(state_id_result, VARHDRSZ + sizeof(state.state_id.bytes));
    memcpy(VARDATA(state_id_result), state.state_id.bytes, sizeof(state.state_id.bytes));

    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace discourse persistence could not close SPI")));
    }
    PG_RETURN_BYTEA_P(state_id_result);
}

static bytea* read_validated_frame_by_sql(
    const char* sql,
    const bytea* key,
    const laplace_digest256* expected_state_id,
    const laplace_digest256* expected_discourse_id) {
    Oid types[1] = {BYTEAOID};
    Datum values[1];
    int spi_status;
    bool is_null = false;
    Datum frame_datum;
    bytea* stored_frame;
    bytea* result;
    laplace_cognition_discourse_state state;
    laplace_cognition_discourse_frame_receipt receipt;

    values[0] = PointerGetDatum((bytea*)key);
    spi_status = SPI_execute_with_args(sql, 1, types, values, NULL, true, 1);
    if (spi_status != SPI_OK_SELECT) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace discourse persistence read failed")));
    }
    if (SPI_processed == 0u) {
        return NULL;
    }
    if (SPI_processed != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace discourse persistence returned multiple immutable states")));
    }
    frame_datum = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace discourse persistence returned a null frame")));
    }
    stored_frame = DatumGetByteaPP(frame_datum);
    decode_frame_or_error(stored_frame, &state, &receipt);
    if (expected_state_id != NULL &&
        memcmp(
            state.state_id.bytes, expected_state_id->bytes,
            sizeof(state.state_id.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace discourse state lookup returned a different native identity")));
    }
    if (expected_discourse_id != NULL &&
        memcmp(
            state.discourse_id.bytes, expected_discourse_id->bytes,
            sizeof(state.discourse_id.bytes)) != 0) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace discourse lookup crossed discourse identity")));
    }
    result = copy_spi_bytea(frame_datum);
    return result;
}

Datum laplace_pg_cognition_discourse_read(PG_FUNCTION_ARGS) {
    static const char sql[] =
        "SELECT frame FROM " LAPLACE_PG_SCHEMA
        ".cognition_discourse_state WHERE state_id = $1";
    bytea* key = PG_GETARG_BYTEA_PP(0);
    laplace_digest256 state_id;
    bytea* result;

    require_digest_argument(key, "discourse state identity", &state_id);
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace discourse persistence could not connect to SPI")));
    }
    result = read_validated_frame_by_sql(sql, key, &state_id, NULL);
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace discourse persistence could not close SPI")));
    }
    if (result == NULL) PG_RETURN_NULL();
    PG_RETURN_BYTEA_P(result);
}

Datum laplace_pg_cognition_discourse_latest(PG_FUNCTION_ARGS) {
    static const char sql[] =
        "SELECT frame FROM " LAPLACE_PG_SCHEMA
        ".cognition_discourse_state WHERE discourse_id = $1 "
        "ORDER BY turn_ordinal DESC LIMIT 1";
    bytea* key = PG_GETARG_BYTEA_PP(0);
    laplace_digest256 discourse_id;
    bytea* result;

    require_digest_argument(key, "discourse identity", &discourse_id);
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace discourse persistence could not connect to SPI")));
    }
    result = read_validated_frame_by_sql(sql, key, NULL, &discourse_id);
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace discourse persistence could not close SPI")));
    }
    if (result == NULL) PG_RETURN_NULL();
    PG_RETURN_BYTEA_P(result);
}
