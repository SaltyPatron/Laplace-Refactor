/*
 * Product source admission must not stop at the tabular envelope. Route the
 * existing admission call through the recursive engine path using the same
 * verified Unicode source estate that builds the active Unicode product.
 *
 * The backend-local metrics below are execution evidence, not semantic state.
 * They retain the two most recent source-admission executions in one backend so
 * a replay contract can compare first publication with the immediately
 * following replay without inferring work from SQL text or row counts.
 */
#include "postgres.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/errcodes.h"

#include <inttypes.h>
#include <string.h>

#include "laplace/source_profile.h"
#include "laplace/tabular_source_recursive.h"
#include "laplace/unicode_root.h"
#include "composition_pg.h"
#include "source_structural_witness_pg.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#ifndef WIN32
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "access/htup_details.h"
#include "catalog/pg_authid_d.h"
#include "catalog/pg_type.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "storage/fd.h"
#include "utils/acl.h"
#include "utils/array.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/wait_event.h"
#include "laplace/source_evidence.h"
#include "laplace/contract/evidence_lineage.h"
#include "laplace/contract/evidence_testimony.h"
#include "laplace/contract/postgresql_bindings.h"
#include "laplace/evidence_lineage.h"
#include "laplace/evidence_testimony.h"
#include "laplace/persistence.h"
#include "laplace/tabular_source.h"
#include "laplace_pg_internal.h"
#include "reference_mapping_pg.h"
#include "reference_topology_pg.h"
#include "set_pg.h"
#include "source_profile_pg.h"
#include "unicode_atoms_pg.h"

#ifndef LAPLACE_UNICODE_SOURCE_ROOT
#error "LAPLACE_UNICODE_SOURCE_ROOT is required for recursive source admission"
#endif

PG_FUNCTION_INFO_V1(laplace_source_admission_last_execution_metrics);

typedef struct laplace_pg_source_execution_metrics {
    uint64_t sequence;
    uint64_t source_stage_spi_execute_with_args_count;
    uint64_t composition_entity_candidate_count;
    uint64_t composition_physicality_candidate_count;
    uint64_t composition_entity_presence_round_count;
    uint64_t composition_physicality_presence_round_count;
    uint64_t composition_persistence_plan_count;
    uint64_t composition_receipt_persistence_call_count;
    uint8_t composition_persistence_executed;
    uint8_t valid;
} laplace_pg_source_execution_metrics;

static laplace_pg_source_execution_metrics laplace_pg_source_metrics_previous;
static laplace_pg_source_execution_metrics laplace_pg_source_metrics_active;
static uint64_t laplace_pg_source_metrics_sequence = 0u;

static void laplace_pg_source_metrics_begin(void) {
    laplace_pg_source_metrics_previous = laplace_pg_source_metrics_active;
    memset(&laplace_pg_source_metrics_active, 0,
           sizeof(laplace_pg_source_metrics_active));
    ++laplace_pg_source_metrics_sequence;
    laplace_pg_source_metrics_active.sequence = laplace_pg_source_metrics_sequence;
}

static void laplace_pg_source_metrics_capture_composition(
    const laplace_pg_composition_execution* execution) {
    if (execution == NULL) {
        return;
    }
    laplace_pg_source_metrics_active.composition_entity_candidate_count =
        execution->presence.entity_candidate_count;
    laplace_pg_source_metrics_active.composition_physicality_candidate_count =
        execution->presence.physicality_candidate_count;
    laplace_pg_source_metrics_active.composition_entity_presence_round_count =
        execution->presence.entity_round_count;
    laplace_pg_source_metrics_active.composition_physicality_presence_round_count =
        execution->presence.physicality_round_count;
    laplace_pg_source_metrics_active.composition_persistence_plan_count =
        (uint64_t)execution->persistence.plan_count;
    laplace_pg_source_metrics_active.composition_persistence_executed =
        execution->persistence_executed;
    laplace_pg_source_metrics_active.valid = 1u;
}

static void laplace_pg_source_composition_persist_receipt(
    const laplace_pg_composition_execution* execution,
    const laplace_composition_working_set_input* input) {
    LAPLACE_PG_COMPOSITION_PERSIST_RECEIPT_SYMBOL(execution, input);
    ++laplace_pg_source_metrics_active.composition_receipt_persistence_call_count;
}

static void laplace_pg_append_metrics_json(
    StringInfo output,
    const char* name,
    const laplace_pg_source_execution_metrics* metrics) {
    appendStringInfo(
        output,
        "\"%s\":{"
        "\"valid\":%s,"
        "\"sequence\":%" PRIu64 ","
        "\"source_stage_spi_execute_with_args_count\":%" PRIu64 ","
        "\"composition_entity_candidate_count\":%" PRIu64 ","
        "\"composition_physicality_candidate_count\":%" PRIu64 ","
        "\"composition_entity_presence_round_count\":%" PRIu64 ","
        "\"composition_physicality_presence_round_count\":%" PRIu64 ","
        "\"composition_persistence_plan_count\":%" PRIu64 ","
        "\"composition_receipt_persistence_call_count\":%" PRIu64 ","
        "\"composition_persistence_executed\":%s}",
        name,
        metrics->valid != 0u ? "true" : "false",
        metrics->sequence,
        metrics->source_stage_spi_execute_with_args_count,
        metrics->composition_entity_candidate_count,
        metrics->composition_physicality_candidate_count,
        metrics->composition_entity_presence_round_count,
        metrics->composition_physicality_presence_round_count,
        metrics->composition_persistence_plan_count,
        metrics->composition_receipt_persistence_call_count,
        metrics->composition_persistence_executed != 0u ? "true" : "false");
}

Datum laplace_source_admission_last_execution_metrics(PG_FUNCTION_ARGS) {
    StringInfoData output;
    (void)fcinfo;
    initStringInfo(&output);
    appendStringInfoString(
        &output,
        "{\"schema\":\"laplace.source-admission-execution-metrics/v1\",");
    laplace_pg_append_metrics_json(
        &output, "previous", &laplace_pg_source_metrics_previous);
    appendStringInfoChar(&output, ',');
    laplace_pg_append_metrics_json(
        &output, "last", &laplace_pg_source_metrics_active);
    appendStringInfoChar(&output, '}');
    PG_RETURN_TEXT_P(cstring_to_text(output.data));
}

static laplace_tabular_source_status
laplace_pg_tabular_source_plan_create_recursive(
    const laplace_tabular_source_input* input,
    laplace_tabular_source_plan** plan) {
    laplace_unicode_source_bundle* unicode_bundle = NULL;
    laplace_unicode_source_receipt unicode_receipt;
    laplace_tabular_source_status status;
    laplace_pg_source_metrics_begin();
    memset(&unicode_receipt, 0, sizeof(unicode_receipt));
    if (laplace_unicode_source_bundle_open(
            LAPLACE_UNICODE_SOURCE_ROOT,
            &unicode_bundle,
            &unicode_receipt) != LAPLACE_UNICODE_OK ||
        unicode_bundle == NULL) {
        laplace_unicode_source_bundle_close(&unicode_bundle);
        return LAPLACE_TABULAR_SOURCE_PROFILE_INVALID;
    }
    status = laplace_tabular_source_plan_create_recursive(
        input, unicode_bundle, plan);
    laplace_unicode_source_bundle_close(&unicode_bundle);
    return status;
}

static void laplace_pg_source_composition_execute(
    const laplace_composition_working_set_input* input,
    laplace_pg_composition_execution* execution) {
    LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(input, execution);
    laplace_pg_source_metrics_capture_composition(execution);
}

/* Count only this admission's set operations through an explicit physical hook. */
static int laplace_pg_source_execute_with_args(
    const char* source, int nargs, Oid* argtypes, Datum* values,
    const char* nulls, bool read_only, long count) {
    ++laplace_pg_source_metrics_active.source_stage_spi_execute_with_args_count;
    return SPI_execute_with_args(
        source, nargs, argtypes, values, nulls, read_only, count);
}

PG_FUNCTION_INFO_V1(LAPLACE_PG_SOURCE_ADMIT_TABULAR_SYMBOL);

_Static_assert(LAPLACE_SOURCE_PROFILE_EVIDENCE_STANDARD == LAPLACE_EVIDENCE_SOURCE_STANDARD,
               "standard provenance constants diverged");
_Static_assert(LAPLACE_SOURCE_PROFILE_EVIDENCE_CURATED_DATASET == LAPLACE_EVIDENCE_SOURCE_CURATED_DATASET,
               "curated provenance constants diverged");
_Static_assert(LAPLACE_SOURCE_PROFILE_EVIDENCE_CORPUS == LAPLACE_EVIDENCE_SOURCE_CORPUS,
               "corpus provenance constants diverged");
_Static_assert(LAPLACE_SOURCE_PROFILE_EVIDENCE_DIRECT_OBSERVATION == LAPLACE_EVIDENCE_SOURCE_DIRECT_OBSERVATION,
               "observation provenance constants diverged");
_Static_assert(LAPLACE_SOURCE_PROFILE_EVIDENCE_CALCULATION == LAPLACE_EVIDENCE_SOURCE_CALCULATION,
               "calculation provenance constants diverged");
_Static_assert(LAPLACE_SOURCE_PROFILE_EVIDENCE_MODEL == LAPLACE_EVIDENCE_SOURCE_MODEL,
               "model provenance constants diverged");
_Static_assert(LAPLACE_SOURCE_PROFILE_EVIDENCE_SELF_ASSERTION == LAPLACE_EVIDENCE_SOURCE_SELF_ASSERTION,
               "self-assertion provenance constants diverged");
_Static_assert(LAPLACE_SOURCE_PROFILE_EVIDENCE_EXTERNAL_PROVIDER == LAPLACE_EVIDENCE_SOURCE_EXTERNAL_PROVIDER,
               "external-provider provenance constants diverged");

