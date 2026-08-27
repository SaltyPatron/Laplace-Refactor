#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "access/htup_details.h"
#include "access/xact.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/array.h"
#include "utils/builtins.h"

#include "blake3.h"
#include "laplace/highway.h"
#include "laplace/isa.h"
#include "laplace/contract/postgresql_bindings.h"
#include "composition_pg.h"
#include "laplace_pg_internal.h"

PG_FUNCTION_INFO_V1(LAPLACE_PG_HIGHWAY_REGISTRY_ACTIVATE_SYMBOL);
PG_FUNCTION_INFO_V1(LAPLACE_PG_HIGHWAY_REGISTRY_RESOLVE_SYMBOL);

typedef struct laplace_pg_active_unicode_root {
    laplace_digest256 root_receipt;
    laplace_id128 activation_epoch_id;
    laplace_digest256 activation_epoch_fingerprint;
} laplace_pg_active_unicode_root;

typedef struct laplace_pg_highway_activation_state {
    const laplace_highway_registry_receipt* registry;
    const laplace_isa_receipt* isa_receipt;
    const laplace_highway_registry_ast_view* ast;
    const laplace_pg_composition_execution* execution;
    const laplace_composition_result* root;
    laplace_framework_activation_receipt* activation_receipt;
    laplace_digest256 admission_receipt_id;
    uint64_t sequence;
} laplace_pg_highway_activation_state;

static void hash_u64(blake3_hasher* hasher, uint64_t value) {
    uint8_t encoded[8];
    size_t index;
    for (index = 0u; index < sizeof(encoded); ++index) {
        encoded[index] = (uint8_t)(value >> (index * 8u));
    }
    blake3_hasher_update(hasher, encoded, sizeof(encoded));
}

static laplace_digest256 finish_hash(blake3_hasher* hasher) {
    laplace_digest256 result;
    blake3_hasher_finalize(hasher, result.bytes, sizeof(result.bytes));
    return result;
}

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
                 errdetail("expected=%zu actual=%zu",
                           expected, (size_t)VARSIZE_ANY_EXHDR(value))));
    }
    memcpy(output, VARDATA_ANY(value), expected);
}

static bool datum_matches_bytes(
    Datum datum,
    const uint8_t* expected,
    size_t expected_size) {
    bytea* value = DatumGetByteaPP(datum);
    return (size_t)VARSIZE_ANY_EXHDR(value) == expected_size &&
        memcmp(VARDATA_ANY(value), expected, expected_size) == 0;
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
                 errmsg("Highway registry Unicode atom set is invalid")));
    }
    values = (Datum*)palloc(sizeof(*values) * count);
    for (index = 0u; index < count; ++index) {
        if (positions[index] > (uint32_t)PG_INT32_MAX) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Highway registry Unicode position is out of range")));
        }
        values[index] = Int32GetDatum((int32)positions[index]);
    }
    return construct_array(
        values, (int)count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
}

static void resolve_active_unicode_atoms(
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
                 errmsg("Highway registry Unicode resolution input is incomplete")));
    }
    memset(active, 0, sizeof(*active));
    if ((context->epoch_mask &
         (UINT64_C(1) << LAPLACE_FRAMEWORK_EPOCH_PERFCACHE)) == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Highway registry deposition requires a pinned Unicode perfcache epoch")));
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Highway registry could not connect to active Unicode state")));
    }
    result = SPI_execute(active_sql, true, 0);
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Highway registry requires exactly one active deposited Unicode root"),
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
                 errmsg("Highway registry context does not pin the active Unicode epoch")));
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
                 errmsg("Active Unicode root did not resolve the complete Highway registry atom set"),
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
            required_tuple_value(
                SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 3,
                "entity id"),
            known[index].entity_id.bytes, sizeof(known[index].entity_id.bytes),
            "entity id");
        read_exact_bytes(
            required_tuple_value(
                SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 4,
                "identity witness"),
            known[index].identity_witness.bytes,
            sizeof(known[index].identity_witness.bytes), "identity witness");
        read_exact_bytes(
            required_tuple_value(
                SPI_tuptable->vals[index], SPI_tuptable->tupdesc, 5,
                "physicality id"),
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
                 errmsg("Highway registry could not close active Unicode resolution")));
    }
}

static void execute_registry_instruction(
    const laplace_framework_context* context,
    laplace_highway_registry_receipt* registry,
    laplace_isa_receipt* receipt,
    bool persist_receipt) {
    uint32_t version = LAPLACE_HIGHWAY_REGISTRY_VERSION;
    laplace_isa_value_view values[2];
    laplace_isa_instruction instruction;
    laplace_isa_program program;
    laplace_isa_error error;
    laplace_isa_status status;
    memset(values, 0, sizeof(values));
    values[0].data = &version;
    values[0].count = 1u;
    values[0].capacity = 1u;
    values[0].stride_bytes = sizeof(version);
    values[0].type = LAPLACE_ISA_VALUE_U32_VECTOR;
    values[1].data = registry;
    values[1].capacity = 1u;
    values[1].stride_bytes = sizeof(*registry);
    values[1].type = LAPLACE_ISA_VALUE_HIGHWAY_REGISTRY_RECEIPT_VECTOR;
    memset(&instruction, 0, sizeof(instruction));
    instruction.opcode = LAPLACE_ISA_OPCODE_HIGHWAY_REGISTRY_MATERIALIZE_BATCH;
    instruction.version =
        LAPLACE_ISA_INSTRUCTION_VERSION_HIGHWAY_REGISTRY_MATERIALIZE_BATCH;
    instruction.output_value = 1u;
    memset(&program, 0, sizeof(program));
    program.instructions = &instruction;
    program.values = values;
    program.context = context;
    program.instruction_count = 1u;
    program.value_count = 2u;
    program.major = LAPLACE_ISA_MAJOR;
    program.minor = LAPLACE_ISA_MINOR;
    program.receipt_detail = LAPLACE_ISA_RECEIPT_DETAIL_FULL;
    memset(receipt, 0, sizeof(*receipt));
    memset(&error, 0, sizeof(error));
    status = laplace_isa_execute(&program, receipt, &error);
    if (status != LAPLACE_ISA_OK || values[1].count != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Highway registry ISA materialization failed"),
                 errdetail("status=%d instruction=%llu", (int)status,
                           (unsigned long long)error.instruction_index)));
    }
    if (persist_receipt) {
        laplace_pg_persist_execution_receipt(receipt, 1u, instruction.opcode);
    }
}

