#include "postgres.h"
#include <string.h>

#include "catalog/pg_type.h"
#include "utils/builtins.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/persistence.h"
#include "laplace_pg_internal.h"
#include "persistence_rows_pg.h"
#include "set_pg.h"

void laplace_pg_entity_binding_open(
    laplace_pg_composite_binding* binding) {
    static const Oid attribute_types[2] = {BYTEAOID, BYTEAOID};
    static const int32 attribute_typmods[2] = {
        LAPLACE_PG_TYPMOD_NONE, LAPLACE_PG_TYPMOD_NONE};
    laplace_pg_composite_binding_open(
        "entity_record", attribute_types, attribute_typmods,
        2, binding);
}

Datum laplace_pg_entity_record(
    const laplace_pg_composite_binding* binding,
    const laplace_persistence_entity_record* entity) {
    Datum values[2];
    bool nulls[2] = {false, false};
    if (entity == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace entity persistence record is null")));
    }
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        entity->entity_id.bytes, sizeof(entity->entity_id.bytes)));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        entity->identity_witness.bytes,
        sizeof(entity->identity_witness.bytes)));
    return laplace_pg_composite_record(binding, values, nulls);
}

const char* laplace_pg_entity_insert_sql(void) {
    return "INSERT INTO " LAPLACE_PG_SCHEMA
        ".entity(entity_id,identity_witness) "
        "SELECT i.entity_id,i.identity_witness FROM unnest($1::"
        LAPLACE_PG_SCHEMA ".entity_record[]) i "
        "WHERE NOT EXISTS (SELECT 1 FROM " LAPLACE_PG_SCHEMA
        ".entity s WHERE s.entity_id=i.entity_id)";
}

const char* laplace_pg_entity_verify_sql(void) {
    return "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".entity_record[]) i JOIN " LAPLACE_PG_SCHEMA
        ".entity s ON s.entity_id=i.entity_id "
        "AND s.identity_witness=i.identity_witness";
}

void laplace_pg_physicality_binding_open(
    laplace_pg_composite_binding* binding) {
    static const Oid attribute_types[18] = {
        BYTEAOID, BYTEAOID,
        INT4OID, INT4OID, INT4OID, INT4OID, INT4OID, INT4OID,
        BYTEAOID, BYTEAOID, BYTEAOID,
        FLOAT8OID, FLOAT8OID, FLOAT8OID, FLOAT8OID, FLOAT8OID,
        NUMERICOID, NUMERICOID};
    static const int32 attribute_typmods[18] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0)};
    laplace_pg_composite_binding_open(
        "physicality_record", attribute_types, attribute_typmods,
        18, binding);
}