typedef struct laplace_pg_source_stage_receipts {
    laplace_digest256 source_profile;
    laplace_digest256 source_profile_isa;
    laplace_digest256 reference_topology;
    laplace_digest256 reference_topology_isa;
    laplace_digest256 reference_mapping;
    laplace_digest256 reference_mapping_isa;
    laplace_digest256 evidence_lineage;
    laplace_digest256 evidence_lineage_isa;
    laplace_digest256 evidence_testimony;
    laplace_digest256 evidence_testimony_isa;
    laplace_digest256 world_admission_id;
    laplace_digest256 world_admission;
    laplace_digest256 world_admission_isa;
    uint64_t reference_occurrence_count;
    uint64_t reference_coordinate_count;
    uint64_t reference_present_count;
    uint64_t reference_retired_count;
    uint64_t reference_unresolved_count;
    uint64_t reference_persistence_batch_count;
    uint64_t reference_maximum_persistence_batch_records;
    uint64_t reference_maximum_encoded_persistence_batch_bytes;
    uint64_t reference_minimum_encoded_persistence_record_bytes;
    uint64_t reference_mapping_occurrence_count;
    uint64_t reference_mapping_proposition_count;
    uint64_t reference_mapping_resolved_count;
    uint64_t reference_mapping_unresolved_count;
    uint64_t reference_mapping_retired_count;
    uint64_t reference_mapping_persistence_batch_count;
    uint64_t reference_mapping_maximum_persistence_batch_records;
    uint64_t reference_mapping_maximum_encoded_persistence_batch_bytes;
    uint64_t reference_mapping_minimum_encoded_persistence_record_bytes;
    uint64_t evidence_node_count;
    uint64_t testimony_count;
} laplace_pg_source_stage_receipts;

typedef laplace_source_claim laplace_pg_source_claim;

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
                 errmsg("Laplace source admission returned null %s", field)));
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
                (errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
                 errmsg("Laplace source-admission %s has invalid width", field),
                 errdetail("expected=%zu actual=%zu", expected,
                           (size_t)VARSIZE_ANY_EXHDR(value))));
    }
    memcpy(output, VARDATA_ANY(value), expected);
}

static uint32_t read_u32(
    HeapTupleHeader tuple,
    int attribute,
    const char* field) {
    const int32 value = DatumGetInt32(
        laplace_pg_required_composite_attribute(tuple, attribute, field));
    if (value < 0) {
        ereport(ERROR,
                (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                 errmsg("Laplace %s cannot be negative", field)));
    }
    return (uint32_t)value;
}

#ifndef WIN32
typedef struct laplace_pg_source_file_view {
    MemoryContextCallback cleanup;
    void* bytes;
    size_t byte_count;
} laplace_pg_source_file_view;

static void release_source_file_view(void* argument) {
    laplace_pg_source_file_view* view = (laplace_pg_source_file_view*)argument;
    if (view->bytes != NULL) {
        (void)munmap(view->bytes, view->byte_count);
        view->bytes = NULL;
    }
}
#endif

static const uint8_t* read_source_file(Datum path_datum, uint64_t byte_count) {
#ifdef WIN32
    (void)path_datum;
    (void)byte_count;
    ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
        errmsg("Laplace server-file source transport requires a POSIX snapshot provider")));
    return NULL;
#else
    text* path_text = DatumGetTextPP(path_datum);
    char* path;
    laplace_pg_source_file_view* view;
    volatile int source = -1;
    volatile File snapshot = -1;
    struct stat information;
    if (!has_privs_of_role(GetUserId(), ROLE_PG_READ_SERVER_FILES)) {
        ereport(ERROR, (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
            errmsg("Laplace source file transport requires pg_read_server_files")));
    }
    if (VARSIZE_ANY_EXHDR(path_text) == 0 ||
        memchr(VARDATA_ANY(path_text), 0, (size_t)VARSIZE_ANY_EXHDR(path_text)) != NULL ||
        byte_count == 0u || byte_count > SIZE_MAX || byte_count > (uint64_t)INT64_MAX) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace source file path or declared extent is invalid")));
    }
    path = text_to_cstring(path_text);
    if (!is_absolute_path(path)) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Laplace source file path must be absolute")));
    }
    view = (laplace_pg_source_file_view*)palloc0(sizeof(*view));
    view->byte_count = (size_t)byte_count;
    view->cleanup.func = release_source_file_view;
    view->cleanup.arg = view;
    PG_TRY();
    {
        uint8_t* buffer;
        uint64_t offset = 0u;
        source = OpenTransientFile(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        if (source < 0 || fstat(source, &information) != 0) {
            const int error_number = errno;
            ereport(ERROR, (errcode_for_file_access(),
                errmsg("could not open Laplace source file \"%s\": %s",
                    path, strerror(error_number))));
        }
        if (!S_ISREG(information.st_mode) || information.st_size < 0 ||
            (uint64_t)information.st_size != byte_count) {
            ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION),
                errmsg("Laplace source file differs from its declared regular-file extent")));
        }
        /* A private snapshot prevents upstream writes between digest validation
         * and parsing. PostgreSQL owns the temporary file and its storage limit.
         * The native graph calculation subsequently verifies the complete SHA. */
        snapshot = OpenTemporaryFile(false);
        buffer = (uint8_t*)palloc(1024u * 1024u);
        while (offset < byte_count) {
            const size_t requested = (size_t)Min(byte_count - offset, UINT64_C(1024) * 1024u);
            ssize_t received;
            size_t written = 0u;
            CHECK_FOR_INTERRUPTS();
            received = read(source, buffer, requested);
            if (received < 0 && errno == EINTR) {
                continue;
            }
            if (received < 0) {
                const int error_number = errno;
                ereport(ERROR, (errcode_for_file_access(),
                    errmsg("could not read Laplace source file \"%s\": %s",
                        path, strerror(error_number))));
            }
            if (received == 0) {
                ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION),
                    errmsg("Laplace source file was truncated during acquisition")));
            }
            while (written < (size_t)received) {
                const ssize_t count = FileWrite(snapshot, buffer + written,
                    (size_t)received - written, (off_t)(offset + written),
                    WAIT_EVENT_DATA_FILE_WRITE);
                if (count <= 0) {
                    const int error_number = errno;
                    ereport(ERROR, (errcode_for_file_access(),
                        errmsg("could not write Laplace source snapshot: %s",
                            strerror(error_number))));
                }
                written += (size_t)count;
            }
            offset += (uint64_t)received;
        }
        if (fstat(source, &information) != 0 || information.st_size < 0 ||
            (uint64_t)information.st_size != byte_count) {
            ereport(ERROR, (errcode(ERRCODE_DATA_EXCEPTION),
                errmsg("Laplace source file extent changed during acquisition")));
        }
        pfree(buffer);
        view->bytes = mmap(NULL, view->byte_count, PROT_READ, MAP_PRIVATE,
            FileGetRawDesc(snapshot), 0);
        if (view->bytes == MAP_FAILED) {
            const int error_number = errno;
            view->bytes = NULL;
            ereport(ERROR, (errcode(ERRCODE_OUT_OF_MEMORY),
                errmsg("could not map Laplace source snapshot: %s",
                    strerror(error_number))));
        }
        MemoryContextRegisterResetCallback(CurrentMemoryContext, &view->cleanup);
        /* The resource owner retains the snapshot until transaction cleanup so
         * its disk usage remains charged to PostgreSQL's temp_file_limit. */
        snapshot = -1;
        (void)CloseTransientFile(source);
        source = -1;
    }
    PG_CATCH();
    {
        if (source >= 0) {
            (void)CloseTransientFile(source);
        }
        if (snapshot >= 0) {
            FileClose(snapshot);
        }
        PG_RE_THROW();
    }
    PG_END_TRY();
    return (const uint8_t*)view->bytes;
#endif
}

