#include "postgres.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "utils/memutils.h"

#include "blake3.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/framework.h"
#include "laplace/perfcache_modules.h"
#include "laplace/perfcache_registry.h"
#include "laplace/persistence.h"
#include "laplace/spool.h"
#include "laplace/unicode_root.h"
#include "laplace/unicode_root_builder.h"
#include "laplace/unicode_tier0_sink.h"
#include "laplace_pg_internal.h"
#include "perfcache_pg.h"
#include "persistence_rows_pg.h"
#include "set_pg.h"
#include "unicode_root_pg.h"

PG_FUNCTION_INFO_V1(LAPLACE_PG_UNICODE_ROOT_SYMBOL);

typedef struct unicode_batch_counts {
    uint64 family[4];
} unicode_batch_counts;

typedef struct unicode_batch_arrays {
    ArrayType* entities;
    ArrayType* physicalities;
    ArrayType* atoms;
    ArrayType* ducet_positions;
    ArrayType* ducet_contractions;
    ArrayType* normalizations;
} unicode_batch_arrays;

struct laplace_pg_unicode_sink {
    laplace_pg_unicode_sink_configuration configuration;
    laplace_pg_unicode_sink_result result;
    laplace_unicode_root_stream_validator* validator;
    MemoryContext batch_context;
    uint8 canonical_manifest[LAPLACE_UNICODE_ROOT_MANIFEST_BYTES];
    uint64 expected_frames;
    uint64 expected_bytes;
    uint64 staged_frames;
    uint64 staged_bytes;
    uint64 batch_memory_limit;
    uint64 plan_invocations[LAPLACE_PG_UNICODE_ROOT_PLAN_COUNT];
    blake3_hasher plan_sequence_hasher;
    uint32 manifest_seen;
    uint32 begun;
    uint32 aborted;
    uint32 spi_connected;
};

static SPIPlanPtr entity_insert_plan = NULL;
static SPIPlanPtr entity_verify_plan = NULL;
static SPIPlanPtr physicality_insert_plan = NULL;
static SPIPlanPtr physicality_verify_plan = NULL;
static SPIPlanPtr atom_insert_plan = NULL;
static SPIPlanPtr atom_verify_plan = NULL;
static SPIPlanPtr ducet_position_insert_plan = NULL;
static SPIPlanPtr ducet_position_verify_plan = NULL;
static SPIPlanPtr ducet_contraction_insert_plan = NULL;
static SPIPlanPtr ducet_contraction_verify_plan = NULL;
static SPIPlanPtr normalization_insert_plan = NULL;
static SPIPlanPtr normalization_verify_plan = NULL;
static SPIPlanPtr generation_insert_plan = NULL;
static SPIPlanPtr generation_verify_plan = NULL;
static SPIPlanPtr deposit_receipt_insert_plan = NULL;
static SPIPlanPtr deposit_receipt_verify_plan = NULL;
static SPIPlanPtr effect_verify_plan = NULL;

static const char unicode_effect_verify_statement[] =
    "SELECT "
    "(SELECT count(DISTINCT entity_id) FROM " LAPLACE_PG_SCHEMA
    ".unicode_atom_binding WHERE root_receipt=$1),"
    "(SELECT count(DISTINCT physicality_id) FROM " LAPLACE_PG_SCHEMA
    ".unicode_atom_binding WHERE root_receipt=$1),"
    "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA
    ".unicode_atom_binding WHERE root_receipt=$1),"
    "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA
    ".unicode_ducet_position WHERE root_receipt=$1),"
    "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA
    ".unicode_ducet_contraction WHERE root_receipt=$1),"
    "(SELECT count(*) FROM " LAPLACE_PG_SCHEMA
    ".unicode_normalization_composition WHERE root_receipt=$1)";

static const char unicode_generation_insert_statement[] =
    "INSERT INTO " LAPLACE_PG_SCHEMA ".unicode_root_generation("
    "root_receipt,source_fingerprint,recipe_fingerprint,numeric_provider_receipt,"
    "stream_contract_fingerprint,algorithmic_hangul_rule_fingerprint,"
    "atom_record_contract_fingerprint,physicality_recipe_fingerprint,"
    "placement_rank_permutation_fingerprint,coordinate_table_fingerprint,"
    "geometry_epoch,physicality_recipe_version,stream_fingerprint,"
    "atom_section_fingerprint,ducet_position_section_fingerprint,"
    "ducet_contraction_section_fingerprint,normalization_section_fingerprint,"
    "atom_count,ducet_position_count,ducet_contraction_count,"
    "normalization_composition_count,total_frame_count,total_encoded_bytes,"
    "canonical_manifest,postgresql_contract_fingerprint,"
    "plan_manifest_fingerprint,plan_sequence_fingerprint,"
    "postgresql_artifact_fingerprint) "
    "VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,"
    "$17,$18,$19,$20,$21,$22,$23,$24,$25,$26,$27,$28) "
    "ON CONFLICT DO NOTHING";

static const char unicode_generation_verify_statement[] =
    "SELECT count(*) FROM " LAPLACE_PG_SCHEMA
    ".unicode_root_generation WHERE root_receipt=$1 "
    "AND source_fingerprint=$2 AND recipe_fingerprint=$3 "
    "AND numeric_provider_receipt=$4 AND stream_contract_fingerprint=$5 "
    "AND algorithmic_hangul_rule_fingerprint=$6 "
    "AND atom_record_contract_fingerprint=$7 "
    "AND physicality_recipe_fingerprint=$8 "
    "AND placement_rank_permutation_fingerprint=$9 "
    "AND coordinate_table_fingerprint=$10 AND geometry_epoch=$11 "
    "AND physicality_recipe_version=$12 AND stream_fingerprint=$13 "
    "AND atom_section_fingerprint=$14 "
    "AND ducet_position_section_fingerprint=$15 "
    "AND ducet_contraction_section_fingerprint=$16 "
    "AND normalization_section_fingerprint=$17 "
    "AND atom_count=$18 AND ducet_position_count=$19 "
    "AND ducet_contraction_count=$20 "
    "AND normalization_composition_count=$21 "
    "AND total_frame_count=$22 AND total_encoded_bytes=$23 "
    "AND canonical_manifest=$24 "
    "AND postgresql_contract_fingerprint=$25 "
    "AND plan_manifest_fingerprint=$26 "
    "AND plan_sequence_fingerprint=$27 "
    "AND postgresql_artifact_fingerprint=$28";

static const char unicode_deposit_insert_statement[] =
    "INSERT INTO " LAPLACE_PG_SCHEMA ".unicode_root_deposit_receipt("
    "receipt_id,root_receipt,producer_receipt,staged_stream_receipt,"
    "sink_artifacts_fingerprint,postgresql_artifact_fingerprint,"
    "tier0_artifact_fingerprint,perfcache_manifest_fingerprint,"
    "perfcache_encoded_manifest_fingerprint,activation_epoch_id,"
    "activation_epoch_fingerprint,admission_receipt,total_frame_count,"
    "total_encoded_bytes,batch_count,plan_manifest_fingerprint,"
    "plan_sequence_fingerprint,plan_count,entity_count,physicality_count,"
    "atom_count,ducet_position_count,ducet_contraction_count,"
    "normalization_composition_count) "
    "VALUES($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,"
    "$17,$18,$19,$20,$21,$22,$23,$24) ON CONFLICT DO NOTHING";

static const char unicode_deposit_verify_statement[] =
    "SELECT count(*) FROM " LAPLACE_PG_SCHEMA
    ".unicode_root_deposit_receipt WHERE receipt_id=$1 "
    "AND root_receipt=$2 AND producer_receipt=$3 "
    "AND staged_stream_receipt=$4 AND sink_artifacts_fingerprint=$5 "
    "AND postgresql_artifact_fingerprint=$6 "
    "AND tier0_artifact_fingerprint=$7 "
    "AND perfcache_manifest_fingerprint=$8 "
    "AND perfcache_encoded_manifest_fingerprint=$9 "
    "AND activation_epoch_id=$10 AND activation_epoch_fingerprint=$11 "
    "AND admission_receipt=$12 AND total_frame_count=$13 "
    "AND total_encoded_bytes=$14 AND batch_count=$15 "
    "AND plan_manifest_fingerprint=$16 AND plan_sequence_fingerprint=$17 "
    "AND plan_count=$18 AND entity_count=$19 AND physicality_count=$20 "
    "AND atom_count=$21 AND ducet_position_count=$22 "
    "AND ducet_contraction_count=$23 "
    "AND normalization_composition_count=$24";

static void plan_manifest_fingerprint(laplace_digest256* fingerprint);

static void hash_u32(blake3_hasher* hasher, uint32 value) {
    uint8 encoded[4];
    encoded[0] = (uint8)(value & 0xffu);
    encoded[1] = (uint8)((value >> 8u) & 0xffu);
    encoded[2] = (uint8)((value >> 16u) & 0xffu);
    encoded[3] = (uint8)((value >> 24u) & 0xffu);
    blake3_hasher_update(hasher, encoded, sizeof(encoded));
}

static void hash_u64(blake3_hasher* hasher, uint64 value) {
    uint8 encoded[8];
    size_t index;
    for (index = 0u; index < sizeof(encoded); ++index) {
        encoded[index] = (uint8)((value >> (index * 8u)) & 0xffu);
    }
    blake3_hasher_update(hasher, encoded, sizeof(encoded));
}

static void finish_plan_sequence(
    const laplace_pg_unicode_sink* state,
    laplace_digest256* fingerprint) {
    blake3_hasher copy = state->plan_sequence_hasher;
    hash_u64(&copy, state->result.plan_count);
    blake3_hasher_finalize(&copy, fingerprint->bytes, sizeof(fingerprint->bytes));
}

static void predict_two_plans(
    const laplace_pg_unicode_sink* state,
    uint32 first,
    uint32 second,
    laplace_digest256* fingerprint) {
    blake3_hasher copy = state->plan_sequence_hasher;
    hash_u32(&copy, first);
    hash_u32(&copy, second);
    hash_u64(&copy, state->result.plan_count + 2u);
    blake3_hasher_finalize(&copy, fingerprint->bytes, sizeof(fingerprint->bytes));
}

static bool digest_equal(
    const laplace_digest256* left,
    const laplace_digest256* right) {
    return left != NULL && right != NULL &&
        memcmp(left->bytes, right->bytes, sizeof(left->bytes)) == 0;
}

static int hex_nibble(char value) {
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    return -1;
}

