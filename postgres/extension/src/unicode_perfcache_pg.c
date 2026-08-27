#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type_d.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "varatt.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/perfcache_modules.h"
#include "laplace_pg_internal.h"
#include "perfcache_pg.h"

static void read_exact_binary(
    Datum value,
    uint8_t* destination,
    Size expected_bytes,
    const char* field) {
    bytea* binary = DatumGetByteaPP(value);
    if ((Size)VARSIZE_ANY_EXHDR(binary) != expected_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("%s must contain exactly %zu bytes", field,
                        (size_t)expected_bytes)));
    }
    memcpy(destination, VARDATA_ANY(binary), expected_bytes);
}

static void read_expected_epoch(
    Datum epoch_id,
    Datum epoch_fingerprint,
    laplace_pg_perfcache_epoch* epoch) {
    memset(epoch, 0, sizeof(*epoch));
    read_exact_binary(epoch_id, epoch->activation_epoch_id.bytes,
                      sizeof(epoch->activation_epoch_id.bytes),
                      "activation epoch ID");
    read_exact_binary(epoch_fingerprint, epoch->epoch_fingerprint.bytes,
                      sizeof(epoch->epoch_fingerprint.bytes),
                      "activation epoch fingerprint");
}

static void raise_pin_status(laplace_pg_perfcache_status status) {
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("exact Unicode perfcache pin failed with status %u",
                        (unsigned int)status)));
    }
}

static void raise_registry_status(
    laplace_perfcache_registry_status status,
    const char* operation) {
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("native Unicode perfcache %s failed with status %u",
                        operation, (unsigned int)status)));
    }
}

static void require_one_dimensional_batch(
    ArrayType* array,
    int item_count,
    const char* field) {
    if (ARR_NDIM(array) != 1 || ARR_LBOUND(array)[0] != 1 || item_count <= 0 ||
        item_count > (int)LAPLACE_PG_UNICODE_ACCESS_MAXIMUM_BATCH_ITEMS) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s must be a one-dimensional, one-based batch of 1..%u items",
                        field,
                        (unsigned int)LAPLACE_PG_UNICODE_ACCESS_MAXIMUM_BATCH_ITEMS)));
    }
}

static ArrayType* form_array(
    Datum* values,
    bool* nulls,
    int item_count,
    Oid type_oid) {
    int dimensions[1] = {item_count};
    int lower_bounds[1] = {1};
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    get_typlenbyvalalign(type_oid, &type_length, &type_by_value,
                         &type_alignment);
    return construct_md_array(values, nulls, 1, dimensions, lower_bounds,
                              type_oid, type_length, type_by_value,
                              type_alignment);
}