static laplace_tabular_artifact* read_artifacts(
    ArrayType* array,
    size_t* artifact_count,
    uint64_t maximum_input_bytes) {
    const Oid type_oid = ARR_ELEMTYPE(array);
    const bool extended = type_oid !=
        laplace_pg_composite_type_oid("tabular_source_artifact");
    const bool files = extended && type_oid !=
        laplace_pg_composite_type_oid("tabular_source_artifact_v2");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_tabular_artifact* artifacts;
    uint64_t input_bytes = 0u;
    int index;
    if ((ARR_NDIM(array) != 1 && ARR_NDIM(array) != 0) ||
        (files && type_oid !=
            laplace_pg_composite_type_oid("tabular_source_artifact_file"))) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace tabular source input must be an exact one-dimensional artifact array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count <= 0) {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("Laplace tabular source requires at least one artifact")));
    }
    if ((size_t)count > MaxAllocSize / sizeof(*artifacts)) {
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace artifact descriptors exceed PostgreSQL allocation capacity")));
    }
    artifacts = (laplace_tabular_artifact*)palloc0(
        sizeof(*artifacts) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        bytea* name;
        bytea* media_type;
        ArrayType* column_names;
        Datum* column_values = NULL;
        bool* column_nulls = NULL;
        int column_count = 0;
        laplace_tabular_column* columns = NULL;
        char* exact_name;
        char* exact_media_type;
        int column_index;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace tabular artifact array cannot contain nulls")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(tuple, 1, "artifact_id"),
            &artifacts[index].artifact_id, "tabular artifact_id");
        laplace_pg_read_digest(
            laplace_pg_required_composite_attribute(
                tuple, 2, "parent_artifact_id"),
            &artifacts[index].parent_artifact_id,
            "tabular parent_artifact_id");
        read_exact_bytes(
            laplace_pg_required_composite_attribute(tuple, 3, "expected_sha256"),
            artifacts[index].expected_sha256,
            sizeof(artifacts[index].expected_sha256), "expected SHA-256");
        if (files) {
            artifacts[index].byte_count = laplace_pg_uint64_from_numeric(
                laplace_pg_required_composite_attribute(tuple, 23, "artifact byte count"),
                "tabular artifact byte count");
            if (artifacts[index].byte_count > maximum_input_bytes - input_bytes) {
                ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                    errmsg("Laplace source bytes exceed the supplied resource grant")));
            }
            artifacts[index].bytes = read_source_file(
                laplace_pg_required_composite_attribute(tuple, 4, "artifact source path"),
                artifacts[index].byte_count);
        } else {
            bytea* content = DatumGetByteaPP(laplace_pg_required_composite_attribute(
                tuple, 4, "artifact content"));
            artifacts[index].bytes = (const uint8_t*)VARDATA_ANY(content);
            artifacts[index].byte_count = (uint64_t)VARSIZE_ANY_EXHDR(content);
        }
        if (artifacts[index].byte_count > maximum_input_bytes - input_bytes) {
            ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                errmsg("Laplace source bytes exceed the supplied resource grant")));
        }
        input_bytes += artifacts[index].byte_count;
        name = DatumGetByteaPP(laplace_pg_required_composite_attribute(
            tuple, 5, "artifact name"));
        media_type = DatumGetByteaPP(laplace_pg_required_composite_attribute(
            tuple, 6, "artifact media_type"));
        artifacts[index].name_byte_count = (uint64_t)VARSIZE_ANY_EXHDR(name);
        artifacts[index].media_type_byte_count =
            (uint64_t)VARSIZE_ANY_EXHDR(media_type);
        if (artifacts[index].name_byte_count == 0u ||
            artifacts[index].name_byte_count > SIZE_MAX - 1u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace tabular artifact name is invalid")));
        }
        exact_name = (char*)palloc((size_t)artifacts[index].name_byte_count + 1u);
        memcpy(exact_name, VARDATA_ANY(name),
               (size_t)artifacts[index].name_byte_count);
        exact_name[artifacts[index].name_byte_count] = '\0';
        artifacts[index].name = exact_name;
        if (artifacts[index].media_type_byte_count == 0u ||
            artifacts[index].media_type_byte_count > SIZE_MAX - 1u) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace tabular artifact media type is invalid")));
        }
        exact_media_type = (char*)palloc(
            (size_t)artifacts[index].media_type_byte_count + 1u);
        memcpy(exact_media_type, VARDATA_ANY(media_type),
               (size_t)artifacts[index].media_type_byte_count);
        exact_media_type[artifacts[index].media_type_byte_count] = '\0';
        artifacts[index].media_type = exact_media_type;
        artifacts[index].expected_record_count = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 7, "expected_record_count"),
            "tabular expected_record_count");
        artifacts[index].expected_field_count = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 8, "expected_field_count"),
            "tabular expected_field_count");
        artifacts[index].reference_column_mask = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 9, "reference_column_mask"),
            "tabular reference_column_mask");
        artifacts[index].mode = read_u32(tuple, 10, "tabular mode");
        artifacts[index].delimiter = read_u32(tuple, 11, "tabular delimiter");
        artifacts[index].line_terminator = read_u32(
            tuple, 12, "tabular line_terminator");
        artifacts[index].expected_column_count = read_u32(
            tuple, 13, "tabular expected_column_count");
        artifacts[index].outcome_type = read_u32(
            tuple, 14, "tabular outcome_type");
        artifacts[index].flags = read_u32(tuple, 15, "tabular flags");
        column_names = DatumGetArrayTypeP(
            laplace_pg_required_composite_attribute(
                tuple, 16, "tabular column_names"));
        if ((ARR_NDIM(column_names) != 1 && ARR_NDIM(column_names) != 0) ||
            ARR_ELEMTYPE(column_names) != BYTEAOID) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATATYPE_MISMATCH),
                     errmsg("Laplace tabular column names must be an exact one-dimensional bytea array")));
        }
        deconstruct_array(
            column_names, BYTEAOID, -1, false, 'i', &column_values,
            &column_nulls, &column_count);
        if (column_count != (int)artifacts[index].expected_column_count) {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("Laplace tabular column declaration does not match its artifact width")));
        }
        if (column_count != 0) {
            columns = (laplace_tabular_column*)palloc0(
                sizeof(*columns) * (size_t)column_count);
        }
        for (column_index = 0; column_index < column_count; ++column_index) {
            bytea* column;
            if (column_nulls[column_index]) {
                ereport(ERROR,
                        (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                         errmsg("Laplace tabular column names cannot contain nulls")));
            }
            column = DatumGetByteaPP(column_values[column_index]);
            columns[column_index].bytes =
                (const uint8_t*)VARDATA_ANY(column);
            columns[column_index].byte_count =
                (uint64_t)VARSIZE_ANY_EXHDR(column);
        }
        artifacts[index].columns = columns;
        artifacts[index].header_record_count = read_u32(
            tuple, 17, "tabular header_record_count");
        if (extended) {
            const Oid field_oid = laplace_pg_composite_type_oid(
                "tabular_fixed_width_field");
            ArrayType* fields = DatumGetArrayTypeP(
                laplace_pg_required_composite_attribute(
                    tuple, 18, "tabular fixed_width_fields"));
            Datum* field_values = NULL;
            bool* field_nulls = NULL;
            int field_count = 0;
            int field_index;
            int16 field_length;
            bool field_by_value;
            char field_alignment;
            laplace_tabular_fixed_width_field* native_fields = NULL;
            uint64_t overflow_field;
            uint64_t overflow_records;
            if ((ARR_NDIM(fields) != 0 && ARR_NDIM(fields) != 1) ||
                ARR_ELEMTYPE(fields) != field_oid) {
                ereport(ERROR, (errcode(ERRCODE_DATATYPE_MISMATCH),
                    errmsg("Laplace fixed-width fields must be an exact one-dimensional field array")));
            }
            get_typlenbyvalalign(
                field_oid, &field_length, &field_by_value, &field_alignment);
            deconstruct_array(fields, field_oid, field_length, field_by_value,
                field_alignment, &field_values, &field_nulls, &field_count);
            if ((artifacts[index].mode == LAPLACE_TABULAR_ARTIFACT_FIXED_WIDTH &&
                    (uint64_t)field_count != artifacts[index].expected_column_count) ||
                (artifacts[index].mode != LAPLACE_TABULAR_ARTIFACT_FIXED_WIDTH &&
                    field_count != 0)) {
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("Laplace fixed-width declarations differ from the artifact mode or column count")));
            }
            if (field_count != 0) {
                if ((size_t)field_count > MaxAllocSize / sizeof(*native_fields)) {
                    ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                        errmsg("Laplace fixed-width declaration exceeds PostgreSQL array capacity")));
                }
                native_fields = (laplace_tabular_fixed_width_field*)palloc0(
                    sizeof(*native_fields) * (size_t)field_count);
            }
            for (field_index = 0; field_index < field_count; ++field_index) {
                HeapTupleHeader field;
                if (field_nulls[field_index]) {
                    ereport(ERROR, (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                        errmsg("Laplace fixed-width declarations cannot contain null fields")));
                }
                field = DatumGetHeapTupleHeader(field_values[field_index]);
                native_fields[field_index].width = read_u32(
                    field, 1, "fixed-width field width");
                native_fields[field_index].flags = read_u32(
                    field, 2, "fixed-width field flags");
            }
            artifacts[index].fixed_width_fields = native_fields;
            artifacts[index].padding_byte = read_u32(
                tuple, 19, "fixed-width padding byte");
            overflow_field = laplace_pg_uint64_from_numeric(
                laplace_pg_required_composite_attribute(
                    tuple, 20, "fixed-width overflow field"),
                "fixed-width overflow field");
            overflow_records = laplace_pg_uint64_from_numeric(
                laplace_pg_required_composite_attribute(
                    tuple, 22, "fixed-width overflow record count"),
                "fixed-width overflow record count");
            if (overflow_field > UINT32_MAX || overflow_records > UINT32_MAX) {
                ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                    errmsg("Laplace fixed-width overflow declarations exceed their native range")));
            }
            artifacts[index].overflow_field_index = (uint32_t)overflow_field;
            artifacts[index].maximum_overflow_bytes = read_u32(
                tuple, 21, "fixed-width maximum overflow bytes");
            artifacts[index].expected_overflow_record_count = (uint32_t)overflow_records;
        } else if (artifacts[index].mode == LAPLACE_TABULAR_ARTIFACT_FIXED_WIDTH) {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("Laplace fixed-width admission requires tabular_source_artifact_v2 field declarations")));
        }
    }
    *artifact_count = (size_t)count;
    return artifacts;
}

static laplace_tabular_reference_rule* read_reference_rules(
    ArrayType* array,
    size_t* rule_count) {
    const Oid type_oid = laplace_pg_composite_type_oid(
        "tabular_reference_rule");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_tabular_reference_rule* rules;
    int index;
    if ((ARR_NDIM(array) != 1 && ARR_NDIM(array) != 0) ||
        ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace tabular reference rules must be an exact one-dimensional rule array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count == 0) {
        *rule_count = 0u;
        return NULL;
    }
    rules = (laplace_tabular_reference_rule*)palloc0(
        sizeof(*rules) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace tabular reference rule array cannot contain nulls")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        rules[index].artifact_index = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 1, "reference-rule artifact_index"),
            "reference-rule artifact_index");
        rules[index].column_index = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 2, "reference-rule column_index"),
            "reference-rule column_index");
        read_exact_bytes(
            laplace_pg_required_composite_attribute(
                tuple, 3, "reference-rule namespace"),
            rules[index].name_space.bytes,
            sizeof(rules[index].name_space.bytes),
            "reference-rule namespace");
        rules[index].kind = read_u32(tuple, 4, "reference-rule kind");
        rules[index].flags = read_u32(tuple, 5, "reference-rule flags");
    }
    *rule_count = (size_t)count;
    return rules;
}

