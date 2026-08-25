#include "postgres.h"

#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "access/parallel.h"
#include "access/transam.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "catalog/pg_type_d.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "storage/dsm_registry.h"
#include "storage/ipc.h"
#include "storage/lwlock.h"
#include "storage/proc.h"
#include "storage/procarray.h"
#include "storage/procnumber.h"
#include "utils/builtins.h"
#include "utils/array.h"
#include "utils/guc.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/resowner.h"
#include "varatt.h"

#include "laplace/contract/postgresql_bindings.h"
#include "laplace/perfcache_modules.h"
#include "perfcache_pg.h"

#define LAPLACE_PG_PERFCACHE_DSM_NAME_FORMAT "laplace.pc.epoch.v1.%u"
#define LAPLACE_PG_PERFCACHE_MAGIC UINT32_C(0x4c504331)
#define LAPLACE_PG_PERFCACHE_ABI_MAJOR UINT16_C(1)
#define LAPLACE_PG_PERFCACHE_ABI_MINOR UINT16_C(0)
#define LAPLACE_PG_PERFCACHE_NO_GENERATION UINT32_MAX

typedef enum laplace_pg_perfcache_generation_state {
    LAPLACE_PG_PERFCACHE_GENERATION_UNUSED = 0,
    LAPLACE_PG_PERFCACHE_GENERATION_ACTIVE = 1,
    LAPLACE_PG_PERFCACHE_GENERATION_RETIRED = 2,
    LAPLACE_PG_PERFCACHE_GENERATION_PREPARED = 3
} laplace_pg_perfcache_generation_state;

typedef struct laplace_pg_perfcache_generation_slot {
    laplace_pg_perfcache_epoch epoch;
    laplace_digest256 manifest_fingerprint;
    laplace_digest256 admission_receipt_id;
    uint64 sequence;
    uint64 reader_count;
    uint32 state;
    uint32 reserved;
} laplace_pg_perfcache_generation_slot;

typedef struct laplace_pg_perfcache_owner_slot {
    laplace_pg_perfcache_epoch epoch;
    int32 pid;
    int64 backend_start_timestamp;
    uint32 generation_index;
    uint32 pin_depth;
    uint32 reserved;
} laplace_pg_perfcache_owner_slot;

typedef struct laplace_pg_perfcache_reservation {
    laplace_pg_perfcache_epoch expected_epoch;
    laplace_pg_perfcache_epoch next_epoch;
    laplace_digest256 manifest_fingerprint;
    laplace_digest256 admission_receipt_id;
    uint64 serial;
    uint64 expected_sequence;
    uint64 next_sequence;
    TransactionId transaction_id;
    int32 owner_proc_number;
    int32 owner_pid;
    int64 owner_start_timestamp;
    uint32 candidate_generation_index;
    uint32 candidate_was_new;
    uint32 has_expected_epoch;
    uint32 state;
    uint32 present;
} laplace_pg_perfcache_reservation;

typedef struct laplace_pg_perfcache_shared {
    uint32 magic;
    uint16 abi_major;
    uint16 abi_minor;
    uint32 max_backends;
    uint32 generation_capacity;
    uint32 active_generation_index;
    Oid database_id;
    uint64 system_identifier;
    uint64 next_reservation_serial;
    LWLock lock;
    laplace_pg_perfcache_reservation reservation;
} laplace_pg_perfcache_shared;

typedef struct laplace_pg_perfcache_pending {
    laplace_pg_perfcache_epoch expected_epoch;
    laplace_pg_perfcache_epoch next_epoch;
    laplace_digest256 manifest_fingerprint;
    laplace_digest256 admission_receipt_id;
    uint64 reservation_serial;
    uint64 expected_sequence;
    uint64 next_sequence;
    TransactionId transaction_id;
    SubTransactionId subtransaction_id;
    uint32 candidate_generation_index;
    uint32 candidate_was_new;
    uint32 has_expected_epoch;
    uint32 present;
} laplace_pg_perfcache_pending;

typedef struct laplace_pg_perfcache_native_pending {
    laplace_perfcache_generation_manifest* manifest;
    laplace_perfcache_activation* activation;
    laplace_framework_context context;
    laplace_framework_activation_request request;
    laplace_framework_activation_provider_v1 provider;
    laplace_framework_activation_receipt receipt;
    uint32 present;
} laplace_pg_perfcache_native_pending;

static laplace_pg_perfcache_shared* perfcache_shared = NULL;
static laplace_pg_perfcache_pending perfcache_pending;
static laplace_pg_perfcache_native_pending perfcache_native_pending;
static bool callbacks_registered = false;
static bool invalidation_callback_registered = false;
static bool exit_callback_registered = false;
static bool perfcache_backend_synchronized = false;
#if !defined(LAPLACE_TEST_SKIP_PG_PERFCACHE_EXPECTED_EPOCH) && \
    !defined(LAPLACE_TEST_BLIND_CONFLICT)
static bool perfcache_guc_registered = false;
#endif
static char* perfcache_root = NULL;
static char* perfcache_dsm_tranche_name = NULL;
static laplace_perfcache_registry* perfcache_native_registry = NULL;
static laplace_perfcache_artifact_provider_v1 perfcache_file_provider;
static laplace_perfcache_module_v1 perfcache_framework_probe_module;
static uint64 perfcache_manifest_load_count = 0u;
static uint64 perfcache_catalog_select_count = 0u;
static uint64 perfcache_manifest_select_count = 0u;
static SPIPlanPtr perfcache_generation_plan = NULL;
static SPIPlanPtr perfcache_active_update_plan = NULL;
static SPIPlanPtr perfcache_active_select_plan = NULL;
static SPIPlanPtr perfcache_manifest_select_plan = NULL;

static void synchronize_from_catalog(void);
static void abort_native_pending(void);

static void perfcache_relcache_invalidation(Datum argument, Oid relation_id) {
    (void)argument;
    (void)relation_id;
    perfcache_backend_synchronized = false;
}

enum {
    LAPLACE_PG_PERFCACHE_RESERVATION_PREPARED = 1u,
    LAPLACE_PG_PERFCACHE_RESERVATION_COMMITTING = 2u
};

static Size perfcache_owner_offset(void) {
    return MAXALIGN(sizeof(laplace_pg_perfcache_shared));
}

static Size perfcache_generation_offset(void) {
    return perfcache_owner_offset() +
        MAXALIGN(mul_size(sizeof(laplace_pg_perfcache_owner_slot),
                          (Size)MaxBackends));
}

static Size perfcache_shared_size(void) {
    return perfcache_generation_offset() +
        MAXALIGN(mul_size(sizeof(laplace_pg_perfcache_generation_slot),
                          add_size((Size)MaxBackends, 2u)));
}

static laplace_pg_perfcache_owner_slot* perfcache_owners(void) {
    return (laplace_pg_perfcache_owner_slot*)
        ((char*)perfcache_shared + perfcache_owner_offset());
}

static laplace_pg_perfcache_generation_slot* perfcache_generations(void) {
    return (laplace_pg_perfcache_generation_slot*)
        ((char*)perfcache_shared + perfcache_generation_offset());
}

static bool epoch_equal(
    const laplace_pg_perfcache_epoch* left,
    const laplace_pg_perfcache_epoch* right) {
    return memcmp(left, right, sizeof(*left)) == 0;
}

static laplace_pg_perfcache_status ensure_native_registry(void) {
    laplace_perfcache_registry_status status;
    if (perfcache_native_registry != NULL) {
        return LAPLACE_PG_PERFCACHE_OK;
    }
    status = laplace_perfcache_framework_probe_module(
        &perfcache_framework_probe_module);
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    }
    status = laplace_perfcache_file_provider(&perfcache_file_provider);
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    }
    status = laplace_perfcache_registry_create(
        &perfcache_framework_probe_module, 1u, &perfcache_native_registry);
    return status == LAPLACE_PERFCACHE_REGISTRY_OK
        ? LAPLACE_PG_PERFCACHE_OK
        : LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
}

static bool reset_native_registry(void) {
    laplace_perfcache_registry_status status;
    if (perfcache_native_registry == NULL) {
        return true;
    }
    status = laplace_perfcache_registry_destroy(perfcache_native_registry);
    if (status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return false;
    }
    perfcache_native_registry = NULL;
    memset(&perfcache_file_provider, 0, sizeof(perfcache_file_provider));
    memset(&perfcache_framework_probe_module, 0,
           sizeof(perfcache_framework_probe_module));
    perfcache_manifest_load_count = 0u;
    return true;
}

static bool path_is_under_perfcache_root(const char* path) {
    const char* configured_root = perfcache_root;
    char root_buffer[PATH_MAX];
    char path_buffer[PATH_MAX];
    size_t root_length;
    if (configured_root == NULL) {
        configured_root = GetConfigOption(
            "laplace.perfcache_root", false, false);
    }
    if (configured_root == NULL || configured_root[0] != '/' || path == NULL ||
        realpath(configured_root, root_buffer) == NULL ||
        realpath(path, path_buffer) == NULL) {
        return false;
    }
    root_length = strlen(root_buffer);
    if (root_length <= 1u || strncmp(root_buffer, path_buffer, root_length) != 0) {
        return false;
    }
    return path_buffer[root_length] == '/';
}

static bool manifest_paths_are_admitted(
    const laplace_perfcache_generation_request* request) {
    size_t index;
    if (request == NULL || request->artifacts == NULL ||
        request->artifact_count == 0u) {
        return false;
    }
    for (index = 0u; index < request->artifact_count; ++index) {
        if (!path_is_under_perfcache_root(request->artifacts[index].path)) {
            return false;
        }
    }
    return true;
}

static void release_native_pending_storage(void) {
    if (perfcache_native_pending.activation != NULL) {
        laplace_perfcache_activation_destroy(
            perfcache_native_pending.activation);
    }
    if (perfcache_native_pending.manifest != NULL) {
        laplace_perfcache_generation_manifest_close(
            perfcache_native_pending.manifest);
    }
    memset(&perfcache_native_pending, 0, sizeof(perfcache_native_pending));
}

static void abort_native_pending(void) {
    if (perfcache_native_pending.present != 0u) {
        (void)laplace_framework_abort_admitted_stream(
            &perfcache_native_pending.context,
            &perfcache_native_pending.request,
            &perfcache_native_pending.provider,
            &perfcache_native_pending.receipt);
    }
    release_native_pending_storage();
}

