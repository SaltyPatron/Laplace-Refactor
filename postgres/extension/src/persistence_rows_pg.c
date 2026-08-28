#include "postgres.h"

#include "catalog/pg_type.h"

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