static void contract_fingerprint(laplace_digest256* fingerprint) {
    const char* encoded = LAPLACE_PG_UNICODE_ROOT_CONTRACT_FINGERPRINT_HEX;
    size_t index;
    if (fingerprint == NULL || strlen(encoded) != 64u) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Unicode PostgreSQL sink contract fingerprint is invalid")));
    }
    for (index = 0u; index < sizeof(fingerprint->bytes); ++index) {
        const int high = hex_nibble(encoded[index * 2u]);
        const int low = hex_nibble(encoded[index * 2u + 1u]);
        if (high < 0 || low < 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Unicode PostgreSQL sink contract fingerprint is invalid")));
        }
        fingerprint->bytes[index] = (uint8_t)((high << 4) | low);
    }
}

static bytea* digest_bytea(const laplace_digest256* digest) {
    return laplace_pg_bytes_to_bytea(digest->bytes, sizeof(digest->bytes));
}

static bytea* id_bytea(const laplace_id128* id) {
    return laplace_pg_bytes_to_bytea(id->bytes, sizeof(id->bytes));
}

static void fail_sink(laplace_pg_unicode_sink* state, const char* message) {
    if (state != NULL) {
        state->aborted = 1u;
    }
    ereport(ERROR,
            (errcode(ERRCODE_DATA_CORRUPTED),
             errmsg("%s", message)));
}

static void note_plan(laplace_pg_unicode_sink* state, uint32 plan_id) {
    if (state == NULL || plan_id == 0u ||
        plan_id > LAPLACE_PG_UNICODE_ROOT_PLAN_COUNT ||
        state->result.plan_count == UINT64_MAX ||
        state->plan_invocations[plan_id - 1u] == UINT64_MAX) {
        fail_sink(state, "Unicode PostgreSQL plan accounting overflowed");
    }
    ++state->plan_invocations[plan_id - 1u];
    ++state->result.plan_count;
    hash_u32(&state->plan_sequence_hasher, plan_id);
}

static bool frame_counts(
    const laplace_framework_canonical_batch* batch,
    unicode_batch_counts* counts) {
    size_t offset = 0u;
    uint64 index;
    memset(counts, 0, sizeof(*counts));
    for (index = 0u; index < batch->record_count; ++index) {
        laplace_unicode_root_frame_view frame;
        size_t consumed = 0u;
        memset(&frame, 0, sizeof(frame));
        if (offset >= (size_t)batch->byte_count ||
            laplace_unicode_root_frame_open(
                batch->canonical_bytes + offset,
                (size_t)batch->byte_count - offset,
                &frame, &consumed) != LAPLACE_UNICODE_OK ||
            consumed == 0u ||
            consumed > (size_t)batch->byte_count - offset ||
            frame.value.kind < LAPLACE_UNICODE_ROOT_FRAME_ATOM ||
            frame.value.kind > LAPLACE_UNICODE_ROOT_FRAME_MANIFEST) {
            return false;
        }
        if (frame.value.kind != LAPLACE_UNICODE_ROOT_FRAME_MANIFEST) {
            ++counts->family[frame.value.kind - 1u];
        }
        offset += consumed;
    }
    return offset == (size_t)batch->byte_count;
}

static Datum* datum_array(uint64 count) {
    if (count == 0u) {
        return NULL;
    }
    if (count > SIZE_MAX / sizeof(Datum)) {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("Unicode PostgreSQL batch cardinality is too large")));
    }
    return (Datum*)palloc(sizeof(Datum) * (size_t)count);
}

static void build_batch_arrays(
    laplace_pg_unicode_sink* state,
    const laplace_framework_canonical_batch* batch,
    const unicode_batch_counts* counts,
    unicode_batch_arrays* arrays) {
    static const Oid atom_types[14] = {
        BYTEAOID, INT4OID, INT4OID, INT2OID, INT2OID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        FLOAT8OID, FLOAT8OID, FLOAT8OID, FLOAT8OID, BYTEAOID};
    static const int32 atom_typmods[14] = {
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1};
    static const Oid ducet_position_types[6] = {
        BYTEAOID, INT4OID, INT2OID, INT4OID,
        BYTEAOID, BYTEAOID};
    static const int32 ducet_position_typmods[6] = {
        -1, -1, -1, -1, -1, -1};
    static const Oid ducet_contraction_types[6] = {
        BYTEAOID, INT4OID, INT4OID, BYTEAOID,
        INT4OID, BYTEAOID};
    static const int32 ducet_contraction_typmods[6] = {
        -1, -1, -1, -1, -1, -1};
    static const Oid normalization_types[4] = {
        BYTEAOID, INT4OID, INT4OID, INT4OID};
    static const int32 normalization_typmods[4] = {
        -1, -1, -1, -1};
    Datum* entity_rows = datum_array(counts->family[0]);
    Datum* physicality_rows = datum_array(counts->family[0]);
    Datum* atom_rows = datum_array(counts->family[0]);
    Datum* ducet_position_rows = datum_array(counts->family[1]);
    Datum* ducet_contraction_rows = datum_array(counts->family[2]);
    Datum* normalization_rows = datum_array(counts->family[3]);
    uint64 row[4] = {0u, 0u, 0u, 0u};
    laplace_pg_composite_binding entity_binding;
    laplace_pg_composite_binding physicality_binding;
    laplace_pg_composite_binding atom_binding;
    laplace_pg_composite_binding ducet_position_binding;
    laplace_pg_composite_binding ducet_contraction_binding;
    laplace_pg_composite_binding normalization_binding;
    bytea* root_receipt = digest_bytea(
        &state->configuration.expected_summary.receipt_id);
    size_t offset = 0u;
    uint64 index;
    memset(arrays, 0, sizeof(*arrays));
    memset(&entity_binding, 0, sizeof(entity_binding));
    memset(&physicality_binding, 0, sizeof(physicality_binding));
    memset(&atom_binding, 0, sizeof(atom_binding));
    memset(&ducet_position_binding, 0, sizeof(ducet_position_binding));
    memset(&ducet_contraction_binding, 0, sizeof(ducet_contraction_binding));
    memset(&normalization_binding, 0, sizeof(normalization_binding));
    if (counts->family[0] != 0u) {
        laplace_pg_entity_binding_open(&entity_binding);
        laplace_pg_physicality_binding_open(&physicality_binding);
        laplace_pg_composite_binding_open(
            "unicode_atom_deposit_record", atom_types, atom_typmods,
            14, &atom_binding);
    }
    if (counts->family[1] != 0u) {
        laplace_pg_composite_binding_open(
            "unicode_ducet_position_deposit_record",
            ducet_position_types, ducet_position_typmods,
            6, &ducet_position_binding);
    }
    if (counts->family[2] != 0u) {
        laplace_pg_composite_binding_open(
            "unicode_ducet_contraction_deposit_record",
            ducet_contraction_types, ducet_contraction_typmods,
            6, &ducet_contraction_binding);
    }
    if (counts->family[3] != 0u) {
        laplace_pg_composite_binding_open(
            "unicode_normalization_deposit_record",
            normalization_types, normalization_typmods,
            4, &normalization_binding);
    }
    for (index = 0u; index < batch->record_count; ++index) {
        laplace_unicode_root_frame_view frame;
        size_t consumed = 0u;
        memset(&frame, 0, sizeof(frame));
        if (laplace_unicode_root_frame_open(
                batch->canonical_bytes + offset,
                (size_t)batch->byte_count - offset,
                &frame, &consumed) != LAPLACE_UNICODE_OK) {
            fail_sink(state, "Unicode PostgreSQL sink could not reopen a canonical frame");
        }
        if (frame.value.kind == LAPLACE_UNICODE_ROOT_FRAME_ATOM) {
            laplace_unicode_atom_record_view atom;
            laplace_persistence_entity_record entity;
            laplace_persistence_physicality_record physicality;
            Datum fields[14];
            bool nulls[14] = {false};
            size_t atom_bytes = 0u;
            memset(&atom, 0, sizeof(atom));
            memset(&entity, 0, sizeof(entity));
            memset(&physicality, 0, sizeof(physicality));
            if (laplace_unicode_atom_record_open(
                    frame.value.payload, frame.value.payload_bytes,
                    &atom, &atom_bytes) != LAPLACE_UNICODE_OK ||
                atom_bytes != frame.value.payload_bytes ||
                laplace_unicode_atom_persistence_project(
                    &atom.value, &state->configuration.expectation,
                    &entity, &physicality) != LAPLACE_UNICODE_OK) {
                fail_sink(state, "Unicode PostgreSQL sink rejected an atom projection");
            }
            entity_rows[row[0]] = laplace_pg_entity_record(
                &entity_binding, &entity);
            physicality_rows[row[0]] = laplace_pg_physicality_record(
                &physicality_binding, &physicality);
            fields[0] = PointerGetDatum(root_receipt);
            fields[1] = Int32GetDatum((int32)atom.value.codepoint_position);
            fields[2] = Int32GetDatum((int32)atom.value.placement_rank);
            fields[3] = Int16GetDatum((int16)atom.value.position_class);
            fields[4] = Int16GetDatum((int16)atom.value.lup_v1_length);
            fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atom.value.lup_v1_bytes, atom.value.lup_v1_length));
            fields[6] = PointerGetDatum(id_bytea(&atom.value.content_id));
            fields[7] = PointerGetDatum(digest_bytea(
                &atom.value.identity_preimage_fingerprint));
            fields[8] = PointerGetDatum(digest_bytea(
                &atom.value.physicality_id));
            fields[9] = Float8GetDatum(atom.value.coordinate.component[0]);
            fields[10] = Float8GetDatum(atom.value.coordinate.component[1]);
            fields[11] = Float8GetDatum(atom.value.coordinate.component[2]);
            fields[12] = Float8GetDatum(atom.value.coordinate.component[3]);
            fields[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                atom.value.hilbert_key, sizeof(atom.value.hilbert_key)));
            atom_rows[row[0]] = laplace_pg_composite_record(
                &atom_binding, fields, nulls);
            ++row[0];
        } else if (frame.value.kind ==
                   LAPLACE_UNICODE_ROOT_FRAME_DUCET_POSITION) {
            laplace_unicode_ducet_position_view position;
            Datum fields[6];
            bool nulls[6] = {false};
            size_t position_bytes = 0u;
            memset(&position, 0, sizeof(position));
            if (laplace_unicode_ducet_position_open(
                    frame.value.payload, frame.value.payload_bytes,
                    &position, &position_bytes) != LAPLACE_UNICODE_OK ||
                position_bytes != frame.value.payload_bytes) {
                fail_sink(state, "Unicode PostgreSQL sink rejected a DUCET position");
            }
            fields[0] = PointerGetDatum(root_receipt);
            fields[1] = Int32GetDatum((int32)position.codepoint_position);
            fields[2] = Int16GetDatum((int16)position.provenance);
            fields[3] = Int32GetDatum((int32)position.element_count);
            fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                position.encoded_elements,
                (size_t)position.element_count *
                    LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES));
            fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                position.equivalence_key, position.equivalence_key_bytes));
            ducet_position_rows[row[1]++] = laplace_pg_composite_record(
                &ducet_position_binding, fields, nulls);
        } else if (frame.value.kind ==
                   LAPLACE_UNICODE_ROOT_FRAME_DUCET_CONTRACTION) {
            laplace_unicode_ducet_contraction_view contraction;
            Datum fields[6];
            bool nulls[6] = {false};
            size_t contraction_bytes = 0u;
            memset(&contraction, 0, sizeof(contraction));
            if (laplace_unicode_ducet_contraction_open(
                    frame.value.payload, frame.value.payload_bytes,
                    &contraction, &contraction_bytes) != LAPLACE_UNICODE_OK ||
                contraction_bytes != frame.value.payload_bytes) {
                fail_sink(state, "Unicode PostgreSQL sink rejected a DUCET contraction");
            }
            fields[0] = PointerGetDatum(root_receipt);
            fields[1] = Int32GetDatum((int32)contraction.source_line_ordinal);
            fields[2] = Int32GetDatum((int32)contraction.sequence_count);
            fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                contraction.encoded_sequence,
                (size_t)contraction.sequence_count * sizeof(uint32_t)));
            fields[4] = Int32GetDatum((int32)contraction.element_count);
            fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                contraction.encoded_elements,
                (size_t)contraction.element_count *
                    LAPLACE_UNICODE_COLLATION_ELEMENT_BYTES));
            ducet_contraction_rows[row[2]++] = laplace_pg_composite_record(
                &ducet_contraction_binding, fields, nulls);
        } else if (frame.value.kind ==
                   LAPLACE_UNICODE_ROOT_FRAME_NORMALIZATION_COMPOSITION) {
            laplace_unicode_normalization_composition normalization;
            Datum fields[4];
            bool nulls[4] = {false};
            size_t normalization_bytes = 0u;
            memset(&normalization, 0, sizeof(normalization));
            if (laplace_unicode_normalization_composition_open(
                    frame.value.payload, frame.value.payload_bytes,
                    &normalization, &normalization_bytes) != LAPLACE_UNICODE_OK ||
                normalization_bytes != frame.value.payload_bytes) {
                fail_sink(state, "Unicode PostgreSQL sink rejected a normalization composition");
            }
            fields[0] = PointerGetDatum(root_receipt);
            fields[1] = Int32GetDatum((int32)normalization.starter_position);
            fields[2] = Int32GetDatum((int32)normalization.combining_position);
            fields[3] = Int32GetDatum((int32)normalization.composite_position);
            normalization_rows[row[3]++] = laplace_pg_composite_record(
                &normalization_binding, fields, nulls);
        } else {
            if (state->manifest_seen != 0u ||
                frame.value.payload_bytes != LAPLACE_UNICODE_ROOT_MANIFEST_BYTES) {
                fail_sink(state, "Unicode PostgreSQL sink observed an invalid manifest frame");
            }
            memcpy(state->canonical_manifest, frame.value.payload,
                   sizeof(state->canonical_manifest));
            state->manifest_seen = 1u;
        }
        offset += consumed;
    }
    if (row[0] != counts->family[0] || row[1] != counts->family[1] ||
        row[2] != counts->family[2] || row[3] != counts->family[3] ||
        offset != (size_t)batch->byte_count) {
        fail_sink(state, "Unicode PostgreSQL batch family counts changed during projection");
    }
    if (counts->family[0] != 0u) {
        arrays->entities = laplace_pg_composite_array(
            &entity_binding, entity_rows, row[0]);
        arrays->physicalities = laplace_pg_composite_array(
            &physicality_binding, physicality_rows, row[0]);
        arrays->atoms = laplace_pg_composite_array(
            &atom_binding, atom_rows, row[0]);
    }
    if (counts->family[1] != 0u) {
        arrays->ducet_positions = laplace_pg_composite_array(
            &ducet_position_binding, ducet_position_rows, row[1]);
    }
    if (counts->family[2] != 0u) {
        arrays->ducet_contractions = laplace_pg_composite_array(
            &ducet_contraction_binding, ducet_contraction_rows, row[2]);
    }
    if (counts->family[3] != 0u) {
        arrays->normalizations = laplace_pg_composite_array(
            &normalization_binding, normalization_rows, row[3]);
    }
    laplace_pg_composite_binding_close(&entity_binding);
    laplace_pg_composite_binding_close(&physicality_binding);
    laplace_pg_composite_binding_close(&atom_binding);
    laplace_pg_composite_binding_close(&ducet_position_binding);
    laplace_pg_composite_binding_close(&ducet_contraction_binding);
    laplace_pg_composite_binding_close(&normalization_binding);
}