static void verify_registry_id_array(
    Datum value,
    const laplace_highway_kind_contract_row* kinds,
    const laplace_highway_alias_contract_row* aliases,
    const laplace_highway_disposition_contract_row* dispositions,
    size_t expected_count,
    const char* label) {
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    size_t index;
    deconstruct_array(
        DatumGetArrayTypeP(value), INT4OID, sizeof(int32), true, TYPALIGN_INT,
        &values, &nulls, &count);
    if (count < 0 || (size_t)count != expected_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Active Highway %s count differs", label)));
    }
    for (index = 0u; index < expected_count; ++index) {
        uint32_t expected;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Active Highway %s contains null", label)));
        }
        if (kinds != NULL) {
            expected = kinds[index].id;
        } else if (aliases != NULL) {
            expected = aliases[index].kind_id;
        } else {
            expected = dispositions[index].id;
        }
        if (DatumGetInt32(values[index]) <= 0 ||
            (uint32_t)DatumGetInt32(values[index]) != expected) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Active Highway %s ordering or identifier differs",
                            label)));
        }
    }
}

static void verify_entity_id_array(
    Datum value, size_t expected_count, const char* label) {
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    size_t index;
    deconstruct_array(
        DatumGetArrayTypeP(value), BYTEAOID, -1, false, TYPALIGN_INT,
        &values, &nulls, &count);
    if (count < 0 || (size_t)count != expected_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Active Highway %s count differs", label)));
    }
    for (index = 0u; index < expected_count; ++index) {
        bytea* entity;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Active Highway %s contains null", label)));
        }
        entity = DatumGetByteaPP(values[index]);
        if (VARSIZE_ANY_EXHDR(entity) != 16) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Active Highway %s contains invalid identity", label)));
        }
    }
}

static ArrayType* result_entity_array(
    const laplace_composition_result* results,
    size_t result_count,
    const uint64_t* indexes,
    size_t count) {
    Datum* values;
    size_t index;
    if (count == 0u) {
        return construct_empty_array(BYTEAOID);
    }
    values = (Datum*)palloc(sizeof(*values) * count);
    for (index = 0u; index < count; ++index) {
        if (indexes[index] >= result_count) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Highway registry AST name result index is invalid")));
        }
        values[index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            results[indexes[index]].entity_id.bytes,
            sizeof(results[indexes[index]].entity_id.bytes)));
    }
    return construct_array(
        values, (int)count, BYTEAOID, -1, false, TYPALIGN_INT);
}

static ArrayType* uint32_array(const uint32_t* values, size_t count) {
    Datum* datums;
    size_t index;
    if (count == 0u) {
        return construct_empty_array(INT4OID);
    }
    datums = (Datum*)palloc(sizeof(*datums) * count);
    for (index = 0u; index < count; ++index) {
        if (values[index] > (uint32_t)PG_INT32_MAX) {
            ereport(ERROR,
                    (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                     errmsg("Highway registry identifier exceeds PostgreSQL integer")));
        }
        datums[index] = Int32GetDatum((int32)values[index]);
    }
    return construct_array(
        datums, (int)count, INT4OID, sizeof(int32), true, TYPALIGN_INT);
}

static ArrayType* uint64_array(const uint64_t* values, size_t count) {
    Datum* datums;
    size_t index;
    if (count == 0u) {
        return construct_empty_array(INT8OID);
    }
    datums = (Datum*)palloc(sizeof(*datums) * count);
    for (index = 0u; index < count; ++index) {
        datums[index] = Int64GetDatum(laplace_pg_checked_int64(
            values[index], "Highway registry lifecycle version"));
    }
    return construct_array(
        datums, (int)count, INT8OID, sizeof(int64), true, TYPALIGN_DOUBLE);
}

static uint64 scalar_count(const char* label) {
    Datum value;
    if (SPI_processed != 1u || SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway registry %s verification returned no scalar", label)));
    }
    value = required_tuple_value(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, label);
    return (uint64)DatumGetInt64(value);
}