void laplace_pg_physicality_read_record(
    Datum value, laplace_persistence_physicality_record* physicality) {
    HeapTupleHeader tuple = DatumGetHeapTupleHeader(value);
    uint32_t* scalars[6];
    bytea* id;
    laplace_digest256 actual;
    int index;
    if (physicality == NULL)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace physicality output cannot be null")));
    memset(physicality, 0, sizeof(*physicality));
    laplace_pg_read_digest(laplace_pg_required_composite_attribute(
        tuple, 1, "physicality_id"), &physicality->physicality_id, "physicality_id");
    id = DatumGetByteaPP(laplace_pg_required_composite_attribute(tuple, 2, "entity_id"));
    if ((size_t)VARSIZE_ANY_EXHDR(id) != sizeof(physicality->entity_id.bytes))
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace physicality entity identity has invalid width")));
    memcpy(physicality->entity_id.bytes, VARDATA_ANY(id), sizeof(physicality->entity_id.bytes));
    scalars[0] = &physicality->physicality_type;
    scalars[1] = &physicality->vertex_class;
    scalars[2] = &physicality->recipe_version;
    scalars[3] = &physicality->structural_form;
    scalars[4] = &physicality->dimension_count;
    scalars[5] = &physicality->flags;
    for (index = 0; index < 6; ++index) {
        int32 scalar = DatumGetInt32(laplace_pg_required_composite_attribute(
            tuple, index + 3, "physicality scalar"));
        if (scalar < 0)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("Laplace physicality scalar cannot be negative")));
        *scalars[index] = (uint32_t)scalar;
    }
    laplace_pg_read_digest(laplace_pg_required_composite_attribute(tuple, 9, "recipe_fingerprint"),
        &physicality->recipe_fingerprint, "recipe_fingerprint");
    laplace_pg_read_digest(laplace_pg_required_composite_attribute(tuple, 10, "geometry_epoch"),
        &physicality->geometry_epoch, "geometry_epoch");
    laplace_pg_read_digest(laplace_pg_required_composite_attribute(tuple, 11, "trajectory_fingerprint"),
        &physicality->trajectory_fingerprint, "trajectory_fingerprint");
    for (index = 0; index < 4; ++index)
        physicality->centroid.component[index] = DatumGetFloat8(
            laplace_pg_required_composite_attribute(tuple, index + 12, "centroid"));
    physicality->radius = DatumGetFloat8(
        laplace_pg_required_composite_attribute(tuple, 16, "radius"));
    physicality->logical_count = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 17, "logical_count"), "logical_count");
    physicality->vertex_count = laplace_pg_uint64_from_numeric(
        laplace_pg_required_composite_attribute(tuple, 18, "vertex_count"), "vertex_count");
    if (laplace_persistence_physicality_identify(physicality, &actual) != LAPLACE_PERSISTENCE_OK ||
        memcmp(actual.bytes, physicality->physicality_id.bytes, sizeof(actual.bytes)) != 0)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace physicality fields differ from their native immutable identity")));
}

Datum laplace_pg_physicality_record(
    const laplace_pg_composite_binding* binding,
    const laplace_persistence_physicality_record* physicality) {
    Datum fields[18];
    bool nulls[18] = {false};
    if (physicality == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace physicality persistence record is null")));
    }
    fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->physicality_id.bytes,
        sizeof(physicality->physicality_id.bytes)));
    fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->entity_id.bytes, sizeof(physicality->entity_id.bytes)));
    fields[2] = Int32GetDatum((int32)physicality->physicality_type);
    fields[3] = Int32GetDatum((int32)physicality->vertex_class);
    fields[4] = Int32GetDatum((int32)physicality->recipe_version);
    fields[5] = Int32GetDatum((int32)physicality->structural_form);
    fields[6] = Int32GetDatum((int32)physicality->dimension_count);
    fields[7] = Int32GetDatum((int32)physicality->flags);
    fields[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->recipe_fingerprint.bytes,
        sizeof(physicality->recipe_fingerprint.bytes)));
    fields[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->geometry_epoch.bytes,
        sizeof(physicality->geometry_epoch.bytes)));
    fields[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->trajectory_fingerprint.bytes,
        sizeof(physicality->trajectory_fingerprint.bytes)));
    fields[11] = Float8GetDatum(physicality->centroid.component[0]);
    fields[12] = Float8GetDatum(physicality->centroid.component[1]);
    fields[13] = Float8GetDatum(physicality->centroid.component[2]);
    fields[14] = Float8GetDatum(physicality->centroid.component[3]);
    fields[15] = Float8GetDatum(physicality->radius);
    fields[16] = laplace_pg_numeric_from_uint64(physicality->logical_count);
    fields[17] = laplace_pg_numeric_from_uint64(physicality->vertex_count);
    return laplace_pg_composite_record(binding, fields, nulls);
}

static Datum physicality_required_attribute(
    HeapTuple tuple, TupleDesc descriptor, int attribute, const char* field) {
    bool is_null = false;
    Datum value = heap_getattr(tuple, attribute, descriptor, &is_null);
    if (is_null) {
        ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
            errmsg("Laplace physicality %s cannot be null", field)));
    }
    return value;
}