static bool commit_native_pending(void) {
    laplace_framework_status status;
    if (perfcache_native_pending.present == 0u) {
        return false;
    }
    status = laplace_framework_commit_admitted_stream(
        &perfcache_native_pending.context,
        &perfcache_native_pending.request,
        &perfcache_native_pending.provider,
        &perfcache_native_pending.receipt);
    if (status != LAPLACE_FRAMEWORK_OK) {
        abort_native_pending();
        return false;
    }
    release_native_pending_storage();
    return true;
}

static laplace_pg_perfcache_status prepare_native_admission(
    const uint8* encoded_manifest,
    Size encoded_manifest_bytes,
    laplace_pg_perfcache_epoch* next_epoch,
    laplace_digest256* manifest_fingerprint,
    laplace_digest256* encoded_manifest_fingerprint,
    laplace_digest256* admission_receipt_id) {
    laplace_perfcache_generation_manifest* manifest = NULL;
    const laplace_framework_context* context = NULL;
    const laplace_framework_stream_receipt* staged = NULL;
    const laplace_perfcache_generation_request* request = NULL;
    laplace_perfcache_prepared_generation* prepared = NULL;
    laplace_perfcache_generation_receipt prepared_receipt;
    laplace_perfcache_registry_snapshot snapshot;
    laplace_perfcache_epoch expected_native_epoch;
    laplace_perfcache_registry_status registry_status;
    laplace_framework_activation_request activation_request;
    laplace_framework_activation_provider_v1 activation_provider;
    laplace_framework_activation_receipt activation_receipt;
    laplace_perfcache_activation* activation = NULL;
    uint32 has_expected_native_epoch;
    laplace_framework_status framework_status;
    laplace_pg_perfcache_status status;
    if (encoded_manifest == NULL || encoded_manifest_bytes == 0u ||
        next_epoch == NULL || manifest_fingerprint == NULL ||
        encoded_manifest_fingerprint == NULL || admission_receipt_id == NULL ||
        perfcache_native_pending.present != 0u) {
        return LAPLACE_PG_PERFCACHE_INVALID_ARGUMENT;
    }
    status = ensure_native_registry();
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        return status;
    }
    registry_status = laplace_perfcache_generation_manifest_open(
        encoded_manifest, encoded_manifest_bytes, &manifest,
        encoded_manifest_fingerprint);
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK || manifest == NULL) {
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    registry_status = laplace_perfcache_generation_manifest_view(
        manifest, &context, &staged, &request);
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK ||
        !manifest_paths_are_admitted(request)) {
        laplace_perfcache_generation_manifest_close(manifest);
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    registry_status = laplace_perfcache_generation_manifest_fingerprint(
        request, manifest_fingerprint);
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        laplace_perfcache_generation_manifest_close(manifest);
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    next_epoch->activation_epoch_id = request->activation_epoch_id;
    next_epoch->epoch_fingerprint = request->epoch_fingerprint;
    memset(&prepared_receipt, 0, sizeof(prepared_receipt));
    registry_status = laplace_perfcache_registry_prepare(
        perfcache_native_registry, context, staged, &perfcache_file_provider,
        request, &prepared, &prepared_receipt);
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        laplace_perfcache_generation_manifest_close(manifest);
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    registry_status = laplace_perfcache_registry_snapshot_get(
        perfcache_native_registry, &snapshot);
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        laplace_perfcache_registry_discard_prepared(&prepared);
        laplace_perfcache_generation_manifest_close(manifest);
        return LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    }
    has_expected_native_epoch = snapshot.has_active_generation;
    memset(&expected_native_epoch, 0, sizeof(expected_native_epoch));
    if (has_expected_native_epoch != 0u) {
        expected_native_epoch.activation_epoch_id =
            snapshot.active_activation_epoch_id;
        expected_native_epoch.epoch_fingerprint =
            snapshot.active_epoch_fingerprint;
    }
    memset(&activation_provider, 0, sizeof(activation_provider));
    registry_status = laplace_perfcache_activation_create(
        perfcache_native_registry, &prepared, has_expected_native_epoch,
        has_expected_native_epoch != 0u ? &expected_native_epoch : NULL,
        &activation, &activation_provider);
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        laplace_perfcache_registry_discard_prepared(&prepared);
        laplace_perfcache_generation_manifest_close(manifest);
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    memset(&activation_request, 0, sizeof(activation_request));
    activation_request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_PERFCACHE;
    activation_request.expected_epoch =
        context->epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE];
    activation_request.next_epoch = request->epoch_fingerprint;
    memset(&activation_receipt, 0, sizeof(activation_receipt));
    framework_status = laplace_framework_admit_staged_stream(
        context, staged, &activation_request, &activation_provider,
        &activation_receipt);
    if (framework_status != LAPLACE_FRAMEWORK_OK) {
        laplace_perfcache_activation_destroy(activation);
        laplace_perfcache_generation_manifest_close(manifest);
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    memset(&perfcache_native_pending, 0, sizeof(perfcache_native_pending));
    perfcache_native_pending.manifest = manifest;
    perfcache_native_pending.activation = activation;
    perfcache_native_pending.context = *context;
    perfcache_native_pending.request = activation_request;
    perfcache_native_pending.provider = activation_provider;
    perfcache_native_pending.receipt = activation_receipt;
    perfcache_native_pending.present = 1u;
    *admission_receipt_id = activation_receipt.receipt_id;
    return LAPLACE_PG_PERFCACHE_OK;
}

static void perfcache_shared_initialize(void* address) {
    laplace_pg_perfcache_shared* state =
        (laplace_pg_perfcache_shared*)address;
    memset(state, 0, perfcache_shared_size());
    state->magic = LAPLACE_PG_PERFCACHE_MAGIC;
    state->abi_major = LAPLACE_PG_PERFCACHE_ABI_MAJOR;
    state->abi_minor = LAPLACE_PG_PERFCACHE_ABI_MINOR;
    state->max_backends = (uint32)MaxBackends;
    /*
     * Every backend may pin a distinct retired generation.  Activation must
     * still have room for both the formerly active generation and its
     * candidate until the handoff completes.
     */
    state->generation_capacity = (uint32)MaxBackends + 2u;
    state->active_generation_index = LAPLACE_PG_PERFCACHE_NO_GENERATION;
    state->database_id = MyDatabaseId;
    state->system_identifier = GetSystemIdentifier();
    state->next_reservation_serial = 1u;
    LWLockInitialize(&state->lock, LWLockNewTrancheId());
}

static bool backend_owner_is_live(int32 proc_number, int32 pid) {
    PGPROC* process;
    if (proc_number < 0 || proc_number >= MaxBackends || pid <= 0) {
        return false;
    }
    process = ProcNumberGetProc(proc_number);
    return process != NULL && process->pid == pid;
}

static void clear_owner_locked(uint32 owner_index) {
    laplace_pg_perfcache_owner_slot* owner = &perfcache_owners()[owner_index];
    laplace_pg_perfcache_generation_slot* generations = perfcache_generations();
    if (owner->pin_depth != 0u &&
        owner->generation_index < perfcache_shared->generation_capacity) {
        laplace_pg_perfcache_generation_slot* generation =
            &generations[owner->generation_index];
        if (generation->reader_count >= (uint64)owner->pin_depth) {
            generation->reader_count -= (uint64)owner->pin_depth;
        } else {
            generation->reader_count = 0u;
        }
    }
    memset(owner, 0, sizeof(*owner));
    owner->generation_index = LAPLACE_PG_PERFCACHE_NO_GENERATION;
}

static void collect_retired_locked(void) {
    laplace_pg_perfcache_generation_slot* generations = perfcache_generations();
    uint32 index;
    for (index = 0u; index < perfcache_shared->generation_capacity; ++index) {
        if (generations[index].state ==
                LAPLACE_PG_PERFCACHE_GENERATION_RETIRED &&
            generations[index].reader_count == 0u) {
            memset(&generations[index], 0, sizeof(generations[index]));
        }
    }
}

static void discard_reserved_candidate_locked(
    const laplace_pg_perfcache_reservation* reservation) {
    uint32 candidate = reservation->candidate_generation_index;
    if (reservation->candidate_was_new != 0u &&
        candidate < perfcache_shared->generation_capacity &&
        perfcache_generations()[candidate].state ==
            LAPLACE_PG_PERFCACHE_GENERATION_PREPARED) {
        memset(&perfcache_generations()[candidate], 0,
               sizeof(perfcache_generations()[candidate]));
    }
}

static void reclaim_stale_owners_locked(void) {
    laplace_pg_perfcache_owner_slot* owners = perfcache_owners();
    uint32 index;
    for (index = 0u; index < perfcache_shared->max_backends; ++index) {
        if (owners[index].pin_depth != 0u &&
            !backend_owner_is_live((int32)index, owners[index].pid)) {
            clear_owner_locked(index);
        }
    }
    if (perfcache_shared->reservation.present != 0u &&
        perfcache_shared->reservation.state ==
            LAPLACE_PG_PERFCACHE_RESERVATION_PREPARED &&
        !backend_owner_is_live(
            perfcache_shared->reservation.owner_proc_number,
            perfcache_shared->reservation.owner_pid)) {
        discard_reserved_candidate_locked(&perfcache_shared->reservation);
        memset(&perfcache_shared->reservation, 0,
               sizeof(perfcache_shared->reservation));
    }
    collect_retired_locked();
}

static uint32 find_generation_locked(
    const laplace_pg_perfcache_epoch* epoch) {
    laplace_pg_perfcache_generation_slot* generations = perfcache_generations();
    uint32 index;
    for (index = 0u; index < perfcache_shared->generation_capacity; ++index) {
        if (generations[index].state != LAPLACE_PG_PERFCACHE_GENERATION_UNUSED &&
            epoch_equal(&generations[index].epoch, epoch)) {
            return index;
        }
    }
    return LAPLACE_PG_PERFCACHE_NO_GENERATION;
}

static uint32 allocate_generation_locked(void) {
    laplace_pg_perfcache_generation_slot* generations = perfcache_generations();
    uint32 index;
    collect_retired_locked();
    for (index = 0u; index < perfcache_shared->generation_capacity; ++index) {
        if (generations[index].state == LAPLACE_PG_PERFCACHE_GENERATION_UNUSED) {
            return index;
        }
    }
    return LAPLACE_PG_PERFCACHE_NO_GENERATION;
}