static const char* atom_insert_sql(void) {
    return "INSERT INTO " LAPLACE_PG_SCHEMA ".unicode_atom_binding "
        "SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".unicode_atom_deposit_record[]) ON CONFLICT DO NOTHING";
}

static const char* atom_verify_sql(void) {
    return "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".unicode_atom_deposit_record[]) i JOIN " LAPLACE_PG_SCHEMA
        ".unicode_atom_binding s ON s.root_receipt=i.root_receipt "
        "AND s.codepoint_position=i.codepoint_position "
        "AND s.placement_rank=i.placement_rank "
        "AND s.position_class=i.position_class "
        "AND s.lup_v1_length=i.lup_v1_length "
        "AND s.lup_v1_bytes=i.lup_v1_bytes AND s.entity_id=i.entity_id "
        "AND s.identity_preimage_fingerprint=i.identity_preimage_fingerprint "
        "AND s.physicality_id=i.physicality_id "
        "AND float8send(s.coordinate_x)=float8send(i.coordinate_x) "
        "AND float8send(s.coordinate_y)=float8send(i.coordinate_y) "
        "AND float8send(s.coordinate_z)=float8send(i.coordinate_z) "
        "AND float8send(s.coordinate_m)=float8send(i.coordinate_m) "
        "AND s.hilbert_key=i.hilbert_key";
}

static const char* ducet_position_insert_sql(void) {
    return "INSERT INTO " LAPLACE_PG_SCHEMA ".unicode_ducet_position "
        "SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".unicode_ducet_position_deposit_record[]) ON CONFLICT DO NOTHING";
}

static const char* ducet_position_verify_sql(void) {
    return "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".unicode_ducet_position_deposit_record[]) i JOIN "
        LAPLACE_PG_SCHEMA ".unicode_ducet_position s "
        "ON s.root_receipt=i.root_receipt "
        "AND s.codepoint_position=i.codepoint_position "
        "AND s.provenance=i.provenance AND s.element_count=i.element_count "
        "AND s.encoded_elements=i.encoded_elements "
        "AND s.equivalence_key=i.equivalence_key";
}

static const char* ducet_contraction_insert_sql(void) {
    return "INSERT INTO " LAPLACE_PG_SCHEMA ".unicode_ducet_contraction "
        "SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".unicode_ducet_contraction_deposit_record[]) ON CONFLICT DO NOTHING";
}

static const char* ducet_contraction_verify_sql(void) {
    return "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".unicode_ducet_contraction_deposit_record[]) i JOIN "
        LAPLACE_PG_SCHEMA ".unicode_ducet_contraction s "
        "ON s.root_receipt=i.root_receipt "
        "AND s.source_line_ordinal=i.source_line_ordinal "
        "AND s.sequence_count=i.sequence_count "
        "AND s.encoded_sequence=i.encoded_sequence "
        "AND s.element_count=i.element_count "
        "AND s.encoded_elements=i.encoded_elements";
}

static const char* normalization_insert_sql(void) {
    return "INSERT INTO " LAPLACE_PG_SCHEMA
        ".unicode_normalization_composition "
        "SELECT * FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".unicode_normalization_deposit_record[]) ON CONFLICT DO NOTHING";
}

static const char* normalization_verify_sql(void) {
    return "SELECT count(*) FROM unnest($1::" LAPLACE_PG_SCHEMA
        ".unicode_normalization_deposit_record[]) i JOIN "
        LAPLACE_PG_SCHEMA ".unicode_normalization_composition s "
        "ON s.root_receipt=i.root_receipt "
        "AND s.starter_position=i.starter_position "
        "AND s.combining_position=i.combining_position "
        "AND s.composite_position=i.composite_position";
}

static void plan_manifest_fingerprint(laplace_digest256* fingerprint) {
    static const char domain[] = "laplace/unicode/postgresql/plan-manifest/v1";
    const char* statements[LAPLACE_PG_UNICODE_ROOT_PLAN_COUNT];
    blake3_hasher hasher;
    uint32 plan_id;
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_GENERATION_INSERT - 1u] =
        unicode_generation_insert_statement;
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_GENERATION_VERIFY - 1u] =
        unicode_generation_verify_statement;
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_ENTITY_INSERT - 1u] =
        laplace_pg_entity_insert_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_ENTITY_VERIFY - 1u] =
        laplace_pg_entity_verify_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_PHYSICALITY_INSERT - 1u] =
        laplace_pg_physicality_insert_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_PHYSICALITY_VERIFY - 1u] =
        laplace_pg_physicality_verify_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_ATOM_INSERT - 1u] = atom_insert_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_ATOM_VERIFY - 1u] = atom_verify_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_POSITION_INSERT - 1u] =
        ducet_position_insert_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_POSITION_VERIFY - 1u] =
        ducet_position_verify_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_CONTRACTION_INSERT - 1u] =
        ducet_contraction_insert_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_CONTRACTION_VERIFY - 1u] =
        ducet_contraction_verify_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_NORMALIZATION_INSERT - 1u] =
        normalization_insert_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_NORMALIZATION_VERIFY - 1u] =
        normalization_verify_sql();
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_EFFECT_VERIFY - 1u] =
        unicode_effect_verify_statement;
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_DEPOSIT_RECEIPT_INSERT - 1u] =
        unicode_deposit_insert_statement;
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_DEPOSIT_RECEIPT_VERIFY - 1u] =
        unicode_deposit_verify_statement;