static void physicality_read_id(Datum datum, laplace_id128* id, const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if (VARSIZE_ANY_EXHDR(value) != (int)sizeof(id->bytes)) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
            errmsg("Laplace physicality %s must contain exactly 16 bytes", field)));
    }
    memcpy(id->bytes, VARDATA_ANY(value), sizeof(id->bytes));
}

void laplace_pg_read_physicality_row(
    HeapTuple tuple,
    TupleDesc descriptor,
    laplace_persistence_physicality_record* value) {
    memset(value, 0, sizeof(*value));
    laplace_pg_read_digest(
        physicality_required_attribute(tuple, descriptor, 1, "physicality_id"),
        &value->physicality_id, "physicality_id");
    physicality_read_id(
        physicality_required_attribute(tuple, descriptor, 2, "entity_id"),
        &value->entity_id, "entity_id");
    value->physicality_type = (uint32_t)DatumGetInt32(
        physicality_required_attribute(tuple, descriptor, 3, "physicality_type"));
    value->vertex_class = (uint32_t)DatumGetInt32(
        physicality_required_attribute(tuple, descriptor, 4, "vertex_class"));
    value->recipe_version = (uint32_t)DatumGetInt32(
        physicality_required_attribute(tuple, descriptor, 5, "recipe_version"));
    value->structural_form = (uint32_t)DatumGetInt32(
        physicality_required_attribute(tuple, descriptor, 6, "structural_form"));
    value->dimension_count = (uint32_t)DatumGetInt32(
        physicality_required_attribute(tuple, descriptor, 7, "dimension_count"));
    value->flags = (uint32_t)DatumGetInt32(
        physicality_required_attribute(tuple, descriptor, 8, "flags"));
    laplace_pg_read_digest(
        physicality_required_attribute(tuple, descriptor, 9, "recipe_fingerprint"),
        &value->recipe_fingerprint, "recipe_fingerprint");
    laplace_pg_read_digest(
        physicality_required_attribute(tuple, descriptor, 10, "geometry_epoch"),
        &value->geometry_epoch, "geometry_epoch");
    laplace_pg_read_digest(
        physicality_required_attribute(tuple, descriptor, 11, "trajectory_fingerprint"),
        &value->trajectory_fingerprint, "trajectory_fingerprint");
    value->centroid.component[0] = DatumGetFloat8(
        physicality_required_attribute(tuple, descriptor, 12, "centroid_x"));
    value->centroid.component[1] = DatumGetFloat8(
        physicality_required_attribute(tuple, descriptor, 13, "centroid_y"));
    value->centroid.component[2] = DatumGetFloat8(
        physicality_required_attribute(tuple, descriptor, 14, "centroid_z"));
    value->centroid.component[3] = DatumGetFloat8(
        physicality_required_attribute(tuple, descriptor, 15, "centroid_m"));
    value->radius = DatumGetFloat8(
        physicality_required_attribute(tuple, descriptor, 16, "radius"));
    value->logical_count = laplace_pg_uint64_from_numeric(
        physicality_required_attribute(tuple, descriptor, 17, "logical_count"),
        "physicality logical_count");
    value->vertex_count = laplace_pg_uint64_from_numeric(
        physicality_required_attribute(tuple, descriptor, 18, "vertex_count"),
        "physicality vertex_count");
}


void laplace_pg_physicality_deposit_binding_open(
    laplace_pg_composite_binding* binding) {
    static const Oid attribute_types[19] = {
        BYTEAOID, BYTEAOID,
        INT4OID, INT4OID, INT4OID, INT4OID, INT4OID, INT4OID,
        BYTEAOID, BYTEAOID, BYTEAOID,
        FLOAT8OID, FLOAT8OID, FLOAT8OID, FLOAT8OID, FLOAT8OID,
        NUMERICOID, NUMERICOID, BYTEAOID};
    static const int32 attribute_typmods[19] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1};
    laplace_pg_composite_binding_open(
        "physicality_deposit_record", attribute_types, attribute_typmods,
        19, binding);
}