static void release_backend_state(int code, Datum argument) {
    uint32 owner_index;
    (void)code;
    (void)argument;
    if (perfcache_shared == NULL || MyProcNumber < 0 ||
        MyProcNumber >= MaxBackends) {
        abort_native_pending();
        return;
    }
    owner_index = (uint32)MyProcNumber;
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    if (perfcache_owners()[owner_index].pid == MyProcPid &&
        perfcache_owners()[owner_index].backend_start_timestamp ==
            (int64)MyStartTimestamp) {
        clear_owner_locked(owner_index);
    }
    if (perfcache_shared->reservation.present != 0u &&
        perfcache_shared->reservation.state ==
            LAPLACE_PG_PERFCACHE_RESERVATION_PREPARED &&
        perfcache_shared->reservation.owner_proc_number == MyProcNumber &&
        perfcache_shared->reservation.owner_pid == MyProcPid &&
        perfcache_shared->reservation.owner_start_timestamp ==
            (int64)MyStartTimestamp) {
        memset(&perfcache_shared->reservation, 0,
               sizeof(perfcache_shared->reservation));
    }
    collect_retired_locked();
    LWLockRelease(&perfcache_shared->lock);
    memset(&perfcache_pending, 0, sizeof(perfcache_pending));
    abort_native_pending();
}

static void attach_shared_state(void) {
    bool found = false;
    char segment_name[64];
    if (perfcache_shared != NULL) {
        return;
    }
    if (snprintf(segment_name, sizeof(segment_name),
                 LAPLACE_PG_PERFCACHE_DSM_NAME_FORMAT,
                 (unsigned int)MyDatabaseId) < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("cannot format Laplace perfcache DSM name")));
    }
    perfcache_shared = (laplace_pg_perfcache_shared*)GetNamedDSMSegment(
        segment_name,
        perfcache_shared_size(),
        perfcache_shared_initialize,
        &found);
    if (perfcache_shared->magic != LAPLACE_PG_PERFCACHE_MAGIC ||
        perfcache_shared->abi_major != LAPLACE_PG_PERFCACHE_ABI_MAJOR ||
        perfcache_shared->abi_minor != LAPLACE_PG_PERFCACHE_ABI_MINOR ||
        perfcache_shared->max_backends != (uint32)MaxBackends ||
        perfcache_shared->generation_capacity != (uint32)MaxBackends + 2u ||
        perfcache_shared->database_id != MyDatabaseId ||
        perfcache_shared->system_identifier != GetSystemIdentifier()) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace perfcache shared-control ABI mismatch")));
    }
    perfcache_dsm_tranche_name =
        MemoryContextStrdup(TopMemoryContext, segment_name);
    LWLockRegisterTranche(perfcache_shared->lock.tranche,
                          perfcache_dsm_tranche_name);
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    if (MyProcNumber >= 0 && MyProcNumber < MaxBackends) {
        laplace_pg_perfcache_owner_slot* owner =
            &perfcache_owners()[(uint32)MyProcNumber];
        if (owner->pin_depth != 0u &&
            (owner->pid != MyProcPid ||
             owner->backend_start_timestamp != (int64)MyStartTimestamp)) {
            clear_owner_locked((uint32)MyProcNumber);
        }
        if (perfcache_shared->reservation.present != 0u &&
            perfcache_shared->reservation.state ==
                LAPLACE_PG_PERFCACHE_RESERVATION_PREPARED &&
            perfcache_shared->reservation.owner_proc_number == MyProcNumber &&
            (perfcache_shared->reservation.owner_pid != MyProcPid ||
             perfcache_shared->reservation.owner_start_timestamp !=
                (int64)MyStartTimestamp)) {
            memset(&perfcache_shared->reservation, 0,
                   sizeof(perfcache_shared->reservation));
        }
    }
    LWLockRelease(&perfcache_shared->lock);
    if (!exit_callback_registered) {
        before_shmem_exit(release_backend_state, (Datum)0);
        exit_callback_registered = true;
    }
}

static Datum digest_datum(const uint8* bytes, Size length) {
    bytea* value = (bytea*)palloc(VARHDRSZ + length);
    SET_VARSIZE(value, VARHDRSZ + length);
    memcpy(VARDATA(value), bytes, length);
    return PointerGetDatum(value);
}

static void ensure_catalog_plans(void) {
    static const char generation_sql[] =
        "WITH inserted AS (INSERT INTO " LAPLACE_PG_SCHEMA
        ".perfcache_generation("
        "activation_epoch_id,epoch_fingerprint,manifest_fingerprint,"
        "encoded_manifest_fingerprint,encoded_manifest) "
        "VALUES($1,$2,$3,$4,$5) "
        "ON CONFLICT (activation_epoch_id,epoch_fingerprint) DO NOTHING "
        "RETURNING 1) SELECT 1 FROM inserted UNION ALL SELECT 1 FROM "
        LAPLACE_PG_SCHEMA ".perfcache_generation WHERE "
        "activation_epoch_id=$1 AND epoch_fingerprint=$2 AND "
        "manifest_fingerprint=$3 AND encoded_manifest_fingerprint=$4 AND "
        "encoded_manifest=$5 LIMIT 1";
    static const char update_sql[] =
        "WITH activated AS (UPDATE " LAPLACE_PG_SCHEMA
        ".perfcache_active_control SET "
        "sequence=$1,active_present=true,activation_epoch_id=$2,"
        "epoch_fingerprint=$3,manifest_fingerprint=$4,admission_receipt_id=$5 "
#if defined(LAPLACE_TEST_SKIP_PG_PERFCACHE_EXPECTED_EPOCH)
        "WHERE singleton AND $6=$6 AND $7=$7 AND $8=$8 AND $9=$9 "
#else
        "WHERE singleton AND sequence=$6 AND active_present=$7 "
        "AND (NOT $7 OR (activation_epoch_id=$8 AND epoch_fingerprint=$9)) "
#endif
        "RETURNING sequence,activation_epoch_id,epoch_fingerprint), "
        "recorded AS (INSERT INTO " LAPLACE_PG_SCHEMA
        ".perfcache_activation_event("
        "sequence,activation_epoch_id,epoch_fingerprint,admission_receipt_id,"
        "activation_transaction_id) "
        "SELECT sequence,activation_epoch_id,epoch_fingerprint,$5,$10 FROM activated "
        "ON CONFLICT (sequence) DO NOTHING RETURNING sequence) "
        "SELECT 1 FROM recorded UNION ALL SELECT 1 FROM " LAPLACE_PG_SCHEMA
        ".perfcache_activation_event activation_event "
        "JOIN activated USING (sequence) WHERE "
        "activation_event.activation_epoch_id=activated.activation_epoch_id AND "
        "activation_event.epoch_fingerprint=activated.epoch_fingerprint AND "
        "activation_event.admission_receipt_id=$5 AND "
        "activation_event.activation_transaction_id=$10 LIMIT 1";
    static const char select_sql[] =
        "SELECT sequence,active_present,activation_epoch_id,epoch_fingerprint,"
        "manifest_fingerprint,admission_receipt_id FROM " LAPLACE_PG_SCHEMA
        ".perfcache_active_control WHERE singleton";
    static const char manifest_select_sql[] =
        "SELECT encoded_manifest_fingerprint,encoded_manifest FROM "
        LAPLACE_PG_SCHEMA ".perfcache_generation WHERE "
        "activation_epoch_id=$1 AND epoch_fingerprint=$2 AND "
        "manifest_fingerprint=$3";
    static Oid generation_types[5] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID};
    static Oid update_types[10] = {
        INT8OID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        INT8OID, BOOLOID, BYTEAOID, BYTEAOID, XIDOID};
    static Oid manifest_select_types[3] = {BYTEAOID, BYTEAOID, BYTEAOID};
    if (perfcache_generation_plan == NULL) {
        perfcache_generation_plan =
            SPI_prepare(generation_sql, 5, generation_types);
        if (perfcache_generation_plan == NULL ||
            SPI_keepplan(perfcache_generation_plan) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("cannot prepare Laplace perfcache generation plan")));
        }
    }
    if (perfcache_active_update_plan == NULL) {
        perfcache_active_update_plan = SPI_prepare(update_sql, 10, update_types);
        if (perfcache_active_update_plan == NULL ||
            SPI_keepplan(perfcache_active_update_plan) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("cannot prepare Laplace perfcache active-epoch plan")));
        }
    }
    if (perfcache_active_select_plan == NULL) {
        perfcache_active_select_plan = SPI_prepare(select_sql, 0, NULL);
        if (perfcache_active_select_plan == NULL ||
            SPI_keepplan(perfcache_active_select_plan) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("cannot prepare Laplace perfcache active-epoch read plan")));
        }
    }
    if (perfcache_manifest_select_plan == NULL) {
        perfcache_manifest_select_plan =
            SPI_prepare(manifest_select_sql, 3, manifest_select_types);
        if (perfcache_manifest_select_plan == NULL ||
            SPI_keepplan(perfcache_manifest_select_plan) != 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INTERNAL_ERROR),
                     errmsg("cannot prepare Laplace perfcache manifest plan")));
        }
    }
}

static void read_catalog_binary(
    HeapTuple tuple,
    TupleDesc descriptor,
    int column,
    uint8* destination,
    Size expected_bytes) {
    bool is_null = false;
    Datum datum = SPI_getbinval(tuple, descriptor, column, &is_null);
    bytea* value;
    if (is_null) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace perfcache control row contains null binary state")));
    }
    value = DatumGetByteaPP(datum);
    if ((Size)VARSIZE_ANY_EXHDR(value) != expected_bytes) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace perfcache control row has invalid binary width")));
    }
    memcpy(destination, VARDATA_ANY(value), expected_bytes);
}

