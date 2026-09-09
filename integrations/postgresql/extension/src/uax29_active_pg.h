#ifndef LAPLACE_POSTGRES_UAX29_ACTIVE_PG_H
#define LAPLACE_POSTGRES_UAX29_ACTIVE_PG_H

#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "utils/builtins.h"
#include "utils/errcodes.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/perfcache_modules.h"
#include "laplace/uax29.h"
#include "laplace_pg_internal.h"
#include "perfcache_pg.h"

typedef struct laplace_pg_active_uax_authority {
    laplace_digest256 root_receipt;
    laplace_digest256 source_fingerprint;
    laplace_digest256 recipe_fingerprint;
    laplace_id128 activation_epoch_id;
    laplace_digest256 activation_epoch_fingerprint;
} laplace_pg_active_uax_authority;

static Datum laplace_pg_uax_required_value(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    const char* field) {
    bool is_null = false;
    Datum value = SPI_getbinval(tuple, descriptor, column, &is_null);
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace active UAX authority returned null %s", field)));
    }
    return value;
}

static void laplace_pg_uax_read_bytes(
    Datum datum,
    uint8_t* output,
    size_t expected,
    const char* field) {
    bytea* value = DatumGetByteaPP(datum);
    if ((size_t)VARSIZE_ANY_EXHDR(value) != expected) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace active UAX %s has invalid width", field),
                 errdetail("expected=%zu actual=%zu", expected,
                           (size_t)VARSIZE_ANY_EXHDR(value))));
    }
    memcpy(output, VARDATA_ANY(value), expected);
}

static void laplace_pg_uax_read_authority(
    const laplace_pg_perfcache_epoch* epoch,
    laplace_pg_active_uax_authority* authority) {
    static const char sql[] =
        "SELECT d.root_receipt,g.source_fingerprint,g.recipe_fingerprint "
        "FROM " LAPLACE_PG_SCHEMA ".unicode_root_deposit_receipt d JOIN "
        LAPLACE_PG_SCHEMA ".unicode_root_generation g "
        "ON g.root_receipt=d.root_receipt "
        "WHERE d.activation_epoch_id=$1::" LAPLACE_PG_SCHEMA
        ".content_id_128 AND d.activation_epoch_fingerprint=$2::"
        LAPLACE_PG_SCHEMA ".record_id_256";
    Oid types[2] = {BYTEAOID, BYTEAOID};
    Datum values[2];
    int result;

    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        epoch->activation_epoch_id.bytes,
        sizeof(epoch->activation_epoch_id.bytes)));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        epoch->epoch_fingerprint.bytes,
        sizeof(epoch->epoch_fingerprint.bytes)));

    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace could not connect to active Unicode authority metadata")));
    }
    result = SPI_execute_with_args(sql, 2, types, values, NULL, true, 0);
    if (result != SPI_OK_SELECT || SPI_processed != 1u || SPI_tuptable == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace active Unicode epoch has no unique canonical source authority"),
                 errdetail("matching roots=%llu",
                           (unsigned long long)SPI_processed)));
    }
    laplace_pg_uax_read_bytes(
        laplace_pg_uax_required_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, "root receipt"),
        authority->root_receipt.bytes, sizeof(authority->root_receipt.bytes),
        "root receipt");
    laplace_pg_uax_read_bytes(
        laplace_pg_uax_required_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
            "source fingerprint"),
        authority->source_fingerprint.bytes,
        sizeof(authority->source_fingerprint.bytes), "source fingerprint");
    laplace_pg_uax_read_bytes(
        laplace_pg_uax_required_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
            "recipe fingerprint"),
        authority->recipe_fingerprint.bytes,
        sizeof(authority->recipe_fingerprint.bytes), "recipe fingerprint");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace could not close active Unicode authority lookup")));
    }
}