#if defined(LAPLACE_TEST_UNICODE_PG_PLAN_SUBSTITUTION)
    statements[LAPLACE_PG_UNICODE_ROOT_PLAN_ATOM_VERIFY - 1u] = "SELECT 1";
#endif
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    hash_u32(&hasher, LAPLACE_PG_UNICODE_ROOT_PLAN_COUNT);
    for (plan_id = 1u; plan_id <= LAPLACE_PG_UNICODE_ROOT_PLAN_COUNT;
         ++plan_id) {
        const char* statement = statements[plan_id - 1u];
        const size_t bytes = statement == NULL ? 0u : strlen(statement);
        if (bytes == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Unicode PostgreSQL plan manifest is incomplete")));
        }
        hash_u32(&hasher, plan_id);
        hash_u64(&hasher, (uint64)bytes);
        blake3_hasher_update(&hasher, statement, bytes);
    }
    blake3_hasher_finalize(
        &hasher, fingerprint->bytes, sizeof(fingerprint->bytes));
}

#if defined(LAPLACE_POSTGRES_TESTING)
PG_FUNCTION_INFO_V1(laplace_test_unicode_postgresql_plan_manifest);

Datum laplace_test_unicode_postgresql_plan_manifest(PG_FUNCTION_ARGS) {
    laplace_digest256 fingerprint;
    (void)fcinfo;
    plan_manifest_fingerprint(&fingerprint);
    PG_RETURN_BYTEA_P(digest_bytea(&fingerprint));
}
#endif

static void execute_family(
    laplace_pg_unicode_sink* state,
    const char* type_name,
    ArrayType* records,
    uint64 count,
    SPIPlanPtr* insert_plan,
    SPIPlanPtr* verify_plan,
    const char* insert_statement,
    const char* verify_statement,
    uint32 insert_plan_id,
    uint32 verify_plan_id) {
    Oid types[1];
    Datum values[1];
    int result;
    uint64 inserted;
    uint64 verified;
    if (count == 0u) {
        return;
    }
    if (records == NULL) {
        fail_sink(state, "Unicode PostgreSQL set operation has no record array");
    }
    types[0] = laplace_pg_composite_array_oid(type_name);
    values[0] = PointerGetDatum(records);
    laplace_pg_keep_plan(insert_plan, insert_statement, 1, types);
    result = SPI_execute_plan(*insert_plan, values, NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed > count) {
        fail_sink(state, "Unicode PostgreSQL insert was not set-bounded");
    }
    inserted = SPI_processed;
    note_plan(state, insert_plan_id);
    laplace_pg_keep_plan(verify_plan, verify_statement, 1, types);
    result = SPI_execute_plan(*verify_plan, values, NULL, false, 1);
    if (result != SPI_OK_SELECT) {
        fail_sink(state, "Unicode PostgreSQL exact verification did not execute");
    }
    note_plan(state, verify_plan_id);
    verified = laplace_pg_scalar_count("Unicode root exact verification");
#if defined(LAPLACE_TEST_UNICODE_PG_BLIND_CONFLICT)
    (void)verified;
#else
    if (verified != count) {
        state->aborted = 1u;
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Unicode PostgreSQL %s records collide with different exact fields",
                        type_name),
                 errdetail("incoming=%llu inserted=%llu exact=%llu",
                           (unsigned long long)count,
                           (unsigned long long)inserted,
                           (unsigned long long)verified)));
    }
#endif
}

static void execute_batch_arrays(
    laplace_pg_unicode_sink* state,
    const unicode_batch_counts* counts,
    const unicode_batch_arrays* arrays) {
    if (counts->family[0] != 0u) {
        execute_family(
            state, "canonical_entity_record", arrays->entities,
            counts->family[0], &entity_insert_plan, &entity_verify_plan,
            laplace_pg_entity_insert_sql(), laplace_pg_entity_verify_sql(),
            LAPLACE_PG_UNICODE_ROOT_PLAN_ENTITY_INSERT,
            LAPLACE_PG_UNICODE_ROOT_PLAN_ENTITY_VERIFY);
        execute_family(
            state, "physicality_record", arrays->physicalities,
            counts->family[0], &physicality_insert_plan,
            &physicality_verify_plan, laplace_pg_physicality_insert_sql(),
            laplace_pg_physicality_verify_sql(),
            LAPLACE_PG_UNICODE_ROOT_PLAN_PHYSICALITY_INSERT,
            LAPLACE_PG_UNICODE_ROOT_PLAN_PHYSICALITY_VERIFY);
        execute_family(
            state, "unicode_atom_deposit_record", arrays->atoms,
            counts->family[0], &atom_insert_plan, &atom_verify_plan,
            atom_insert_sql(), atom_verify_sql(),
            LAPLACE_PG_UNICODE_ROOT_PLAN_ATOM_INSERT,
            LAPLACE_PG_UNICODE_ROOT_PLAN_ATOM_VERIFY);
        ++state->result.family_batch_counts[0];
    }
    if (counts->family[1] != 0u) {
        execute_family(
            state, "unicode_ducet_position_deposit_record",
            arrays->ducet_positions, counts->family[1],
            &ducet_position_insert_plan, &ducet_position_verify_plan,
            ducet_position_insert_sql(), ducet_position_verify_sql(),
            LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_POSITION_INSERT,
            LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_POSITION_VERIFY);
        ++state->result.family_batch_counts[1];
    }
    if (counts->family[2] != 0u) {
        execute_family(
            state, "unicode_ducet_contraction_deposit_record",
            arrays->ducet_contractions, counts->family[2],
            &ducet_contraction_insert_plan, &ducet_contraction_verify_plan,
            ducet_contraction_insert_sql(), ducet_contraction_verify_sql(),
            LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_CONTRACTION_INSERT,
            LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_CONTRACTION_VERIFY);
        ++state->result.family_batch_counts[2];
    }
    if (counts->family[3] != 0u) {
        execute_family(
            state, "unicode_normalization_deposit_record",
            arrays->normalizations, counts->family[3],
            &normalization_insert_plan, &normalization_verify_plan,
            normalization_insert_sql(), normalization_verify_sql(),
            LAPLACE_PG_UNICODE_ROOT_PLAN_NORMALIZATION_INSERT,
            LAPLACE_PG_UNICODE_ROOT_PLAN_NORMALIZATION_VERIFY);
        ++state->result.family_batch_counts[3];
    }
}

static void verify_persisted_effects(
    laplace_pg_unicode_sink* state,
    const laplace_unicode_root_stream_summary* summary) {
    Oid types[1] = {BYTEAOID};
    Datum values[1] = {
        PointerGetDatum(digest_bytea(&summary->receipt_id))};
    uint64 expected[6] = {
        summary->section_counts[0], summary->section_counts[0],
        summary->section_counts[0], summary->section_counts[1],
        summary->section_counts[2], summary->section_counts[3]};
    int result;
    size_t index;
    laplace_pg_keep_plan(
        &effect_verify_plan, unicode_effect_verify_statement, 1, types);
    result = SPI_execute_plan(effect_verify_plan, values, NULL, false, 1);
    if (result != SPI_OK_SELECT || SPI_processed != 1u) {
        fail_sink(state, "Unicode PostgreSQL durable effect verification failed");
    }
    for (index = 0u; index < 6u; ++index) {
        bool is_null = false;
        const Datum value = SPI_getbinval(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc,
            (int)index + 1, &is_null);
        if (is_null || DatumGetInt64(value) < 0) {
            fail_sink(state, "Unicode PostgreSQL effect cardinality is invalid");
        }
        state->result.persisted_counts[index] = (uint64)DatumGetInt64(value);
        if (state->result.persisted_counts[index] != expected[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Unicode PostgreSQL durable effects are incomplete"),
                     errdetail("family=%zu expected=%llu actual=%llu",
                               index,
                               (unsigned long long)expected[index],
                               (unsigned long long)
                                   state->result.persisted_counts[index])));
        }
    }
    note_plan(state, LAPLACE_PG_UNICODE_ROOT_PLAN_EFFECT_VERIFY);
}

static void postgresql_artifact_fingerprint(
    const laplace_pg_unicode_sink* state,
    const laplace_digest256* stream_fingerprint,
    const laplace_unicode_root_stream_summary* summary,
    const laplace_digest256* contract,
    laplace_digest256* fingerprint) {
    static const char domain[] = "laplace/unicode/postgresql/artifact/v2";
    blake3_hasher hasher;
    size_t index;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, domain, sizeof(domain) - 1u);
    blake3_hasher_update(
        &hasher, summary->receipt_id.bytes, sizeof(summary->receipt_id.bytes));
    blake3_hasher_update(
        &hasher, stream_fingerprint->bytes, sizeof(stream_fingerprint->bytes));
    blake3_hasher_update(&hasher, contract->bytes, sizeof(contract->bytes));
    blake3_hasher_update(
        &hasher, state->result.plan_manifest_fingerprint.bytes,
        sizeof(state->result.plan_manifest_fingerprint.bytes));
    blake3_hasher_update(
        &hasher, state->result.plan_sequence_fingerprint.bytes,
        sizeof(state->result.plan_sequence_fingerprint.bytes));
    for (index = 0u; index < 4u; ++index) {
        hash_u64(&hasher, summary->section_counts[index]);
        blake3_hasher_update(
            &hasher, summary->section_fingerprints[index].bytes,
            sizeof(summary->section_fingerprints[index].bytes));
    }
    for (index = 0u; index < 6u; ++index) {
        hash_u64(&hasher, state->result.persisted_counts[index]);
    }
    blake3_hasher_finalize(
        &hasher, fingerprint->bytes, sizeof(fingerprint->bytes));
}