Datum laplace_pg_physicality_deposit_record(
    const laplace_pg_composite_binding* binding,
    const laplace_persistence_physicality_record* physicality,
    const uint8_t* trajectory,
    size_t trajectory_bytes) {
    Datum fields[19];
    bool nulls[19] = {false};
    if (physicality == NULL ||
        (trajectory_bytes != 0u && trajectory == NULL) ||
        physicality->vertex_count > SIZE_MAX / sizeof(laplace_trajectory_carrier) ||
        trajectory_bytes != (size_t)physicality->vertex_count *
            sizeof(laplace_trajectory_carrier)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace physicality deposit trajectory is invalid")));
    }
    fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->physicality_id.bytes,
        sizeof(physicality->physicality_id.bytes)));
    fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->entity_id.bytes, sizeof(physicality->entity_id.bytes)));
    fields[2] = Int32GetDatum((int32)physicality->physicality_type);
    fields[3] = Int32GetDatum((int32)physicality->vertex_class);
    fields[4] = Int32GetDatum((int32)physicality->recipe_version);
    fields[5] = Int32GetDatum((int32)physicality->structural_form);
    fields[6] = Int32GetDatum((int32)physicality->dimension_count);
    fields[7] = Int32GetDatum((int32)physicality->flags);
    fields[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->recipe_fingerprint.bytes,
        sizeof(physicality->recipe_fingerprint.bytes)));
    fields[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->geometry_epoch.bytes,
        sizeof(physicality->geometry_epoch.bytes)));
    fields[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        physicality->trajectory_fingerprint.bytes,
        sizeof(physicality->trajectory_fingerprint.bytes)));
    fields[11] = Float8GetDatum(physicality->centroid.component[0]);
    fields[12] = Float8GetDatum(physicality->centroid.component[1]);
    fields[13] = Float8GetDatum(physicality->centroid.component[2]);
    fields[14] = Float8GetDatum(physicality->centroid.component[3]);
    fields[15] = Float8GetDatum(physicality->radius);
    fields[16] = laplace_pg_numeric_from_uint64(physicality->logical_count);
    fields[17] = laplace_pg_numeric_from_uint64(physicality->vertex_count);
    fields[18] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        trajectory, trajectory_bytes));
    return laplace_pg_composite_record(binding, fields, nulls);
}

const char* laplace_pg_physicality_insert_sql(void) {
    return "INSERT INTO " LAPLACE_PG_SCHEMA
        ".physicality(physicality_id,entity_id,physicality_type,vertex_class,"
        "recipe_version,structural_form,dimension_count,flags,recipe_fingerprint,"
        "geometry_epoch,trajectory_fingerprint,centroid_x,centroid_y,centroid_z,"
        "centroid_m,radius,logical_count,vertex_count,trajectory) "
        "SELECT i.physicality_id,i.entity_id,i.physicality_type,i.vertex_class,"
        "i.recipe_version,i.structural_form,i.dimension_count,i.flags,"
        "i.recipe_fingerprint,i.geometry_epoch,i.trajectory_fingerprint,"
        "i.centroid_x,i.centroid_y,i.centroid_z,i.centroid_m,i.radius,"
        "i.logical_count,i.vertex_count,'\\x'::bytea FROM unnest($1::"
        LAPLACE_PG_SCHEMA ".physicality_record[]) i "
        "WHERE NOT EXISTS (SELECT 1 FROM " LAPLACE_PG_SCHEMA
        ".physicality s WHERE s.physicality_id=i.physicality_id)";
}

