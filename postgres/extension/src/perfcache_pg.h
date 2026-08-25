#ifndef LAPLACE_POSTGRES_PERFCACHE_H
#define LAPLACE_POSTGRES_PERFCACHE_H

#include "postgres.h"

#include <stdint.h>

#include "laplace/identity.h"
#include "laplace/perfcache_registry.h"
#include "laplace/types.h"

#if defined(LAPLACE_TEST_SKIP_PG_PERFCACHE_EXPECTED_EPOCH)
#define laplace_pg_perfcache_admit \
    laplace_pg_perfcache_admit_expected_epoch_mutant_core
#define laplace_pg_perfcache_initialize \
    laplace_pg_perfcache_initialize_expected_epoch_mutant_core
#endif

typedef struct laplace_pg_perfcache_epoch {
    laplace_id128 activation_epoch_id;
    laplace_digest256 epoch_fingerprint;
} laplace_pg_perfcache_epoch;

typedef struct laplace_pg_perfcache_pin {
    laplace_pg_perfcache_epoch epoch;
    laplace_digest256 manifest_fingerprint;
    laplace_perfcache_pin native_pin;
    int32 owner_proc_number;
    int32 owner_pid;
    uint32 generation_index;
    uint32 held;
    void* resource_owner;
} laplace_pg_perfcache_pin;

typedef struct laplace_pg_perfcache_snapshot {
    laplace_pg_perfcache_epoch active_epoch;
    uint64 active_reader_count;
    uint64 retired_generation_count;
    uint64 retired_reader_count;
    uint64 reservation_serial;
    uint64 active_sequence;
    uint32 has_active_epoch;
    uint32 has_reservation;
} laplace_pg_perfcache_snapshot;

typedef enum laplace_pg_perfcache_status {
    LAPLACE_PG_PERFCACHE_OK = 0,
    LAPLACE_PG_PERFCACHE_INVALID_ARGUMENT = 1,
    LAPLACE_PG_PERFCACHE_NO_ACTIVE_EPOCH = 2,
    LAPLACE_PG_PERFCACHE_EPOCH_MISMATCH = 3,
    LAPLACE_PG_PERFCACHE_ALREADY_RESERVED = 4,
    LAPLACE_PG_PERFCACHE_ALREADY_PINNED = 5,
    LAPLACE_PG_PERFCACHE_CAPACITY_EXHAUSTED = 6,
    LAPLACE_PG_PERFCACHE_INTERNAL_ERROR = 7,
    LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH = 8
} laplace_pg_perfcache_status;

void laplace_pg_perfcache_initialize(void);

laplace_pg_perfcache_status laplace_pg_perfcache_admit(
    uint64 expected_sequence,
    uint32 has_expected_epoch,
    const laplace_pg_perfcache_epoch* expected_epoch,
    const uint8* encoded_manifest,
    Size encoded_manifest_bytes);

laplace_pg_perfcache_status laplace_pg_perfcache_pin_active(
    uint32 has_expected_epoch,
    const laplace_pg_perfcache_epoch* expected_epoch,
    laplace_pg_perfcache_pin** pin);

laplace_pg_perfcache_status laplace_pg_perfcache_pin_epoch(
    const laplace_pg_perfcache_epoch* expected_epoch,
    laplace_pg_perfcache_pin** pin);

void laplace_pg_perfcache_pin_release(laplace_pg_perfcache_pin** pin);

laplace_pg_perfcache_status laplace_pg_perfcache_snapshot_get(
    laplace_pg_perfcache_snapshot* snapshot);

#endif