static laplace_tabular_mapping_rule* read_mapping_rules(
    ArrayType* array,
    size_t* rule_count) {
    const Oid type_oid = laplace_pg_composite_type_oid(
        "tabular_mapping_rule");
    Datum* values = NULL;
    bool* nulls = NULL;
    int count = 0;
    int16 type_length;
    bool type_by_value;
    char type_alignment;
    laplace_tabular_mapping_rule* rules;
    int index;
    if ((ARR_NDIM(array) != 1 && ARR_NDIM(array) != 0) ||
        ARR_ELEMTYPE(array) != type_oid) {
        ereport(ERROR,
                (errcode(ERRCODE_DATATYPE_MISMATCH),
                 errmsg("Laplace tabular mapping rules must be an exact one-dimensional rule array")));
    }
    get_typlenbyvalalign(
        type_oid, &type_length, &type_by_value, &type_alignment);
    deconstruct_array(
        array, type_oid, type_length, type_by_value, type_alignment,
        &values, &nulls, &count);
    if (count == 0) {
        *rule_count = 0u;
        return NULL;
    }
    rules = (laplace_tabular_mapping_rule*)palloc0(
        sizeof(*rules) * (size_t)count);
    for (index = 0; index < count; ++index) {
        HeapTupleHeader tuple;
        bytea* relation_content;
        if (nulls[index]) {
            ereport(ERROR,
                    (errcode(ERRCODE_NULL_VALUE_NOT_ALLOWED),
                     errmsg("Laplace tabular mapping rule array cannot contain nulls")));
        }
        tuple = DatumGetHeapTupleHeader(values[index]);
        rules[index].artifact_index = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 1, "mapping-rule artifact_index"),
            "mapping-rule artifact_index");
        rules[index].left_column_index = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 2, "mapping-rule left_column_index"),
            "mapping-rule left_column_index");
        rules[index].right_column_index = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 3, "mapping-rule right_column_index"),
            "mapping-rule right_column_index");
        relation_content = DatumGetByteaPP(
            laplace_pg_required_composite_attribute(
                tuple, 4, "mapping-rule relation_content"));
        rules[index].relation_content =
            (const uint8_t*)VARDATA_ANY(relation_content);
        rules[index].relation_content_byte_count =
            (uint64_t)VARSIZE_ANY_EXHDR(relation_content);
        rules[index].relation_version = laplace_pg_uint64_from_numeric(
            laplace_pg_required_composite_attribute(
                tuple, 5, "mapping-rule relation_version"),
            "mapping-rule relation_version");
        rules[index].relation_kind = read_u32(
            tuple, 6, "mapping-rule relation_kind");
        rules[index].flags = read_u32(tuple, 7, "mapping-rule flags");
    }
    *rule_count = (size_t)count;
    return rules;
}

static void source_profile_binding_open(laplace_pg_composite_binding* binding) {
    Oid types[51];
    int32 typmods[51];
    int index;
    for (index = 0; index < 51; ++index) {
        types[index] = BYTEAOID;
        typmods[index] = LAPLACE_PG_TYPMOD_NONE;
    }
    types[1] = INT4OID;
    types[6] = NUMERICOID;
    typmods[6] = LAPLACE_PG_NUMERIC_TYPMOD(20, 0);
    for (index = 19; index <= 48; ++index) {
        types[index] = NUMERICOID;
        typmods[index] = LAPLACE_PG_NUMERIC_TYPMOD(20, 0);
    }
    types[49] = INT4OID;
    types[50] = INT4OID;
    laplace_pg_composite_binding_open(
        "source_profile_manifest", types, typmods, 51, binding);
}

static Datum source_profile_record(
    const laplace_pg_composite_binding* binding,
    const laplace_source_profile_manifest* profile) {
    Datum fields[51];
    bool nulls[51] = {false};
    const laplace_digest256* fingerprints[12] = {
        &profile->authority_release_fingerprint,
        &profile->license_fingerprint,
        &profile->artifact_graph_fingerprint,
        &profile->syntax_authority_fingerprint,
        &profile->recipe_program_fingerprint,
        &profile->universal_ast_mapping_fingerprint,
        &profile->highway_references_fingerprint,
        &profile->epistemic_witnessing_fingerprint,
        &profile->denominator_declaration_fingerprint,
        &profile->conformance_fingerprint,
        &profile->completion_law_fingerprint,
        &profile->selected_boundary_fingerprint};
    const uint64_t counts[30] = {
        profile->byte_count, profile->container_count, profile->member_count,
        profile->file_count, profile->record_count, profile->field_count,
        profile->syntax_node_count, profile->span_count, profile->edge_count,
        profile->reference_count, profile->occurrence_count,
        profile->claim_count, profile->mapping_count, profile->error_count,
        profile->unknown_count, profile->transformation_count,
        profile->output_count, profile->closure_subject_count,
        profile->accepted_count, profile->rejected_count,
        profile->duplicate_count, profile->reused_count,
        profile->transformed_count, profile->lossy_count,
        profile->unsupported_count, profile->malformed_count,
        profile->unresolved_count, profile->persisted_count,
        profile->derived_count, profile->not_applicable_mask};
    size_t index;
    fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->profile_id.bytes, 32u));
    fields[1] = Int32GetDatum((int32)profile->coordinate.kind);
    fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->coordinate.authority.bytes, 16u));
    fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->coordinate.release.bytes, 16u));
    fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->coordinate.name_space.bytes, 16u));
    fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
        profile->coordinate.local_identifier.bytes, 16u));
    fields[6] = laplace_pg_numeric_from_uint64(profile->coordinate.version);
    for (index = 0u; index < 12u; ++index) {
        fields[7u + index] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            fingerprints[index]->bytes, 32u));
    }
    for (index = 0u; index < 30u; ++index) {
        fields[19u + index] = laplace_pg_numeric_from_uint64(counts[index]);
    }
    fields[49] = Int32GetDatum((int32)profile->reconstruction_class);
    fields[50] = Int32GetDatum((int32)profile->flags);
    return laplace_pg_composite_record(binding, fields, nulls);
}

static ArrayType* profile_array(
    const laplace_source_profile_manifest* profile,
    Oid* array_oid) {
    laplace_pg_composite_binding binding;
    Datum value;
    ArrayType* result;
    source_profile_binding_open(&binding);
    value = source_profile_record(&binding, profile);
    result = laplace_pg_composite_array(&binding, &value, 1u);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static laplace_reference_candidate* build_reference_candidates(
    const laplace_tabular_source_plan_view* plan,
    const laplace_pg_composition_execution* execution,
    const laplace_source_profile_manifest* profile) {
    laplace_reference_candidate* candidates;
    size_t index;
    if (plan->reference_occurrence_count == 0u ||
        plan->reference_occurrence_count > SIZE_MAX) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace source profile has no typed reference occurrences")));
    }
    candidates = (laplace_reference_candidate*)palloc0(
        sizeof(*candidates) * (size_t)plan->reference_occurrence_count);
    for (index = 0u;
         index < (size_t)plan->reference_occurrence_count; ++index) {
        const laplace_tabular_reference_occurrence* occurrence =
            &plan->reference_occurrences[index];
        laplace_reference_candidate* candidate = &candidates[index];
        if (occurrence->row_result_index >= execution->result_count ||
            occurrence->field_result_index >= execution->result_count ||
            occurrence->value_result_index >= execution->result_count ||
            occurrence->source_ordinal == 0u ||
            occurrence->artifact_ordinal == 0u ||
            occurrence->row_ordinal == 0u ||
            occurrence->column_ordinal == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source reference does not bind exact deposited content")));
        }
        candidate->source_profile_id = profile->profile_id;
        candidate->key.kind = occurrence->kind;
        candidate->key.authority = profile->coordinate.authority;
        candidate->key.release = profile->coordinate.release;
        candidate->key.name_space = occurrence->name_space;
        candidate->key.local_identifier =
            execution->results[occurrence->value_result_index].entity_id;
        candidate->key.version = profile->coordinate.version;
        candidate->row_entity_id =
            execution->results[occurrence->row_result_index].entity_id;
        candidate->field_entity_id =
            execution->results[occurrence->field_result_index].entity_id;
        candidate->value_entity_id =
            execution->results[occurrence->value_result_index].entity_id;
        candidate->source_ordinal = occurrence->source_ordinal;
        candidate->artifact_ordinal = occurrence->artifact_ordinal;
        candidate->row_ordinal = occurrence->row_ordinal;
        candidate->column_ordinal = occurrence->column_ordinal;
        candidate->rule_flags = occurrence->rule_flags;
    }
    return candidates;
}

#if 0
static ArrayType* reference_candidate_array(
    const laplace_reference_candidate* candidates,
    size_t candidate_count,
    Oid* array_oid) {
    static const Oid types[15] = {
        BYTEAOID, INT4OID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID, INT4OID};
    static const int32 typmods[15] = {
        -1, -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0), -1};
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * candidate_count);
    size_t index;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "reference_candidate", types, typmods, 15, &binding);
    for (index = 0u; index < candidate_count; ++index) {
        const laplace_reference_candidate* candidate = &candidates[index];
        Datum fields[15];
        bool nulls[15] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->source_profile_id.bytes, 32u));
        fields[1] = Int32GetDatum((int32)candidate->key.kind);
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->key.authority.bytes, 16u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->key.release.bytes, 16u));
        fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->key.name_space.bytes, 16u));
        fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->key.local_identifier.bytes, 16u));
        fields[6] = laplace_pg_numeric_from_uint64(candidate->key.version);
        fields[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->row_entity_id.bytes, 16u));
        fields[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->field_entity_id.bytes, 16u));
        fields[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->value_entity_id.bytes, 16u));
        fields[10] = laplace_pg_numeric_from_uint64(candidate->source_ordinal);
        fields[11] = laplace_pg_numeric_from_uint64(candidate->artifact_ordinal);
        fields[12] = laplace_pg_numeric_from_uint64(candidate->row_ordinal);
        fields[13] = laplace_pg_numeric_from_uint64(candidate->column_ordinal);
        fields[14] = Int32GetDatum((int32)candidate->rule_flags);
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    result = laplace_pg_composite_array(
        &binding, rows, (uint64_t)candidate_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}
#endif