PG_FUNCTION_INFO_V1(LAPLACE_PG_UNICODE_TIER0_ACCESS_SYMBOL);
Datum LAPLACE_PG_UNICODE_TIER0_ACCESS_SYMBOL(PG_FUNCTION_ARGS) {
    ArrayType* input = PG_GETARG_ARRAYTYPE_P(2);
    Datum* input_values = NULL;
    bool* input_nulls = NULL;
    int item_count = 0;
    uint32_t* positions;
    laplace_unicode_atom_record_view* atoms;
    uint8_t* found;
    laplace_pg_perfcache_epoch expected_epoch;
    laplace_pg_perfcache_pin* pin = NULL;
    Datum* columns[15];
    bool* column_nulls[15];
    Oid column_types[15] = {
        INT4OID, BOOLOID, INT4OID, INT2OID, BYTEAOID,
        BYTEAOID, BYTEAOID, BYTEAOID, FLOAT8OID, FLOAT8OID,
        FLOAT8OID, FLOAT8OID, BYTEAOID, BYTEAOID, BYTEAOID};
    Datum result_values[17] = {0};
    bool result_nulls[17] = {false};
    HeapTuple result_tuple;
    int column;
    int index;

    deconstruct_array(input, INT4OID, 4, true, TYPALIGN_INT,
                      &input_values, &input_nulls, &item_count);
    require_one_dimensional_batch(input, item_count, "Unicode Tier-0 lookup");
    read_expected_epoch(PG_GETARG_DATUM(0), PG_GETARG_DATUM(1),
                        &expected_epoch);
    positions = (uint32_t*)palloc(sizeof(*positions) * (size_t)item_count);
    atoms = (laplace_unicode_atom_record_view*)palloc0(
        sizeof(*atoms) * (size_t)item_count);
    found = (uint8_t*)palloc0((Size)item_count);
    for (column = 0; column < 15; ++column) {
        columns[column] = (Datum*)palloc0(sizeof(Datum) * (size_t)item_count);
        column_nulls[column] = (bool*)palloc0(sizeof(bool) * (size_t)item_count);
    }
    for (index = 0; index < item_count; ++index) {
        int32 position;
        if (input_nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Unicode Tier-0 lookup positions cannot contain nulls")));
        }
        position = DatumGetInt32(input_values[index]);
        if (position < 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Unicode Tier-0 lookup positions must be nonnegative")));
        }
        positions[index] = (uint32_t)position;
        columns[0][index] = Int32GetDatum(position);
    }

#if defined(LAPLACE_TEST_UNICODE_ACCESS_IGNORE_EXPECTED_EPOCH)
    raise_pin_status(laplace_pg_perfcache_pin_active(0u, NULL, &pin));
#else
    raise_pin_status(laplace_pg_perfcache_pin_active(
        1u, &expected_epoch, &pin));
#endif
    PG_TRY();
    {
        raise_registry_status(
            laplace_perfcache_unicode_tier0_resolve_batch(
                &pin->native_pin, positions, (size_t)item_count, atoms, found),
            "Tier-0 batch resolution");
        for (index = 0; index < item_count; ++index) {
            const laplace_unicode_atom_record* atom = &atoms[index].value;
            int optional_column;
            columns[1][index] = BoolGetDatum(found[index] != 0u);
            if (found[index] == 0u) {
                for (optional_column = 2; optional_column < 15;
                     ++optional_column) {
                    column_nulls[optional_column][index] = true;
                }
                continue;
            }
            columns[2][index] = Int32GetDatum((int32)atom->placement_rank);
            columns[3][index] = Int16GetDatum((int16)atom->position_class);
            columns[4][index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atom->lup_v1_bytes, atom->lup_v1_length));
            columns[5][index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atom->content_id.bytes, sizeof(atom->content_id.bytes)));
            columns[6][index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atom->identity_preimage_fingerprint.bytes,
                sizeof(atom->identity_preimage_fingerprint.bytes)));
            columns[7][index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atom->physicality_id.bytes,
                sizeof(atom->physicality_id.bytes)));
            columns[8][index] = Float8GetDatum(atom->coordinate.component[0]);
            columns[9][index] = Float8GetDatum(atom->coordinate.component[1]);
            columns[10][index] = Float8GetDatum(atom->coordinate.component[2]);
            columns[11][index] = Float8GetDatum(atom->coordinate.component[3]);
            columns[12][index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atom->hilbert_key, sizeof(atom->hilbert_key)));
            columns[13][index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atom->geometry_epoch.bytes, sizeof(atom->geometry_epoch.bytes)));
            columns[14][index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atoms[index].encoded_record, atoms[index].encoded_bytes));
        }
        for (column = 0; column < 15; ++column) {
            result_values[column] = PointerGetDatum(form_array(
                columns[column], column_nulls[column], item_count,
                column_types[column]));
        }
        result_values[15] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            pin->epoch.activation_epoch_id.bytes,
            sizeof(pin->epoch.activation_epoch_id.bytes)));
        result_values[16] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            pin->epoch.epoch_fingerprint.bytes,
            sizeof(pin->epoch.epoch_fingerprint.bytes)));
        laplace_pg_perfcache_pin_release(&pin);
    }
    PG_CATCH();
    {
        laplace_pg_perfcache_pin_release(&pin);
        PG_RE_THROW();
    }
    PG_END_TRY();
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 17);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}