static laplace_pg_perfcache_status native_pin_epoch(
    const laplace_pg_perfcache_epoch* epoch,
    const laplace_digest256* manifest_fingerprint,
    laplace_perfcache_pin* pin) {
    laplace_perfcache_epoch native_epoch;
    laplace_perfcache_registry_status registry_status;
    Datum values[3];
    char nulls[3] = {' ', ' ', ' '};
    laplace_digest256 stored_encoded_fingerprint;
    laplace_digest256 decoded_encoded_fingerprint;
    laplace_perfcache_generation_manifest* manifest = NULL;
    const laplace_framework_context* context = NULL;
    const laplace_framework_stream_receipt* staged = NULL;
    const laplace_perfcache_generation_request* request = NULL;
    laplace_perfcache_prepared_generation* prepared = NULL;
    laplace_perfcache_generation_receipt receipt;
    laplace_digest256 decoded_manifest_fingerprint;
    bool is_null = false;
    Datum manifest_datum;
    bytea* manifest_bytes;
    int result;
    laplace_pg_perfcache_status status = ensure_native_registry();
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        return status;
    }
    native_epoch.activation_epoch_id = epoch->activation_epoch_id;
    native_epoch.epoch_fingerprint = epoch->epoch_fingerprint;
    registry_status = laplace_perfcache_registry_pin_epoch(
        perfcache_native_registry, &native_epoch, pin);
    if (registry_status == LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_PG_PERFCACHE_OK;
    }
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_EPOCH_MISMATCH) {
        return LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        return LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    }
    ensure_catalog_plans();
    values[0] = digest_datum(
        epoch->activation_epoch_id.bytes,
        sizeof(epoch->activation_epoch_id.bytes));
    values[1] = digest_datum(
        epoch->epoch_fingerprint.bytes,
        sizeof(epoch->epoch_fingerprint.bytes));
    values[2] = digest_datum(
        manifest_fingerprint->bytes, sizeof(manifest_fingerprint->bytes));
    result = SPI_execute_plan(
        perfcache_manifest_select_plan, values, nulls, true, 1);
    perfcache_manifest_select_count += 1u;
    if (result != SPI_OK_SELECT || SPI_processed != 1u) {
        SPI_finish();
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    read_catalog_binary(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
        stored_encoded_fingerprint.bytes,
        sizeof(stored_encoded_fingerprint.bytes));
    manifest_datum = SPI_getbinval(
        SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2, &is_null);
    if (is_null) {
        SPI_finish();
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    manifest_bytes = DatumGetByteaPP(manifest_datum);
    registry_status = laplace_perfcache_generation_manifest_open(
        (const uint8*)VARDATA_ANY(manifest_bytes),
        (size_t)VARSIZE_ANY_EXHDR(manifest_bytes), &manifest,
        &decoded_encoded_fingerprint);
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK || manifest == NULL ||
        memcmp(stored_encoded_fingerprint.bytes,
               decoded_encoded_fingerprint.bytes,
               sizeof(stored_encoded_fingerprint.bytes)) != 0) {
        SPI_finish();
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    registry_status = laplace_perfcache_generation_manifest_view(
        manifest, &context, &staged, &request);
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK ||
        !manifest_paths_are_admitted(request) ||
        memcmp(request->activation_epoch_id.bytes,
               epoch->activation_epoch_id.bytes,
               sizeof(epoch->activation_epoch_id.bytes)) != 0 ||
        memcmp(request->epoch_fingerprint.bytes,
               epoch->epoch_fingerprint.bytes,
               sizeof(epoch->epoch_fingerprint.bytes)) != 0 ||
        laplace_perfcache_generation_manifest_fingerprint(
            request, &decoded_manifest_fingerprint) !=
            LAPLACE_PERFCACHE_REGISTRY_OK ||
        memcmp(decoded_manifest_fingerprint.bytes,
               manifest_fingerprint->bytes,
               sizeof(decoded_manifest_fingerprint.bytes)) != 0) {
        laplace_perfcache_generation_manifest_close(manifest);
        SPI_finish();
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    memset(&receipt, 0, sizeof(receipt));
    registry_status = laplace_perfcache_registry_prepare(
        perfcache_native_registry, context, staged, &perfcache_file_provider,
        request, &prepared, &receipt);
    if (registry_status == LAPLACE_PERFCACHE_REGISTRY_OK) {
        registry_status = laplace_perfcache_registry_materialize_prepared(
            perfcache_native_registry, &prepared, &receipt);
    }
    if (prepared != NULL) {
        laplace_perfcache_registry_discard_prepared(&prepared);
    }
    laplace_perfcache_generation_manifest_close(manifest);
    SPI_finish();
    if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    perfcache_manifest_load_count += 1u;
    registry_status = laplace_perfcache_registry_pin_epoch(
        perfcache_native_registry, &native_epoch, pin);
    return registry_status == LAPLACE_PERFCACHE_REGISTRY_OK
        ? LAPLACE_PG_PERFCACHE_OK
        : LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
}

static bool install_active_locked(
    uint64 sequence,
    uint32 next_index,
    const laplace_pg_perfcache_epoch* epoch,
    const laplace_digest256* manifest_fingerprint,
    const laplace_digest256* admission_receipt_id) {
    laplace_pg_perfcache_generation_slot* generations = perfcache_generations();
    uint32 previous_index = perfcache_shared->active_generation_index;
    if (next_index >= perfcache_shared->generation_capacity ||
        !epoch_equal(&generations[next_index].epoch, epoch) ||
        memcmp(generations[next_index].manifest_fingerprint.bytes,
               manifest_fingerprint->bytes,
               sizeof(manifest_fingerprint->bytes)) != 0) {
        return false;
    }
    generations[next_index].epoch = *epoch;
    generations[next_index].manifest_fingerprint = *manifest_fingerprint;
    generations[next_index].admission_receipt_id = *admission_receipt_id;
    generations[next_index].sequence = sequence;
    generations[next_index].state = LAPLACE_PG_PERFCACHE_GENERATION_ACTIVE;
    perfcache_shared->active_generation_index = next_index;
    if (previous_index != LAPLACE_PG_PERFCACHE_NO_GENERATION &&
        previous_index != next_index &&
        previous_index < perfcache_shared->generation_capacity) {
        generations[previous_index].state =
            LAPLACE_PG_PERFCACHE_GENERATION_RETIRED;
    }
    collect_retired_locked();
    return true;
}

static bool install_reconciled_active_locked(
    uint64 sequence,
    const laplace_pg_perfcache_epoch* epoch,
    const laplace_digest256* manifest_fingerprint,
    const laplace_digest256* admission_receipt_id) {
    uint32 next_index = find_generation_locked(epoch);
    if (next_index == LAPLACE_PG_PERFCACHE_NO_GENERATION) {
        next_index = allocate_generation_locked();
        if (next_index == LAPLACE_PG_PERFCACHE_NO_GENERATION) {
            return false;
        }
        perfcache_generations()[next_index].epoch = *epoch;
        perfcache_generations()[next_index].manifest_fingerprint =
            *manifest_fingerprint;
        perfcache_generations()[next_index].state =
            LAPLACE_PG_PERFCACHE_GENERATION_PREPARED;
    }
    return install_active_locked(sequence, next_index, epoch,
                                 manifest_fingerprint,
                                 admission_receipt_id);
}

static laplace_pg_perfcache_status resolve_committing_reservation(void) {
    laplace_pg_perfcache_reservation observed;
    bool committed;
    bool installed = true;
    memset(&observed, 0, sizeof(observed));
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    if (perfcache_shared->reservation.present != 0u &&
        perfcache_shared->reservation.state ==
            LAPLACE_PG_PERFCACHE_RESERVATION_COMMITTING) {
        observed = perfcache_shared->reservation;
    }
    LWLockRelease(&perfcache_shared->lock);
    if (observed.present == 0u ||
        TransactionIdIsInProgress(observed.transaction_id)) {
        return LAPLACE_PG_PERFCACHE_OK;
    }
    committed = TransactionIdDidCommit(observed.transaction_id);
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    if (perfcache_shared->reservation.present != 0u &&
        perfcache_shared->reservation.state ==
            LAPLACE_PG_PERFCACHE_RESERVATION_COMMITTING &&
        perfcache_shared->reservation.serial == observed.serial &&
        perfcache_shared->reservation.transaction_id ==
            observed.transaction_id) {
        if (committed) {
            installed = install_active_locked(
                observed.next_sequence,
                observed.candidate_generation_index,
                &observed.next_epoch,
                &observed.manifest_fingerprint,
                &observed.admission_receipt_id);
        } else {
            discard_reserved_candidate_locked(&observed);
        }
        if (installed) {
            memset(&perfcache_shared->reservation, 0,
                   sizeof(perfcache_shared->reservation));
        }
    }
    LWLockRelease(&perfcache_shared->lock);
    return installed ? LAPLACE_PG_PERFCACHE_OK
                     : LAPLACE_PG_PERFCACHE_CAPACITY_EXHAUSTED;
}

static laplace_pg_perfcache_status ensure_control_ready(void) {
    laplace_pg_perfcache_status status;
    attach_shared_state();
    if (!perfcache_backend_synchronized) {
        synchronize_from_catalog();
    }
    status = resolve_committing_reservation();
    return status;
}

static void install_no_active_locked(void) {
    laplace_pg_perfcache_generation_slot* generations = perfcache_generations();
    uint32 previous_index = perfcache_shared->active_generation_index;
    perfcache_shared->active_generation_index =
        LAPLACE_PG_PERFCACHE_NO_GENERATION;
    if (previous_index != LAPLACE_PG_PERFCACHE_NO_GENERATION &&
        previous_index < perfcache_shared->generation_capacity) {
        generations[previous_index].state =
            LAPLACE_PG_PERFCACHE_GENERATION_RETIRED;
    }
    collect_retired_locked();
}

static void synchronize_from_catalog(void) {
    laplace_pg_perfcache_epoch epoch;
    laplace_digest256 manifest_fingerprint;
    laplace_digest256 admission_receipt_id;
    uint64 sequence;
    bool active_present;
    bool is_null = false;
    Datum datum;
    int result;
    bool installed = true;
    bool reset_native = false;
    if (perfcache_pending.present != 0u) {
        return;
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("cannot connect SPI for Laplace perfcache synchronization")));
    }
    ensure_catalog_plans();
    result = SPI_execute_plan(perfcache_active_select_plan, NULL, NULL, true, 1);
    perfcache_catalog_select_count += 1u;
    if (result != SPI_OK_SELECT || SPI_processed != 1u) {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace perfcache active-control singleton is missing")));
    }
    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                          &is_null);
    if (is_null || DatumGetInt64(datum) < 0) {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace perfcache active-control sequence is invalid")));
    }
    sequence = (uint64)DatumGetInt64(datum);
    datum = SPI_getbinval(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                          &is_null);
    if (is_null) {
        SPI_finish();
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace perfcache active-control presence is null")));
    }
    active_present = DatumGetBool(datum);
    read_catalog_binary(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
                        epoch.activation_epoch_id.bytes,
                        sizeof(epoch.activation_epoch_id.bytes));
    read_catalog_binary(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 4,
                        epoch.epoch_fingerprint.bytes,
                        sizeof(epoch.epoch_fingerprint.bytes));
    read_catalog_binary(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 5,
                        manifest_fingerprint.bytes,
                        sizeof(manifest_fingerprint.bytes));
    read_catalog_binary(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 6,
                        admission_receipt_id.bytes,
                        sizeof(admission_receipt_id.bytes));
    SPI_finish();
    attach_shared_state();
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    reclaim_stale_owners_locked();
    if (perfcache_shared->reservation.present == 0u && !active_present) {
        install_no_active_locked();
        reset_native = true;
    } else if (perfcache_shared->reservation.present == 0u) {
        uint64 current_sequence = 0u;
        if (perfcache_shared->active_generation_index !=
            LAPLACE_PG_PERFCACHE_NO_GENERATION) {
            current_sequence = perfcache_generations()[
                perfcache_shared->active_generation_index].sequence;
        }
        if (perfcache_shared->active_generation_index ==
                LAPLACE_PG_PERFCACHE_NO_GENERATION ||
            sequence > current_sequence) {
            installed = install_reconciled_active_locked(
                sequence, &epoch, &manifest_fingerprint,
                &admission_receipt_id);
        }
    }
    LWLockRelease(&perfcache_shared->lock);
    if (!installed) {
        ereport(ERROR,
                (errcode(ERRCODE_OUT_OF_MEMORY),
                 errmsg("Laplace perfcache shared generation capacity is exhausted")));
    }
    if (reset_native && !reset_native_registry()) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_IN_USE),
                 errmsg("Laplace native perfcache registry is still pinned during durable reset")));
    }
    perfcache_backend_synchronized = true;
}

