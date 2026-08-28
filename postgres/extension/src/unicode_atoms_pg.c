#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "utils/array.h"
#include "utils/builtins.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace_pg_internal.h"
#include "unicode_atoms_pg.h"

static Datum required_tuple_value(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    const char* field) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace active Unicode resolver returned null %s", field)));
    }
    return value;
}

static void read_exact_bytes(
    Datum datum,
    uint8_t* output,
    size_t expected,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != expected) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace active Unicode %s has invalid width", field),
                 errdetail("expected=%zu actual=%zu", expected,
                           (size_t)VARSIZE_ANY_EXHDR(value))));
    }
    memcpy(output, VARDATA_ANY(value), expected);
}

static bool digest_equal(
    const laplace_digest256* left,
    const laplace_digest256* right) {
    return memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static ArrayType* position_array(const uint32_t* positions, size_t count) {
    Datum* values;
    size_t index;
    if (positions == NULL || count == 0u || count > (size_t)INT_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Laplace active Unicode atom request is invalid")));
    }
    values = (Datum*)palloc(sizeof(*values) * count);
    for (index = 0u; index < count; ++index) {
        if (positions[index] > (uint32_t)PG_INT32_MAX) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Laplace Unicode position is out of range")));
        }
        values[index] = Int32GetDatum((int32)positions[index]);
    }
    return construct_array(
        values, (int)count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
}

void laplace_pg_resolve_active_unicode_atoms(
    const laplace_framework_context* context,
    const uint32_t* positions,
    size_t count,
    laplace_composition_known_entity* known,
    laplace_pg_active_unicode_root* active) {
    static const char active_sql[] =
        "SELECT a.activation_epoch_id,a.epoch_fingerprint,d.root_receipt "
        "FROM " LAPLACE_PG_SCHEMA ".perfcache_active_control a JOIN "
        LAPLACE_PG_SCHEMA ".unicode_root_deposit_receipt d ON "
        "d.activation_epoch_id=a.activation_epoch_id AND "
        "d.activation_epoch_fingerprint=a.epoch_fingerprint "
        "WHERE a.singleton AND a.active_present "
        "GROUP BY a.activation_epoch_id,a.epoch_fingerprint,d.root_receipt";
    static const char atoms_sql[] =
        "WITH requested(codepoint_position,ordinality) AS ("
        "SELECT * FROM unnest($1::integer[]) WITH ORDINALITY) "
        "SELECT r.ordinality,b.codepoint_position,b.entity_id,"
        "b.identity_preimage_fingerprint,b.physicality_id,"
        "b.coordinate_x,b.coordinate_y,b.coordinate_z,b.coordinate_m "
        "FROM requested r JOIN " LAPLACE_PG_SCHEMA ".unicode_atom_binding b "
        "ON b.root_receipt=$2::" LAPLACE_PG_SCHEMA ".record_id_256 "
        "AND b.codepoint_position=r.codepoint_position ORDER BY r.ordinality";
    Oid atom_types[2] = {INT4ARRAYOID, BYTEAOID};
    Datum atom_values[2];
    int result;
    size_t index;
    HeapTuple tuple;
    TupleDesc descriptor;

    if (context == NULL || positions == NULL || count == 0u || known == NULL ||
        active == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace active Unicode resolution input is incomplete")));
    }
    memset(active, 0, sizeof(*active));
    if ((context->epoch_mask &
         (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE)) == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace composition requires a pinned Unicode perfcache epoch")));
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace could not connect to active Unicode state")));
    }
    result = SPI_execute(active_sql, true, 0);
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace requires exactly one active deposited Unicode root"),
                 errdetail("matching roots=%llu",
                           (unsigned long long)SPI_processed)));
    }
    tuple = SPI_tuptable->vals[0];
    descriptor = SPI_tuptable->tupdesc;
    read_exact_bytes(
        required_tuple_value(tuple, descriptor, 1, "activation epoch id"),
        active->activation_epoch_id.bytes,
        sizeof(active->activation_epoch_id.bytes), "activation epoch id");
    read_exact_bytes(
        required_tuple_value(tuple, descriptor, 2, "activation epoch fingerprint"),
        active->activation_epoch_fingerprint.bytes,
        sizeof(active->activation_epoch_fingerprint.bytes),
        "activation epoch fingerprint");
    read_exact_bytes(
        required_tuple_value(tuple, descriptor, 3, "root receipt"),
        active->root_receipt.bytes, sizeof(active->root_receipt.bytes),
        "root receipt");
    if (!digest_equal(
            &context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE],
            &active->activation_epoch_fingerprint)) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace context does not pin the active Unicode epoch")));
    }

    atom_values[0] = PointerGetDatum(position_array(positions, count));
    atom_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        active->root_receipt.bytes, sizeof(active->root_receipt.bytes)));
    result = SPI_execute_with_args(
        atoms_sql, 2, atom_types, atom_values, NULL, true, 0);
    if (result != SPI_OK_SELECT || SPI_processed != (uint64)count ||
        SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Active Unicode root did not resolve the complete atom set"),
                 errdetail("requested=%zu resolved=%llu", count,
                           (unsigned long long)SPI_processed)));
    }
    for (index = 0u; index < count; ++index) {
        const uint64 ordinal = (uint64)DatumGetInt64(required_tuple_value(
            SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 1, "ordinal"));
        const int32 position = DatumGetInt32(required_tuple_value(
            SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 2, "position"));
        if (ordinal != (uint64)(index + 1u) || position < 0 ||
            (uint32_t)position != positions[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Active Unicode atom resolution changed order or identity")));
        }
        memset(&known[index], 0, sizeof(known[index]));
        read_exact_bytes(
            required_tuple_value(SPI_tuptable->vals[index],
                                 SPI_tuptable->tupdesc, 3, "entity id"),
            known[index].entity_id.bytes, sizeof(known[index].entity_id.bytes),
            "entity id");
        read_exact_bytes(
            required_tuple_value(SPI_tuptable->vals[index],
                                 SPI_tuptable->tupdesc, 4, "identity witness"),
            known[index].identity_witness.bytes,
            sizeof(known[index].identity_witness.bytes), "identity witness");
        read_exact_bytes(
            required_tuple_value(SPI_tuptable->vals[index],
                                 SPI_tuptable->tupdesc, 5, "physicality id"),
            known[index].physicality_id.bytes,
            sizeof(known[index].physicality_id.bytes), "physicality id");
        known[index].centroid.component[0] = DatumGetFloat8(required_tuple_value(
            SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 6, "coordinate x"));
        known[index].centroid.component[1] = DatumGetFloat8(required_tuple_value(
            SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 7, "coordinate y"));
        known[index].centroid.component[2] = DatumGetFloat8(required_tuple_value(
            SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 8, "coordinate z"));
        known[index].centroid.component[3] = DatumGetFloat8(required_tuple_value(
            SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 9, "coordinate m"));
        known[index].atom = (uint32_t)position;
        known[index].has_atom = 1u;
        known[index].tier_floor = 0u;
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace could not close active Unicode resolution")));
    }
}