static laplace_reference_mapping_candidate* build_mapping_candidates(
    const laplace_tabular_source_plan_view* plan,
    const laplace_pg_composition_execution* execution,
    const laplace_source_profile_manifest* profile,
    const laplace_reference_candidate* reference_candidates,
    const laplace_reference_record* reference_records) {
    laplace_reference_mapping_candidate* candidates;
    size_t index;
    if (plan->mapping_occurrence_count == 0u ||
        plan->mapping_occurrence_count > SIZE_MAX ||
        reference_candidates == NULL || reference_records == NULL) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace source profile has no complete mapping occurrences")));
    }
    candidates = (laplace_reference_mapping_candidate*)palloc0(
        sizeof(*candidates) * (size_t)plan->mapping_occurrence_count);
    for (index = 0u; index < (size_t)plan->mapping_occurrence_count; ++index) {
        const laplace_tabular_mapping_occurrence* occurrence =
            &plan->mapping_occurrences[index];
        laplace_reference_mapping_candidate* candidate = &candidates[index];
        const laplace_reference_candidate* left;
        const laplace_reference_candidate* right;
        const laplace_reference_record* left_record;
        const laplace_reference_record* right_record;
        if (occurrence->left_reference_occurrence_index >=
                plan->reference_occurrence_count ||
            occurrence->right_reference_occurrence_index >=
                plan->reference_occurrence_count ||
            occurrence->row_result_index >= execution->result_count ||
            occurrence->relation_result_index >= execution->result_count ||
            occurrence->source_ordinal == 0u ||
            occurrence->artifact_ordinal == 0u ||
            occurrence->row_ordinal == 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source mapping does not bind exact reference occurrences")));
        }
        left = &reference_candidates[
            occurrence->left_reference_occurrence_index];
        right = &reference_candidates[
            occurrence->right_reference_occurrence_index];
        left_record = &reference_records[
            occurrence->left_reference_occurrence_index];
        right_record = &reference_records[
            occurrence->right_reference_occurrence_index];
        if (memcmp(left->source_profile_id.bytes, profile->profile_id.bytes,
                   32u) != 0 ||
            memcmp(right->source_profile_id.bytes, profile->profile_id.bytes,
                   32u) != 0 ||
            memcmp(left->row_entity_id.bytes, right->row_entity_id.bytes,
                   16u) != 0 ||
            left->source_ordinal != occurrence->source_ordinal ||
            right->source_ordinal != occurrence->source_ordinal ||
            left->artifact_ordinal != occurrence->artifact_ordinal ||
            right->artifact_ordinal != occurrence->artifact_ordinal ||
            left->row_ordinal != occurrence->row_ordinal ||
            right->row_ordinal != occurrence->row_ordinal) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source mapping crosses its declared row or profile boundary")));
        }
        candidate->boundary_id = profile->selected_boundary_fingerprint;
        candidate->source_profile_id = profile->profile_id;
        candidate->left_reference_id = left_record->reference_id;
        candidate->right_reference_id = right_record->reference_id;
        candidate->left_coordinate = left_record->coordinate;
        candidate->right_coordinate = right_record->coordinate;
        candidate->relation_id = execution->results[
            occurrence->relation_result_index].entity_id;
        candidate->row_entity_id = left->row_entity_id;
        candidate->left_field_entity_id = left->field_entity_id;
        candidate->left_value_entity_id = left->value_entity_id;
        candidate->right_field_entity_id = right->field_entity_id;
        candidate->right_value_entity_id = right->value_entity_id;
        candidate->source_ordinal = occurrence->source_ordinal;
        candidate->artifact_ordinal = occurrence->artifact_ordinal;
        candidate->row_ordinal = occurrence->row_ordinal;
        candidate->relation_version = occurrence->relation_version;
        candidate->relation_kind = occurrence->relation_kind;
        candidate->flags = occurrence->flags;
        candidate->left_disposition = left_record->disposition;
        candidate->right_disposition = right_record->disposition;
    }
    return candidates;
}

#if 0
static ArrayType* mapping_candidate_array(
    const laplace_reference_mapping_candidate* candidates,
    size_t candidate_count,
    Oid* array_oid) {
    static const Oid types[26] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        BYTEAOID, BYTEAOID, INT4OID, NUMERICOID,
        BYTEAOID, BYTEAOID, INT4OID, NUMERICOID,
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, NUMERICOID, NUMERICOID, NUMERICOID,
        INT4OID, INT4OID, INT4OID, INT4OID};
    static const int32 typmods[26] = {
        -1, -1, -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * candidate_count);
    size_t index;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "reference_mapping_candidate", types, typmods, 26, &binding);
    for (index = 0u; index < candidate_count; ++index) {
        const laplace_reference_mapping_candidate* candidate =
            &candidates[index];
        Datum fields[26];
        bool nulls[26] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->boundary_id.bytes, 32u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->source_profile_id.bytes, 32u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->left_reference_id.bytes, 32u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->right_reference_id.bytes, 32u));
        fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->left_coordinate.coordinate.bytes, 16u));
        fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->left_coordinate.collision_fingerprint.bytes, 32u));
        fields[6] = Int32GetDatum((int32)candidate->left_coordinate.kind);
        fields[7] = laplace_pg_numeric_from_uint64(
            candidate->left_coordinate.version);
        fields[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->right_coordinate.coordinate.bytes, 16u));
        fields[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->right_coordinate.collision_fingerprint.bytes, 32u));
        fields[10] = Int32GetDatum((int32)candidate->right_coordinate.kind);
        fields[11] = laplace_pg_numeric_from_uint64(
            candidate->right_coordinate.version);
        fields[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->relation_id.bytes, 16u));
        fields[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->row_entity_id.bytes, 16u));
        fields[14] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->left_field_entity_id.bytes, 16u));
        fields[15] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->left_value_entity_id.bytes, 16u));
        fields[16] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->right_field_entity_id.bytes, 16u));
        fields[17] = PointerGetDatum(laplace_pg_bytes_to_bytea(
            candidate->right_value_entity_id.bytes, 16u));
        fields[18] = laplace_pg_numeric_from_uint64(candidate->source_ordinal);
        fields[19] = laplace_pg_numeric_from_uint64(candidate->artifact_ordinal);
        fields[20] = laplace_pg_numeric_from_uint64(candidate->row_ordinal);
        fields[21] = laplace_pg_numeric_from_uint64(candidate->relation_version);
        fields[22] = Int32GetDatum((int32)candidate->relation_kind);
        fields[23] = Int32GetDatum((int32)candidate->flags);
        fields[24] = Int32GetDatum((int32)candidate->left_disposition);
        fields[25] = Int32GetDatum((int32)candidate->right_disposition);
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    result = laplace_pg_composite_array(
        &binding, rows, (uint64_t)candidate_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}
#endif

static ArrayType* lineage_array(
    const laplace_pg_source_claim* claims,
    size_t claim_count,
    Oid* array_oid) {
    static const Oid types[11] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, INT4OID, INT4OID, INT4OID, INT4OID};
    static const int32 typmods[11] = {
        -1, -1, -1, -1, -1, -1, LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * claim_count);
    size_t index;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "evidence_lineage_record", types, typmods, 11, &binding);
    for (index = 0u; index < claim_count; ++index) {
        const laplace_evidence_lineage_record* record = &claims[index].lineage;
        Datum fields[11];
        bool nulls[11] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->node_id.bytes, 32u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->proposition_id.bytes, 16u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->occurrence_id.bytes, 32u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->source_id.bytes, 32u));
        fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->context_id.bytes, 32u));
        fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->parent_node_id.bytes, 32u));
        fields[6] = laplace_pg_numeric_from_uint64(record->source_ordinal);
        fields[7] = Int32GetDatum((int32)record->record_kind);
        fields[8] = Int32GetDatum((int32)record->epistemic_kind);
        fields[9] = Int32GetDatum((int32)record->flags);
        fields[10] = Int32GetDatum((int32)record->reserved);
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    result = laplace_pg_composite_array(&binding, rows, (uint64_t)claim_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* testimony_array(
    const laplace_evidence_testimony_record* records,
    size_t record_count,
    Oid* array_oid) {
    static const Oid types[13] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID,
        NUMERICOID, NUMERICOID, NUMERICOID,
        INT4OID, INT4OID, INT4OID, INT4OID};
    static const int32 typmods[13] = {
        -1, -1, -1, -1, -1, -1,
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        LAPLACE_PG_NUMERIC_TYPMOD(20, 0),
        -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum* rows = (Datum*)palloc(sizeof(*rows) * record_count);
    size_t index;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "evidence_testimony_record", types, typmods, 13, &binding);
    for (index = 0u; index < record_count; ++index) {
        const laplace_evidence_testimony_record* record = &records[index];
        Datum fields[13];
        bool nulls[13] = {false};
        fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->testimony_id.bytes, 32u));
        fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->evidence_node_id.bytes, 32u));
        fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->source_profile_id.bytes, 32u));
        fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->recipe_receipt_id.bytes, 32u));
        fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->trust_input_id.bytes, 32u));
        fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(record->outcome_detail_id.bytes, 32u));
        fields[6] = laplace_pg_numeric_from_uint64(record->uncertainty_numerator);
        fields[7] = laplace_pg_numeric_from_uint64(record->uncertainty_denominator);
        fields[8] = laplace_pg_numeric_from_uint64(record->sample_count);
        fields[9] = Int32GetDatum((int32)record->source_type);
        fields[10] = Int32GetDatum((int32)record->outcome_type);
        fields[11] = Int32GetDatum((int32)record->disposition);
        fields[12] = Int32GetDatum((int32)record->flags);
        rows[index] = laplace_pg_composite_record(&binding, fields, nulls);
    }
    result = laplace_pg_composite_array(&binding, rows, (uint64_t)record_count);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static ArrayType* world_request_array(
    const laplace_source_profile_manifest* profile,
    const laplace_pg_composition_execution* execution,
    const laplace_pg_source_stage_receipts* receipts,
    Oid* array_oid) {
    static const Oid types[6] = {
        BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID, BYTEAOID};
    static const int32 typmods[6] = {-1, -1, -1, -1, -1, -1};
    laplace_pg_composite_binding binding;
    Datum fields[6];
    bool nulls[6] = {false};
    Datum row;
    ArrayType* result;
    laplace_pg_composite_binding_open(
        "world_admission_request", types, typmods, 6, &binding);
    fields[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(profile->profile_id.bytes, 32u));
    fields[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts->source_profile.bytes, 32u));
    fields[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(profile->recipe_program_fingerprint.bytes, 32u));
    fields[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution->summary.receipt_id.bytes, 32u));
    fields[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts->evidence_lineage.bytes, 32u));
    fields[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts->evidence_testimony.bytes, 32u));
    row = laplace_pg_composite_record(&binding, fields, nulls);
    result = laplace_pg_composite_array(&binding, &row, 1u);
    *array_oid = binding.array_oid;
    laplace_pg_composite_binding_close(&binding);
    return result;
}

static void require_spi_result(int result, int columns, const char* stage) {
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        SPI_tuptable == NULL || SPI_tuptable->tupdesc->natts != columns) {
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace %s did not return one exact receipt", stage)));
    }
}