static laplace_framework_status highway_activation_prepare(
    void* opaque,
    const laplace_framework_context* context,
    const laplace_framework_stream_receipt* staged_receipt,
    const laplace_framework_activation_request* request,
    laplace_digest256* preparation_fingerprint) {
    static const char generation_insert_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA ".highway_registry_generation("
        "activation_epoch_id,activation_epoch_fingerprint,registry_version,"
        "registry_fingerprint,root_entity_id,root_physicality_id,"
        "source_fingerprint,recipe_fingerprint,isa_receipt,"
        "working_set_receipt,producer_receipt,staged_stream_receipt,"
        "stream_fingerprint,sink_artifacts_fingerprint,kind_count,alias_count,"
        "disposition_count) VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,"
        "$13,$14,$15,$16,$17) ON CONFLICT DO NOTHING";
    static const char generation_verify_sql[] =
        "SELECT registry_version,registry_fingerprint,root_entity_id,"
        "root_physicality_id,source_fingerprint,recipe_fingerprint,isa_receipt,"
        "working_set_receipt,producer_receipt,staged_stream_receipt,"
        "stream_fingerprint,sink_artifacts_fingerprint,kind_count,alias_count,"
        "disposition_count FROM " LAPLACE_PG_SCHEMA
        ".highway_registry_generation WHERE activation_epoch_id=$1 "
        "AND activation_epoch_fingerprint=$2";
    static const char kind_insert_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA
        ".highway_registry_kind_projection(activation_epoch_id,"
        "activation_epoch_fingerprint,kind_id,name_entity_id,introduced,retired) "
        "SELECT $1,$2,u.kind_id,u.name_entity_id,u.introduced,u.retired FROM "
        "unnest($3::integer[],$4::bytea[],$5::bigint[],$6::bigint[]) "
        "u(kind_id,name_entity_id,introduced,retired) ON CONFLICT DO NOTHING";
    static const char kind_verify_sql[] =
        "SELECT count(*) FROM unnest($3::integer[],$4::bytea[],$5::bigint[],"
        "$6::bigint[]) u(kind_id,name_entity_id,introduced,retired) JOIN "
        LAPLACE_PG_SCHEMA ".highway_registry_kind_projection p ON "
        "p.activation_epoch_id=$1 AND p.activation_epoch_fingerprint=$2 "
        "AND p.kind_id=u.kind_id AND p.name_entity_id=u.name_entity_id "
        "AND p.introduced=u.introduced AND p.retired=u.retired";
    static const char alias_insert_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA
        ".highway_registry_alias_projection(activation_epoch_id,"
        "activation_epoch_fingerprint,alias_name_entity_id,kind_id,"
        "introduced,retired) SELECT $1,$2,u.name_entity_id,u.kind_id,"
        "u.introduced,u.retired FROM unnest($3::bytea[],$4::integer[],"
        "$5::bigint[],$6::bigint[]) u(name_entity_id,kind_id,introduced,retired) "
        "ON CONFLICT DO NOTHING";
    static const char alias_verify_sql[] =
        "SELECT count(*) FROM unnest($3::bytea[],$4::integer[],$5::bigint[],"
        "$6::bigint[]) u(name_entity_id,kind_id,introduced,retired) JOIN "
        LAPLACE_PG_SCHEMA ".highway_registry_alias_projection p ON "
        "p.activation_epoch_id=$1 AND p.activation_epoch_fingerprint=$2 "
        "AND p.alias_name_entity_id=u.name_entity_id AND p.kind_id=u.kind_id "
        "AND p.introduced=u.introduced AND p.retired=u.retired";
    static const char disposition_insert_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA
        ".highway_registry_disposition_projection(activation_epoch_id,"
        "activation_epoch_fingerprint,disposition_id,name_entity_id) "
        "SELECT $1,$2,u.disposition_id,u.name_entity_id FROM "
        "unnest($3::integer[],$4::bytea[]) u(disposition_id,name_entity_id) "
        "ON CONFLICT DO NOTHING";
    static const char disposition_verify_sql[] =
        "SELECT count(*) FROM unnest($3::integer[],$4::bytea[]) "
        "u(disposition_id,name_entity_id) JOIN " LAPLACE_PG_SCHEMA
        ".highway_registry_disposition_projection p ON "
        "p.activation_epoch_id=$1 AND p.activation_epoch_fingerprint=$2 "
        "AND p.disposition_id=u.disposition_id "
        "AND p.name_entity_id=u.name_entity_id";
    laplace_pg_highway_activation_state* state =
        (laplace_pg_highway_activation_state*)opaque;
    Oid generation_types[17] = {
        BYTEAOID, BYTEAOID, INT8OID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, INT8OID, INT8OID, INT8OID};
    Datum generation_values[17];
    Oid projection_types[6] = {
        BYTEAOID, BYTEAOID, INT4ARRAYOID, BYTEAARRAYOID,
        INT8ARRAYOID, INT8ARRAYOID};
    Datum projection_values[6];
    const laplace_highway_kind_contract_row* kinds;
    const laplace_highway_alias_contract_row* aliases;
    const laplace_highway_disposition_contract_row* dispositions;
    uint32_t* identifiers;
    uint64_t* introduced;
    uint64_t* retired;
    size_t projection_capacity;
    size_t count;
    size_t index;
    int result;
    uint64 inserted_generation;
    const char* generation_mismatch = NULL;
    blake3_hasher hasher;

    if (state == NULL || state->registry == NULL || state->isa_receipt == NULL ||
        state->ast == NULL || state->execution == NULL || state->root == NULL ||
        state->activation_receipt == NULL || context == NULL ||
        staged_receipt == NULL || request == NULL ||
        preparation_fingerprint == NULL ||
        request->epoch_slot != LAPLACE_FRAMEWORK_EPOCH_NUMERIC ||
        !digest_equal(&request->next_epoch,
                      &state->registry->activation_epoch_fingerprint) ||
        !digest_equal(&staged_receipt->receipt_id,
                      &state->execution->persistence.producer.stream.receipt_id)) {
        return LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_INVALID;
    }
    generation_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->registry->activation_epoch_id.bytes, 16u));
    generation_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->registry->activation_epoch_fingerprint.bytes, 32u));
    generation_values[2] = Int64GetDatum(laplace_pg_checked_int64(
        state->registry->registry_version, "Highway registry version"));
    generation_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->registry->registry_fingerprint.bytes, 32u));
    generation_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->root->entity_id.bytes, 16u));
    generation_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->root->physicality_id.bytes, 32u));
    generation_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->ast->source_fingerprint.bytes, 32u));
    generation_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->ast->recipe_fingerprint.bytes, 32u));
    generation_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->isa_receipt->receipt_id.bytes, 32u));
    generation_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->execution->summary.receipt_id.bytes, 32u));
    generation_values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->execution->persistence.producer.receipt_id.bytes, 32u));
    generation_values[11] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->execution->persistence.producer.stream.receipt_id.bytes, 32u));
    generation_values[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->execution->persistence.producer.stream.stream_fingerprint.bytes,
        32u));
    generation_values[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->execution->persistence.producer.stream.sink_artifacts_fingerprint.bytes,
        32u));
    generation_values[14] = Int64GetDatum(laplace_pg_checked_int64(
        state->ast->kind_count, "Highway kind count"));
    generation_values[15] = Int64GetDatum(laplace_pg_checked_int64(
        state->ast->alias_count, "Highway alias count"));
    generation_values[16] = Int64GetDatum(laplace_pg_checked_int64(
        state->ast->disposition_count, "Highway disposition count"));

    if (SPI_connect() != SPI_OK_CONNECT) {
        return LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_INVALID;
    }
    result = SPI_execute_with_args(
        generation_insert_sql, 17, generation_types, generation_values,
        NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed > 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry generation staging failed")));
    }
    inserted_generation = SPI_processed;
    CommandCounterIncrement();
    result = SPI_execute_with_args(
        generation_verify_sql, 2, generation_types, generation_values,
        NULL, false, 1);
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        SPI_tuptable == NULL) {
        generation_mismatch = "keyed row";
    } else if (DatumGetInt64(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
            "registry version")) != DatumGetInt64(generation_values[2])) {
        generation_mismatch = "registry_version";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
            "registry fingerprint"),
            state->registry->registry_fingerprint.bytes, 32u)) {
        generation_mismatch = "registry_fingerprint";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
            "root entity"), state->root->entity_id.bytes, 16u)) {
        generation_mismatch = "root_entity_id";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 4,
            "root physicality"), state->root->physicality_id.bytes, 32u)) {
        generation_mismatch = "root_physicality_id";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 5,
            "source fingerprint"), state->ast->source_fingerprint.bytes, 32u)) {
        generation_mismatch = "source_fingerprint";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 6,
            "recipe fingerprint"), state->ast->recipe_fingerprint.bytes, 32u)) {
        generation_mismatch = "recipe_fingerprint";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 7,
            "ISA receipt"), state->isa_receipt->receipt_id.bytes, 32u)) {
        generation_mismatch = "isa_receipt";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 8,
            "working-set receipt"), state->execution->summary.receipt_id.bytes,
            32u)) {
        generation_mismatch = "working_set_receipt";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 9,
            "producer receipt"),
            state->execution->persistence.producer.receipt_id.bytes, 32u)) {
        generation_mismatch = "producer_receipt";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 10,
            "staged stream receipt"),
            state->execution->persistence.producer.stream.receipt_id.bytes,
            32u)) {
        generation_mismatch = "staged_stream_receipt";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 11,
            "stream fingerprint"),
            state->execution->persistence.producer.stream.stream_fingerprint.bytes,
            32u)) {
        generation_mismatch = "stream_fingerprint";
    } else if (!datum_matches_bytes(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 12,
            "sink artifacts fingerprint"),
            state->execution->persistence.producer.stream.sink_artifacts_fingerprint.bytes,
            32u)) {
        generation_mismatch = "sink_artifacts_fingerprint";
    } else if ((uint64)DatumGetInt64(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 13,
            "kind count")) != state->ast->kind_count) {
        generation_mismatch = "kind_count";
    } else if ((uint64)DatumGetInt64(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 14,
            "alias count")) != state->ast->alias_count) {
        generation_mismatch = "alias_count";
    } else if ((uint64)DatumGetInt64(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 15,
            "disposition count")) != state->ast->disposition_count) {
        generation_mismatch = "disposition_count";
    }
    if (generation_mismatch != NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway registry generation identity collides with different state"),
                 errdetail("field=%s inserted=%llu", generation_mismatch,
                           (unsigned long long)inserted_generation)));
    }

    projection_capacity = (size_t)state->ast->kind_count;
    if ((size_t)state->ast->alias_count > projection_capacity) {
        projection_capacity = (size_t)state->ast->alias_count;
    }
    if ((size_t)state->ast->disposition_count > projection_capacity) {
        projection_capacity = (size_t)state->ast->disposition_count;
    }
    if (projection_capacity == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway registry projection set is empty")));
    }
    identifiers = (uint32_t*)palloc(sizeof(*identifiers) * projection_capacity);
    introduced = (uint64_t*)palloc(sizeof(*introduced) * projection_capacity);
    retired = (uint64_t*)palloc(sizeof(*retired) * projection_capacity);

    kinds = laplace_highway_registry_kinds(&count);
    if (kinds == NULL || count != state->ast->kind_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway kind projection source changed during activation")));
    }
    for (index = 0u; index < count; ++index) {
        identifiers[index] = kinds[index].id;
        introduced[index] = kinds[index].introduced;
        retired[index] = kinds[index].retired;
    }
    projection_values[0] = generation_values[0];
    projection_values[1] = generation_values[1];
    projection_values[2] = PointerGetDatum(uint32_array(identifiers, count));
    projection_values[3] = PointerGetDatum(result_entity_array(
        state->execution->results, state->execution->result_count,
        state->ast->kind_name_result_indexes, count));
    projection_values[4] = PointerGetDatum(uint64_array(introduced, count));
    projection_values[5] = PointerGetDatum(uint64_array(retired, count));
    result = SPI_execute_with_args(
        kind_insert_sql, 6, projection_types, projection_values, NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed > (uint64)count) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway kind projection staging failed")));
    }
    CommandCounterIncrement();
    result = SPI_execute_with_args(
        kind_verify_sql, 6, projection_types, projection_values, NULL, false, 1);
    if (result != SPI_OK_SELECT || scalar_count("kind projection") != count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway kind projection differs from canonical AST")));
    }

    aliases = laplace_highway_registry_aliases(&count);
    if (count != state->ast->alias_count || (count != 0u && aliases == NULL)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway alias projection source changed during activation")));
    }
    if (count != 0u) {
        for (index = 0u; index < count; ++index) {
            identifiers[index] = aliases[index].kind_id;
            introduced[index] = aliases[index].introduced;
            retired[index] = aliases[index].retired;
        }
        projection_values[2] = PointerGetDatum(result_entity_array(
            state->execution->results, state->execution->result_count,
            state->ast->alias_name_result_indexes, count));
        projection_values[3] = PointerGetDatum(uint32_array(identifiers, count));
        projection_values[4] = PointerGetDatum(uint64_array(introduced, count));
        projection_values[5] = PointerGetDatum(uint64_array(retired, count));
        projection_types[2] = BYTEAARRAYOID;
        projection_types[3] = INT4ARRAYOID;
        result = SPI_execute_with_args(
            alias_insert_sql, 6, projection_types, projection_values,
            NULL, false, 0);
        if (result != SPI_OK_INSERT || SPI_processed > (uint64)count) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Highway alias projection staging failed")));
        }
        CommandCounterIncrement();
        result = SPI_execute_with_args(
            alias_verify_sql, 6, projection_types, projection_values,
            NULL, false, 1);
        if (result != SPI_OK_SELECT || scalar_count("alias projection") != count) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Highway alias projection differs from canonical AST")));
        }
        projection_types[2] = INT4ARRAYOID;
        projection_types[3] = BYTEAARRAYOID;
    }

    dispositions = laplace_highway_registry_dispositions(&count);
    if (dispositions == NULL || count != state->ast->disposition_count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway disposition projection source changed during activation")));
    }
    for (index = 0u; index < count; ++index) {
        identifiers[index] = dispositions[index].id;
    }
    projection_values[2] = PointerGetDatum(uint32_array(identifiers, count));
    projection_values[3] = PointerGetDatum(result_entity_array(
        state->execution->results, state->execution->result_count,
        state->ast->disposition_name_result_indexes, count));
    result = SPI_execute_with_args(
        disposition_insert_sql, 4, projection_types, projection_values,
        NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed > (uint64)count) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway disposition projection staging failed")));
    }
    CommandCounterIncrement();
    result = SPI_execute_with_args(
        disposition_verify_sql, 4, projection_types, projection_values,
        NULL, false, 1);
    if (result != SPI_OK_SELECT ||
        scalar_count("disposition projection") != count) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway disposition projection differs from canonical AST")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry could not close generation staging")));
    }

    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, "laplace-highway-registry-activation-preparation-v1",
        sizeof("laplace-highway-registry-activation-preparation-v1") - 1u);
    blake3_hasher_update(&hasher, staged_receipt->receipt_id.bytes, 32u);
    blake3_hasher_update(&hasher, state->root->entity_id.bytes, 16u);
    blake3_hasher_update(
        &hasher, state->root->physicality_id.bytes, 32u);
    blake3_hasher_update(
        &hasher, state->registry->registry_fingerprint.bytes, 32u);
    hash_u64(&hasher, state->ast->kind_count);
    hash_u64(&hasher, state->ast->alias_count);
    hash_u64(&hasher, state->ast->disposition_count);
    *preparation_fingerprint = finish_hash(&hasher);
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status highway_activation_commit(
    void* opaque,
    const laplace_framework_activation_request* request,
    const laplace_digest256* preparation_fingerprint,
    laplace_digest256* activation_fingerprint) {
    static const char select_sql[] =
        "SELECT sequence,active_present,activation_epoch_fingerprint FROM "
        LAPLACE_PG_SCHEMA ".highway_registry_active_control "
        "WHERE singleton FOR UPDATE";
    static const char update_sql[] =
        "UPDATE " LAPLACE_PG_SCHEMA ".highway_registry_active_control SET "
        "sequence=$1,active_present=true,activation_epoch_id=$2,"
        "activation_epoch_fingerprint=$3,admission_receipt=$4,"
        "activation_receipt=$4,activation_fingerprint=$5 WHERE singleton";
    static const char event_sql[] =
        "INSERT INTO " LAPLACE_PG_SCHEMA
        ".highway_registry_activation_event(sequence,activation_epoch_id,"
        "activation_epoch_fingerprint,admission_receipt,activation_receipt,"
        "activation_fingerprint,activation_transaction_id) "
        "VALUES($1,$2,$3,$4,$4,$5,$6)";
    laplace_pg_highway_activation_state* state =
        (laplace_pg_highway_activation_state*)opaque;
    Oid types[6] = {INT8OID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, XIDOID};
    Datum values[6];
    HeapTuple tuple;
    TupleDesc descriptor;
    uint64 current_sequence;
    bool active_present;
    laplace_digest256 current_epoch;
    blake3_hasher hasher;
    int result;

    if (state == NULL || request == NULL || preparation_fingerprint == NULL ||
        activation_fingerprint == NULL || state->registry == NULL ||
        state->activation_receipt == NULL ||
        request->epoch_slot != LAPLACE_FRAMEWORK_EPOCH_NUMERIC ||
        !digest_equal(&request->next_epoch,
                      &state->registry->activation_epoch_fingerprint)) {
        return LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_INVALID;
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        return LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_INVALID;
    }
    result = SPI_execute(select_sql, false, 1);
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway registry active control is absent")));
    }
    tuple = SPI_tuptable->vals[0];
    descriptor = SPI_tuptable->tupdesc;
    current_sequence = (uint64)DatumGetInt64(required_tuple_value(
        tuple, descriptor, 1, "active sequence"));
    active_present = DatumGetBool(required_tuple_value(
        tuple, descriptor, 2, "active presence"));
    read_exact_bytes(
        required_tuple_value(tuple, descriptor, 3, "active epoch"),
        current_epoch.bytes, sizeof(current_epoch.bytes), "active epoch");
    if ((!active_present && current_sequence != 0u) ||
        (active_present &&
         !digest_equal(&current_epoch, &request->expected_epoch)) ||
        current_sequence == (uint64)PG_INT64_MAX) {
        SPI_finish();
        return LAPLACE_FRAMEWORK_ACTIVATION_COMMIT_FAILED;
    }
    state->sequence = current_sequence + 1u;
    state->admission_receipt_id = state->activation_receipt->receipt_id;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(
        &hasher, "laplace-highway-registry-activation-commit-v1",
        sizeof("laplace-highway-registry-activation-commit-v1") - 1u);
    blake3_hasher_update(&hasher, preparation_fingerprint->bytes, 32u);
    blake3_hasher_update(&hasher, request->next_epoch.bytes, 32u);
    blake3_hasher_update(
        &hasher, state->admission_receipt_id.bytes, 32u);
    hash_u64(&hasher, state->sequence);
    *activation_fingerprint = finish_hash(&hasher);

    values[0] = Int64GetDatum((int64)state->sequence);
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->registry->activation_epoch_id.bytes, 16u));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->registry->activation_epoch_fingerprint.bytes, 32u));
    values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->admission_receipt_id.bytes, 32u));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        activation_fingerprint->bytes, 32u));
    values[5] = TransactionIdGetDatum(GetTopTransactionId());
    result = SPI_execute_with_args(
        update_sql, 5, types, values, NULL, false, 0);
    if (result != SPI_OK_UPDATE || SPI_processed != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry active control update failed")));
    }
    result = SPI_execute_with_args(
        event_sql, 6, types, values, NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry activation event insert failed")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry could not close activation commit")));
    }
    return LAPLACE_FRAMEWORK_OK;
}

