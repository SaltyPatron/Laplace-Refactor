#include "postgres.h"

#include <string.h>

#include "executor/spi.h"
#include "fmgr.h"
#include "catalog/pg_type_d.h"
#include "access/detoast.h"
#include "utils/array.h"
#include "utils/builtins.h"

#include "laplace/cognition_materialization.h"
#include "laplace/physicality_entity.h"
#include "laplace_pg_internal.h"
#include "materialization_pg.h"
#include "persistence_rows_pg.h"

/* BUILD_TESTING only: exercise the real generic kernel/provider over physical
 * rows produced by native composition. No cognition realization is invented. */
PG_FUNCTION_INFO_V1(laplace_pg_test_content_materialize_utf8);

Datum laplace_pg_test_content_materialize_utf8(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_id128 root;
    laplace_digest256 source_receipt, recipe;
    laplace_cognition_materialization_request request;
    laplace_cognition_materialization_provider_v1 provider;
    laplace_pg_materialization_provider_state* owner = NULL;
    laplace_content_materialization_receipt receipt;
    laplace_cognition_materialization_status status;
    ErrorData* provider_error;
    bytea* root_bytes = PG_GETARG_BYTEA_PP(1);
    uint8_t* output;
    size_t output_bytes = 0u;
    volatile bool connected = false;

    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if (VARSIZE_ANY_EXHDR(root_bytes) != sizeof(root.bytes))
        ereport(ERROR, (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
            errmsg("test materialization root must contain sixteen bytes")));
    memcpy(root.bytes, VARDATA_ANY(root_bytes), sizeof(root.bytes));
    laplace_pg_read_digest(PG_GETARG_DATUM(2), &source_receipt, "test source receipt");
    laplace_pg_read_digest(PG_GETARG_DATUM(3), &recipe, "test source recipe");
    memset(&request, 0, sizeof(request));
    request.maximum_nodes = 10000u;
    request.maximum_trajectory_carriers = 100000u;
    request.maximum_output_bytes = 65536u;
    request.maximum_depth = 256u;
    request.version = LAPLACE_COGNITION_MATERIALIZATION_VERSION;
    if (request.maximum_output_bytes > context.resource_grant.memory_bytes / 4u)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("test materialization requires its finite output reservation")));
    output = (uint8_t*)palloc((Size)request.maximum_output_bytes);
    laplace_pg_materialization_provider_create(&context, &owner, &provider);
    PG_TRY();
    {
        if (SPI_connect() != SPI_OK_CONNECT)
            ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                errmsg("test materialization could not connect to SPI")));
        connected = true;
        status = laplace_content_materialize_encoded(&root, &source_receipt, &recipe,
            &request, &provider, LAPLACE_COGNITION_OUTPUT_UTF8, output,
            (size_t)request.maximum_output_bytes, &output_bytes, &receipt);
        if (SPI_finish() != SPI_OK_FINISH)
            ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                errmsg("test materialization could not finish SPI")));
        connected = false;
    }
    PG_CATCH();
    {
        if (connected) (void)SPI_finish();
        laplace_pg_materialization_provider_destroy(&owner);
        PG_RE_THROW();
    }
    PG_END_TRY();
    provider_error = laplace_pg_materialization_provider_take_error(owner);
    laplace_pg_materialization_provider_destroy(&owner);
    if (provider_error != NULL) ReThrowError(provider_error);
    if (status != LAPLACE_COGNITION_MATERIALIZATION_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("test generic materialization failed native validation"),
            errdetail("native_status=%u", (unsigned)status)));
    {
        bytea* result = laplace_pg_bytes_to_bytea(output, output_bytes);
        pfree(output);
        PG_RETURN_BYTEA_P(result);
    }
}

/* BUILD_TESTING only. The ordinary composer transparently collapses [E] and
 * therefore does not emit a distinct singleton physicality. This explicit
 * physical-input fixture uses the existing native body/trajectory/frame owners;
 * SQL deposits its returned frames through the public canonical sink. */
PG_FUNCTION_INFO_V1(laplace_pg_test_singleton_physicality_frames);