static void persist_generation(
    laplace_pg_unicode_sink* state,
    const laplace_digest256* stream_fingerprint,
    const laplace_unicode_root_stream_summary* summary,
    laplace_digest256* artifact_fingerprint) {
    Oid types[28] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, INT4OID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, INT8OID, INT8OID, INT8OID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID};
    Datum values[28];
    laplace_digest256 contract;
    int result;
    contract_fingerprint(&contract);
    postgresql_artifact_fingerprint(
        state, stream_fingerprint, summary, &contract, artifact_fingerprint);
    values[0] = PointerGetDatum(digest_bytea(&summary->receipt_id));
    values[1] = PointerGetDatum(digest_bytea(&summary->manifest.source_fingerprint));
    values[2] = PointerGetDatum(digest_bytea(&summary->manifest.recipe_fingerprint));
    values[3] = PointerGetDatum(digest_bytea(&summary->manifest.numeric_provider_receipt));
    values[4] = PointerGetDatum(digest_bytea(&summary->manifest.stream_contract_fingerprint));
    values[5] = PointerGetDatum(digest_bytea(&summary->manifest.algorithmic_hangul_rule_fingerprint));
    values[6] = PointerGetDatum(digest_bytea(&summary->manifest.atom_record_contract_fingerprint));
    values[7] = PointerGetDatum(digest_bytea(&summary->manifest.physicality_recipe_fingerprint));
    values[8] = PointerGetDatum(digest_bytea(&summary->manifest.placement_rank_permutation_fingerprint));
    values[9] = PointerGetDatum(digest_bytea(&summary->manifest.coordinate_table_fingerprint));
    values[10] = PointerGetDatum(digest_bytea(&summary->manifest.geometry_epoch));
    values[11] = Int32GetDatum((int32)summary->manifest.physicality_recipe_version);
    values[12] = PointerGetDatum(digest_bytea(stream_fingerprint));
    values[13] = PointerGetDatum(digest_bytea(&summary->section_fingerprints[0]));
    values[14] = PointerGetDatum(digest_bytea(&summary->section_fingerprints[1]));
    values[15] = PointerGetDatum(digest_bytea(&summary->section_fingerprints[2]));
    values[16] = PointerGetDatum(digest_bytea(&summary->section_fingerprints[3]));
    values[17] = Int64GetDatum((int64)summary->section_counts[0]);
    values[18] = Int64GetDatum((int64)summary->section_counts[1]);
    values[19] = Int64GetDatum((int64)summary->section_counts[2]);
    values[20] = Int64GetDatum((int64)summary->section_counts[3]);
    values[21] = Int64GetDatum((int64)summary->total_frame_count);
    values[22] = Int64GetDatum((int64)summary->total_encoded_bytes);
    values[23] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        state->canonical_manifest, sizeof(state->canonical_manifest)));
    values[24] = PointerGetDatum(digest_bytea(&contract));
    values[25] = PointerGetDatum(digest_bytea(
        &state->result.plan_manifest_fingerprint));
    values[26] = PointerGetDatum(digest_bytea(
        &state->result.plan_sequence_fingerprint));
    values[27] = PointerGetDatum(digest_bytea(artifact_fingerprint));
    laplace_pg_keep_plan(
        &generation_insert_plan, unicode_generation_insert_statement, 28, types);
    result = SPI_execute_plan(generation_insert_plan, values, NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed > 1u) {
        fail_sink(state, "Unicode root generation insert was not bounded");
    }
    note_plan(state, LAPLACE_PG_UNICODE_ROOT_PLAN_GENERATION_INSERT);
    laplace_pg_keep_plan(
        &generation_verify_plan, unicode_generation_verify_statement, 28, types);
    result = SPI_execute_plan(generation_verify_plan, values, NULL, false, 1);
    if (result != SPI_OK_SELECT ||
        laplace_pg_scalar_count("Unicode root generation verification") != 1u) {
        fail_sink(state, "Unicode root generation collides with different exact fields");
    }
    note_plan(state, LAPLACE_PG_UNICODE_ROOT_PLAN_GENERATION_VERIFY);
}

static bool summary_matches_expected(
    const laplace_unicode_root_stream_summary* actual,
    const laplace_unicode_root_stream_summary* expected) {
    size_t index;
    if (!digest_equal(&actual->receipt_id, &expected->receipt_id) ||
        actual->total_frame_count != expected->total_frame_count ||
        actual->total_encoded_bytes != expected->total_encoded_bytes ||
        actual->status != LAPLACE_UNICODE_OK) {
        return false;
    }
    for (index = 0u; index < 4u; ++index) {
        if (actual->section_counts[index] != expected->section_counts[index] ||
            !digest_equal(
                &actual->section_fingerprints[index],
                &expected->section_fingerprints[index])) {
            return false;
        }
    }
    return true;
}

static bool expectation_matches_manifest(
    const laplace_unicode_root_stream_expectation* expectation,
    const laplace_unicode_root_manifest* manifest) {
    return digest_equal(
               &expectation->source_fingerprint,
               &manifest->source_fingerprint) &&
        digest_equal(
               &expectation->recipe_fingerprint,
               &manifest->recipe_fingerprint) &&
        digest_equal(
               &expectation->numeric_provider_receipt,
               &manifest->numeric_provider_receipt) &&
        digest_equal(
               &expectation->stream_contract_fingerprint,
               &manifest->stream_contract_fingerprint) &&
        digest_equal(
               &expectation->algorithmic_hangul_rule_fingerprint,
               &manifest->algorithmic_hangul_rule_fingerprint) &&
        digest_equal(
               &expectation->atom_record_contract_fingerprint,
               &manifest->atom_record_contract_fingerprint) &&
        digest_equal(
               &expectation->physicality_recipe_fingerprint,
               &manifest->physicality_recipe_fingerprint) &&
        digest_equal(
               &expectation->placement_rank_permutation_fingerprint,
               &manifest->placement_rank_permutation_fingerprint) &&
        digest_equal(
               &expectation->coordinate_table_fingerprint,
               &manifest->coordinate_table_fingerprint) &&
        digest_equal(
               &expectation->geometry_epoch,
               &manifest->geometry_epoch) &&
        expectation->physicality_recipe_version ==
            manifest->physicality_recipe_version;
}

static laplace_framework_status unicode_sink_begin(
    void* opaque,
    const laplace_framework_context* context,
    uint32 record_type,
    uint64 total_records,
    uint64 total_bytes) {
    laplace_pg_unicode_sink* state = (laplace_pg_unicode_sink*)opaque;
    if (state == NULL || context == NULL || state->begun != 0u ||
        state->result.sealed != 0u || state->aborted != 0u ||
        record_type != LAPLACE_PG_UNICODE_ROOT_RECORD_TYPE ||
        total_records !=
            state->configuration.expected_summary.total_frame_count ||
        total_bytes !=
            state->configuration.expected_summary.total_encoded_bytes ||
        context->resource_grant.memory_bytes == 0u) {
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    if (laplace_unicode_root_stream_validator_create(
            &state->configuration.expectation,
            &state->validator) != LAPLACE_UNICODE_OK) {
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        laplace_unicode_root_stream_validator_destroy(state->validator);
        state->validator = NULL;
        return LAPLACE_FRAMEWORK_SINK_BEGIN_FAILED;
    }
    state->spi_connected = 1u;
    state->batch_context = AllocSetContextCreate(
        CurTransactionContext, "Laplace Unicode PostgreSQL batch",
        ALLOCSET_DEFAULT_SIZES);
    state->expected_frames = total_records;
    state->expected_bytes = total_bytes;
    state->batch_memory_limit = context->resource_grant.memory_bytes;
    state->begun = 1u;
    return LAPLACE_FRAMEWORK_OK;
}

static laplace_framework_status unicode_sink_stage(
    void* opaque,
    const laplace_framework_canonical_batch* batch) {
    laplace_pg_unicode_sink* state = (laplace_pg_unicode_sink*)opaque;
    unicode_batch_counts counts;
    unicode_batch_arrays arrays;
    MemoryContext previous;
    uint64 record_overhead;
    uint64 stream_memory;
    if (state == NULL || batch == NULL || state->begun == 0u ||
        state->aborted != 0u || state->result.sealed != 0u ||
        batch->record_type != LAPLACE_PG_UNICODE_ROOT_RECORD_TYPE ||
        batch->canonical_bytes == NULL || batch->byte_count == 0u ||
        batch->record_count == 0u || batch->byte_count > SIZE_MAX ||
        batch->first_ordinal != state->staged_frames ||
        state->staged_frames > UINT64_MAX - batch->record_count ||
        state->staged_bytes > UINT64_MAX - batch->byte_count ||
        batch->record_count > UINT64_MAX /
            LAPLACE_PG_UNICODE_ROOT_PER_FRAME_OVERHEAD_BYTES ||
        batch->byte_count > UINT64_MAX /
            LAPLACE_PG_UNICODE_ROOT_STREAM_BYTE_MULTIPLIER) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    record_overhead = batch->record_count *
        LAPLACE_PG_UNICODE_ROOT_PER_FRAME_OVERHEAD_BYTES;
    stream_memory = batch->byte_count *
        LAPLACE_PG_UNICODE_ROOT_STREAM_BYTE_MULTIPLIER;
    if (record_overhead > state->batch_memory_limit ||
        stream_memory > state->batch_memory_limit - record_overhead) {
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    if (laplace_unicode_root_stream_validator_consume(
            state->validator, batch->canonical_bytes,
            (size_t)batch->byte_count, batch->record_count,
            batch->first_ordinal) != LAPLACE_UNICODE_OK ||
        !frame_counts(batch, &counts)) {
        state->aborted = 1u;
        return LAPLACE_FRAMEWORK_SINK_STAGE_FAILED;
    }
    previous = MemoryContextSwitchTo(state->batch_context);
    PG_TRY();
    {
        build_batch_arrays(state, batch, &counts, &arrays);
        execute_batch_arrays(state, &counts, &arrays);
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(previous);
        MemoryContextReset(state->batch_context);
        state->aborted = 1u;
        PG_RE_THROW();
    }
    PG_END_TRY();
    MemoryContextSwitchTo(previous);
    MemoryContextReset(state->batch_context);
    state->staged_frames += batch->record_count;
    state->staged_bytes += batch->byte_count;
    ++state->result.batch_count;
    return LAPLACE_FRAMEWORK_OK;
}

static bool plan_counts_are_complete(const laplace_pg_unicode_sink* state) {
    const uint64 atom_batches = state->result.family_batch_counts[0];
    return state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_ENTITY_INSERT - 1u] ==
               atom_batches &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_ENTITY_VERIFY - 1u] ==
               atom_batches &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_PHYSICALITY_INSERT - 1u] ==
               atom_batches &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_PHYSICALITY_VERIFY - 1u] ==
               atom_batches &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_ATOM_INSERT - 1u] ==
               atom_batches &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_ATOM_VERIFY - 1u] ==
               atom_batches &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_POSITION_INSERT - 1u] ==
               state->result.family_batch_counts[1] &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_POSITION_VERIFY - 1u] ==
               state->result.family_batch_counts[1] &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_CONTRACTION_INSERT - 1u] ==
               state->result.family_batch_counts[2] &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_DUCET_CONTRACTION_VERIFY - 1u] ==
               state->result.family_batch_counts[2] &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_NORMALIZATION_INSERT - 1u] ==
               state->result.family_batch_counts[3] &&
        state->plan_invocations[
               LAPLACE_PG_UNICODE_ROOT_PLAN_NORMALIZATION_VERIFY - 1u] ==
               state->result.family_batch_counts[3];
}