static laplace_pg_perfcache_status persist_admission(
    uint64 expected_sequence,
    uint32 has_expected_epoch,
    const laplace_pg_perfcache_epoch* expected_epoch,
    const laplace_pg_perfcache_epoch* next_epoch,
    const laplace_digest256* manifest_fingerprint,
    const laplace_digest256* encoded_manifest_fingerprint,
    const uint8* encoded_manifest,
    Size encoded_manifest_bytes,
    TransactionId activation_transaction_id,
    const laplace_digest256* admission_receipt_id) {
    Datum generation_values[5];
    Datum update_values[10];
    char generation_nulls[5] = {' ', ' ', ' ', ' ', ' '};
    char update_nulls[10] = {
        ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
    int result;
    if (expected_sequence >= (uint64)PG_INT64_MAX) {
        return LAPLACE_PG_PERFCACHE_INVALID_ARGUMENT;
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        return LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    }
    ensure_catalog_plans();
    generation_values[0] = digest_datum(
        next_epoch->activation_epoch_id.bytes,
        sizeof(next_epoch->activation_epoch_id.bytes));
    generation_values[1] = digest_datum(
        next_epoch->epoch_fingerprint.bytes,
        sizeof(next_epoch->epoch_fingerprint.bytes));
    generation_values[2] = digest_datum(
        manifest_fingerprint->bytes, sizeof(manifest_fingerprint->bytes));
    generation_values[3] = digest_datum(
        encoded_manifest_fingerprint->bytes,
        sizeof(encoded_manifest_fingerprint->bytes));
    generation_values[4] = digest_datum(
        encoded_manifest, encoded_manifest_bytes);
    result = SPI_execute_plan(perfcache_generation_plan, generation_values,
                              generation_nulls, false, 1);
    if (result != SPI_OK_SELECT || SPI_processed != 1u) {
        SPI_finish();
        return LAPLACE_PG_PERFCACHE_INTERNAL_ERROR;
    }
    update_values[0] = Int64GetDatum((int64)(expected_sequence + 1u));
    update_values[1] = generation_values[0];
    update_values[2] = generation_values[1];
    update_values[3] = generation_values[2];
    update_values[4] = digest_datum(
        admission_receipt_id->bytes, sizeof(admission_receipt_id->bytes));
    update_values[5] = Int64GetDatum((int64)expected_sequence);
    update_values[6] = BoolGetDatum(has_expected_epoch != 0u);
    update_values[7] = digest_datum(
        expected_epoch != NULL ? expected_epoch->activation_epoch_id.bytes
                               : next_epoch->activation_epoch_id.bytes,
        sizeof(next_epoch->activation_epoch_id.bytes));
    update_values[8] = digest_datum(
        expected_epoch != NULL ? expected_epoch->epoch_fingerprint.bytes
                               : next_epoch->epoch_fingerprint.bytes,
        sizeof(next_epoch->epoch_fingerprint.bytes));
    update_values[9] = TransactionIdGetDatum(activation_transaction_id);
    result = SPI_execute_plan(perfcache_active_update_plan, update_values,
                              update_nulls, false, 1);
    if (result != SPI_OK_SELECT || SPI_processed != 1u) {
        SPI_finish();
        return LAPLACE_PG_PERFCACHE_EPOCH_MISMATCH;
    }
    SPI_finish();
    return LAPLACE_PG_PERFCACHE_OK;
}

static void abort_pending_locked(void) {
    if (perfcache_pending.present != 0u &&
        perfcache_shared->reservation.present != 0u &&
        perfcache_shared->reservation.serial ==
            perfcache_pending.reservation_serial &&
        perfcache_shared->reservation.owner_proc_number == MyProcNumber &&
        perfcache_shared->reservation.owner_pid == MyProcPid &&
        perfcache_shared->reservation.owner_start_timestamp ==
            (int64)MyStartTimestamp) {
        uint32 candidate =
            perfcache_shared->reservation.candidate_generation_index;
        if (perfcache_shared->reservation.candidate_was_new != 0u &&
            candidate < perfcache_shared->generation_capacity &&
            perfcache_generations()[candidate].state ==
                LAPLACE_PG_PERFCACHE_GENERATION_PREPARED) {
            memset(&perfcache_generations()[candidate], 0,
                   sizeof(perfcache_generations()[candidate]));
        }
        memset(&perfcache_shared->reservation, 0,
               sizeof(perfcache_shared->reservation));
    }
    memset(&perfcache_pending, 0, sizeof(perfcache_pending));
}

static bool pending_reservation_valid_locked(void) {
    return perfcache_pending.present != 0u &&
        perfcache_shared->reservation.present != 0u &&
        perfcache_shared->reservation.serial ==
            perfcache_pending.reservation_serial &&
        perfcache_shared->reservation.owner_proc_number == MyProcNumber &&
        perfcache_shared->reservation.owner_pid == MyProcPid &&
        perfcache_shared->reservation.owner_start_timestamp ==
            (int64)MyStartTimestamp &&
        perfcache_shared->reservation.candidate_generation_index ==
            perfcache_pending.candidate_generation_index &&
        epoch_equal(&perfcache_shared->reservation.next_epoch,
                    &perfcache_pending.next_epoch);
}

static bool commit_pending_locked(void) {
    if (!pending_reservation_valid_locked()) {
        abort_pending_locked();
        return false;
    }
    if (!install_active_locked(
            perfcache_pending.next_sequence,
            perfcache_pending.candidate_generation_index,
            &perfcache_pending.next_epoch,
            &perfcache_pending.manifest_fingerprint,
            &perfcache_pending.admission_receipt_id)) {
        return false;
    }
    memset(&perfcache_shared->reservation, 0,
           sizeof(perfcache_shared->reservation));
    memset(&perfcache_pending, 0, sizeof(perfcache_pending));
    collect_retired_locked();
    return true;
}

static void perfcache_xact_callback(XactEvent event, void* argument) {
    (void)argument;
    if (perfcache_pending.present == 0u || perfcache_shared == NULL) {
        return;
    }
    if (event == XACT_EVENT_PRE_PREPARE) {
        ereport(ERROR,
                (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                 errmsg("prepared transactions cannot carry a pending Laplace perfcache activation")));
    }
    if (event == XACT_EVENT_PRE_COMMIT) {
        bool valid;
        if (perfcache_native_pending.present == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                     errmsg("Laplace native perfcache activation is missing before commit")));
        }
        if (laplace_perfcache_activation_commit_ready(
                perfcache_native_pending.activation) !=
                LAPLACE_PERFCACHE_REGISTRY_OK ||
            laplace_framework_admitted_stream_validate(
                &perfcache_native_pending.context,
                &perfcache_native_pending.request,
                &perfcache_native_pending.provider,
                &perfcache_native_pending.receipt) !=
                LAPLACE_FRAMEWORK_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                     errmsg("Laplace native perfcache activation is not commit-ready")));
        }
        LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
        reclaim_stale_owners_locked();
        valid = pending_reservation_valid_locked();
        LWLockRelease(&perfcache_shared->lock);
        if (!valid) {
            ereport(ERROR,
                    (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                     errmsg("Laplace perfcache activation reservation was lost before commit")));
        }
        LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
        if (pending_reservation_valid_locked()) {
            perfcache_shared->reservation.state =
                LAPLACE_PG_PERFCACHE_RESERVATION_COMMITTING;
        }
        LWLockRelease(&perfcache_shared->lock);
    } else if (event == XACT_EVENT_COMMIT) {
        bool committed;
        bool native_committed = commit_native_pending();
        if (!native_committed) {
            memset(&perfcache_pending, 0, sizeof(perfcache_pending));
            return;
        }
        LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
        committed = commit_pending_locked();
        LWLockRelease(&perfcache_shared->lock);
        if (!committed) {
            /*
             * The durable CAS has committed.  Retaining COMMITTING state is
             * safer than publishing the wrong epoch; a subsequent helper can
             * complete the exact reserved handoff.
             */
            memset(&perfcache_pending, 0, sizeof(perfcache_pending));
        }
    } else if (event == XACT_EVENT_ABORT) {
        LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
        abort_pending_locked();
        LWLockRelease(&perfcache_shared->lock);
        abort_native_pending();
    }
}

static void perfcache_subxact_callback(
    SubXactEvent event,
    SubTransactionId subtransaction_id,
    SubTransactionId parent_subtransaction_id,
    void* argument) {
    (void)argument;
    if (perfcache_pending.present == 0u ||
        perfcache_pending.subtransaction_id != subtransaction_id ||
        perfcache_shared == NULL) {
        return;
    }
    if (event == SUBXACT_EVENT_COMMIT_SUB) {
        perfcache_pending.subtransaction_id = parent_subtransaction_id;
    } else if (event == SUBXACT_EVENT_ABORT_SUB) {
        LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
        abort_pending_locked();
        LWLockRelease(&perfcache_shared->lock);
        abort_native_pending();
    }
}