Datum laplace_pg_test_singleton_physicality_frames(PG_FUNCTION_ARGS) {
    laplace_persistence_physicality_record body;
    laplace_physicality_entity_validation child, singleton;
    laplace_digest256 witness, recipe;
    laplace_trajectory_carrier carrier;
    bytea* trajectory;
    size_t bytes;
    if (toast_raw_datum_size(PG_GETARG_DATUM(1)) <= VARHDRSZ ||
        toast_raw_datum_size(PG_GETARG_DATUM(1)) > VARHDRSZ + 4096u)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("singleton fixture child trajectory exceeds its finite envelope before detoast")));
    trajectory = PG_GETARG_BYTEA_PP(1);
    bytes = (size_t)VARSIZE_ANY_EXHDR(trajectory);
    size_t count, emitted;
    Datum frames[3];
    const uint16_t kinds[3] = {LAPLACE_PERSISTENCE_RECORD_ENTITY,
        LAPLACE_PERSISTENCE_RECORD_PHYSICALITY,
        LAPLACE_PERSISTENCE_RECORD_PHYSICALITY_TRAJECTORY_SEGMENT};
    laplace_pg_physicality_read_record(PG_GETARG_DATUM(0), &body);
    laplace_pg_read_digest(PG_GETARG_DATUM(2), &witness, "singleton child witness");
    laplace_pg_read_digest(PG_GETARG_DATUM(3), &recipe, "singleton fixture recipe");
    if (bytes == 0u || bytes > 4096u || bytes % sizeof(carrier) != 0u)
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("singleton fixture child trajectory exceeds its finite envelope")));
    count = bytes / sizeof(carrier);
    /* Copy bytea's possibly packed payload before using the native typed ABI. */
    laplace_trajectory_carrier* input = palloc(bytes);
    memcpy(input, VARDATA_ANY(trajectory), bytes);
    if (body.logical_count < 2u ||
        laplace_physicality_entity_record_validate(&body, input, count,
            128u, 1048576u, &child) != LAPLACE_PHYSICALITY_ENTITY_OK ||
        child.witness_available != 1u || child.tier_available != 1u ||
        memcmp(child.realized_identity_witness.bytes, witness.bytes, 32u) != 0)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("singleton fixture lacks an independently validated child body and witness")));
    pfree(input);
    if (laplace_trajectory_composition_encode(&body.entity_id, 1u, 1u,
            (uint64_t)child.tier_floor << LAPLACE_TRAJECTORY_TIER_SHIFT,
            &carrier) != LAPLACE_TRAJECTORY_OK)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("singleton fixture native carrier construction failed")));
    body.recipe_fingerprint = recipe;
    body.logical_count = 1u;
    body.vertex_count = 1u;
    body.radius = 0.0;
    if (laplace_persistence_trajectory_fingerprint(&carrier, 1u,
            &body.trajectory_fingerprint) != LAPLACE_PERSISTENCE_OK ||
        laplace_persistence_physicality_identify(&body,
            &body.physicality_id) != LAPLACE_PERSISTENCE_OK ||
        laplace_physicality_entity_record_validate(&body, &carrier, 1u,
            1u, 1u, &singleton) != LAPLACE_PHYSICALITY_ENTITY_OK ||
        singleton.witness_available != 0u || singleton.tier_available != 0u)
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("singleton fixture native physicality validation failed")));
    for (size_t index = 0u; index < 3u; ++index) {
        size_t capacity = laplace_persistence_frame_bytes(kinds[index]);
        bytea* frame = palloc(VARHDRSZ + capacity);
        laplace_persistence_status status;
        if (index == 0u)
            status = laplace_persistence_frame_encode_entity(&body.entity_id,
                &witness, (uint8_t*)VARDATA(frame), capacity, &emitted);
        else if (index == 1u)
            status = laplace_persistence_frame_encode_physicality(&body,
                (uint8_t*)VARDATA(frame), capacity, &emitted);
        else
            status = laplace_persistence_frame_encode_trajectory_segment(
                &body.physicality_id, 0u, &carrier,
                (uint8_t*)VARDATA(frame), capacity, &emitted);
        if (status != LAPLACE_PERSISTENCE_OK || emitted != capacity)
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                errmsg("singleton fixture native persistence framing failed")));
        SET_VARSIZE(frame, VARHDRSZ + emitted);
        frames[index] = PointerGetDatum(frame);
    }
    PG_RETURN_ARRAYTYPE_P(construct_array(frames, 3, BYTEAOID, -1, false, TYPALIGN_INT));
}