const char* laplace_pg_physicality_verify_sql(void) {
    return "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".physicality_record[]) i JOIN " LAPLACE_PG_SCHEMA
        ".physicality s ON s.physicality_id=i.physicality_id "
        "AND s.entity_id=i.entity_id "
        "AND s.physicality_type=i.physicality_type "
        "AND s.vertex_class=i.vertex_class "
        "AND s.recipe_version=i.recipe_version "
        "AND s.structural_form=i.structural_form "
        "AND s.dimension_count=i.dimension_count AND s.flags=i.flags "
        "AND s.recipe_fingerprint=i.recipe_fingerprint "
        "AND s.geometry_epoch=i.geometry_epoch "
        "AND s.trajectory_fingerprint=i.trajectory_fingerprint "
        "AND float8send(s.centroid_x)=float8send(i.centroid_x) "
        "AND float8send(s.centroid_y)=float8send(i.centroid_y) "
        "AND float8send(s.centroid_z)=float8send(i.centroid_z) "
        "AND float8send(s.centroid_m)=float8send(i.centroid_m) "
        "AND float8send(s.radius)=float8send(i.radius) "
        "AND s.logical_count=i.logical_count "
        "AND s.vertex_count=i.vertex_count "
        "AND s.trajectory='\\x'::bytea";
}

void laplace_pg_attestation_binding_open(
    laplace_pg_composite_binding* binding) {
    static const Oid attribute_types[8] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, NUMERICOID, INT4OID, INT4OID};
    static const int32 attribute_typmods[8] = {
        -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1};
    laplace_pg_composite_binding_open(
        "attestation_record", attribute_types, attribute_typmods,
        8, binding);
}

Datum laplace_pg_attestation_record(
    const laplace_pg_composite_binding* binding,
    const laplace_persistence_attestation_record* attestation) {
    Datum fields[8];
    bool nulls[8] = {false};
    if (attestation == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace attestation persistence record is null")));
    }
    fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        attestation->attestation_id.bytes,
        sizeof(attestation->attestation_id.bytes)));
    fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        attestation->entity_id.bytes, sizeof(attestation->entity_id.bytes)));
    if ((attestation->flags &
         LAPLACE_PERSISTENCE_ATTESTATION_HAS_PHYSICALITY) != 0u) {
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            attestation->physicality_id.bytes,
            sizeof(attestation->physicality_id.bytes)));
    } else {
        fields[2] = (Datum)0;
        nulls[2] = true;
    }
    fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        attestation->source_fingerprint.bytes,
        sizeof(attestation->source_fingerprint.bytes)));
    fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        attestation->context_fingerprint.bytes,
        sizeof(attestation->context_fingerprint.bytes)));
    fields[5] = laplace_pg_numeric_from_uint64(attestation->source_ordinal);
    fields[6] = Int32GetDatum((int32)attestation->flags);
    fields[7] = Int32GetDatum((int32)attestation->attestation_kind);
    return laplace_pg_composite_record(binding, fields, nulls);
}

const char* laplace_pg_attestation_insert_sql(void) {
    return "INSERT INTO " LAPLACE_PG_SCHEMA
        ".attestation(attestation_id,entity_id,physicality_id,"
        "source_fingerprint,context_fingerprint,source_ordinal,flags,"
        "attestation_kind) "
        "SELECT i.attestation_id,i.entity_id,i.physicality_id,"
        "i.source_fingerprint,i.context_fingerprint,i.source_ordinal,"
        "i.flags,i.attestation_kind FROM unnest($1::"
        LAPLACE_PG_SCHEMA ".attestation_record[]) i "
        "WHERE NOT EXISTS (SELECT 1 FROM " LAPLACE_PG_SCHEMA
        ".attestation s WHERE s.attestation_id=i.attestation_id)";
}

const char* laplace_pg_attestation_verify_sql(void) {
    return "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".attestation_record[]) i JOIN " LAPLACE_PG_SCHEMA
        ".attestation s ON s.attestation_id=i.attestation_id "
        "AND s.entity_id=i.entity_id "
        "AND s.physicality_id IS NOT DISTINCT FROM i.physicality_id "
        "AND s.source_fingerprint=i.source_fingerprint "
        "AND s.context_fingerprint=i.context_fingerprint "
        "AND s.source_ordinal=i.source_ordinal AND s.flags=i.flags "
        "AND s.attestation_kind=i.attestation_kind";
}