static void laplace_pg_uax29_tables_from_active_unicode(
    laplace_uax29_tables** tables,
    laplace_pg_active_uax_authority* authority) {
    enum { LAPLACE_PG_UAX_BATCH = 4096 };
    laplace_pg_perfcache_pin* pin = NULL;
    laplace_uax29_atom_table_builder* builder = NULL;
    laplace_unicode_atom_record_view* views = NULL;
    uint32_t* positions = NULL;
    uint8_t* found = NULL;
    laplace_pg_perfcache_status pin_status;
    laplace_uax29_status uax_status;
    uint32_t first;

    if (tables == NULL || authority == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace active UAX output is incomplete")));
    }
    *tables = NULL;
    memset(authority, 0, sizeof(*authority));

    pin_status = laplace_pg_perfcache_pin_active(0u, NULL, &pin);
    if (pin_status != LAPLACE_PG_PERFCACHE_OK || pin == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace could not pin the active canonical Unicode generation for UAX"),
                 errdetail("status=%u", (unsigned int)pin_status)));
    }

    uax_status = laplace_uax29_atom_table_builder_create(&builder);
    if (uax_status != LAPLACE_UAX29_OK || builder == NULL) {
        laplace_pg_perfcache_pin_release(&pin);
        ereport(ERROR,
                (errcode(ERRCODE_OUT_OF_MEMORY),
                 errmsg("Laplace could not allocate active UAX property tables"),
                 errdetail("status=%u", (unsigned int)uax_status)));
    }

    views = (laplace_unicode_atom_record_view*)palloc0(
        sizeof(*views) * LAPLACE_PG_UAX_BATCH);
    positions = (uint32_t*)palloc(
        sizeof(*positions) * LAPLACE_PG_UAX_BATCH);
    found = (uint8_t*)palloc0(LAPLACE_PG_UAX_BATCH);

    PG_TRY();
    {
        authority->activation_epoch_id = pin->epoch.activation_epoch_id;
        authority->activation_epoch_fingerprint = pin->epoch.epoch_fingerprint;
        laplace_pg_uax_read_authority(&pin->epoch, authority);

        for (first = 0u; first < LAPLACE_UNICODE_ROOT_POPULATION;) {
            size_t count = (size_t)(LAPLACE_UNICODE_ROOT_POPULATION - first);
            size_t index;
            laplace_perfcache_registry_status resolve_status;
            if (count > LAPLACE_PG_UAX_BATCH) {
                count = LAPLACE_PG_UAX_BATCH;
            }
            memset(views, 0, sizeof(*views) * count);
            memset(found, 0, count);
            for (index = 0u; index < count; ++index) {
                positions[index] = first + (uint32_t)index;
            }
            resolve_status = laplace_perfcache_unicode_tier0_resolve_batch(
                &pin->native_pin, positions, count, views, found);
            if (resolve_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace active Unicode Tier-0 cannot derive UAX authority"),
                         errdetail("first=%u count=%zu status=%u",
                                   (unsigned int)first, count,
                                   (unsigned int)resolve_status)));
            }
            for (index = 0u; index < count; ++index) {
                if (found[index] == 0u ||
                    views[index].value.codepoint_position != positions[index]) {
                    ereport(ERROR,
                            (errcode(ERRCODE_DATA_CORRUPTED),
                             errmsg("Laplace active Unicode Tier-0 is incomplete for UAX derivation"),
                             errdetail("position=%u found=%u resolved=%u",
                                       (unsigned int)positions[index],
                                       (unsigned int)found[index],
                                       (unsigned int)views[index].value.codepoint_position)));
                }
            }
            uax_status = laplace_uax29_atom_table_builder_consume(
                builder, views, count);
            if (uax_status != LAPLACE_UAX29_OK) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace canonical Unicode atom properties cannot derive UAX authority"),
                         errdetail("first=%u count=%zu status=%u",
                                   (unsigned int)first, count,
                                   (unsigned int)uax_status)));
            }
            first += (uint32_t)count;
        }

        uax_status = laplace_uax29_atom_table_builder_finish(&builder, tables);
        if (uax_status != LAPLACE_UAX29_OK || *tables == NULL) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace active Unicode atom stream did not close UAX authority"),
                     errdetail("status=%u", (unsigned int)uax_status)));
        }
        laplace_pg_perfcache_pin_release(&pin);
        pfree(found);
        pfree(positions);
        pfree(views);
        found = NULL;
        positions = NULL;
        views = NULL;
    }
    PG_CATCH();
    {
        laplace_uax29_atom_table_builder_destroy(&builder);
        laplace_uax29_tables_destroy(tables);
        laplace_pg_perfcache_pin_release(&pin);
        if (found != NULL) pfree(found);
        if (positions != NULL) pfree(positions);
        if (views != NULL) pfree(views);
        PG_RE_THROW();
    }
    PG_END_TRY();
}

#endif