static void call_source_profile(
    Datum context,
    ArrayType* profiles,
    Oid profile_array_oid,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT (r).source_profile_receipt_id,(r).isa_receipt_id "
        "FROM (SELECT " LAPLACE_PG_SCHEMA "." LAPLACE_PG_SOURCE_PROFILE_VALIDATE_SQL
        "($1,$2) AS r) q";
    Oid types[2] = {
        laplace_pg_composite_type_oid("execution_context"), profile_array_oid};
    Datum values[2] = {context, PointerGetDatum(profiles)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                        errmsg("Laplace source-profile stage could not connect")));
    }
    result = laplace_pg_source_execute_with_args(sql, 2, types, values, NULL, false, 0);
    require_spi_result(result, 2, "source-profile stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "source-profile receipt"),
        &receipts->source_profile, "source-profile receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "source-profile ISA receipt"),
        &receipts->source_profile_isa, "source-profile ISA receipt");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("Laplace source-profile stage could not close")));
    }
}

#if 0
static void call_reference_topology(
    Datum context,
    ArrayType* candidates,
    Oid candidate_array_oid,
    uint64_t preferred_batch_bytes,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT (r).reference_topology_receipt_id,(r).isa_receipt_id,"
        "(r).occurrence_count,(r).coordinate_count,(r).present_count,"
        "(r).retired_count,(r).unresolved_count,"
        "(r).persistence_batch_count,"
        "(r).maximum_persistence_batch_records,"
        "(r).maximum_encoded_persistence_batch_bytes,"
        "(r).minimum_encoded_persistence_record_bytes FROM (SELECT "
        LAPLACE_PG_SCHEMA "." LAPLACE_PG_REFERENCE_TOPOLOGY_RESOLVE_SQL
        "($1,$2,$3) AS r) q";
    Oid types[3] = {
        laplace_pg_composite_type_oid("execution_context"),
        candidate_array_oid, NUMERICOID};
    Datum values[3] = {
        context, PointerGetDatum(candidates),
        laplace_pg_numeric_from_uint64(preferred_batch_bytes)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace reference-topology stage could not connect")));
    }
    result = laplace_pg_source_execute_with_args(sql, 3, types, values, NULL, false, 0);
    require_spi_result(result, 11, "reference-topology stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "reference-topology receipt"),
        &receipts->reference_topology, "reference-topology receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "reference-topology ISA receipt"),
        &receipts->reference_topology_isa, "reference-topology ISA receipt");
    receipts->reference_occurrence_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
                             "reference occurrence count"),
        "reference occurrence count");
    receipts->reference_coordinate_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 4,
                             "reference coordinate count"),
        "reference coordinate count");
    receipts->reference_present_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 5,
                             "reference present count"),
        "reference present count");
    receipts->reference_retired_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 6,
                             "reference retired count"),
        "reference retired count");
    receipts->reference_unresolved_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 7,
                             "reference unresolved count"),
        "reference unresolved count");
    receipts->reference_persistence_batch_count =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 8,
            "reference persistence batch count"),
            "reference persistence batch count");
    receipts->reference_maximum_persistence_batch_records =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 9,
            "reference maximum persistence batch records"),
            "reference maximum persistence batch records");
    receipts->reference_maximum_encoded_persistence_batch_bytes =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 10,
            "reference maximum encoded persistence batch bytes"),
            "reference maximum encoded persistence batch bytes");
    receipts->reference_minimum_encoded_persistence_record_bytes =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 11,
            "reference minimum encoded persistence record bytes"),
            "reference minimum encoded persistence record bytes");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace reference-topology stage could not close")));
    }
}

static void call_reference_mapping(
    Datum context,
    ArrayType* candidates,
    Oid candidate_array_oid,
    uint64_t preferred_batch_bytes,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT (r).reference_mapping_receipt_id,(r).isa_receipt_id,"
        "(r).occurrence_count,(r).proposition_count,(r).resolved_count,"
        "(r).unresolved_count,(r).retired_count,"
        "(r).persistence_batch_count,"
        "(r).maximum_persistence_batch_records,"
        "(r).maximum_encoded_persistence_batch_bytes,"
        "(r).minimum_encoded_persistence_record_bytes FROM (SELECT "
        LAPLACE_PG_SCHEMA "." LAPLACE_PG_REFERENCE_MAPPING_RESOLVE_SQL
        "($1,$2,$3) AS r) q";
    Oid types[3] = {
        laplace_pg_composite_type_oid("execution_context"),
        candidate_array_oid, NUMERICOID};
    Datum values[3] = {
        context, PointerGetDatum(candidates),
        laplace_pg_numeric_from_uint64(preferred_batch_bytes)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR,
                (errcode(ERRCODE_CONNECTION_FAILURE),
                 errmsg("Laplace reference-mapping stage could not connect")));
    }
    result = laplace_pg_source_execute_with_args(sql, 3, types, values, NULL, false, 0);
    require_spi_result(result, 11, "reference-mapping stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "reference-mapping receipt"),
        &receipts->reference_mapping, "reference-mapping receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "reference-mapping ISA receipt"),
        &receipts->reference_mapping_isa, "reference-mapping ISA receipt");
    receipts->reference_mapping_occurrence_count =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
            "reference-mapping occurrence count"),
            "reference-mapping occurrence count");
    receipts->reference_mapping_proposition_count =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 4,
            "reference-mapping proposition count"),
            "reference-mapping proposition count");
    receipts->reference_mapping_resolved_count =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 5,
            "reference-mapping resolved count"),
            "reference-mapping resolved count");
    receipts->reference_mapping_unresolved_count =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 6,
            "reference-mapping unresolved count"),
            "reference-mapping unresolved count");
    receipts->reference_mapping_retired_count =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 7,
            "reference-mapping retired count"),
            "reference-mapping retired count");
    receipts->reference_mapping_persistence_batch_count =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 8,
            "reference-mapping persistence batch count"),
            "reference-mapping persistence batch count");
    receipts->reference_mapping_maximum_persistence_batch_records =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 9,
            "reference-mapping maximum persistence batch records"),
            "reference-mapping maximum persistence batch records");
    receipts->reference_mapping_maximum_encoded_persistence_batch_bytes =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 10,
            "reference-mapping maximum encoded persistence batch bytes"),
            "reference-mapping maximum encoded persistence batch bytes");
    receipts->reference_mapping_minimum_encoded_persistence_record_bytes =
        laplace_pg_uint64_from_numeric(required_tuple_value(
            SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 11,
            "reference-mapping minimum encoded persistence record bytes"),
            "reference-mapping minimum encoded persistence record bytes");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR,
                (errcode(ERRCODE_INTERNAL_ERROR),
                 errmsg("Laplace reference-mapping stage could not close")));
    }
}
#endif

static void call_lineage(
    Datum context,
    ArrayType* lineage,
    Oid lineage_array_oid,
    uint64_t root_capacity,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT (r).lineage_receipt_id,(r).isa_receipt_id,(r).node_count "
        "FROM (SELECT " LAPLACE_PG_SCHEMA "." LAPLACE_PG_EVIDENCE_RECORD_SQL
        "($1,$2,$3) AS r) q";
    Oid types[3] = {
        laplace_pg_composite_type_oid("execution_context"),
        lineage_array_oid, NUMERICOID};
    Datum values[3] = {
        context, PointerGetDatum(lineage),
        laplace_pg_numeric_from_uint64(root_capacity)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                        errmsg("Laplace evidence-lineage stage could not connect")));
    }
    result = laplace_pg_source_execute_with_args(sql, 3, types, values, NULL, false, 0);
    require_spi_result(result, 3, "evidence-lineage stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "lineage receipt"),
        &receipts->evidence_lineage, "lineage receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "lineage ISA receipt"),
        &receipts->evidence_lineage_isa, "lineage ISA receipt");
    receipts->evidence_node_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
                             "lineage node count"),
        "lineage node count");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("Laplace evidence-lineage stage could not close")));
    }
}

static void call_testimony(
    Datum context,
    ArrayType* testimony,
    Oid testimony_array_oid,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT (r).testimony_receipt_id,(r).isa_receipt_id,(r).testimony_count "
        "FROM (SELECT " LAPLACE_PG_SCHEMA "." LAPLACE_PG_EVIDENCE_TESTIMONY_SQL
        "($1,$2) AS r) q";
    Oid types[2] = {
        laplace_pg_composite_type_oid("execution_context"),
        testimony_array_oid};
    Datum values[2] = {context, PointerGetDatum(testimony)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                        errmsg("Laplace evidence-testimony stage could not connect")));
    }
    result = laplace_pg_source_execute_with_args(sql, 2, types, values, NULL, false, 0);
    require_spi_result(result, 3, "evidence-testimony stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "testimony receipt"),
        &receipts->evidence_testimony, "testimony receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "testimony ISA receipt"),
        &receipts->evidence_testimony_isa, "testimony ISA receipt");
    receipts->testimony_count = laplace_pg_uint64_from_numeric(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
                             "testimony count"),
        "testimony count");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("Laplace evidence-testimony stage could not close")));
    }
}

static void call_world_admission(
    Datum context,
    ArrayType* request,
    Oid request_array_oid,
    laplace_pg_source_stage_receipts* receipts) {
    static const char sql[] =
        "SELECT ((r).admission_ids)[1],(r).world_admission_receipt_id,"
        "(r).isa_receipt_id FROM (SELECT " LAPLACE_PG_SCHEMA "."
        LAPLACE_PG_WORLD_ADMISSION_CLOSE_SQL "($1,$2) AS r) q";
    Oid types[2] = {
        laplace_pg_composite_type_oid("execution_context"), request_array_oid};
    Datum values[2] = {context, PointerGetDatum(request)};
    int result;
    if (SPI_connect() != SPI_OK_CONNECT) {
        ereport(ERROR, (errcode(ERRCODE_CONNECTION_FAILURE),
                        errmsg("Laplace world-admission stage could not connect")));
    }
    result = laplace_pg_source_execute_with_args(sql, 2, types, values, NULL, false, 0);
    require_spi_result(result, 3, "world-admission stage");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1,
                             "world admission id"),
        &receipts->world_admission_id, "world admission id");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 2,
                             "world-admission receipt"),
        &receipts->world_admission, "world-admission receipt");
    laplace_pg_read_digest(
        required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 3,
                             "world-admission ISA receipt"),
        &receipts->world_admission_isa, "world-admission ISA receipt");
    if (SPI_finish() != SPI_OK_FINISH) {
        ereport(ERROR, (errcode(ERRCODE_INTERNAL_ERROR),
                        errmsg("Laplace world-admission stage could not close")));
    }
}