void laplace_pg_perfcache_initialize(void) {
#if !defined(LAPLACE_TEST_SKIP_PG_PERFCACHE_EXPECTED_EPOCH) && \
    !defined(LAPLACE_TEST_BLIND_CONFLICT)
    if (!perfcache_guc_registered) {
        DefineCustomStringVariable(
            "laplace.perfcache_root",
            "Absolute root containing content-addressed Laplace perfcache artifacts.",
            NULL,
            &perfcache_root,
            NULL,
            PGC_SUSET,
            GUC_SUPERUSER_ONLY,
            NULL,
            NULL,
            NULL);
        perfcache_guc_registered = true;
    }
#endif
    if (!invalidation_callback_registered) {
        CacheRegisterRelcacheCallback(
            perfcache_relcache_invalidation, (Datum)0);
        invalidation_callback_registered = true;
    }
    if (!callbacks_registered) {
        RegisterXactCallback(perfcache_xact_callback, NULL);
        RegisterSubXactCallback(perfcache_subxact_callback, NULL);
        callbacks_registered = true;
    }
}

static laplace_pg_perfcache_status admit_epoch_metadata(
    uint64 expected_sequence,
    uint32 has_expected_epoch,
    const laplace_pg_perfcache_epoch* expected_epoch,
    const laplace_pg_perfcache_epoch* next_epoch,
    const laplace_digest256* manifest_fingerprint,
    const laplace_digest256* encoded_manifest_fingerprint,
    const uint8* encoded_manifest,
    Size encoded_manifest_bytes,
    const laplace_digest256* admission_receipt_id) {
    laplace_pg_perfcache_reservation* reservation;
    bool expected_matches;
    laplace_pg_perfcache_status persistence_status;
    TransactionId top_transaction_id;
    uint32 candidate_generation_index;
    uint32 candidate_was_new = 0u;
    if (has_expected_epoch > 1u || next_epoch == NULL ||
        manifest_fingerprint == NULL ||
        encoded_manifest_fingerprint == NULL || encoded_manifest == NULL ||
        encoded_manifest_bytes == 0u || admission_receipt_id == NULL ||
        expected_sequence >= (uint64)PG_INT64_MAX ||
        ((has_expected_epoch != 0u) != (expected_epoch != NULL)) ||
        IsInParallelMode() || IsParallelWorker()) {
        return LAPLACE_PG_PERFCACHE_INVALID_ARGUMENT;
    }
    laplace_pg_perfcache_initialize();
    persistence_status = ensure_control_ready();
    if (persistence_status != LAPLACE_PG_PERFCACHE_OK) {
        return persistence_status;
    }
    if (perfcache_pending.present != 0u) {
        return LAPLACE_PG_PERFCACHE_ALREADY_RESERVED;
    }
    top_transaction_id = GetTopTransactionId();
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    reclaim_stale_owners_locked();
    reservation = &perfcache_shared->reservation;
    if (reservation->present != 0u) {
        LWLockRelease(&perfcache_shared->lock);
        return LAPLACE_PG_PERFCACHE_ALREADY_RESERVED;
    }
#if defined(LAPLACE_TEST_SKIP_PG_PERFCACHE_EXPECTED_EPOCH)
    expected_matches = true;
#else
    expected_matches = has_expected_epoch != 0u
        ? perfcache_shared->active_generation_index !=
                LAPLACE_PG_PERFCACHE_NO_GENERATION &&
            perfcache_generations()[
                perfcache_shared->active_generation_index].sequence ==
                expected_sequence &&
            epoch_equal(
                &perfcache_generations()[
                    perfcache_shared->active_generation_index].epoch,
                expected_epoch)
        : perfcache_shared->active_generation_index ==
                LAPLACE_PG_PERFCACHE_NO_GENERATION &&
            expected_sequence == 0u;
#endif
    if (!expected_matches) {
        LWLockRelease(&perfcache_shared->lock);
        return LAPLACE_PG_PERFCACHE_EPOCH_MISMATCH;
    }
    candidate_generation_index = find_generation_locked(next_epoch);
    if (candidate_generation_index == LAPLACE_PG_PERFCACHE_NO_GENERATION) {
        candidate_generation_index = allocate_generation_locked();
        if (candidate_generation_index == LAPLACE_PG_PERFCACHE_NO_GENERATION) {
            LWLockRelease(&perfcache_shared->lock);
            return LAPLACE_PG_PERFCACHE_CAPACITY_EXHAUSTED;
        }
        perfcache_generations()[candidate_generation_index].epoch = *next_epoch;
        perfcache_generations()[candidate_generation_index].manifest_fingerprint =
            *manifest_fingerprint;
        perfcache_generations()[candidate_generation_index].state =
            LAPLACE_PG_PERFCACHE_GENERATION_PREPARED;
        candidate_was_new = 1u;
    } else if (memcmp(
                   perfcache_generations()[candidate_generation_index]
                       .manifest_fingerprint.bytes,
                   manifest_fingerprint->bytes,
                   sizeof(manifest_fingerprint->bytes)) != 0) {
        LWLockRelease(&perfcache_shared->lock);
        return LAPLACE_PG_PERFCACHE_GENERATION_MISMATCH;
    }
    memset(reservation, 0, sizeof(*reservation));
    if (has_expected_epoch != 0u) {
        reservation->expected_epoch = *expected_epoch;
    }
    reservation->next_epoch = *next_epoch;
    reservation->manifest_fingerprint = *manifest_fingerprint;
    reservation->admission_receipt_id = *admission_receipt_id;
    reservation->serial = perfcache_shared->next_reservation_serial++;
    reservation->expected_sequence = expected_sequence;
    reservation->next_sequence = expected_sequence + 1u;
    reservation->transaction_id = top_transaction_id;
    reservation->owner_proc_number = MyProcNumber;
    reservation->owner_pid = MyProcPid;
    reservation->owner_start_timestamp = (int64)MyStartTimestamp;
    reservation->candidate_generation_index = candidate_generation_index;
    reservation->candidate_was_new = candidate_was_new;
    reservation->has_expected_epoch = has_expected_epoch;
    reservation->state = LAPLACE_PG_PERFCACHE_RESERVATION_PREPARED;
    reservation->present = 1u;
    memset(&perfcache_pending, 0, sizeof(perfcache_pending));
    if (has_expected_epoch != 0u) {
        perfcache_pending.expected_epoch = *expected_epoch;
    }
    perfcache_pending.next_epoch = *next_epoch;
    perfcache_pending.manifest_fingerprint = *manifest_fingerprint;
    perfcache_pending.admission_receipt_id = *admission_receipt_id;
    perfcache_pending.reservation_serial = reservation->serial;
    perfcache_pending.expected_sequence = expected_sequence;
    perfcache_pending.next_sequence = expected_sequence + 1u;
    perfcache_pending.transaction_id = reservation->transaction_id;
    perfcache_pending.subtransaction_id = GetCurrentSubTransactionId();
    perfcache_pending.candidate_generation_index =
        candidate_generation_index;
    perfcache_pending.candidate_was_new = candidate_was_new;
    perfcache_pending.has_expected_epoch = has_expected_epoch;
    perfcache_pending.present = 1u;
    LWLockRelease(&perfcache_shared->lock);
    persistence_status = persist_admission(
        expected_sequence, has_expected_epoch, expected_epoch, next_epoch,
        manifest_fingerprint, encoded_manifest_fingerprint,
        encoded_manifest, encoded_manifest_bytes, top_transaction_id,
        admission_receipt_id);
    if (persistence_status != LAPLACE_PG_PERFCACHE_OK) {
        LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
        abort_pending_locked();
        LWLockRelease(&perfcache_shared->lock);
    }
    return persistence_status;
}

laplace_pg_perfcache_status laplace_pg_perfcache_admit(
    uint64 expected_sequence,
    uint32 has_expected_epoch,
    const laplace_pg_perfcache_epoch* expected_epoch,
    const uint8* encoded_manifest,
    Size encoded_manifest_bytes) {
    laplace_pg_perfcache_epoch next_epoch;
    laplace_digest256 manifest_fingerprint;
    laplace_digest256 encoded_manifest_fingerprint;
    laplace_digest256 admission_receipt_id;
    laplace_pg_perfcache_status status;
    if (has_expected_epoch > 1u ||
        ((has_expected_epoch != 0u) != (expected_epoch != NULL))) {
        return LAPLACE_PG_PERFCACHE_INVALID_ARGUMENT;
    }
    status = ensure_control_ready();
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        return status;
    }
    memset(&next_epoch, 0, sizeof(next_epoch));
    memset(&manifest_fingerprint, 0, sizeof(manifest_fingerprint));
    memset(&encoded_manifest_fingerprint, 0,
           sizeof(encoded_manifest_fingerprint));
    memset(&admission_receipt_id, 0, sizeof(admission_receipt_id));
    status = prepare_native_admission(
        encoded_manifest, encoded_manifest_bytes, &next_epoch,
        &manifest_fingerprint, &encoded_manifest_fingerprint,
        &admission_receipt_id);
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        return status;
    }
    if (has_expected_epoch != 0u &&
        memcmp(
            perfcache_native_pending
                .context.epochs[LAPLACE_FRAMEWORK_EPOCH_PERFCACHE].bytes,
            expected_epoch->epoch_fingerprint.bytes,
            sizeof(expected_epoch->epoch_fingerprint.bytes)) != 0) {
        abort_native_pending();
        return LAPLACE_PG_PERFCACHE_EPOCH_MISMATCH;
    }
    status = admit_epoch_metadata(
        expected_sequence, has_expected_epoch, expected_epoch, &next_epoch,
        &manifest_fingerprint, &encoded_manifest_fingerprint,
        encoded_manifest, encoded_manifest_bytes, &admission_receipt_id);
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        abort_native_pending();
    }
    return status;
}

static void release_pin_internal(laplace_pg_perfcache_pin* pin) {
    laplace_pg_perfcache_owner_slot* owner;
    laplace_pg_perfcache_generation_slot* generation;
    if (pin == NULL || pin->held == 0u || perfcache_shared == NULL ||
        pin->owner_proc_number != MyProcNumber ||
        pin->owner_pid != MyProcPid ||
        pin->generation_index >= perfcache_shared->generation_capacity) {
        return;
    }
    if (pin->native_pin.registry != NULL) {
        (void)laplace_perfcache_pin_release(&pin->native_pin);
    }
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    owner = &perfcache_owners()[pin->owner_proc_number];
    generation = &perfcache_generations()[pin->generation_index];
    if (owner->pid == pin->owner_pid && owner->pin_depth != 0u &&
        owner->generation_index == pin->generation_index &&
        epoch_equal(&owner->epoch, &pin->epoch)) {
        owner->pin_depth -= 1u;
        if (generation->reader_count != 0u) {
            generation->reader_count -= 1u;
        }
        if (owner->pin_depth == 0u) {
            memset(owner, 0, sizeof(*owner));
            owner->generation_index = LAPLACE_PG_PERFCACHE_NO_GENERATION;
        }
    }
    collect_retired_locked();
    LWLockRelease(&perfcache_shared->lock);
    pin->held = 0u;
}