static void highway_activation_abort(
    void* opaque,
    const laplace_framework_activation_request* request,
    const laplace_digest256* preparation_fingerprint) {
    (void)opaque;
    (void)request;
    (void)preparation_fingerprint;
}

static void record_final_activation_receipt(
    const laplace_pg_highway_activation_state* state) {
    Oid types[3] = {BYTEAOID, INT8OID, BYTEAOID};
    Datum values[3];
    int result;
    if (state == NULL || state->activation_receipt == NULL ||
        state->sequence == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry final activation receipt is incomplete")));
    }
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->activation_receipt->receipt_id.bytes, 32u));
    values[1] = Int64GetDatum((int64)state->sequence);
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->admission_receipt_id.bytes, 32u));
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Highway registry could not record final activation receipt")));
    }
    /* Match the pre-commit receipt through the event row selected by sequence. */
    result = SPI_execute_with_args(
        "UPDATE " LAPLACE_PG_SCHEMA
        ".highway_registry_active_control c SET activation_receipt=$1 "
        "FROM " LAPLACE_PG_SCHEMA ".highway_registry_activation_event e "
        "WHERE c.singleton AND c.sequence=$2 AND c.admission_receipt=$3 "
        "AND e.sequence=c.sequence AND e.admission_receipt=$3",
        3, types, values, NULL, false, 0);
    if (result != SPI_OK_UPDATE || SPI_processed != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry active receipt finalization failed")));
    }
    result = SPI_execute_with_args(
        "UPDATE " LAPLACE_PG_SCHEMA
        ".highway_registry_activation_event SET activation_receipt=$1 "
        "WHERE sequence=$2 AND admission_receipt=$3",
        3, types, values, NULL, false, 0);
    if (result != SPI_OK_UPDATE || SPI_processed != 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry event receipt finalization failed")));
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry could not close receipt finalization")));
    }
}