static laplace_pg_source_claim* build_claims(
    const laplace_tabular_source_plan_view* plan,
    const laplace_pg_composition_execution* execution) {
    laplace_pg_source_claim* claims;
    laplace_tabular_source_status status;
    if (plan->claim_count == 0u || plan->claim_count > MaxAllocSize / sizeof(*claims)) {
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace claim transport exceeds its allocation bound")));
    }
    claims = (laplace_pg_source_claim*)palloc(sizeof(*claims) * (size_t)plan->claim_count);
    status = laplace_source_claims_build_batch(plan, execution->results,
        execution->result_count, claims, (size_t)plan->claim_count);
    if (status != LAPLACE_TABULAR_SOURCE_OK) {
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace native source claim construction failed"),
            errdetail("native_status=%u", (unsigned)status)));
    }
    return claims;
}

static laplace_evidence_testimony_record* build_testimony(
    const laplace_pg_source_claim* claims, size_t claim_count,
    const laplace_source_profile_manifest* profile,
    const laplace_digest256* source_fingerprint) {
    laplace_evidence_testimony_record* records;
    laplace_tabular_source_status status;
    if (claim_count == 0u || claim_count > MaxAllocSize / sizeof(*records)) {
        ereport(ERROR, (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
            errmsg("Laplace testimony transport exceeds its allocation bound")));
    }
    records = (laplace_evidence_testimony_record*)palloc(sizeof(*records) * claim_count);
    status = laplace_source_testimony_build_batch(claims, claim_count, profile,
        source_fingerprint, records, claim_count);
    if (status != LAPLACE_TABULAR_SOURCE_OK) {
        ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
            errmsg("Laplace native source testimony construction failed"),
            errdetail("native_status=%u", (unsigned)status)));
    }
    return records;
}