static void release_pin_resource(Datum resource) {
    laplace_pg_perfcache_pin* pin =
        (laplace_pg_perfcache_pin*)DatumGetPointer(resource);
    release_pin_internal(pin);
    pfree(pin);
}

static const ResourceOwnerDesc perfcache_pin_resource = {
    .name = "Laplace perfcache generation pin",
    .release_phase = RESOURCE_RELEASE_BEFORE_LOCKS,
    .release_priority = RELEASE_PRIO_FIRST,
    .ReleaseResource = release_pin_resource,
    .DebugPrint = NULL
};

static laplace_pg_perfcache_status pin_generation(
    bool exact_retained_epoch,
    uint32 has_expected_epoch,
    const laplace_pg_perfcache_epoch* expected_epoch,
    laplace_pg_perfcache_pin** pin) {
    laplace_pg_perfcache_owner_slot* owner;
    laplace_pg_perfcache_generation_slot* generation;
    laplace_pg_perfcache_pin* result;
    laplace_pg_perfcache_status status;
    uint32 owner_index;
    uint32 generation_index;
    if (has_expected_epoch > 1u || pin == NULL ||
        ((has_expected_epoch != 0u) != (expected_epoch != NULL)) ||
        MyProcNumber < 0 || MyProcNumber >= MaxBackends) {
        return LAPLACE_PG_PERFCACHE_INVALID_ARGUMENT;
    }
    *pin = NULL;
    laplace_pg_perfcache_initialize();
    status = ensure_control_ready();
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        return status;
    }
    ResourceOwnerEnlarge(CurrentResourceOwner);
    result = (laplace_pg_perfcache_pin*)
        MemoryContextAllocZero(TopMemoryContext, sizeof(*result));
    result->resource_owner = CurrentResourceOwner;
    ResourceOwnerRemember(CurrentResourceOwner, PointerGetDatum(result),
                          &perfcache_pin_resource);
    owner_index = (uint32)MyProcNumber;
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    reclaim_stale_owners_locked();
    generation_index = exact_retained_epoch
        ? find_generation_locked(expected_epoch)
        : perfcache_shared->active_generation_index;
    if (generation_index == LAPLACE_PG_PERFCACHE_NO_GENERATION) {
        LWLockRelease(&perfcache_shared->lock);
        ResourceOwnerForget(CurrentResourceOwner, PointerGetDatum(result),
                            &perfcache_pin_resource);
        pfree(result);
        return exact_retained_epoch ? LAPLACE_PG_PERFCACHE_EPOCH_MISMATCH
                                    : LAPLACE_PG_PERFCACHE_NO_ACTIVE_EPOCH;
    }
    generation = &perfcache_generations()[generation_index];
    if ((generation->state != LAPLACE_PG_PERFCACHE_GENERATION_ACTIVE &&
         generation->state != LAPLACE_PG_PERFCACHE_GENERATION_RETIRED) ||
        (has_expected_epoch != 0u &&
         !epoch_equal(&generation->epoch, expected_epoch))) {
        LWLockRelease(&perfcache_shared->lock);
        ResourceOwnerForget(CurrentResourceOwner, PointerGetDatum(result),
                            &perfcache_pin_resource);
        pfree(result);
        return LAPLACE_PG_PERFCACHE_EPOCH_MISMATCH;
    }
    owner = &perfcache_owners()[owner_index];
    if (owner->pin_depth != 0u &&
        (owner->pid != MyProcPid ||
         owner->generation_index != generation_index ||
         !epoch_equal(&owner->epoch, &generation->epoch))) {
        LWLockRelease(&perfcache_shared->lock);
        ResourceOwnerForget(CurrentResourceOwner, PointerGetDatum(result),
                            &perfcache_pin_resource);
        pfree(result);
        return LAPLACE_PG_PERFCACHE_ALREADY_PINNED;
    }
    if (owner->pin_depth == 0u) {
        memset(owner, 0, sizeof(*owner));
        owner->epoch = generation->epoch;
        owner->pid = MyProcPid;
        owner->backend_start_timestamp = (int64)MyStartTimestamp;
        owner->generation_index = generation_index;
    }
    owner->pin_depth += 1u;
    generation->reader_count += 1u;
    result->epoch = generation->epoch;
    result->manifest_fingerprint = generation->manifest_fingerprint;
    result->owner_proc_number = MyProcNumber;
    result->owner_pid = MyProcPid;
    result->generation_index = generation_index;
    result->held = 1u;
    LWLockRelease(&perfcache_shared->lock);
    status = native_pin_epoch(
        &result->epoch, &result->manifest_fingerprint,
        &result->native_pin);
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        release_pin_internal(result);
        ResourceOwnerForget(CurrentResourceOwner, PointerGetDatum(result),
                            &perfcache_pin_resource);
        pfree(result);
        return status;
    }
    *pin = result;
    return LAPLACE_PG_PERFCACHE_OK;
}

laplace_pg_perfcache_status laplace_pg_perfcache_pin_active(
    uint32 has_expected_epoch,
    const laplace_pg_perfcache_epoch* expected_epoch,
    laplace_pg_perfcache_pin** pin) {
    return pin_generation(false, has_expected_epoch, expected_epoch, pin);
}

laplace_pg_perfcache_status laplace_pg_perfcache_pin_epoch(
    const laplace_pg_perfcache_epoch* expected_epoch,
    laplace_pg_perfcache_pin** pin) {
    if (expected_epoch == NULL) {
        return LAPLACE_PG_PERFCACHE_INVALID_ARGUMENT;
    }
    return pin_generation(true, 1u, expected_epoch, pin);
}

void laplace_pg_perfcache_pin_release(laplace_pg_perfcache_pin** pin) {
    laplace_pg_perfcache_pin* owned;
    if (pin == NULL || *pin == NULL) {
        return;
    }
    owned = *pin;
    *pin = NULL;
    if (owned->resource_owner != NULL) {
        ResourceOwnerForget((ResourceOwner)owned->resource_owner,
                            PointerGetDatum(owned),
                            &perfcache_pin_resource);
    }
    release_pin_internal(owned);
    pfree(owned);
}

laplace_pg_perfcache_status laplace_pg_perfcache_snapshot_get(
    laplace_pg_perfcache_snapshot* snapshot) {
    laplace_pg_perfcache_generation_slot* generations;
    uint32 index;
    if (snapshot == NULL) {
        return LAPLACE_PG_PERFCACHE_INVALID_ARGUMENT;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    laplace_pg_perfcache_initialize();
    {
        laplace_pg_perfcache_status status = ensure_control_ready();
        if (status != LAPLACE_PG_PERFCACHE_OK) {
            return status;
        }
    }
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    reclaim_stale_owners_locked();
    generations = perfcache_generations();
    if (perfcache_shared->active_generation_index !=
        LAPLACE_PG_PERFCACHE_NO_GENERATION) {
        laplace_pg_perfcache_generation_slot* active =
            &generations[perfcache_shared->active_generation_index];
        snapshot->active_epoch = active->epoch;
        snapshot->active_sequence = active->sequence;
        snapshot->active_reader_count = active->reader_count;
        snapshot->has_active_epoch = 1u;
    }
    for (index = 0u; index < perfcache_shared->generation_capacity; ++index) {
        if (generations[index].state ==
            LAPLACE_PG_PERFCACHE_GENERATION_RETIRED) {
            snapshot->retired_generation_count += 1u;
            snapshot->retired_reader_count += generations[index].reader_count;
        }
    }
    snapshot->has_reservation = perfcache_shared->reservation.present;
    snapshot->reservation_serial = perfcache_shared->reservation.serial;
    LWLockRelease(&perfcache_shared->lock);
    return LAPLACE_PG_PERFCACHE_OK;
}

#if defined(LAPLACE_POSTGRES_TESTING)

static void read_exact_bytea(
    Datum value,
    uint8* destination,
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

static bytea* epoch_to_bytea(const laplace_pg_perfcache_epoch* epoch) {
    bytea* result = (bytea*)palloc(VARHDRSZ + sizeof(*epoch));
    SET_VARSIZE(result, VARHDRSZ + sizeof(*epoch));
    memcpy(VARDATA(result), epoch, sizeof(*epoch));
    return result;
}

static void raise_status(
    laplace_pg_perfcache_status status,
    const char* operation) {
    if (status != LAPLACE_PG_PERFCACHE_OK) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("Laplace PostgreSQL perfcache %s failed with status %u",
                        operation, (unsigned int)status)));
    }
}

#if defined(LAPLACE_TEST_SKIP_PG_PERFCACHE_EXPECTED_EPOCH)
#define laplace_pg_test_perfcache_admit \
    laplace_pg_test_perfcache_admit_expected_epoch_mutant
#endif
PG_FUNCTION_INFO_V1(laplace_pg_test_perfcache_admit);
Datum laplace_pg_test_perfcache_admit(PG_FUNCTION_ARGS) {
    laplace_pg_perfcache_epoch expected_epoch;
    bytea* encoded_manifest;
    int64 expected_sequence = PG_GETARG_INT64(0);
    bool has_expected_epoch = PG_GETARG_BOOL(1);
    if (expected_sequence < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("expected perfcache sequence cannot be negative")));
    }
    read_exact_bytea(PG_GETARG_DATUM(2), expected_epoch.activation_epoch_id.bytes,
                     sizeof(expected_epoch.activation_epoch_id.bytes),
                     "expected activation epoch ID");
    read_exact_bytea(PG_GETARG_DATUM(3), expected_epoch.epoch_fingerprint.bytes,
                     sizeof(expected_epoch.epoch_fingerprint.bytes),
                     "expected epoch fingerprint");
    encoded_manifest = PG_GETARG_BYTEA_PP(4);
    raise_status(
        laplace_pg_perfcache_admit(
            (uint64)expected_sequence,
            has_expected_epoch ? 1u : 0u,
            has_expected_epoch ? &expected_epoch : NULL,
            (const uint8*)VARDATA_ANY(encoded_manifest),
            (Size)VARSIZE_ANY_EXHDR(encoded_manifest)),
        "admission");
    PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(laplace_pg_test_perfcache_active_epoch);