static bool load_active_registry_replay(
    laplace_pg_highway_activation_state* state) {
    static const char sql[] =
        "SELECT c.sequence,c.admission_receipt,c.activation_receipt,"
        "c.activation_fingerprint FROM " LAPLACE_PG_SCHEMA
        ".highway_registry_active_control c JOIN " LAPLACE_PG_SCHEMA
        ".highway_registry_generation g ON "
        "g.activation_epoch_id=c.activation_epoch_id AND "
        "g.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "WHERE c.singleton AND c.active_present "
        "AND c.activation_epoch_id=$1 AND c.activation_epoch_fingerprint=$2 "
        "AND g.registry_fingerprint=$3 AND g.root_entity_id=$4 "
        "AND g.root_physicality_id=$5 AND g.isa_receipt=$6 "
        "AND g.source_fingerprint=$3";
    Oid types[6] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID};
    Datum values[6];
    int result;
    HeapTuple tuple;
    TupleDesc descriptor;
    if (state == NULL || state->registry == NULL || state->root == NULL ||
        state->isa_receipt == NULL || state->execution == NULL ||
        state->activation_receipt == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Highway registry replay state is incomplete")));
    }
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->registry->activation_epoch_id.bytes, 16u));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->registry->activation_epoch_fingerprint.bytes, 32u));
    values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->registry->registry_fingerprint.bytes, 32u));
    values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->root->entity_id.bytes, 16u));
    values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->root->physicality_id.bytes, 32u));
    values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->isa_receipt->receipt_id.bytes, 32u));
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Highway registry could not inspect active generation")));
    }
    result = SPI_execute_with_args(sql, 6, types, values, NULL, true, 1);
    if (result != SPI_OK_SELECT || SPI_processed > 1u) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Highway registry active generation lookup is ambiguous")));
    }
    if (SPI_processed == 0u) {
        if (SPI_finish() != SPI_OK_FINISH) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Highway registry could not close inactive lookup")));
        }
        return false;
    }
    tuple = SPI_tuptable->vals[0];
    descriptor = SPI_tuptable->tupdesc;
    state->sequence = (uint64)DatumGetInt64(required_tuple_value(
        tuple, descriptor, 1, "activation sequence"));
    read_exact_bytes(
        required_tuple_value(tuple, descriptor, 2, "admission receipt"),
        state->admission_receipt_id.bytes, 32u, "admission receipt");
    read_exact_bytes(
        required_tuple_value(tuple, descriptor, 3, "activation receipt"),
        state->activation_receipt->receipt_id.bytes, 32u,
        "activation receipt");
    read_exact_bytes(
        required_tuple_value(tuple, descriptor, 4, "activation fingerprint"),
        state->activation_receipt->activation_fingerprint.bytes, 32u,
        "activation fingerprint");
    state->activation_receipt->effect_disposition =
        LAPLACE_FRAMEWORK_EFFECT_ACTIVATED;
    state->activation_receipt->status = LAPLACE_FRAMEWORK_OK;
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry could not close active replay lookup")));
    }
    return true;
}