Datum LAPLACE_PG_SOURCE_ADMIT_TABULAR_SYMBOL(PG_FUNCTION_ARGS) {
    laplace_framework_context context;
    laplace_source_profile_manifest profile_declaration;
    laplace_source_profile_manifest profile;
    laplace_digest256 geometry_epoch;
    laplace_digest256 occurrence_context;
    laplace_tabular_artifact* artifacts;
    size_t artifact_count = 0u;
    laplace_tabular_reference_rule* reference_rules;
    size_t reference_rule_count = 0u;
    laplace_tabular_mapping_rule* mapping_rules;
    size_t mapping_rule_count = 0u;
    laplace_tabular_source_input source_input;
    laplace_tabular_source_plan* source_plan = NULL;
    laplace_tabular_source_plan_view plan;
    laplace_composition_known_entity* known = NULL;
    laplace_pg_active_unicode_root active_unicode;
    laplace_composition_working_set_input composition_input;
    laplace_pg_composition_execution execution;
    laplace_pg_source_claim* claims = NULL;
    laplace_evidence_testimony_record* testimonies = NULL;
    laplace_reference_candidate* reference_candidates = NULL;
    laplace_reference_record* reference_records = NULL;
    laplace_reference_mapping_candidate* mapping_candidates = NULL;
    laplace_reference_mapping_record* mapping_records = NULL;
    laplace_pg_reference_topology_execution reference_execution;
    laplace_pg_reference_mapping_execution mapping_execution;
    laplace_pg_source_stage_receipts receipts;
    ArrayType* profiles;
    ArrayType* lineage;
    ArrayType* testimony;
    ArrayType* world_request;
    Oid profile_array_oid;
    Oid lineage_array_oid;
    Oid testimony_array_oid;
    Oid world_request_array_oid;
    const laplace_composition_result* root;
    Datum result_values[50];
    bool result_nulls[50] = {false};
    HeapTuple result_tuple;
    const Datum context_datum = PG_GETARG_DATUM(0);
    const uint64_t preferred_batch_bytes = laplace_pg_uint64_from_numeric(
        PG_GETARG_DATUM(7), "source preferred_batch_bytes");
    laplace_tabular_source_status source_status;

    memset(&profile_declaration, 0, sizeof(profile_declaration));
    memset(&profile, 0, sizeof(profile));
    memset(&source_input, 0, sizeof(source_input));
    memset(&plan, 0, sizeof(plan));
    memset(&active_unicode, 0, sizeof(active_unicode));
    memset(&composition_input, 0, sizeof(composition_input));
    memset(&execution, 0, sizeof(execution));
    memset(&receipts, 0, sizeof(receipts));
    memset(&reference_execution, 0, sizeof(reference_execution));
    memset(&mapping_execution, 0, sizeof(mapping_execution));
    laplace_pg_read_execution_context(context_datum, &context);
    if ((context.flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0u) {
        ereport(ERROR,
                (errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
                 errmsg("Laplace source admission requires a writable execution context")));
    }
    laplace_pg_read_source_profile(
        DatumGetHeapTupleHeader(PG_GETARG_DATUM(1)), &profile_declaration);
    laplace_pg_read_digest(
        PG_GETARG_DATUM(2), &geometry_epoch, "source geometry_epoch");
    laplace_pg_read_digest(
        PG_GETARG_DATUM(3), &occurrence_context,
        "source occurrence_context_fingerprint");
    artifacts = read_artifacts(PG_GETARG_ARRAYTYPE_P(4), &artifact_count,
        context.resource_grant.memory_bytes);
    reference_rules = read_reference_rules(
        PG_GETARG_ARRAYTYPE_P(5), &reference_rule_count);
    mapping_rules = read_mapping_rules(
        PG_GETARG_ARRAYTYPE_P(6), &mapping_rule_count);
    source_input.profile_declaration = profile_declaration;
    source_input.geometry_epoch = geometry_epoch;
    source_input.occurrence_context_fingerprint = occurrence_context;
    source_input.artifacts = artifacts;
    source_input.artifact_count = (uint64_t)artifact_count;
    source_input.reference_rules = reference_rules;
    source_input.reference_rule_count = (uint64_t)reference_rule_count;
    source_input.mapping_rules = mapping_rules;
    source_input.mapping_rule_count = (uint64_t)mapping_rule_count;
    source_input.preferred_batch_bytes = preferred_batch_bytes;
    source_status = laplace_pg_tabular_source_plan_create_recursive(
        &source_input, &source_plan);
    if (source_status != LAPLACE_TABULAR_SOURCE_OK || source_plan == NULL ||
        laplace_tabular_source_plan_view_get(source_plan, &plan) !=
            LAPLACE_TABULAR_SOURCE_OK) {
        laplace_tabular_source_plan_destroy(&source_plan);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_EXCEPTION),
                 errmsg("Laplace tabular source compilation failed"),
                 errdetail("status=%d", (int)source_status)));
    }
    if (plan.atom_count == 0u || plan.atom_count > SIZE_MAX ||
        plan.request_count == 0u || plan.claim_count == 0u ||
        plan.root_result_index >= plan.request_count) {
        laplace_tabular_source_plan_destroy(&source_plan);
        ereport(ERROR,
                (errcode(ERRCODE_DATA_CORRUPTED),
                 errmsg("Laplace tabular source plan is incomplete")));
    }

    PG_TRY();
    {
        known = (laplace_composition_known_entity*)palloc0(
            sizeof(*known) * (size_t)plan.atom_count);
        laplace_pg_resolve_active_unicode_atoms(
            &context, plan.atom_positions, (size_t)plan.atom_count,
            known, &active_unicode);
        composition_input.context = &context;
        composition_input.source_fingerprint = &plan.source_fingerprint;
        composition_input.calculation_recipe_fingerprint =
            &plan.profile.recipe_program_fingerprint;
        composition_input.known_entities = known;
        composition_input.known_entity_count = plan.atom_count;
        composition_input.operands = plan.operands;
        composition_input.operand_count = plan.operand_count;
        composition_input.requests = plan.requests;
        composition_input.request_count = plan.request_count;
        composition_input.preferred_batch_bytes = preferred_batch_bytes;
        laplace_pg_source_composition_execute(&composition_input, &execution);
        laplace_pg_source_composition_persist_receipt(
            &execution, &composition_input);
        if (laplace_tabular_source_profile_finalize(
                source_plan, &execution.summary, &profile) !=
            LAPLACE_TABULAR_SOURCE_OK) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace tabular source profile did not close over deposition")));
        }
        profiles = profile_array(&profile, &profile_array_oid);
        call_source_profile(
            context_datum, profiles, profile_array_oid, &receipts);
        /* Deposit the owner before its witnesses. Keep both in this admission
         * transaction, including when the caller requests immediate FK checks. */
        laplace_pg_persist_source_structural_witnesses(
            source_plan, &execution, &composition_input, &profile);
        if (plan.reference_occurrence_count != 0u) {
            reference_candidates = build_reference_candidates(
                &plan, &execution, &profile);
            reference_records = (laplace_reference_record*)palloc0(
                sizeof(*reference_records) *
                (size_t)plan.reference_occurrence_count);
            laplace_pg_reference_topology_execute_and_persist(
                &context, reference_candidates,
                (size_t)plan.reference_occurrence_count,
                preferred_batch_bytes, reference_records,
                &reference_execution);
            receipts.reference_topology =
                reference_execution.semantic_receipt.receipt_id;
            receipts.reference_topology_isa =
                reference_execution.isa_receipt.receipt_id;
            receipts.reference_occurrence_count =
                reference_execution.semantic_receipt.occurrence_count;
            receipts.reference_coordinate_count =
                reference_execution.semantic_receipt.coordinate_count;
            receipts.reference_present_count =
                reference_execution.semantic_receipt.present_count;
            receipts.reference_retired_count =
                reference_execution.semantic_receipt.retired_count;
            receipts.reference_unresolved_count =
                reference_execution.semantic_receipt.unresolved_count;
            receipts.reference_persistence_batch_count =
                reference_execution.persistence.batch_count;
            receipts.reference_maximum_persistence_batch_records =
                reference_execution.persistence.maximum_batch_records;
            receipts.reference_maximum_encoded_persistence_batch_bytes =
                reference_execution.persistence.maximum_encoded_batch_bytes;
            receipts.reference_minimum_encoded_persistence_record_bytes =
                reference_execution.persistence.minimum_encoded_record_bytes;
            if (receipts.reference_occurrence_count !=
                    plan.reference_occurrence_count ||
                memcmp(receipts.reference_topology.bytes,
                       reference_execution.semantic_receipt.receipt_id.bytes,
                       32u) != 0 ||
                receipts.reference_coordinate_count !=
                    reference_execution.semantic_receipt.coordinate_count ||
                receipts.reference_present_count +
                    receipts.reference_retired_count +
                    receipts.reference_unresolved_count !=
                    plan.reference_occurrence_count) {
                ereport(ERROR,
                        (errcode(ERRCODE_DATA_CORRUPTED),
                         errmsg("Laplace reference topology changed source cardinality")));
            }
            if (plan.mapping_occurrence_count != 0u) {
                mapping_candidates = build_mapping_candidates(
                    &plan, &execution, &profile,
                    reference_candidates, reference_records);
                mapping_records = (laplace_reference_mapping_record*)palloc0(
                    sizeof(*mapping_records) *
                    (size_t)plan.mapping_occurrence_count);
                laplace_pg_reference_mapping_execute_and_persist(
                    &context, mapping_candidates,
                    (size_t)plan.mapping_occurrence_count,
                    preferred_batch_bytes, mapping_records,
                    &mapping_execution);
                receipts.reference_mapping =
                    mapping_execution.semantic_receipt.receipt_id;
                receipts.reference_mapping_isa =
                    mapping_execution.isa_receipt.receipt_id;
                receipts.reference_mapping_occurrence_count =
                    mapping_execution.semantic_receipt.occurrence_count;
                receipts.reference_mapping_proposition_count =
                    mapping_execution.semantic_receipt.proposition_count;
                receipts.reference_mapping_resolved_count =
                    mapping_execution.semantic_receipt.resolved_count;
                receipts.reference_mapping_unresolved_count =
                    mapping_execution.semantic_receipt.unresolved_count;
                receipts.reference_mapping_retired_count =
                    mapping_execution.semantic_receipt.retired_count;
                receipts.reference_mapping_persistence_batch_count =
                    mapping_execution.persistence.batch_count;
                receipts.reference_mapping_maximum_persistence_batch_records =
                    mapping_execution.persistence.maximum_batch_records;
                receipts.reference_mapping_maximum_encoded_persistence_batch_bytes =
                    mapping_execution.persistence.maximum_encoded_batch_bytes;
                receipts.reference_mapping_minimum_encoded_persistence_record_bytes =
                    mapping_execution.persistence.minimum_encoded_record_bytes;
                if (receipts.reference_mapping_occurrence_count !=
                        plan.mapping_occurrence_count ||
                    memcmp(receipts.reference_mapping.bytes,
                           mapping_execution.semantic_receipt.receipt_id.bytes,
                           32u) != 0 ||
                    receipts.reference_mapping_proposition_count !=
                        mapping_execution.semantic_receipt.proposition_count ||
                    receipts.reference_mapping_resolved_count +
                        receipts.reference_mapping_unresolved_count +
                        receipts.reference_mapping_retired_count !=
                        plan.mapping_occurrence_count ||
                    profile.mapping_count != plan.mapping_occurrence_count) {
                    ereport(ERROR,
                            (errcode(ERRCODE_DATA_CORRUPTED),
                             errmsg("Laplace reference mapping changed source cardinality")));
                }
            }
        } else if (plan.mapping_occurrence_count != 0u) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source mapping bypassed reference topology")));
        }
        claims = build_claims(&plan, &execution);
        lineage = lineage_array(
            claims, (size_t)plan.claim_count, &lineage_array_oid);
        call_lineage(
            context_datum, lineage, lineage_array_oid,
            plan.claim_count, &receipts);
        testimonies = build_testimony(
            claims, (size_t)plan.claim_count, &profile,
            &plan.source_fingerprint);
        testimony = testimony_array(
            testimonies, (size_t)plan.claim_count, &testimony_array_oid);
        call_testimony(
            context_datum, testimony, testimony_array_oid, &receipts);
        world_request = world_request_array(
            &profile, &execution, &receipts, &world_request_array_oid);
        call_world_admission(
            context_datum, world_request, world_request_array_oid, &receipts);
        if (receipts.evidence_node_count != plan.claim_count ||
            receipts.testimony_count != plan.claim_count) {
            ereport(ERROR,
                    (errcode(ERRCODE_DATA_CORRUPTED),
                     errmsg("Laplace source evidence closure changed claim cardinality")));
        }
        root = &execution.results[plan.root_result_index];
        result_values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(profile.profile_id.bytes, 32u));
        result_values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.source_profile.bytes, 32u));
        result_values[2] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.source_profile_isa.bytes, 32u));
        if (plan.reference_occurrence_count == 0u) {
            result_nulls[3] = true;
            result_nulls[4] = true;
        } else {
            result_values[3] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                receipts.reference_topology.bytes, 32u));
            result_values[4] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                receipts.reference_topology_isa.bytes, 32u));
        }
        result_values[5] = PointerGetDatum(laplace_pg_bytes_to_bytea(plan.source_fingerprint.bytes, 32u));
        result_values[6] = PointerGetDatum(laplace_pg_bytes_to_bytea(plan.reconstruction_fingerprint.bytes, 32u));
        result_values[7] = PointerGetDatum(laplace_pg_bytes_to_bytea(active_unicode.root_receipt.bytes, 32u));
        result_values[8] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution.summary.receipt_id.bytes, 32u));
        result_values[9] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution.presence.semantic_receipt_id.bytes, 32u));
        result_values[10] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution.persistence.producer.receipt_id.bytes, 32u));
        result_values[11] = PointerGetDatum(laplace_pg_bytes_to_bytea(execution.persistence.producer.stream.receipt_id.bytes, 32u));
        result_values[12] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.evidence_lineage.bytes, 32u));
        result_values[13] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.evidence_lineage_isa.bytes, 32u));
        result_values[14] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.evidence_testimony.bytes, 32u));
        result_values[15] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.evidence_testimony_isa.bytes, 32u));
        result_values[16] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.world_admission_id.bytes, 32u));
        result_values[17] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.world_admission.bytes, 32u));
        result_values[18] = PointerGetDatum(laplace_pg_bytes_to_bytea(receipts.world_admission_isa.bytes, 32u));
        result_values[19] = PointerGetDatum(laplace_pg_bytes_to_bytea(root->entity_id.bytes, 16u));
        result_values[20] = PointerGetDatum(laplace_pg_bytes_to_bytea(root->physicality_id.bytes, 32u));
        result_values[21] = laplace_pg_numeric_from_uint64(plan.artifact_count);
        result_values[22] = laplace_pg_numeric_from_uint64(plan.claim_count);
        result_values[23] = laplace_pg_numeric_from_uint64(plan.request_count);
        result_values[24] = laplace_pg_numeric_from_uint64(execution.summary.occurrence_count);
        result_values[25] = laplace_pg_numeric_from_uint64(execution.summary.logical_occurrence_count);
        result_values[26] = laplace_pg_numeric_from_uint64(receipts.reference_occurrence_count);
        result_values[27] = laplace_pg_numeric_from_uint64(receipts.reference_coordinate_count);
        result_values[28] = laplace_pg_numeric_from_uint64(receipts.reference_present_count);
        result_values[29] = laplace_pg_numeric_from_uint64(receipts.reference_retired_count);
        result_values[30] = laplace_pg_numeric_from_uint64(receipts.reference_unresolved_count);
        result_values[31] = laplace_pg_numeric_from_uint64(receipts.evidence_node_count);
        result_values[32] = laplace_pg_numeric_from_uint64(receipts.testimony_count);
        result_values[33] = laplace_pg_numeric_from_uint64(execution.summary.stream_record_count);
        if (plan.mapping_occurrence_count == 0u) {
            result_nulls[34] = true;
            result_nulls[35] = true;
        } else {
            result_values[34] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                receipts.reference_mapping.bytes, 32u));
            result_values[35] = PointerGetDatum(laplace_pg_bytes_to_bytea(
                receipts.reference_mapping_isa.bytes, 32u));
        }
        result_values[36] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_occurrence_count);
        result_values[37] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_proposition_count);
        result_values[38] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_resolved_count);
        result_values[39] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_unresolved_count);
        result_values[40] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_retired_count);
        result_values[41] = laplace_pg_numeric_from_uint64(
            receipts.reference_persistence_batch_count);
        result_values[42] = laplace_pg_numeric_from_uint64(
            receipts.reference_maximum_persistence_batch_records);
        result_values[43] = laplace_pg_numeric_from_uint64(
            receipts.reference_maximum_encoded_persistence_batch_bytes);
        result_values[44] = laplace_pg_numeric_from_uint64(
            receipts.reference_minimum_encoded_persistence_record_bytes);
        result_values[45] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_persistence_batch_count);
        result_values[46] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_maximum_persistence_batch_records);
        result_values[47] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_maximum_encoded_persistence_batch_bytes);
        result_values[48] = laplace_pg_numeric_from_uint64(
            receipts.reference_mapping_minimum_encoded_persistence_record_bytes);
        result_values[49] = Int32GetDatum((int32)LAPLACE_TABULAR_SOURCE_OK);
        result_tuple = laplace_pg_form_result_tuple(
            fcinfo, result_values, result_nulls, 50);
    }
    PG_CATCH();
    {
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(&execution);
        laplace_tabular_source_plan_destroy(&source_plan);
        PG_RE_THROW();
    }
    PG_END_TRY();
    LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(&execution);
    laplace_tabular_source_plan_destroy(&source_plan);
    PG_RETURN_DATUM(HeapTupleGetDatum(result_tuple));
}