static laplace_framework_status unicode_sink_seal(
    void* opaque,
    const laplace_digest256* stream_fingerprint,
    laplace_digest256* artifact_fingerprint) {
    laplace_pg_unicode_sink* state = (laplace_pg_unicode_sink*)opaque;
    laplace_unicode_root_stream_summary summary;
    if (state == NULL || stream_fingerprint == NULL ||
        artifact_fingerprint == NULL || state->begun == 0u ||
        state->aborted != 0u || state->manifest_seen == 0u ||
        state->staged_frames != state->expected_frames ||
        state->staged_bytes != state->expected_bytes ||
        !plan_counts_are_complete(state)) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    memset(&summary, 0, sizeof(summary));
    if (laplace_unicode_root_stream_validator_finish(
            state->validator, &summary) != LAPLACE_UNICODE_OK ||
        !summary_matches_expected(
            &summary, &state->configuration.expected_summary) ||
        summary.section_counts[0] != LAPLACE_UNICODE_ROOT_POPULATION ||
        summary.section_counts[1] != LAPLACE_UNICODE_ROOT_POPULATION ||
        summary.total_frame_count > INT64_MAX ||
        summary.total_encoded_bytes > INT64_MAX ||
        summary.section_counts[2] > INT64_MAX ||
        summary.section_counts[3] > INT64_MAX) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    verify_persisted_effects(state, &summary);
    predict_two_plans(
        state, LAPLACE_PG_UNICODE_ROOT_PLAN_GENERATION_INSERT,
        LAPLACE_PG_UNICODE_ROOT_PLAN_GENERATION_VERIFY,
        &state->result.plan_sequence_fingerprint);
    persist_generation(
        state, stream_fingerprint, &summary, artifact_fingerprint);
    if (state->plan_invocations[
            LAPLACE_PG_UNICODE_ROOT_PLAN_GENERATION_INSERT - 1u] != 1u ||
        state->plan_invocations[
            LAPLACE_PG_UNICODE_ROOT_PLAN_GENERATION_VERIFY - 1u] != 1u) {
        return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
    }
    {
        laplace_digest256 actual_sequence;
        finish_plan_sequence(state, &actual_sequence);
        if (!digest_equal(
                &actual_sequence, &state->result.plan_sequence_fingerprint)) {
            return LAPLACE_FRAMEWORK_SINK_SEAL_FAILED;
        }
    }
    state->result.root_summary = summary;
    state->result.artifact_fingerprint = *artifact_fingerprint;
    state->result.sealed = 1u;
    state->begun = 0u;
    laplace_unicode_root_stream_validator_destroy(state->validator);
    state->validator = NULL;
    MemoryContextDelete(state->batch_context);
    state->batch_context = NULL;
    if (SPI_finish() != SPI_OK_FINISH) {
        state->spi_connected = 0u;
        fail_sink(state, "Unicode PostgreSQL sink could not finish SPI");
    }
    state->spi_connected = 0u;
    return LAPLACE_FRAMEWORK_OK;
}

static void unicode_sink_abort(void* opaque) {
    laplace_pg_unicode_sink* state = (laplace_pg_unicode_sink*)opaque;
    if (state == NULL) {
        return;
    }
    if (state->validator != NULL) {
        laplace_unicode_root_stream_validator_destroy(state->validator);
        state->validator = NULL;
    }
    if (state->batch_context != NULL) {
        MemoryContextDelete(state->batch_context);
        state->batch_context = NULL;
    }
    if (state->spi_connected != 0u) {
        (void)SPI_finish();
        state->spi_connected = 0u;
    }
    state->begun = 0u;
    state->aborted = 1u;
}

void laplace_pg_unicode_sink_create(
    const laplace_pg_unicode_sink_configuration* configuration,
    laplace_pg_unicode_sink** output_state,
    laplace_framework_sink_v1* sink) {
    laplace_pg_unicode_sink* state;
    if (configuration == NULL || output_state == NULL || sink == NULL ||
        *output_state != NULL ||
        configuration->expected_summary.status != LAPLACE_UNICODE_OK ||
        configuration->expected_summary.section_counts[0] !=
            LAPLACE_UNICODE_ROOT_POPULATION ||
        configuration->expected_summary.section_counts[1] !=
            LAPLACE_UNICODE_ROOT_POPULATION ||
        !expectation_matches_manifest(
            &configuration->expectation,
            &configuration->expected_summary.manifest)) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Unicode PostgreSQL sink configuration is invalid")));
    }
    state = (laplace_pg_unicode_sink*)palloc0(sizeof(*state));
    state->configuration = *configuration;
    plan_manifest_fingerprint(&state->result.plan_manifest_fingerprint);
    blake3_hasher_init(&state->plan_sequence_hasher);
    blake3_hasher_update(
        &state->plan_sequence_hasher,
        "laplace/unicode/postgresql/plan-sequence/v1",
        sizeof("laplace/unicode/postgresql/plan-sequence/v1") - 1u);
    blake3_hasher_update(
        &state->plan_sequence_hasher,
        state->result.plan_manifest_fingerprint.bytes,
        sizeof(state->result.plan_manifest_fingerprint.bytes));
    memset(sink, 0, sizeof(*sink));
    sink->state = state;
    sink->begin = unicode_sink_begin;
    sink->stage = unicode_sink_stage;
    sink->seal = unicode_sink_seal;
    sink->abort = unicode_sink_abort;
    sink->abi_major = LAPLACE_FRAMEWORK_SINK_ABI_MAJOR;
    sink->abi_minor = LAPLACE_FRAMEWORK_SINK_ABI_MINOR;
    *output_state = state;
}

void laplace_pg_unicode_sink_result_get(
    const laplace_pg_unicode_sink* state,
    laplace_pg_unicode_sink_result* result) {
    if (state == NULL || result == NULL || state->result.sealed == 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Unicode PostgreSQL sink has no sealed result")));
    }
    *result = state->result;
}

void laplace_pg_unicode_sink_destroy(
    laplace_pg_unicode_sink** state) {
    if (state == NULL || *state == NULL) {
        return;
    }
    if ((*state)->begun != 0u || (*state)->validator != NULL ||
        (*state)->batch_context != NULL || (*state)->spi_connected != 0u) {
        unicode_sink_abort(*state);
    }
    pfree(*state);
    *state = NULL;
}

_Static_assert(
    LAPLACE_PG_UNICODE_ROOT_RECORD_TYPE ==
        LAPLACE_UNICODE_ROOT_STREAM_RECORD_TYPE,
    "Unicode PostgreSQL and native stream record types differ");

static void persist_deposit_receipt(
    laplace_pg_unicode_sink* sink_state,
    const laplace_framework_producer_receipt* producer,
    const laplace_digest256 sink_artifacts[2],
    const laplace_unicode_tier0_sink_result* tier0,
    const laplace_pg_perfcache_admission_result* admission,
    laplace_digest256* receipt_id) {
    Oid types[24] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, INT8OID, INT8OID, BYTEAOID,
        BYTEAOID, INT8OID, INT8OID, INT8OID,
        INT8OID, INT8OID, INT8OID, INT8OID};
    Datum values[24];
    uint64 final_plan_count;
    laplace_digest256 predicted_sequence;
    blake3_hasher receipt_hasher;
    int result;
    size_t index;
    if (sink_state->result.plan_count > UINT64_MAX - 2u) {
        fail_sink(sink_state, "Unicode root deposit receipt identity failed");
    }
    final_plan_count = sink_state->result.plan_count + 2u;
    predict_two_plans(
        sink_state, LAPLACE_PG_UNICODE_ROOT_PLAN_DEPOSIT_RECEIPT_INSERT,
        LAPLACE_PG_UNICODE_ROOT_PLAN_DEPOSIT_RECEIPT_VERIFY,
        &predicted_sequence);
    blake3_hasher_init(&receipt_hasher);
    blake3_hasher_update(
        &receipt_hasher, "laplace/unicode/postgresql/deposit-receipt/v2",
        sizeof("laplace/unicode/postgresql/deposit-receipt/v2") - 1u);
    blake3_hasher_update(&receipt_hasher,
        sink_state->result.root_summary.receipt_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, producer->receipt_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, producer->stream.receipt_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher,
        producer->stream.sink_artifacts_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, sink_artifacts[0].bytes, 32u);
    blake3_hasher_update(&receipt_hasher, sink_artifacts[1].bytes, 32u);
    blake3_hasher_update(&receipt_hasher,
        admission->manifest_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher,
        admission->admission_receipt_id.bytes, 32u);
    blake3_hasher_update(&receipt_hasher,
        sink_state->result.plan_manifest_fingerprint.bytes, 32u);
    blake3_hasher_update(&receipt_hasher, predicted_sequence.bytes, 32u);
    hash_u64(&receipt_hasher, final_plan_count);
    for (index = 0u; index < 6u; ++index) {
        hash_u64(&receipt_hasher, sink_state->result.persisted_counts[index]);
    }
    blake3_hasher_finalize(
        &receipt_hasher, receipt_id->bytes, sizeof(receipt_id->bytes));
    values[0] = PointerGetDatum(digest_bytea(receipt_id));
    values[1] = PointerGetDatum(digest_bytea(
        &sink_state->result.root_summary.receipt_id));
    values[2] = PointerGetDatum(digest_bytea(&producer->receipt_id));
    values[3] = PointerGetDatum(digest_bytea(&producer->stream.receipt_id));
    values[4] = PointerGetDatum(digest_bytea(
        &producer->stream.sink_artifacts_fingerprint));
    values[5] = PointerGetDatum(digest_bytea(&sink_artifacts[0]));
    values[6] = PointerGetDatum(digest_bytea(&sink_artifacts[1]));
    values[7] = PointerGetDatum(digest_bytea(
        &admission->manifest_fingerprint));
    values[8] = PointerGetDatum(digest_bytea(
        &admission->encoded_manifest_fingerprint));
    values[9] = PointerGetDatum(id_bytea(
        &admission->next_epoch.activation_epoch_id));
    values[10] = PointerGetDatum(digest_bytea(
        &admission->next_epoch.epoch_fingerprint));
    values[11] = PointerGetDatum(digest_bytea(
        &admission->admission_receipt_id));
    values[12] = Int64GetDatum(laplace_pg_checked_int64(
        producer->stream.total_records, "Unicode root frame count"));
    values[13] = Int64GetDatum(laplace_pg_checked_int64(
        producer->stream.total_bytes, "Unicode root encoded bytes"));
    values[14] = Int64GetDatum(laplace_pg_checked_int64(
        producer->stream.batch_count, "Unicode root batch count"));
    values[15] = PointerGetDatum(digest_bytea(
        &sink_state->result.plan_manifest_fingerprint));
    values[16] = PointerGetDatum(digest_bytea(&predicted_sequence));
    values[17] = Int64GetDatum(laplace_pg_checked_int64(
        final_plan_count, "Unicode PostgreSQL plan count"));
    for (index = 0u; index < 6u; ++index) {
        values[18u + index] = Int64GetDatum(laplace_pg_checked_int64(
            sink_state->result.persisted_counts[index],
            "Unicode PostgreSQL persisted cardinality"));
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        fail_sink(sink_state, "Unicode root deposit receipt could not connect to SPI");
    }
    laplace_pg_keep_plan(
        &deposit_receipt_insert_plan, unicode_deposit_insert_statement,
        24, types);
    result = SPI_execute_plan(
        deposit_receipt_insert_plan, values, NULL, false, 0);
    if (result != SPI_OK_INSERT || SPI_processed > 1u) {
        fail_sink(sink_state, "Unicode root deposit receipt insert was not bounded");
    }
    note_plan(sink_state, LAPLACE_PG_UNICODE_ROOT_PLAN_DEPOSIT_RECEIPT_INSERT);
    laplace_pg_keep_plan(
        &deposit_receipt_verify_plan, unicode_deposit_verify_statement,
        24, types);
    result = SPI_execute_plan(
        deposit_receipt_verify_plan, values, NULL, false, 1);
    if (result != SPI_OK_SELECT ||
        laplace_pg_scalar_count("Unicode root deposit receipt verification") != 1u) {
        fail_sink(sink_state, "Unicode root deposit receipt collides with different fields");
    }
    note_plan(sink_state, LAPLACE_PG_UNICODE_ROOT_PLAN_DEPOSIT_RECEIPT_VERIFY);
    finish_plan_sequence(
        sink_state, &sink_state->result.plan_sequence_fingerprint);
    if (!digest_equal(
            &predicted_sequence,
            &sink_state->result.plan_sequence_fingerprint)) {
        fail_sink(sink_state, "Unicode root deposit plan sequence diverged");
    }
    if (SPI_finish() != SPI_OK_FINISH) {
        fail_sink(sink_state, "Unicode root deposit receipt could not finish SPI");
    }
    if (!digest_equal(
            &tier0->artifact_set_fingerprint, &sink_artifacts[1]) ||
        !digest_equal(
            &tier0->root_summary.receipt_id,
            &sink_state->result.root_summary.receipt_id)) {
        fail_sink(sink_state, "Unicode root sibling sink evidence diverged");
    }
}