Datum LAPLACE_PG_HIGHWAY_REGISTRY_ACTIVATE_SYMBOL(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_highway_registry_receipt registry;
    laplace_isa_receipt isa_receipt;
    laplace_highway_registry_ast_plan* ast_plan = NULL;
    laplace_highway_registry_ast_view ast;
    laplace_composition_known_entity* known = NULL;
    laplace_pg_active_unicode_root active_unicode;
    laplace_composition_working_set_input input;
    laplace_pg_composition_execution execution;
    laplace_framework_activation_request activation_request;
    laplace_framework_activation_receipt activation_receipt;
    laplace_framework_activation_provider_v1 activation_provider;
    laplace_pg_highway_activation_state activation_state;
    const laplace_composition_result* root;
    Datum result_values[41];
    bool result_nulls[41] = {false};
    HeapTuple tuple;
    uint64_t preferred_batch_bytes;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if ((context.flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("Highway registry deposition requires a writable execution context")));
    }
    preferred_batch_bytes = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(1), "highway registry preferred_batch_bytes");
    memset(&registry, 0, sizeof(registry));
    memset(&isa_receipt, 0, sizeof(isa_receipt));
    memset(&ast, 0, sizeof(ast));
    memset(&execution, 0, sizeof(execution));
    memset(&activation_request, 0, sizeof(activation_request));
    memset(&activation_receipt, 0, sizeof(activation_receipt));
    memset(&activation_provider, 0, sizeof(activation_provider));
    memset(&activation_state, 0, sizeof(activation_state));
    execute_registry_instruction(&context, &registry, &isa_receipt, true);

    PG_TRY();
    {
        if (laplace_highway_registry_ast_plan_create(
                &context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY],
                &registry.context_fingerprint, &ast_plan) != LAPLACE_HIGHWAY_OK ||
            laplace_highway_registry_ast_plan_view(ast_plan, &ast) !=
                LAPLACE_HIGHWAY_OK ||
            !digest_equal(&ast.source_fingerprint,
                          &registry.registry_fingerprint)) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_EXCEPTION),
                     errmsg("Highway registry universal AST compilation failed")));
        }
        known = (laplace_composition_known_entity*)palloc0(
            sizeof(*known) * (size_t)ast.atom_count);
        resolve_active_unicode_atoms(
            &context, ast.atom_positions, (size_t)ast.atom_count, known,
            &active_unicode);
        memset(&input, 0, sizeof(input));
        input.context = &context;
        input.source_fingerprint = &ast.source_fingerprint;
        input.calculation_recipe_fingerprint = &ast.recipe_fingerprint;
        input.known_entities = known;
        input.known_entity_count = ast.atom_count;
        input.operands = ast.operands;
        input.operand_count = ast.operand_count;
        input.requests = ast.requests;
        input.request_count = ast.request_count;
        input.preferred_batch_bytes = preferred_batch_bytes;
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(&input, &execution);
        if (ast.root_result_index >= execution.result_count) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Highway registry root result is absent after deposition")));
        }
        root = &execution.results[ast.root_result_index];
        activation_state.registry = &registry;
        activation_state.isa_receipt = &isa_receipt;
        activation_state.ast = &ast;
        activation_state.execution = &execution;
        activation_state.root = root;
        activation_state.activation_receipt = &activation_receipt;
        if (!load_active_registry_replay(&activation_state)) {
            activation_request.expected_epoch =
                context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC];
            activation_request.next_epoch =
                registry.activation_epoch_fingerprint;
            activation_request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_NUMERIC;
            activation_provider.state = &activation_state;
            activation_provider.prepare = highway_activation_prepare;
            activation_provider.commit = highway_activation_commit;
            activation_provider.abort = highway_activation_abort;
            activation_provider.abi_major =
                LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MAJOR;
            activation_provider.abi_minor =
                LAPLACE_FRAMEWORK_ACTIVATION_PROVIDER_ABI_MINOR;
            if (laplace_framework_activate_staged_stream(
                    &context,
                    &execution.persistence.producer.stream,
                    &activation_request,
                    &activation_provider,
                    &activation_receipt) != LAPLACE_FRAMEWORK_OK) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_EXCEPTION),
                         errmsg("Highway registry numeric epoch activation failed"),
                         errdetail("status=%d effect=%u",
                                   (int)activation_receipt.status,
                                   activation_receipt.effect_disposition)));
            }
            record_final_activation_receipt(&activation_state);
        }
        result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            root->entity_id.bytes, sizeof(root->entity_id.bytes)));
        result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            root->physicality_id.bytes, sizeof(root->physicality_id.bytes)));
        result_values[2] = Int16GetDatum((int16)root->tier_floor);
        result_values[3] = Int64GetDatum(laplace_pg_checked_int64(
            registry.registry_version, "highway registry version"));
        result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            registry.registry_fingerprint.bytes, 32u));
        result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            registry.activation_epoch_id.bytes, 16u));
        result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            registry.activation_epoch_fingerprint.bytes, 32u));
        result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            isa_receipt.receipt_id.bytes, 32u));
        result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            ast.source_fingerprint.bytes, 32u));
        result_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            ast.recipe_fingerprint.bytes, 32u));
        result_values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            active_unicode.root_receipt.bytes, 32u));
        result_values[11] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            active_unicode.activation_epoch_id.bytes, 16u));
        result_values[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            active_unicode.activation_epoch_fingerprint.bytes, 32u));
        result_values[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.summary.receipt_id.bytes, 32u));
        result_values[14] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.presence.semantic_receipt_id.bytes, 32u));
        result_values[15] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.presence.execution_receipt_id.bytes, 32u));
        result_values[16] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.producer.receipt_id.bytes, 32u));
        result_values[17] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.producer.stream.receipt_id.bytes, 32u));
        result_values[18] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.producer.stream.stream_fingerprint.bytes, 32u));
        result_values[19] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.producer.stream.sink_artifacts_fingerprint.bytes,
            32u));
        result_values[20] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            activation_state.admission_receipt_id.bytes, 32u));
        result_values[21] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            activation_receipt.receipt_id.bytes, 32u));
        result_values[22] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            activation_receipt.activation_fingerprint.bytes, 32u));
        result_values[23] = Int64GetDatum(laplace_pg_checked_int64(
            activation_state.sequence, "Highway registry activation sequence"));
        result_values[24] = Int32GetDatum(
            (int32)activation_receipt.effect_disposition);
        result_values[25] = PointerGetDatum(result_entity_array(
            execution.results, execution.result_count,
            ast.kind_name_result_indexes, (size_t)ast.kind_count));
        result_values[26] = PointerGetDatum(result_entity_array(
            execution.results, execution.result_count,
            ast.alias_name_result_indexes, (size_t)ast.alias_count));
        result_values[27] = PointerGetDatum(result_entity_array(
            execution.results, execution.result_count,
            ast.disposition_name_result_indexes,
            (size_t)ast.disposition_count));
        result_values[28] = Int64GetDatum(laplace_pg_checked_int64(
            ast.atom_count, "highway registry atom count"));
        result_values[29] = Int64GetDatum(laplace_pg_checked_int64(
            ast.request_count, "highway registry request count"));
        result_values[30] = laplace_pg_numeric_from_uint64(
            execution.summary.unique_entity_count);
        result_values[31] = laplace_pg_numeric_from_uint64(
            execution.summary.unique_physicality_count);
        result_values[32] = laplace_pg_numeric_from_uint64(
            execution.summary.novel_entity_count);
        result_values[33] = laplace_pg_numeric_from_uint64(
            execution.summary.novel_physicality_count);
        result_values[34] = laplace_pg_numeric_from_uint64(
            execution.persistence.inserted[0]);
        result_values[35] = laplace_pg_numeric_from_uint64(
            execution.persistence.inserted[1]);
        result_values[36] = laplace_pg_numeric_from_uint64(
            execution.persistence.inserted[2]);
        result_values[37] = laplace_pg_numeric_from_uint64(
            execution.persistence.inserted[3]);
        result_values[38] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            execution.persistence.plan_sequence_fingerprint.bytes, 32u));
        result_values[39] = Int32GetDatum((int32)execution.persistence.plan_count);
        result_values[40] = Int32GetDatum((int32)execution.summary.status);
        tuple = laplace_pg_form_result_tuple(
            fcinfo, result_values, result_nulls, 41);
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(&execution);
        laplace_highway_registry_ast_plan_destroy(&ast_plan);
    }
    PG_CATCH();
    {
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(&execution);
        laplace_highway_registry_ast_plan_destroy(&ast_plan);
        PG_RE_THROW();
    }
    PG_END_TRY();
    return HeapTupleGetDatum(tuple);
}