PG_FUNCTION_INFO_V1(LAPLACE_PG_UNICODE_REVERSE_ACCESS_SYMBOL);
Datum LAPLACE_PG_UNICODE_REVERSE_ACCESS_SYMBOL(PG_FUNCTION_ARGS) {
    ArrayType* content_input = PG_GETARG_ARRAYTYPE_P(2);
    ArrayType* fingerprint_input = PG_GETARG_ARRAYTYPE_P(3);
    Datum* content_values = NULL;
    Datum* fingerprint_values = NULL;
    bool* content_nulls = NULL;
    bool* fingerprint_nulls = NULL;
    int content_count = 0;
    int fingerprint_count = 0;
    laplace_unicode_identity_key* identities;
    uint32_t* positions;
    uint8_t* found;
    laplace_pg_perfcache_epoch expected_epoch;
    laplace_pg_perfcache_pin* pin = NULL;
    Datum* output_content;
    Datum* output_fingerprints;
    Datum* output_found;
    Datum* output_positions;
    bool* output_position_nulls;
    bool* no_nulls;
    Datum result_values[6] = {0};
    bool result_nulls[6] = {false};
    HeapTuple result_tuple;
    int index;

    deconstruct_array(content_input, BYTEAOID, -1, false, TYPALIGN_INT,
                      &content_values, &content_nulls, &content_count);
    deconstruct_array(fingerprint_input, BYTEAOID, -1, false, TYPALIGN_INT,
                      &fingerprint_values, &fingerprint_nulls,
                      &fingerprint_count);
    require_one_dimensional_batch(
        content_input, content_count, "Unicode reverse identity lookup");
    require_one_dimensional_batch(
        fingerprint_input, fingerprint_count,
        "Unicode reverse fingerprint lookup");
    if (content_count != fingerprint_count) {
        ereport(ERROR,
                (errcode(ERRCODE_ARRAY_SUBSCRIPT_ERROR),
                 errmsg("Unicode reverse identity arrays must have equal cardinality")));
    }
    read_expected_epoch(PG_GETARG_DATUM(0), PG_GETARG_DATUM(1),
                        &expected_epoch);
    identities = (laplace_unicode_identity_key*)palloc0(
        sizeof(*identities) * (size_t)content_count);
    positions = (uint32_t*)palloc0(sizeof(*positions) * (size_t)content_count);
    found = (uint8_t*)palloc0((Size)content_count);
    output_content = (Datum*)palloc0(sizeof(Datum) * (size_t)content_count);
    output_fingerprints = (Datum*)palloc0(
        sizeof(Datum) * (size_t)content_count);
    output_found = (Datum*)palloc0(sizeof(Datum) * (size_t)content_count);
    output_positions = (Datum*)palloc0(sizeof(Datum) * (size_t)content_count);
    output_position_nulls = (bool*)palloc0(sizeof(bool) * (size_t)content_count);
    no_nulls = (bool*)palloc0(sizeof(bool) * (size_t)content_count);
    for (index = 0; index < content_count; ++index) {
        if (content_nulls[index] || fingerprint_nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Unicode reverse identity arrays cannot contain nulls")));
        }
        read_exact_binary(content_values[index],
                          identities[index].content_id.bytes,
                          sizeof(identities[index].content_id.bytes),
                          "Unicode content identity");
        read_exact_binary(
            fingerprint_values[index],
            identities[index].identity_preimage_fingerprint.bytes,
            sizeof(identities[index].identity_preimage_fingerprint.bytes),
            "Unicode identity preimage fingerprint");
        output_content[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            identities[index].content_id.bytes,
            sizeof(identities[index].content_id.bytes)));
        output_fingerprints[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            identities[index].identity_preimage_fingerprint.bytes,
            sizeof(identities[index].identity_preimage_fingerprint.bytes)));
    }

#if defined(LAPLACE_TEST_UNICODE_ACCESS_IGNORE_EXPECTED_EPOCH)
    raise_pin_status(laplace_pg_perfcache_pin_active(0u, NULL, &pin));
#else
    raise_pin_status(laplace_pg_perfcache_pin_active(
        1u, &expected_epoch, &pin));
#endif
    PG_TRY();
    {
        raise_registry_status(
            laplace_perfcache_unicode_identity_reverse_resolve_batch(
                &pin->native_pin, identities, (size_t)content_count,
                positions, found),
            "identity reverse batch resolution");
        for (index = 0; index < content_count; ++index) {
            output_found[index] = BoolGetDatum(found[index] != 0u);
            if (found[index] == 0u) {
                output_position_nulls[index] = true;
            } else {
                output_positions[index] = Int32GetDatum((int32)positions[index]);
            }
        }
        result_values[0] = PointerGetDatum(form_array(
            output_content, no_nulls, content_count, BYTEAOID));
        result_values[1] = PointerGetDatum(form_array(
            output_fingerprints, no_nulls, content_count, BYTEAOID));
        result_values[2] = PointerGetDatum(form_array(
            output_found, no_nulls, content_count, BOOLOID));
        result_values[3] = PointerGetDatum(form_array(
            output_positions, output_position_nulls, content_count, INT4OID));
        result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            pin->epoch.activation_epoch_id.bytes,
            sizeof(pin->epoch.activation_epoch_id.bytes)));
        result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            pin->epoch.epoch_fingerprint.bytes,
            sizeof(pin->epoch.epoch_fingerprint.bytes)));
        laplace_pg_perfcache_pin_release(&pin);
    }
    PG_CATCH();
    {
        laplace_pg_perfcache_pin_release(&pin);
        PG_RE_THROW();
    }
    PG_END_TRY();
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 6);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