static void read_exact_bytes(
    Datum value,
    void* output,
    size_t expected_bytes,
    const char* field_name) {
    bytea* bytes = DatumGetByteaPP(value);
    if ((size_t)VARSIZE_ANY_EXHDR(bytes) != expected_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("%s must contain exactly %zu bytes",
                        field_name, expected_bytes)));
    }
    memcpy(output, VARDATA_ANY(bytes), expected_bytes);
}

static void expectation_from_build(
    const laplace_unicode_root_build_summary* build,
    laplace_unicode_root_stream_expectation* expectation) {
    const laplace_unicode_root_manifest* manifest = &build->stream.manifest;
    memset(expectation, 0, sizeof(*expectation));
    expectation->source_fingerprint = manifest->source_fingerprint;
    expectation->recipe_fingerprint = manifest->recipe_fingerprint;
    expectation->numeric_provider_receipt = manifest->numeric_provider_receipt;
    expectation->stream_contract_fingerprint =
        manifest->stream_contract_fingerprint;
    expectation->algorithmic_hangul_rule_fingerprint =
        manifest->algorithmic_hangul_rule_fingerprint;
    expectation->atom_record_contract_fingerprint =
        manifest->atom_record_contract_fingerprint;
    expectation->physicality_recipe_fingerprint =
        manifest->physicality_recipe_fingerprint;
    expectation->placement_rank_permutation_fingerprint =
        manifest->placement_rank_permutation_fingerprint;
    expectation->coordinate_table_fingerprint =
        manifest->coordinate_table_fingerprint;
    expectation->geometry_epoch = manifest->geometry_epoch;
    expectation->physicality_recipe_version =
        manifest->physicality_recipe_version;
}

static int never_cancel(void* state) {
    (void)state;
    return 0;
}

static void ignore_progress(
    void* state,
    const laplace_framework_replay_checkpoint* checkpoint) {
    (void)state;
    (void)checkpoint;
}

static laplace_framework_producer_control_v1 producer_control(void) {
    laplace_framework_producer_control_v1 control;
    memset(&control, 0, sizeof(control));
    control.cancel_requested = never_cancel;
    control.observe_progress = ignore_progress;
    control.abi_major = LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MAJOR;
    control.abi_minor = LAPLACE_FRAMEWORK_PRODUCER_CONTROL_ABI_MINOR;
    return control;
}

