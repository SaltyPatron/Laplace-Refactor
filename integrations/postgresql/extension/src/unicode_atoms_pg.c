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
#include "laplace/perfcache_modules.h"
#include "laplace_pg_internal.h"
#include "perfcache_pg.h"
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

static void require_pinned_unicode_epoch(
    const laplace_framework_context* context,
    const laplace_pg_perfcache_epoch* epoch) {
    if ((context->epoch_mask &
         (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE)) == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace composition requires a pinned Unicode perfcache epoch")));
    }
    if (!digest_equal(
            &context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE],
            &epoch->epoch_fingerprint)) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace context does not pin the active Unicode epoch")));
    }
}

static void read_root_receipt_for_epoch(
    const laplace_pg_perfcache_epoch* epoch,
    laplace_digest256* root_receipt) {
    static const char root_sql[] =
        "SELECT root_receipt FROM " LAPLACE_PG_SCHEMA
        ".unicode_root_deposit_receipt WHERE activation_epoch_id=$1::"
        LAPLACE_PG_SCHEMA ".content_id_128 AND activation_epoch_fingerprint=$2::"
        LAPLACE_PG_SCHEMA ".record_id_256";
    Oid types[2] = {BYTEAOID, BYTEAOID};
    Datum values[2];
    int result;

    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        epoch->activation_epoch_id.bytes,
        sizeof(epoch->activation_epoch_id.bytes)));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        epoch->epoch_fingerprint.bytes, sizeof(epoch->epoch_fingerprint.bytes)));
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace could not connect to Unicode activation metadata")));
    }
    result = SPI_execute_with_args(
        root_sql, 2, types, values, NULL, true, 0);
    if (result != SPI_OK_SELECT || SPI_processed != 1u || SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace active mapped Unicode epoch has no unique durable root binding"),
                 errdetail("matching roots=%llu",
                           (unsigned long long)SPI_processed)));
    }
    read_exact_bytes(
        required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, "root receipt"),
        root_receipt->bytes, sizeof(root_receipt->bytes), "root receipt");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace could not close Unicode activation metadata lookup")));
    }
}

static void resolve_active_unicode_atoms_mapped(
    const laplace_framework_context* context,
    const uint32_t* positions,
    size_t count,
    laplace_composition_known_entity* known,
    laplace_pg_active_unicode_root* active) {
    laplace_pg_perfcache_pin* pin = NULL;
    laplace_unicode_atom_record_view* atoms;
    uint8_t* found;
    laplace_pg_perfcache_status pin_status;
    laplace_perfcache_registry_status resolve_status;
    size_t index;

    if (context == NULL || positions == NULL || count == 0u || known == NULL ||
        active == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace active Unicode resolution input is incomplete")));
    }
    if ((context->epoch_mask &
         (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE)) == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace composition requires a pinned Unicode perfcache epoch")));
    }
    atoms = (laplace_unicode_atom_record_view*)palloc0(
        sizeof(*atoms) * count);
    found = (uint8_t*)palloc0(count);
    memset(active, 0, sizeof(*active));

    pin_status = laplace_pg_perfcache_pin_active(0u, NULL, &pin);
    if (pin_status != LAPLACE_PG_PERFCACHE_OK || pin == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace could not pin the active mapped Unicode generation"),
                 errdetail("status=%u", (unsigned int)pin_status)));
    }

    PG_TRY();
    {
        require_pinned_unicode_epoch(context, &pin->epoch);
        resolve_status = laplace_perfcache_unicode_tier0_resolve_batch(
            &pin->native_pin, positions, count, atoms, found);
        if (resolve_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Active mapped Unicode Tier-0 resolution failed"),
                     errdetail("status=%u", (unsigned int)resolve_status)));
        }
        for (index = 0u; index < count; ++index) {
            const laplace_unicode_atom_record* atom = &atoms[index].value;
            if (found[index] == 0u || atom->codepoint_position != positions[index]) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Active mapped Unicode plane did not resolve the complete ordered atom set"),
                         errdetail("ordinal=%zu requested=%u resolved=%u found=%u",
                                   index + 1u,
                                   (unsigned int)positions[index],
                                   (unsigned int)atom->codepoint_position,
                                   (unsigned int)found[index])));
            }
            memset(&known[index], 0, sizeof(known[index]));
            known[index].entity_id = atom->content_id;
            known[index].identity_witness = atom->identity_preimage_fingerprint;
            known[index].physicality_id = atom->physicality_id;
            known[index].centroid = atom->coordinate;
            known[index].atom = atom->codepoint_position;
            known[index].has_atom = 1u;
            known[index].tier_floor = 0u;
        }
        active->activation_epoch_id = pin->epoch.activation_epoch_id;
        active->activation_epoch_fingerprint = pin->epoch.epoch_fingerprint;
        read_root_receipt_for_epoch(&pin->epoch, &active->root_receipt);
        laplace_pg_perfcache_pin_release(&pin);
    }
    PG_CATCH();
    {
        laplace_pg_perfcache_pin_release(&pin);
        PG_RE_THROW();
    }
    PG_END_TRY();
}

#if defined(LAPLACE_TEST_UNICODE_ATOM_RELATIONAL_LOOKUP)
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

static void resolve_active_unicode_atoms_relational(
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
        "SELECT r.ordinality,r.codepoint_position,a.entity_id,"
        "e.identity_witness,a.physicality_id,"
        "p.centroid_x,p.centroid_y,p.centroid_z,p.centroid_m "
        "FROM requested r JOIN " LAPLACE_PG_SCHEMA ".attestation a "
        "ON a.source_fingerprint=$2::" LAPLACE_PG_SCHEMA ".record_id_256 "
        "AND a.source_ordinal=(r.codepoint_position::bigint + 1) "
        "AND a.attestation_kind=$3::integer AND (a.flags & 1)=1 "
        "JOIN " LAPLACE_PG_SCHEMA ".entity e ON e.entity_id=a.entity_id "
        "JOIN " LAPLACE_PG_SCHEMA ".physicality p "
        "ON p.physicality_id=a.physicality_id "
        "ORDER BY r.ordinality";
    Oid atom_types[3] = {INT4ARRAYOID, BYTEAOID, INT4OID};
    Datum atom_values[3];
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
    atom_values[2] = Int32GetDatum(
        (int32)LAPLACE_PERSISTENCE_ATTESTATION_SOURCE_TESTIMONY);
    result = SPI_execute_with_args(
        atoms_sql, 3, atom_types, atom_values, NULL, true, 0);
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
#endif

void laplace_pg_resolve_active_unicode_atoms(
    const laplace_framework_context* context,
    const uint32_t* positions,
    size_t count,
    laplace_composition_known_entity* known,
    laplace_pg_active_unicode_root* active) {
#if defined(LAPLACE_TEST_UNICODE_ATOM_RELATIONAL_LOOKUP)
    resolve_active_unicode_atoms_relational(context, positions, count, known, active);
#else
    resolve_active_unicode_atoms_mapped(context, positions, count, known, active);
#endif
}