Datum laplace_pg_test_perfcache_active_epoch(PG_FUNCTION_ARGS) {
    laplace_pg_perfcache_snapshot snapshot;
    (void)fcinfo;
    raise_status(laplace_pg_perfcache_snapshot_get(&snapshot), "snapshot");
    if (snapshot.has_active_epoch == 0u) {
        PG_RETURN_NULL();
    }
    PG_RETURN_BYTEA_P(epoch_to_bytea(&snapshot.active_epoch));
}

PG_FUNCTION_INFO_V1(laplace_pg_test_perfcache_metric);
Datum laplace_pg_test_perfcache_metric(PG_FUNCTION_ARGS) {
    laplace_pg_perfcache_snapshot snapshot;
    int32 metric = PG_GETARG_INT32(0);
    uint64 value;
    raise_status(laplace_pg_perfcache_snapshot_get(&snapshot), "snapshot");
    switch (metric) {
        case 1:
            value = snapshot.active_sequence;
            break;
        case 2:
            value = snapshot.active_reader_count;
            break;
        case 3:
            value = snapshot.retired_generation_count;
            break;
        case 4:
            value = snapshot.retired_reader_count;
            break;
        case 5:
            value = snapshot.has_reservation;
            break;
        case 6:
            value = snapshot.has_active_epoch;
            break;
        case 7:
            value = perfcache_manifest_load_count;
            break;
        case 8:
            value = perfcache_catalog_select_count;
            break;
        case 9:
            value = perfcache_manifest_select_count;
            break;
        default:
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("unknown Laplace perfcache test metric")));
            value = 0u;
    }
    if (value > (uint64)PG_INT64_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace perfcache test metric exceeds bigint")));
    }
    PG_RETURN_INT64((int64)value);
}

static uint64 load_u64_le(const uint8* input) {
    uint64 value = 0u;
    size_t index;
    for (index = 0u; index < 8u; ++index) {
        value |= (uint64)input[index] << (index * 8u);
    }
    return value;
}

PG_FUNCTION_INFO_V1(laplace_pg_test_perfcache_lookup);
Datum laplace_pg_test_perfcache_lookup(PG_FUNCTION_ARGS) {
    ArrayType* input = PG_GETARG_ARRAYTYPE_P(0);
    Datum* input_values = NULL;
    bool* input_nulls = NULL;
    int input_count = 0;
    uint8* keys;
    uint64* record_indexes;
    uint8* found;
    Datum* output_values;
    laplace_pg_perfcache_pin* pin = NULL;
    const laplace_perfcache_view* view = NULL;
    laplace_perfcache_registry_status registry_status;
    ArrayType* result;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    int index;
    deconstruct_array(input, INT4OID, 4, true, TYPALIGN_INT,
                      &input_values, &input_nulls, &input_count);
    if (input_count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("perfcache lookup batch cannot be empty")));
    }
    keys = (uint8*)palloc0((Size)input_count * 4u);
    record_indexes = (uint64*)palloc0((Size)input_count * sizeof(uint64));
    found = (uint8*)palloc0((Size)input_count);
    output_values = (Datum*)palloc0((Size)input_count * sizeof(Datum));
    for (index = 0; index < input_count; ++index) {
        uint32 key;
        if (input_nulls[index] || DatumGetInt32(input_values[index]) < 0) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("perfcache lookup keys must be nonnegative and nonnull")));
        }
        key = (uint32)DatumGetInt32(input_values[index]);
        keys[(Size)index * 4u] = (uint8)key;
        keys[(Size)index * 4u + 1u] = (uint8)(key >> 8u);
        keys[(Size)index * 4u + 2u] = (uint8)(key >> 16u);
        keys[(Size)index * 4u + 3u] = (uint8)(key >> 24u);
    }
    raise_status(laplace_pg_perfcache_pin_active(0u, NULL, &pin), "pin");
    PG_TRY();
    {
        registry_status = laplace_perfcache_pin_lookup_batch(
            &pin->native_pin, &perfcache_framework_probe_module.module_id,
            keys, (size_t)input_count, record_indexes, found);
        if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("native perfcache batch lookup failed with status %u",
                            (unsigned int)registry_status)));
        }
        registry_status = laplace_perfcache_pin_view(
            &pin->native_pin, &perfcache_framework_probe_module.module_id,
            &view);
        if (registry_status != LAPLACE_PERFCACHE_REGISTRY_OK || view == NULL ||
            view->record_stride != 12u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("native perfcache probe view is invalid")));
        }
        for (index = 0; index < input_count; ++index) {
            const uint8* record;
            uint64 value;
            if (found[index] == 0u || record_indexes[index] >= view->record_count) {
                ereport(ERROR,
                        (errcode(ERRCODE_NO_DATA_FOUND),
                         errmsg("native perfcache probe key was not found")));
            }
            record = view->records + record_indexes[index] * view->record_stride;
            value = load_u64_le(record + 4u);
            if (value > (uint64)PG_INT64_MAX) {
                ereport(ERROR,
                        (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                         errmsg("native perfcache probe value exceeds bigint")));
            }
            output_values[index] = Int64GetDatum((int64)value);
        }
        laplace_pg_perfcache_pin_release(&pin);
    }
    PG_CATCH();
    {
        laplace_pg_perfcache_pin_release(&pin);
        PG_RE_THROW();
    }
    PG_END_TRY();
    get_typlenbyvalalign(INT8OID, &type_length, &type_by_value,
                        &type_alignment);
    result = construct_array(output_values, input_count, INT8OID,
                             type_length, type_by_value, type_alignment);
    PG_RETURN_ARRAYTYPE_P(result);
}

PG_FUNCTION_INFO_V1(laplace_pg_test_perfcache_hold);
Datum laplace_pg_test_perfcache_hold(PG_FUNCTION_ARGS) {
    laplace_pg_perfcache_epoch expected_epoch;
    laplace_pg_perfcache_pin* pin = NULL;
    int32 milliseconds = PG_GETARG_INT32(2);
    bytea* result;
    if (milliseconds < 0 || milliseconds > 60000) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("perfcache hold duration must be between 0 and 60000 milliseconds")));
    }
    read_exact_bytea(PG_GETARG_DATUM(0), expected_epoch.activation_epoch_id.bytes,
                     sizeof(expected_epoch.activation_epoch_id.bytes),
                     "expected activation epoch ID");
    read_exact_bytea(PG_GETARG_DATUM(1), expected_epoch.epoch_fingerprint.bytes,
                     sizeof(expected_epoch.epoch_fingerprint.bytes),
                     "expected epoch fingerprint");
    raise_status(laplace_pg_perfcache_pin_epoch(&expected_epoch, &pin),
                 "exact epoch pin");
    PG_TRY();
    {
        pg_usleep((long)milliseconds * 1000L);
        result = epoch_to_bytea(&pin->epoch);
        laplace_pg_perfcache_pin_release(&pin);
    }
    PG_CATCH();
    {
        laplace_pg_perfcache_pin_release(&pin);
        PG_RE_THROW();
    }
    PG_END_TRY();
    PG_RETURN_BYTEA_P(result);
}

PG_FUNCTION_INFO_V1(laplace_pg_test_perfcache_pin_then_error);
Datum laplace_pg_test_perfcache_pin_then_error(PG_FUNCTION_ARGS) {
    laplace_pg_perfcache_epoch expected_epoch;
    laplace_pg_perfcache_pin* pin = NULL;
    read_exact_bytea(PG_GETARG_DATUM(0), expected_epoch.activation_epoch_id.bytes,
                     sizeof(expected_epoch.activation_epoch_id.bytes),
                     "expected activation epoch ID");
    read_exact_bytea(PG_GETARG_DATUM(1), expected_epoch.epoch_fingerprint.bytes,
                     sizeof(expected_epoch.epoch_fingerprint.bytes),
                     "expected epoch fingerprint");
    raise_status(laplace_pg_perfcache_pin_active(1u, &expected_epoch, &pin),
                 "pin");
    ereport(ERROR,
            (errcode(ERRCODE_INTERNAL_ERROR),
             errmsg("injected error after Laplace perfcache pin")));
    PG_RETURN_VOID();
}

PG_FUNCTION_INFO_V1(laplace_pg_test_perfcache_replay_committing);
Datum laplace_pg_test_perfcache_replay_committing(PG_FUNCTION_ARGS) {
    TransactionId committed_transaction_id = PG_GETARG_TRANSACTIONID(0);
    laplace_pg_perfcache_generation_slot* generation;
    laplace_pg_perfcache_reservation* reservation;
    laplace_pg_perfcache_status status = ensure_control_ready();
    if (status != LAPLACE_PG_PERFCACHE_OK ||
        !TransactionIdDidCommit(committed_transaction_id)) {
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
                 errmsg("test visibility-gap transaction is not committed")));
    }
    LWLockAcquire(&perfcache_shared->lock, LW_EXCLUSIVE);
    if (perfcache_shared->reservation.present != 0u ||
        perfcache_shared->active_generation_index ==
            LAPLACE_PG_PERFCACHE_NO_GENERATION) {
        LWLockRelease(&perfcache_shared->lock);
        ereport(ERROR,
                (errcode(ERRCODE_OBJECT_IN_USE),
                 errmsg("cannot inject a committed perfcache visibility gap")));
    }
    generation = &perfcache_generations()[
        perfcache_shared->active_generation_index];
    reservation = &perfcache_shared->reservation;
    memset(reservation, 0, sizeof(*reservation));
    reservation->expected_epoch = generation->epoch;
    reservation->next_epoch = generation->epoch;
    reservation->manifest_fingerprint = generation->manifest_fingerprint;
    reservation->admission_receipt_id = generation->admission_receipt_id;
    reservation->serial = perfcache_shared->next_reservation_serial++;
    reservation->expected_sequence = generation->sequence;
    reservation->next_sequence = generation->sequence;
    reservation->transaction_id = committed_transaction_id;
    reservation->owner_proc_number = MyProcNumber;
    reservation->owner_pid = MyProcPid;
    reservation->owner_start_timestamp = (int64)MyStartTimestamp;
    reservation->candidate_generation_index =
        perfcache_shared->active_generation_index;
    reservation->state = LAPLACE_PG_PERFCACHE_RESERVATION_COMMITTING;
    reservation->present = 1u;
    LWLockRelease(&perfcache_shared->lock);
    PG_RETURN_VOID();
}

#endif