Datum LAPLACE_PG_HIGHWAY_REGISTRY_RESOLVE_SYMBOL(PG_FUNCTION_ARGS) {
    static const char active_sql[] =
        "SELECT c.sequence,c.activation_epoch_id,"
        "c.activation_epoch_fingerprint,c.activation_receipt,"
        "c.activation_fingerprint,g.registry_version,g.registry_fingerprint,"
        "g.root_entity_id,g.root_physicality_id,g.kind_count,g.alias_count,"
        "g.disposition_count,"
        "ARRAY(SELECT p.kind_id FROM " LAPLACE_PG_SCHEMA
        ".highway_registry_kind_projection p WHERE "
        "p.activation_epoch_id=c.activation_epoch_id AND "
        "p.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "ORDER BY p.kind_id),"
        "ARRAY(SELECT p.name_entity_id FROM " LAPLACE_PG_SCHEMA
        ".highway_registry_kind_projection p WHERE "
        "p.activation_epoch_id=c.activation_epoch_id AND "
        "p.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "ORDER BY p.kind_id),"
        "ARRAY(SELECT p.kind_id FROM " LAPLACE_PG_SCHEMA
        ".highway_registry_alias_projection p WHERE "
        "p.activation_epoch_id=c.activation_epoch_id AND "
        "p.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "ORDER BY p.alias_name_entity_id),"
        "ARRAY(SELECT p.alias_name_entity_id FROM " LAPLACE_PG_SCHEMA
        ".highway_registry_alias_projection p WHERE "
        "p.activation_epoch_id=c.activation_epoch_id AND "
        "p.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "ORDER BY p.alias_name_entity_id),"
        "ARRAY(SELECT p.disposition_id FROM " LAPLACE_PG_SCHEMA
        ".highway_registry_disposition_projection p WHERE "
        "p.activation_epoch_id=c.activation_epoch_id AND "
        "p.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "ORDER BY p.disposition_id),"
        "ARRAY(SELECT p.name_entity_id FROM " LAPLACE_PG_SCHEMA
        ".highway_registry_disposition_projection p WHERE "
        "p.activation_epoch_id=c.activation_epoch_id AND "
        "p.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "ORDER BY p.disposition_id),"
        "u.activation_epoch_id,u.epoch_fingerprint "
        "FROM " LAPLACE_PG_SCHEMA ".highway_registry_active_control c "
        "JOIN " LAPLACE_PG_SCHEMA ".highway_registry_generation g ON "
        "g.activation_epoch_id=c.activation_epoch_id AND "
        "g.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "JOIN " LAPLACE_PG_SCHEMA ".perfcache_active_control u ON "
        "u.singleton AND u.active_present "
        "JOIN " LAPLACE_PG_SCHEMA ".physicality ph ON "
        "ph.physicality_id=g.root_physicality_id AND "
        "ph.entity_id=g.root_entity_id "
        "WHERE c.singleton AND c.active_present";
    laplace_framework_context context;
    laplace_highway_registry_receipt registry;
    laplace_isa_receipt isa_receipt;
    const laplace_highway_kind_contract_row* kinds;
    const laplace_highway_alias_contract_row* aliases;
    const laplace_highway_disposition_contract_row* dispositions;
    size_t kind_count = 0u;
    size_t alias_count = 0u;
    size_t disposition_count = 0u;
    HeapTuple source;
    TupleDesc descriptor;
    Datum source_values[20];
    Datum result_values[19];
    bool result_nulls[19] = {false};
    HeapTuple result_tuple;
    int result;
    int column;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if ((context.flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Highway registry resolution requires a read-only execution context")));
    }
    memset(&registry, 0, sizeof(registry));
    memset(&isa_receipt, 0, sizeof(isa_receipt));
    execute_registry_instruction(&context, &registry, &isa_receipt, false);
    kinds = laplace_highway_registry_kinds(&kind_count);
    aliases = laplace_highway_registry_aliases(&alias_count);
    dispositions = laplace_highway_registry_dispositions(&disposition_count);
    if (kinds == NULL || dispositions == NULL ||
        (alias_count != 0u && aliases == NULL)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Native Highway registry projection is incomplete")));
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Highway registry could not inspect active state")));
    }
    result = SPI_execute(active_sql, true, 1);
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Exactly one active Highway registry is required"),
                 errdetail("matching active states=%llu",
                           (unsigned long long)SPI_processed)));
    }
    source = SPI_tuptable->vals[0];
    descriptor = SPI_tuptable->tupdesc;
    for (column = 0; column < 20; ++column) {
        source_values[column] = required_tuple_value(
            source, descriptor, column + 1, "active Highway field");
    }
    if ((uint64)DatumGetInt64(source_values[5]) != registry.registry_version ||
        (uint64)DatumGetInt64(source_values[9]) != registry.kind_count ||
        (uint64)DatumGetInt64(source_values[10]) != registry.alias_count ||
        (uint64)DatumGetInt64(source_values[11]) != registry.disposition_count ||
        !datum_matches_bytes(source_values[1], registry.activation_epoch_id.bytes, 16u) ||
        !datum_matches_bytes(source_values[2], registry.activation_epoch_fingerprint.bytes, 32u) ||
        !datum_matches_bytes(source_values[6], registry.registry_fingerprint.bytes, 32u) ||
        !datum_matches_bytes(source_values[2],
            context.epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC].bytes, 32u) ||
        !datum_matches_bytes(source_values[19],
            context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes, 32u)) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Active Highway state differs from its native registry or pinned epochs")));
    }
    verify_registry_id_array(
        source_values[12], kinds, NULL, NULL, kind_count, "kind identifiers");
    verify_entity_id_array(source_values[13], kind_count, "kind names");
    verify_registry_id_array(
        source_values[14], NULL, aliases, NULL, alias_count,
        "alias target identifiers");
    verify_entity_id_array(source_values[15], alias_count, "alias names");
    verify_registry_id_array(
        source_values[16], NULL, NULL, dispositions, disposition_count,
        "disposition identifiers");
    verify_entity_id_array(
        source_values[17], disposition_count, "disposition names");

    result_values[0] = source_values[5];
    result_values[1] = source_values[6];
    result_values[2] = source_values[1];
    result_values[3] = source_values[2];
    result_values[4] = source_values[7];
    result_values[5] = source_values[8];
    result_values[6] = source_values[0];
    result_values[7] = source_values[3];
    result_values[8] = source_values[4];
    result_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        isa_receipt.receipt_id.bytes, 32u));
    result_values[10] = source_values[12];
    result_values[11] = source_values[13];
    result_values[12] = source_values[14];
    result_values[13] = source_values[15];
    result_values[14] = source_values[16];
    result_values[15] = source_values[17];
    result_values[16] = source_values[18];
    result_values[17] = source_values[19];
    result_values[18] = Int32GetDatum((int32)LAPLACE_HIGHWAY_OK);
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 19);
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Highway registry could not close active-state inspection")));
    }
    return HeapTupleGetDatum(result_tuple);
}