Datum LAPLACE_PG_UNICODE_ROOT_SYMBOL(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    char* source_root = text_to_cstring(PG_GETARG_TEXT_PP(1));
    char* spool_directory = text_to_cstring(PG_GETARG_TEXT_PP(2));
    char* tier0_path = text_to_cstring(PG_GETARG_TEXT_PP(3));
    laplace_id128 activation_epoch_id;
    laplace_digest256 activation_epoch_fingerprint;
    int64 expected_sequence_input = PG_GETARG_INT64(6);
    bool expected_present = PG_GETARG_BOOL(7);
    laplace_pg_perfcache_epoch expected_epoch;
    int64 maximum_batch_bytes_input = PG_GETARG_INT64(10);
    int32 maximum_batch_frames_input = PG_GETARG_INT32(11);
    laplace_unicode_numeric_provider_v1 numeric_provider;
    laplace_unicode_root_build_request build_request;
    laplace_unicode_root_build_summary build;
    laplace_canonical_spool* volatile spool = NULL;
    laplace_pg_unicode_sink* volatile pg_sink_state = NULL;
    laplace_unicode_tier0_sink* volatile tier0_sink_state = NULL;
    laplace_pg_unicode_sink_configuration pg_configuration;
    laplace_unicode_tier0_sink_configuration tier0_configuration;
    laplace_framework_sink_v1 sinks[2];
    laplace_framework_producer_v1 producer;
    laplace_framework_producer_control_v1 control;
    laplace_digest256 sink_artifacts[2];
    laplace_framework_sink_artifact_output artifact_output;
    laplace_framework_producer_receipt producer_receipt;
    laplace_pg_unicode_sink_result pg_result;
    laplace_unicode_tier0_sink_result tier0_result;
    laplace_perfcache_module_v2 tier0_module;
    laplace_perfcache_generation_artifact artifact;
    laplace_perfcache_generation_request generation;
    size_t manifest_bytes = 0u;
    size_t manifest_written = 0u;
    uint8_t* manifest = NULL;
    laplace_digest256 manifest_encoded_fingerprint;
    laplace_pg_perfcache_admission_result admission;
    laplace_digest256 deposit_receipt;
    Datum result_values[25];
    bool result_nulls[25] = {false};
    HeapTuple result_tuple;
    uint64 memory_estimate;
    laplace_unicode_root_build_status build_status;
    laplace_perfcache_status tier0_status;
    laplace_framework_status framework_status;
    laplace_perfcache_registry_status registry_status;
    laplace_pg_perfcache_status pg_perfcache_status;

    memset(&context, 0, sizeof(context));
    memset(&activation_epoch_id, 0, sizeof(activation_epoch_id));
    memset(&activation_epoch_fingerprint, 0,
           sizeof(activation_epoch_fingerprint));
    memset(&expected_epoch, 0, sizeof(expected_epoch));
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    read_exact_bytes(
        PG_GETARG_DATUM(4), &activation_epoch_id,
        sizeof(activation_epoch_id), "activation epoch identifier");
    read_exact_bytes(
        PG_GETARG_DATUM(5), &activation_epoch_fingerprint,
        sizeof(activation_epoch_fingerprint), "activation epoch fingerprint");
    read_exact_bytes(
        PG_GETARG_DATUM(8), &expected_epoch.activation_epoch_id,
        sizeof(expected_epoch.activation_epoch_id),
        "expected activation epoch identifier");
    read_exact_bytes(
        PG_GETARG_DATUM(9), &expected_epoch.epoch_fingerprint,
        sizeof(expected_epoch.epoch_fingerprint),
        "expected activation epoch fingerprint");
    if (source_root[0] == '\0' || spool_directory[0] == '\0' ||
        tier0_path[0] == '\0' || expected_sequence_input < 0 ||
        maximum_batch_bytes_input <= 0 || maximum_batch_frames_input <= 0 ||
        (uint64)maximum_batch_bytes_input > UINT64_MAX /
            LAPLACE_PG_UNICODE_ROOT_STREAM_BYTE_MULTIPLIER ||
        (uint64)maximum_batch_frames_input > UINT64_MAX /
            LAPLACE_PG_UNICODE_ROOT_PER_FRAME_OVERHEAD_BYTES) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Unicode root build arguments are invalid")));
    }
    memory_estimate = (uint64)maximum_batch_bytes_input *
        LAPLACE_PG_UNICODE_ROOT_STREAM_BYTE_MULTIPLIER;
    if (memory_estimate > context.resource_grant.memory_bytes ||
        (uint64)maximum_batch_frames_input *
            LAPLACE_PG_UNICODE_ROOT_PER_FRAME_OVERHEAD_BYTES >
            context.resource_grant.memory_bytes - memory_estimate) {
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_RESOURCES),
                 errmsg("Unicode root batch exceeds its conserved memory grant")));
    }
    memset(&numeric_provider, 0, sizeof(numeric_provider));
    if (laplace_unicode_numeric_oneapi_provider(&numeric_provider) !=
        LAPLACE_UNICODE_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("canonical Unicode oneAPI numeric provider is unavailable")));
    }
    memset(&build_request, 0, sizeof(build_request));
    build_request.source_root = source_root;
    build_request.spool_directory = spool_directory;
    build_request.numeric_provider = &numeric_provider;
    build_request.maximum_batch_bytes = (uint64)maximum_batch_bytes_input;
    build_request.maximum_batch_frames = (uint32)maximum_batch_frames_input;
    build_request.abi_major = LAPLACE_UNICODE_ROOT_BUILDER_ABI_MAJOR;
    build_request.abi_minor = LAPLACE_UNICODE_ROOT_BUILDER_ABI_MINOR;
    memset(&build, 0, sizeof(build));
    memset(sinks, 0, sizeof(sinks));
    memset(sink_artifacts, 0, sizeof(sink_artifacts));
    memset(&producer, 0, sizeof(producer));
    memset(&producer_receipt, 0, sizeof(producer_receipt));
    memset(&pg_result, 0, sizeof(pg_result));
    memset(&tier0_result, 0, sizeof(tier0_result));
    memset(&tier0_module, 0, sizeof(tier0_module));
    memset(&artifact, 0, sizeof(artifact));
    memset(&generation, 0, sizeof(generation));
    memset(&manifest_encoded_fingerprint, 0,
           sizeof(manifest_encoded_fingerprint));
    memset(&admission, 0, sizeof(admission));
    memset(&deposit_receipt, 0, sizeof(deposit_receipt));

    PG_TRY();
    {
        build_status = laplace_unicode_root_build_canonical_spool(
            &build_request, (laplace_canonical_spool**)&spool, &build);
        if (build_status != LAPLACE_UNICODE_ROOT_BUILD_OK || spool == NULL) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("canonical Unicode root build failed at stage %u",
                            build.stage),
                     errdetail("build=%u unicode=%u spool=%u",
                               build.status, build.unicode_status,
                               build.spool_status)));
        }
        memset(&pg_configuration, 0, sizeof(pg_configuration));
        expectation_from_build(&build, &pg_configuration.expectation);
        pg_configuration.expected_summary = build.stream;
        laplace_pg_unicode_sink_create(
            &pg_configuration,
            (laplace_pg_unicode_sink**)&pg_sink_state, &sinks[0]);
        memset(&tier0_configuration, 0, sizeof(tier0_configuration));
        tier0_configuration.target_path = tier0_path;
        tier0_configuration.root_expectation = pg_configuration.expectation;
        tier0_configuration.activation_epoch_id = activation_epoch_id;
        tier0_configuration.activation_epoch_fingerprint =
            activation_epoch_fingerprint;
        tier0_configuration.abi_major = LAPLACE_UNICODE_TIER0_SINK_ABI_MAJOR;
        tier0_configuration.abi_minor = LAPLACE_UNICODE_TIER0_SINK_ABI_MINOR;
        tier0_status = laplace_unicode_tier0_sink_create(
            &tier0_configuration,
            (laplace_unicode_tier0_sink**)&tier0_sink_state, &sinks[1]);
        if (tier0_status != LAPLACE_PERFCACHE_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Unicode Tier-0 sink creation failed with status %d",
                            (int)tier0_status)));
        }
        if (laplace_canonical_spool_producer(
                (laplace_canonical_spool*)spool, &producer) !=
            LAPLACE_SPOOL_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Unicode canonical spool cannot produce batches")));
        }
        control = producer_control();
        artifact_output.artifact_fingerprints = sink_artifacts;
        artifact_output.capacity = 2u;
        artifact_output.count = 0u;
        artifact_output.reserved = 0u;
        framework_status = laplace_framework_run_producer_with_artifacts(
            &context, &build.source.source_fingerprint,
            &build.source.recipe_fingerprint, &producer, &control,
            sinks, 2u, &artifact_output, &producer_receipt);
        if (framework_status != LAPLACE_FRAMEWORK_OK ||
            artifact_output.count != 2u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Unicode root sibling sink replay failed with status %d",
                            (int)framework_status)));
        }
        laplace_pg_unicode_sink_result_get(
            (laplace_pg_unicode_sink*)pg_sink_state, &pg_result);
        tier0_status = laplace_unicode_tier0_sink_result_get(
            (laplace_unicode_tier0_sink*)tier0_sink_state, &tier0_result);
        if (tier0_status != LAPLACE_PERFCACHE_OK ||
            !digest_equal(&pg_result.artifact_fingerprint, &sink_artifacts[0]) ||
            !digest_equal(
                &tier0_result.artifact_set_fingerprint,
                &sink_artifacts[1])) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Unicode root sibling sink artifacts diverged")));
        }
        registry_status = laplace_perfcache_unicode_tier0_module(&tier0_module);
        if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Unicode Tier-0 module contract is unavailable")));
        }
        artifact.path = tier0_path;
        artifact.contract = tier0_result.contract;
        artifact.expected_artifact_digest = tier0_result.artifact_digest;
        artifact.flags = LAPLACE_PERFCACHE_GENERATION_ARTIFACT_REQUIRED;
        generation.artifacts = &artifact;
        generation.artifact_count = 1u;
        generation.staged_sink_artifact_fingerprints = sink_artifacts;
        generation.staged_sink_count = 2u;
        generation.perfcache_sink_index = 1u;
        generation.activation_epoch_id = activation_epoch_id;
        generation.epoch_fingerprint = activation_epoch_fingerprint;
        generation.staged_receipt_id = producer_receipt.stream.receipt_id;
        generation.stream_fingerprint = producer_receipt.stream.stream_fingerprint;
        generation.staged_sink_artifacts_fingerprint =
            producer_receipt.stream.sink_artifacts_fingerprint;
        generation.sink_artifact_set_fingerprint =
            tier0_result.artifact_set_fingerprint;
        registry_status = laplace_perfcache_required_module_set_fingerprint(
            &tier0_module, 1u, &generation.required_module_set_fingerprint);
        if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Unicode Tier-0 required module set is invalid")));
        }
        registry_status = laplace_perfcache_generation_manifest_measure(
            &context, &producer_receipt.stream, &generation, &manifest_bytes);
        if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK ||
            manifest_bytes == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Unicode perfcache manifest cannot be measured")));
        }
        manifest = (uint8_t*)palloc(manifest_bytes);
        registry_status = laplace_perfcache_generation_manifest_write(
            &context, &producer_receipt.stream, &generation,
            manifest, manifest_bytes, &manifest_written,
            &manifest_encoded_fingerprint);
        if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK ||
            manifest_written != manifest_bytes) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("Unicode perfcache manifest cannot be encoded")));
        }
        pg_perfcache_status = laplace_pg_perfcache_admit_with_result(
            (uint64)expected_sequence_input, expected_present ? 1u : 0u,
            expected_present ? &expected_epoch : NULL,
            manifest, manifest_bytes, &admission);
        if (pg_perfcache_status != LAPLACE_PG_PERFCACHE_OK ||
            !digest_equal(
                &admission.encoded_manifest_fingerprint,
                &manifest_encoded_fingerprint)) {
            ereport(ERROR,
                    (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                     errmsg("Unicode perfcache epoch admission failed with status %d",
                            (int)pg_perfcache_status),
                     errdetail("stage=%u registry=%u framework=%u",
                               admission.failure_stage,
                               admission.registry_status,
                               admission.framework_status)));
        }
        persist_deposit_receipt(
            (laplace_pg_unicode_sink*)pg_sink_state, &producer_receipt,
            sink_artifacts, &tier0_result, &admission, &deposit_receipt);
        laplace_pg_unicode_sink_result_get(
            (laplace_pg_unicode_sink*)pg_sink_state, &pg_result);
    }
    PG_CATCH();
    {
        laplace_pg_unicode_sink_destroy(
            (laplace_pg_unicode_sink**)&pg_sink_state);
        laplace_unicode_tier0_sink_destroy(
            (laplace_unicode_tier0_sink**)&tier0_sink_state);
        laplace_canonical_spool_destroy((laplace_canonical_spool**)&spool);
        PG_RE_THROW();
    }
    PG_END_TRY();

    result_values[0] = PointerGetDatum(digest_bytea(&build.stream.receipt_id));
    result_values[1] = PointerGetDatum(digest_bytea(&producer_receipt.receipt_id));
    result_values[2] = PointerGetDatum(digest_bytea(&producer_receipt.stream.receipt_id));
    result_values[3] = PointerGetDatum(digest_bytea(
        &producer_receipt.stream.sink_artifacts_fingerprint));
    result_values[4] = PointerGetDatum(digest_bytea(&sink_artifacts[0]));
    result_values[5] = PointerGetDatum(digest_bytea(&sink_artifacts[1]));
    result_values[6] = PointerGetDatum(digest_bytea(&tier0_result.artifact_digest));
    result_values[7] = PointerGetDatum(digest_bytea(&admission.manifest_fingerprint));
    result_values[8] = PointerGetDatum(digest_bytea(
        &admission.encoded_manifest_fingerprint));
    result_values[9] = PointerGetDatum(digest_bytea(&admission.admission_receipt_id));
    result_values[10] = PointerGetDatum(id_bytea(
        &admission.next_epoch.activation_epoch_id));
    result_values[11] = PointerGetDatum(digest_bytea(
        &admission.next_epoch.epoch_fingerprint));
    result_values[12] = Int64GetDatum(laplace_pg_checked_int64(
        producer_receipt.stream.total_records, "Unicode root frame count"));
    result_values[13] = Int64GetDatum(laplace_pg_checked_int64(
        producer_receipt.stream.total_bytes, "Unicode root encoded bytes"));
    result_values[14] = Int64GetDatum(laplace_pg_checked_int64(
        producer_receipt.stream.batch_count, "Unicode root batch count"));
    result_values[15] = PointerGetDatum(digest_bytea(
        &pg_result.plan_manifest_fingerprint));
    result_values[16] = PointerGetDatum(digest_bytea(
        &pg_result.plan_sequence_fingerprint));
    result_values[17] = Int64GetDatum(laplace_pg_checked_int64(
        pg_result.plan_count, "Unicode PostgreSQL plan count"));
    {
        size_t persisted_index;
        for (persisted_index = 0u; persisted_index < 6u;
             ++persisted_index) {
            result_values[18u + persisted_index] =
                Int64GetDatum(laplace_pg_checked_int64(
                    pg_result.persisted_counts[persisted_index],
                    "Unicode PostgreSQL persisted cardinality"));
        }
    }
    result_values[24] = Int64GetDatum(laplace_pg_checked_int64(
        tier0_result.artifact_bytes, "Unicode Tier-0 artifact bytes"));
    result_tuple = laplace_pg_form_result_tuple(
        fcinfo, result_values, result_nulls, 25);
    laplace_pg_unicode_sink_destroy(
        (laplace_pg_unicode_sink**)&pg_sink_state);
    laplace_unicode_tier0_sink_destroy(
        (laplace_unicode_tier0_sink**)&tier0_sink_state);
    laplace_canonical_spool_destroy((laplace_canonical_spool**)&spool);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
