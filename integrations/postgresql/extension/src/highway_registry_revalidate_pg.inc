/* A new observation of committed state. This is not an activation replay and
 * never supplies a predecessor epoch to the activation provider. All temporary
 * composition effects are rolled back inside the native entry point. */

typedef struct highway_committed_snapshot {
    uint64 sequence;
    laplace_id128 epoch_id;
    laplace_digest256 epoch;
    laplace_digest256 admission;
    laplace_digest256 activation;
    laplace_digest256 activation_fingerprint;
    laplace_digest256 original_isa;
    laplace_digest256 working_set_reference;
    laplace_digest256 producer_reference;
    bool composition_receipt_present;
    laplace_digest256 generation_fingerprint;
    laplace_digest256 event_chain_fingerprint;
} highway_committed_snapshot;

/* Keep native-owned resources outside setjmp-modified automatic storage. */
typedef struct highway_revalidation_resources {
    laplace_highway_registry_ast_plan* ast_plan;
    laplace_pg_composition_execution execution;
} highway_revalidation_resources;

static void highway_revalidation_error(const char* field) {
    ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
        errmsg("Committed Highway revalidation failed: %s", field)));
}

static void highway_hash_tuple_bytes(
    blake3_hasher* hasher, HeapTuple tuple, TupleDesc descriptor,
    int column, size_t width, const char* label) {
    uint8_t bytes[32];
    read_exact_bytes(required_tuple_value(tuple, descriptor, column, label),
                     bytes, width, label);
    blake3_hasher_update(hasher, bytes, width);
}

/* These are the receipt bodies the original Highway owner actually persisted.
 * Reuse their native hash laws; a reference digest is not a receipt body. */
static void highway_verify_original_receipt_bodies(Datum epoch_id, Datum epoch) {
    static const char sql[] =
        "SELECT i.receipt_id,i.context_fingerprint,i.program_fingerprint,"
        "i.input_fingerprint,i.output_fingerprint,i.instruction_count,"
        "i.executed_instruction_count,i.isa_major,i.isa_minor,i.receipt_detail,i.status,"
        "d.receipt_id,d.context_fingerprint,d.source_fingerprint,d.recipe_fingerprint,"
        "d.stream_fingerprint,d.sink_artifacts_fingerprint,d.total_records,d.total_bytes,"
        "d.batch_count,d.sink_count,d.record_type,d.effect_disposition,d.status "
        "FROM " LAPLACE_PG_SCHEMA ".highway_registry_generation g "
        "JOIN " LAPLACE_PG_SCHEMA ".execution_receipt i ON i.receipt_id=g.isa_receipt "
        "JOIN " LAPLACE_PG_SCHEMA ".canonical_deposit_receipt d ON d.receipt_id=g.staged_stream_receipt "
        "AND d.context_fingerprint=i.context_fingerprint AND d.source_fingerprint=g.source_fingerprint "
        "AND d.recipe_fingerprint=g.recipe_fingerprint AND d.stream_fingerprint=g.stream_fingerprint "
        "AND d.sink_artifacts_fingerprint=g.sink_artifacts_fingerprint "
        "WHERE g.activation_epoch_id=$1 AND g.activation_epoch_fingerprint=$2 "
        "AND i.opcode=$3 AND i.item_count=1";
    Oid types[3] = {BYTEAOID, BYTEAOID, INT4OID};
    Datum values[3] = {epoch_id, epoch,
        Int32GetDatum(LAPLACE_ISA_OPCODE_HIGHWAY_REGISTRY_MATERIALIZE_BATCH)};
    laplace_isa_receipt isa;
    laplace_framework_stream_receipt stream;
    HeapTuple row;
    TupleDesc descriptor;
    int result;
    memset(&isa, 0, sizeof(isa));
    memset(&stream, 0, sizeof(stream));
    if (SPI_connect() != SPI_OK_CONNECT) highway_revalidation_error("original receipt body connection");
    result = SPI_execute_with_args(sql, 3, types, values, NULL, true, 2);
    if (result != SPI_OK_SELECT || SPI_processed != 1u || SPI_tuptable == NULL)
        highway_revalidation_error("original receipt links");
    row = SPI_tuptable->vals[0];
    descriptor = SPI_tuptable->tupdesc;
#define ORIGINAL_BYTES(column, field) read_exact_bytes(required_tuple_value(row, descriptor, column, #field), field.bytes, 32u, #field)
#define ORIGINAL_U64(column, label) laplace_pg_uint64_from_numeric(required_tuple_value(row, descriptor, column, label), label)
    ORIGINAL_BYTES(1, isa.receipt_id);
    ORIGINAL_BYTES(2, isa.context_fingerprint);
    ORIGINAL_BYTES(3, isa.program_fingerprint);
    ORIGINAL_BYTES(4, isa.input_fingerprint);
    ORIGINAL_BYTES(5, isa.output_fingerprint);
    isa.instruction_count = (uint64)DatumGetInt64(required_tuple_value(row, descriptor, 6, "ISA instruction count"));
    isa.executed_instruction_count = (uint64)DatumGetInt64(required_tuple_value(row, descriptor, 7, "ISA executed count"));
    isa.major = (uint16)DatumGetInt16(required_tuple_value(row, descriptor, 8, "ISA major"));
    isa.minor = (uint16)DatumGetInt16(required_tuple_value(row, descriptor, 9, "ISA minor"));
    isa.receipt_detail = (uint32)DatumGetInt32(required_tuple_value(row, descriptor, 10, "ISA detail"));
    isa.status = (laplace_isa_status)DatumGetInt32(required_tuple_value(row, descriptor, 11, "ISA status"));
    if (isa.instruction_count != 1u || laplace_isa_receipt_validate(&isa) != LAPLACE_ISA_OK)
        highway_revalidation_error("native original ISA receipt identity");
    ORIGINAL_BYTES(12, stream.receipt_id);
    ORIGINAL_BYTES(13, stream.context_fingerprint);
    ORIGINAL_BYTES(14, stream.source_fingerprint);
    ORIGINAL_BYTES(15, stream.recipe_fingerprint);
    ORIGINAL_BYTES(16, stream.stream_fingerprint);
    ORIGINAL_BYTES(17, stream.sink_artifacts_fingerprint);
    stream.total_records = ORIGINAL_U64(18, "stream record count");
    stream.total_bytes = ORIGINAL_U64(19, "stream byte count");
    stream.batch_count = ORIGINAL_U64(20, "stream batch count");
    stream.sink_count = ORIGINAL_U64(21, "stream sink count");
    stream.record_type = (uint32)DatumGetInt32(required_tuple_value(row, descriptor, 22, "stream record type"));
    stream.effect_disposition = (uint32)DatumGetInt32(required_tuple_value(row, descriptor, 23, "stream effect"));
    stream.status = (laplace_framework_status)DatumGetInt32(required_tuple_value(row, descriptor, 24, "stream status"));
    stream.failed_batch_index = LAPLACE_FRAMEWORK_NO_INDEX;
    stream.failed_sink_index = LAPLACE_FRAMEWORK_NO_INDEX;
    if (laplace_framework_stream_receipt_identity_validate(&stream) != LAPLACE_FRAMEWORK_OK)
        highway_revalidation_error("native original staged-stream receipt identity");
#undef ORIGINAL_U64
#undef ORIGINAL_BYTES
    if (SPI_finish() != SPI_OK_FINISH) highway_revalidation_error("original receipt body cleanup");
}

static highway_committed_snapshot highway_verify_committed_generation(
    const laplace_framework_context* context,
    const laplace_highway_registry_receipt* registry,
    const laplace_highway_registry_ast_view* ast,
    const laplace_pg_composition_execution* execution,
    const laplace_composition_result* root) {
    static const char generation_sql[] =
        "SELECT c.sequence,c.activation_epoch_id,c.activation_epoch_fingerprint,"
        "c.admission_receipt,c.activation_receipt,c.activation_fingerprint,"
        "g.registry_version,g.registry_fingerprint,g.root_entity_id,"
        "g.root_physicality_id,g.source_fingerprint,g.recipe_fingerprint,"
        "g.isa_receipt,g.working_set_receipt,g.producer_receipt,"
        "g.staged_stream_receipt,g.stream_fingerprint,g.sink_artifacts_fingerprint,"
        "g.kind_count,g.alias_count,g.disposition_count,"
        "EXISTS(SELECT 1 FROM " LAPLACE_PG_SCHEMA ".execution_receipt i "
        "JOIN " LAPLACE_PG_SCHEMA ".canonical_deposit_receipt d ON "
        "d.receipt_id=g.staged_stream_receipt AND d.context_fingerprint=i.context_fingerprint "
        "AND d.source_fingerprint=g.source_fingerprint AND d.recipe_fingerprint=g.recipe_fingerprint "
        "AND d.stream_fingerprint=g.stream_fingerprint "
        "AND d.sink_artifacts_fingerprint=g.sink_artifacts_fingerprint "
        "WHERE i.receipt_id=g.isa_receipt AND i.status=0 AND i.instruction_count=1 "
        "AND i.executed_instruction_count=1 AND i.opcode=$1),"
        "coverage.present_count>0,coverage.present_count<=1 AND coalesce(coverage.matches,true) "
        "FROM " LAPLACE_PG_SCHEMA ".highway_registry_active_control c "
        "JOIN " LAPLACE_PG_SCHEMA ".highway_registry_generation g ON "
        "g.activation_epoch_id=c.activation_epoch_id AND "
        "g.activation_epoch_fingerprint=c.activation_epoch_fingerprint "
        "LEFT JOIN LATERAL (SELECT count(*) AS present_count,bool_and("
        "x.working_set_receipt=g.working_set_receipt AND x.producer_receipt IS NOT DISTINCT FROM g.producer_receipt "
        "AND x.staged_stream_receipt IS NOT DISTINCT FROM g.staged_stream_receipt "
        "AND x.stream_fingerprint=g.stream_fingerprint "
        "AND x.sink_artifacts_fingerprint IS NOT DISTINCT FROM g.sink_artifacts_fingerprint "
        "AND EXISTS(SELECT 1 FROM " LAPLACE_PG_SCHEMA ".canonical_deposit_receipt d WHERE "
        "d.receipt_id=g.staged_stream_receipt AND x.effect_disposition=d.effect_disposition "
        "AND x.stream_record_count=d.total_records AND x.occurrence_count=d.occurrence_count "
        "AND x.logical_occurrence_count=d.logical_occurrence_count)) AS matches FROM ("
        "(SELECT * FROM " LAPLACE_PG_SCHEMA ".composition_execution_receipt WHERE working_set_receipt=g.working_set_receipt) "
        "UNION ALL (SELECT * FROM " LAPLACE_PG_SCHEMA ".composition_execution_receipt "
        "WHERE staged_stream_receipt=g.staged_stream_receipt AND working_set_receipt<>g.working_set_receipt LIMIT 2)"
        ") x) coverage ON true "
        "WHERE c.singleton AND c.active_present";
    static const char events_sql[] =
        "SELECT e.sequence,e.activation_epoch_id,e.activation_epoch_fingerprint,"
        "e.admission_receipt,e.activation_receipt,e.activation_fingerprint,"
        "g.root_entity_id,g.root_physicality_id,g.registry_fingerprint,"
        "g.staged_stream_receipt,g.kind_count,g.alias_count,g.disposition_count,"
        "g.registry_version,e.activation_transaction_id,i.context_fingerprint FROM " LAPLACE_PG_SCHEMA ".highway_registry_activation_event e "
        "JOIN " LAPLACE_PG_SCHEMA ".highway_registry_generation g ON "
        "g.activation_epoch_id=e.activation_epoch_id AND "
        "g.activation_epoch_fingerprint=e.activation_epoch_fingerprint "
        "JOIN " LAPLACE_PG_SCHEMA ".execution_receipt i ON i.receipt_id=g.isa_receipt "
        "ORDER BY e.sequence";
    highway_committed_snapshot snapshot;
    Oid opcode_type = INT4OID;
    Datum opcode = Int32GetDatum(LAPLACE_ISA_OPCODE_HIGHWAY_REGISTRY_MATERIALIZE_BATCH);
    HeapTuple row;
    TupleDesc descriptor;
    blake3_hasher hasher;
    uint64 event_count;
    SPITupleTable* event_table;
    uint64 previous_version = 0u;
    laplace_digest256 previous_epoch = {{0}};
    uint64 index;
    int column;
    int result;
    memset(&snapshot, 0, sizeof(snapshot));
    if (execution->summary.novel_entity_count != 0u ||
        execution->summary.novel_physicality_count != 0u ||
        execution->persistence.inserted[0] != 0u ||
        execution->persistence.inserted[1] != 0u ||
        execution->persistence.inserted[2] != 0u) {
        highway_revalidation_error("canonical entity or physicality was missing");
    }
    if (SPI_connect() != SPI_OK_CONNECT) {
        highway_revalidation_error("generation connection");
    }
    result = SPI_execute_with_args(generation_sql, 1, &opcode_type, &opcode,
                                   NULL, true, 2);
    if (result != SPI_OK_SELECT || SPI_processed != 1u || SPI_tuptable == NULL) {
        highway_revalidation_error("one active generation required");
    }
    row = SPI_tuptable->vals[0];
    descriptor = SPI_tuptable->tupdesc;
#define HIGHWAY_MATCH(column_number, expected, width, label) \
    do { if (!datum_matches_bytes(required_tuple_value(row, descriptor, \
        column_number, label), expected, width)) highway_revalidation_error(label); } while (0)
    snapshot.sequence = (uint64)DatumGetInt64(required_tuple_value(row, descriptor, 1, "sequence"));
    if (snapshot.sequence == 0u || snapshot.sequence > 1024u) {
        highway_revalidation_error("bounded activation sequence");
    }
    HIGHWAY_MATCH(2, registry->activation_epoch_id.bytes, 16u, "epoch id");
    HIGHWAY_MATCH(3, registry->activation_epoch_fingerprint.bytes, 32u, "epoch fingerprint");
    HIGHWAY_MATCH(3, context->epochs[LAPLACE_FRAMEWORK_EPOCH_NUMERIC].bytes, 32u, "pinned numeric epoch");
    HIGHWAY_MATCH(8, registry->registry_fingerprint.bytes, 32u, "registry fingerprint");
    HIGHWAY_MATCH(9, root->entity_id.bytes, 16u, "canonical root entity");
    HIGHWAY_MATCH(10, root->physicality_id.bytes, 32u, "canonical root physicality");
    HIGHWAY_MATCH(11, ast->source_fingerprint.bytes, 32u, "canonical source");
    HIGHWAY_MATCH(12, ast->recipe_fingerprint.bytes, 32u, "canonical recipe");
    if ((uint64)DatumGetInt64(required_tuple_value(row, descriptor, 7, "version")) != registry->registry_version ||
        (uint64)DatumGetInt64(required_tuple_value(row, descriptor, 19, "kind count")) != ast->kind_count ||
        (uint64)DatumGetInt64(required_tuple_value(row, descriptor, 20, "alias count")) != ast->alias_count ||
        (uint64)DatumGetInt64(required_tuple_value(row, descriptor, 21, "disposition count")) != ast->disposition_count ||
        !DatumGetBool(required_tuple_value(row, descriptor, 22, "original receipt links"))) {
        highway_revalidation_error("generation descriptors or original receipt links");
    }
    if (!DatumGetBool(required_tuple_value(row, descriptor, 24, "composition receipt links")))
        highway_revalidation_error("original receipt links: present composition body differs");
    snapshot.composition_receipt_present = DatumGetBool(required_tuple_value(row, descriptor, 23, "composition receipt presence"));
    read_exact_bytes(required_tuple_value(row, descriptor, 14, "stored working-set reference"), snapshot.working_set_reference.bytes, 32u, "stored working-set reference");
    read_exact_bytes(required_tuple_value(row, descriptor, 15, "stored producer reference"), snapshot.producer_reference.bytes, 32u, "stored producer reference");
    snapshot.epoch_id = registry->activation_epoch_id;
    snapshot.epoch = registry->activation_epoch_fingerprint;
    read_exact_bytes(required_tuple_value(row, descriptor, 4, "original admission"), snapshot.admission.bytes, 32u, "original admission");
    read_exact_bytes(required_tuple_value(row, descriptor, 5, "original activation"), snapshot.activation.bytes, 32u, "original activation");
    read_exact_bytes(required_tuple_value(row, descriptor, 6, "original activation fingerprint"), snapshot.activation_fingerprint.bytes, 32u, "original activation fingerprint");
    read_exact_bytes(required_tuple_value(row, descriptor, 13, "original ISA receipt"), snapshot.original_isa.bytes, 32u, "original ISA receipt");
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, "laplace-highway-committed-generation-observation-v1",
                        sizeof("laplace-highway-committed-generation-observation-v1") - 1u);
    for (column = 1; column <= 21; ++column) {
        if (column == 1 || column == 7 || column >= 19) {
            hash_u64(&hasher, (uint64)DatumGetInt64(required_tuple_value(row, descriptor, column, "generation integer")));
        } else {
            highway_hash_tuple_bytes(&hasher, row, descriptor, column,
                                      column == 2 || column == 9 ? 16u : 32u, "generation bytes");
        }
    }
    snapshot.generation_fingerprint = finish_hash(&hasher);
    result = SPI_execute(events_sql, true, 1025);
    if (result != SPI_OK_SELECT || SPI_processed != snapshot.sequence || SPI_tuptable == NULL) {
        highway_revalidation_error("contiguous activation event chain");
    }
    event_count = SPI_processed;
    event_table = SPI_tuptable;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, "laplace-highway-committed-event-chain-v1",
                        sizeof("laplace-highway-committed-event-chain-v1") - 1u);
    for (index = 0u; index < event_count; ++index) {
        blake3_hasher preparation;
        blake3_hasher commit;
        laplace_digest256 prepared;
        laplace_digest256 committed;
        laplace_digest256 admission;
        laplace_digest256 activation;
        laplace_digest256 original_context;
        laplace_digest256 staged_receipt;
        laplace_framework_activation_request activation_request;
        uint64 version;
        row = event_table->vals[index];
        descriptor = event_table->tupdesc;
        if ((uint64)DatumGetInt64(required_tuple_value(row, descriptor, 1, "event sequence")) != index + 1u) {
            highway_revalidation_error("activation event sequence gap");
        }
        version = (uint64)DatumGetInt64(required_tuple_value(row, descriptor, 14, "event registry version"));
        if (version <= previous_version) {
            highway_revalidation_error("append-only registry versions");
        }
        previous_version = version;
        read_exact_bytes(required_tuple_value(row, descriptor, 4,
            "event admission receipt"), admission.bytes, 32u, "event admission receipt");
        read_exact_bytes(required_tuple_value(row, descriptor, 5,
            "event final activation receipt"), activation.bytes, 32u, "event final activation receipt");
        read_exact_bytes(required_tuple_value(row, descriptor, 10,
            "event staged receipt"), staged_receipt.bytes, 32u, "event staged receipt");
        read_exact_bytes(required_tuple_value(row, descriptor, 16,
            "original event context"), original_context.bytes, 32u, "original event context");
        highway_verify_original_receipt_bodies(
            required_tuple_value(row, descriptor, 2, "original epoch id"),
            required_tuple_value(row, descriptor, 3, "original epoch"));
        blake3_hasher_init(&preparation);
        blake3_hasher_update(&preparation, "laplace-highway-registry-activation-preparation-v1",
                            sizeof("laplace-highway-registry-activation-preparation-v1") - 1u);
        highway_hash_tuple_bytes(&preparation, row, descriptor, 10, 32u, "original staged receipt");
        highway_hash_tuple_bytes(&preparation, row, descriptor, 7, 16u, "original root entity");
        highway_hash_tuple_bytes(&preparation, row, descriptor, 8, 32u, "original root physicality");
        highway_hash_tuple_bytes(&preparation, row, descriptor, 9, 32u, "original registry fingerprint");
        for (column = 11; column <= 13; ++column) {
            hash_u64(&preparation, (uint64)DatumGetInt64(required_tuple_value(row, descriptor, column, "original registry count")));
        }
        prepared = finish_hash(&preparation);
        blake3_hasher_init(&commit);
        blake3_hasher_update(&commit, "laplace-highway-registry-activation-commit-v1",
                            sizeof("laplace-highway-registry-activation-commit-v1") - 1u);
        blake3_hasher_update(&commit, prepared.bytes, 32u);
        highway_hash_tuple_bytes(&commit, row, descriptor, 3, 32u, "event next epoch");
        highway_hash_tuple_bytes(&commit, row, descriptor, 4, 32u, "event admission receipt");
        hash_u64(&commit, index + 1u);
        committed = finish_hash(&commit);
        HIGHWAY_MATCH(6, committed.bytes, 32u, "native activation commit fingerprint");
        memset(&activation_request, 0, sizeof(activation_request));
        activation_request.epoch_slot = LAPLACE_FRAMEWORK_EPOCH_NUMERIC;
        activation_request.expected_epoch = previous_epoch;
        read_exact_bytes(required_tuple_value(row, descriptor, 3,
            "event next epoch"), activation_request.next_epoch.bytes, 32u, "event next epoch");
        if (laplace_framework_committed_receipts_validate(&original_context,
                &staged_receipt, &activation_request, &prepared, &committed,
                &admission, &activation) != LAPLACE_FRAMEWORK_OK) {
            highway_revalidation_error("native admission/final activation receipt identity");
        }
        previous_epoch = activation_request.next_epoch;
        if (index + 1u == event_count) {
            HIGHWAY_MATCH(2, snapshot.epoch_id.bytes, 16u, "event/control epoch id");
            HIGHWAY_MATCH(3, snapshot.epoch.bytes, 32u, "event/control epoch fingerprint");
            HIGHWAY_MATCH(4, snapshot.admission.bytes, 32u, "event/control admission");
            HIGHWAY_MATCH(5, snapshot.activation.bytes, 32u, "event/control activation");
            HIGHWAY_MATCH(6, snapshot.activation_fingerprint.bytes, 32u, "event/control commit fingerprint");
        }
        for (column = 1; column <= 14; ++column) {
            if (column == 1 || column >= 11) {
                hash_u64(&hasher, (uint64)DatumGetInt64(required_tuple_value(
                    row, descriptor, column, "event integer")));
            } else {
                highway_hash_tuple_bytes(&hasher, row, descriptor, column,
                    column == 2 || column == 7 ? 16u : 32u, "event bytes");
            }
        }
        hash_u64(&hasher, (uint64)DatumGetTransactionId(required_tuple_value(
            row, descriptor, 15, "event transaction id")));
        blake3_hasher_update(&hasher, original_context.bytes, 32u);
    }
    snapshot.event_chain_fingerprint = finish_hash(&hasher);
    result = SPI_execute("SELECT count(*) FROM " LAPLACE_PG_SCHEMA ".highway_registry_generation", true, 1);
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        (uint64)DatumGetInt64(required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, "generation count")) != event_count) {
        highway_revalidation_error("unbound registry generation");
    }
    if (SPI_finish() != SPI_OK_FINISH) highway_revalidation_error("generation connection cleanup");
#undef HIGHWAY_MATCH
    return snapshot;
}

static void highway_verify_projection(
    const char* table, const char* identifier, const char* extra,
    const laplace_highway_registry_receipt* registry,
    Datum identifiers, Datum names, Datum introduced, Datum retired,
    size_t expected_count) {
    Oid types[6] = {BYTEAOID, BYTEAOID, INT4ARRAYOID, BYTEAARRAYOID, INT8ARRAYOID, INT8ARRAYOID};
    Datum values[6];
    char* query;
    int result;
    values[0] = PointerGetDatum(laplace_pg_bytes_to_bytea(registry->activation_epoch_id.bytes, 16u));
    values[1] = PointerGetDatum(laplace_pg_bytes_to_bytea(registry->activation_epoch_fingerprint.bytes, 32u));
    values[2] = identifiers; values[3] = names; values[4] = introduced; values[5] = retired;
    query = psprintf("SELECT (SELECT count(*) FROM %s.%s WHERE activation_epoch_id=$1 AND activation_epoch_fingerprint=$2)=$7 "
        "AND (SELECT count(*) FROM unnest($3::integer[],$4::bytea[],$5::bigint[],$6::bigint[]) "
        "u(identifier,name,introduced,retired) JOIN %s.%s p ON "
        "p.activation_epoch_id=$1 AND p.activation_epoch_fingerprint=$2 "
        "AND p.%s=u.identifier AND p.%s=u.name %s)=$7",
        LAPLACE_PG_SCHEMA, table, LAPLACE_PG_SCHEMA, table, identifier,
        strcmp(table, "highway_registry_alias_projection") == 0 ? "alias_name_entity_id" : "name_entity_id", extra);
    if (SPI_connect() != SPI_OK_CONNECT) highway_revalidation_error("projection connection");
    {
        Oid all_types[7]; Datum all_values[7];
        memcpy(all_types, types, sizeof(types)); memcpy(all_values, values, sizeof(values));
        all_types[6] = INT8OID; all_values[6] = Int64GetDatum((int64)expected_count);
        result = SPI_execute_with_args(query, 7, all_types, all_values, NULL, true, 1);
    }
    if (result != SPI_OK_SELECT || SPI_processed != 1u ||
        !DatumGetBool(required_tuple_value(SPI_tuptable->vals[0], SPI_tuptable->tupdesc, 1, "canonical projection"))) {
        highway_revalidation_error(table);
    }
    if (SPI_finish() != SPI_OK_FINISH) highway_revalidation_error("projection connection cleanup");
    pfree(query);
}

Datum LAPLACE_PG_HIGHWAY_REGISTRY_REVALIDATE_SYMBOL(PG_FUNCTION_ARGS) {
    MemoryContext outer_context = CurrentMemoryContext;
    ResourceOwner outer_owner = CurrentResourceOwner;
    HeapTuple result_tuple = NULL;
    highway_revalidation_resources* resources = palloc0(sizeof(*resources));
    laplace_pg_composition_execution* execution = &resources->execution;
    laplace_framework_context context;
    uint64_t batch_bytes;
    laplace_pg_read_execution_context(PG_GETARG_DATUM(0), &context);
    if ((context.flags & LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY) != 0u) {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
            errmsg("Highway revalidation requires a writable context for its internally rolled-back composition")));
    }
    batch_bytes = laplace_pg_uint64_from_numeric(PG_GETARG_DATUM(1), "Highway revalidation batch bytes");
    BeginInternalSubTransaction(NULL);
    PG_TRY();
    {
        laplace_highway_registry_receipt registry;
        laplace_isa_receipt isa;
        laplace_highway_registry_ast_view ast;
        laplace_pg_active_unicode_root unicode;
        laplace_composition_working_set_input input;
        laplace_composition_known_entity* known;
        const laplace_composition_result* root;
        highway_committed_snapshot snapshot;
        blake3_hasher proof;
        laplace_digest256 proof_id;
        Datum values[37];
        bool nulls[37] = {false};
        const laplace_highway_kind_contract_row* kinds;
        const laplace_highway_alias_contract_row* aliases;
        const laplace_highway_disposition_contract_row* dispositions;
        uint32_t* ids;
        uint64_t* introduced;
        uint64_t* retired;
        size_t count;
        size_t index;
        size_t capacity;
        int column = 0;
        if (SPI_connect() != SPI_OK_CONNECT) {
            highway_revalidation_error("lock connection");
        }
        if (SPI_execute("LOCK TABLE " LAPLACE_PG_SCHEMA ".highway_registry_active_control,"
            LAPLACE_PG_SCHEMA ".highway_registry_generation,"
            LAPLACE_PG_SCHEMA ".highway_registry_activation_event,"
            LAPLACE_PG_SCHEMA ".highway_registry_kind_projection,"
            LAPLACE_PG_SCHEMA ".highway_registry_alias_projection,"
            LAPLACE_PG_SCHEMA ".highway_registry_disposition_projection,"
            LAPLACE_PG_SCHEMA ".entity,"
            LAPLACE_PG_SCHEMA ".physicality,"
            LAPLACE_PG_SCHEMA ".execution_receipt,"
            LAPLACE_PG_SCHEMA ".composition_execution_receipt,"
            LAPLACE_PG_SCHEMA ".canonical_deposit_receipt,"
            LAPLACE_PG_SCHEMA ".perfcache_active_control IN SHARE MODE", false, 0) != SPI_OK_UTILITY ||
            SPI_finish() != SPI_OK_FINISH) {
            highway_revalidation_error("stable committed snapshot lock");
        }
        memset(&registry, 0, sizeof(registry));
        memset(&isa, 0, sizeof(isa));
        execute_registry_instruction(&context, &registry, &isa, false);
        if (laplace_highway_registry_ast_plan_create(&context.epochs[LAPLACE_FRAMEWORK_EPOCH_GEOMETRY],
                &registry.context_fingerprint, &resources->ast_plan) != LAPLACE_HIGHWAY_OK ||
            laplace_highway_registry_ast_plan_view(resources->ast_plan, &ast) != LAPLACE_HIGHWAY_OK ||
            !digest_equal(&ast.source_fingerprint, &registry.registry_fingerprint)) {
            highway_revalidation_error("canonical AST compilation");
        }
        known = palloc0(sizeof(*known) * (size_t)ast.atom_count);
        laplace_pg_resolve_active_unicode_atoms(&context, ast.atom_positions,
                                               (size_t)ast.atom_count, known, &unicode);
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
        input.preferred_batch_bytes = batch_bytes;
        LAPLACE_PG_COMPOSITION_EXECUTE_SYMBOL(&input, execution);
        if (ast.root_result_index >= execution->result_count) {
            highway_revalidation_error("canonical root result");
        }
        root = &execution->results[ast.root_result_index];
        snapshot = highway_verify_committed_generation(
            &context, &registry, &ast, execution, root);
        laplace_pg_composition_verify_stored_trajectories(execution, batch_bytes);
        capacity = (size_t)ast.kind_count + (size_t)ast.alias_count + (size_t)ast.disposition_count;
        ids = palloc0(capacity * sizeof(*ids));
        introduced = palloc0(capacity * sizeof(*introduced));
        retired = palloc0(capacity * sizeof(*retired));
        kinds = laplace_highway_registry_kinds(&count);
        for (index = 0; index < count; ++index) {
            ids[index] = kinds[index].id;
            introduced[index] = kinds[index].introduced;
            retired[index] = kinds[index].retired;
        }
        highway_verify_projection(
            "highway_registry_kind_projection", "kind_id",
            "AND p.introduced=u.introduced AND p.retired=u.retired", &registry,
            PointerGetDatum(uint32_array(ids, count)),
            PointerGetDatum(result_entity_array(execution->results,
                execution->result_count, ast.kind_name_result_indexes, count)),
            PointerGetDatum(uint64_array(introduced, count)),
            PointerGetDatum(uint64_array(retired, count)), count);
        aliases = laplace_highway_registry_aliases(&count);
        for (index = 0; index < count; ++index) {
            ids[index] = aliases[index].kind_id;
            introduced[index] = aliases[index].introduced;
            retired[index] = aliases[index].retired;
        }
        highway_verify_projection(
            "highway_registry_alias_projection", "kind_id",
            "AND p.introduced=u.introduced AND p.retired=u.retired", &registry,
            PointerGetDatum(uint32_array(ids, count)),
            PointerGetDatum(result_entity_array(execution->results,
                execution->result_count, ast.alias_name_result_indexes, count)),
            PointerGetDatum(uint64_array(introduced, count)),
            PointerGetDatum(uint64_array(retired, count)), count);
        dispositions = laplace_highway_registry_dispositions(&count);
        for (index = 0; index < count; ++index) {
            ids[index] = dispositions[index].id;
            introduced[index] = 0u;
            retired[index] = 0u;
        }
        highway_verify_projection(
            "highway_registry_disposition_projection", "disposition_id", "", &registry,
            PointerGetDatum(uint32_array(ids, count)),
            PointerGetDatum(result_entity_array(execution->results,
                execution->result_count, ast.disposition_name_result_indexes, count)),
            PointerGetDatum(uint64_array(introduced, count)),
            PointerGetDatum(uint64_array(retired, count)), count);
        blake3_hasher_init(&proof);
        blake3_hasher_update(&proof, "laplace-highway-committed-revalidation-v1",
            sizeof("laplace-highway-committed-revalidation-v1") - 1u);
        blake3_hasher_update(&proof, snapshot.generation_fingerprint.bytes, 32u);
        blake3_hasher_update(&proof, snapshot.event_chain_fingerprint.bytes, 32u);
        hash_u64(&proof, snapshot.composition_receipt_present ? 1u : 0u);
        hash_u64(&proof, 0u); /* Original intermediate receipt bodies are unavailable. */
        blake3_hasher_update(&proof, isa.receipt_id.bytes, 32u);
        blake3_hasher_update(&proof, execution->summary.receipt_id.bytes, 32u);
        blake3_hasher_update(&proof, execution->presence.semantic_receipt_id.bytes, 32u);
        blake3_hasher_update(&proof, execution->presence.execution_receipt_id.bytes, 32u);
        blake3_hasher_update(&proof, execution->persistence.producer.receipt_id.bytes, 32u);
        blake3_hasher_update(&proof, execution->persistence.producer.stream.receipt_id.bytes, 32u);
        blake3_hasher_update(&proof,
            execution->persistence.producer.stream.sink_artifacts_fingerprint.bytes, 32u);
        blake3_hasher_update(&proof, unicode.root_receipt.bytes, 32u);
        blake3_hasher_update(&proof, unicode.activation_epoch_id.bytes, 16u);
        blake3_hasher_update(&proof, unicode.activation_epoch_fingerprint.bytes, 32u);
        hash_u64(&proof, batch_bytes);
        hash_u64(&proof, execution->summary.unique_entity_count);
        hash_u64(&proof, execution->summary.unique_physicality_count);
        hash_u64(&proof, execution->persistence.inserted[3]);
        proof_id = finish_hash(&proof);
#define HIGHWAY_BYTES(value, width) \
    values[column++] = PointerGetDatum(laplace_pg_bytes_to_bytea(value, width))
        HIGHWAY_BYTES(proof_id.bytes, 32u);
        HIGHWAY_BYTES(root->entity_id.bytes, 16u);
        HIGHWAY_BYTES(root->physicality_id.bytes, 32u);
        values[column++] = Int64GetDatum((int64)registry.registry_version);
        HIGHWAY_BYTES(registry.registry_fingerprint.bytes, 32u);
        HIGHWAY_BYTES(snapshot.epoch_id.bytes, 16u);
        HIGHWAY_BYTES(snapshot.epoch.bytes, 32u);
        values[column++] = Int64GetDatum((int64)snapshot.sequence);
        HIGHWAY_BYTES(snapshot.original_isa.bytes, 32u);
        HIGHWAY_BYTES(isa.receipt_id.bytes, 32u);
        HIGHWAY_BYTES(registry.context_fingerprint.bytes, 32u);
        HIGHWAY_BYTES(snapshot.admission.bytes, 32u);
        HIGHWAY_BYTES(snapshot.activation.bytes, 32u);
        HIGHWAY_BYTES(snapshot.activation_fingerprint.bytes, 32u);
        HIGHWAY_BYTES(snapshot.generation_fingerprint.bytes, 32u);
        HIGHWAY_BYTES(snapshot.event_chain_fingerprint.bytes, 32u);
        HIGHWAY_BYTES(execution->summary.receipt_id.bytes, 32u);
        HIGHWAY_BYTES(execution->presence.semantic_receipt_id.bytes, 32u);
        HIGHWAY_BYTES(execution->presence.execution_receipt_id.bytes, 32u);
        HIGHWAY_BYTES(execution->persistence.producer.receipt_id.bytes, 32u);
        HIGHWAY_BYTES(execution->persistence.producer.stream.receipt_id.bytes, 32u);
        HIGHWAY_BYTES(execution->persistence.producer.stream.sink_artifacts_fingerprint.bytes, 32u);
        HIGHWAY_BYTES(unicode.root_receipt.bytes, 32u);
        HIGHWAY_BYTES(unicode.activation_epoch_id.bytes, 16u);
        HIGHWAY_BYTES(unicode.activation_epoch_fingerprint.bytes, 32u);
        values[column++] = Int64GetDatum((int64)ast.kind_count);
        values[column++] = Int64GetDatum((int64)ast.alias_count);
        values[column++] = Int64GetDatum((int64)ast.disposition_count);
        values[column++] = laplace_pg_numeric_from_uint64(execution->summary.unique_entity_count);
        values[column++] = laplace_pg_numeric_from_uint64(execution->summary.unique_physicality_count);
        values[column++] = laplace_pg_numeric_from_uint64(execution->persistence.inserted[3]);
        HIGHWAY_BYTES(snapshot.working_set_reference.bytes, 32u);
        HIGHWAY_BYTES(snapshot.producer_reference.bytes, 32u);
        values[column++] = BoolGetDatum(snapshot.composition_receipt_present);
        values[column++] = BoolGetDatum(false);
        values[column++] = BoolGetDatum(false);
        values[column++] = Int32GetDatum(0);
#undef HIGHWAY_BYTES
        if (column != 37) {
            highway_revalidation_error("typed result width");
        }
        {
            HeapTuple transient = laplace_pg_form_result_tuple(fcinfo, values, nulls, 37);
            MemoryContextSwitchTo(outer_context);
            result_tuple = heap_copytuple(transient);
        }
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(execution);
        laplace_highway_registry_ast_plan_destroy(&resources->ast_plan);
        RollbackAndReleaseCurrentSubTransaction();
        MemoryContextSwitchTo(outer_context);
        CurrentResourceOwner = outer_owner;
    }
    PG_CATCH();
    {
        MemoryContextSwitchTo(outer_context);
        LAPLACE_PG_COMPOSITION_DESTROY_SYMBOL(execution);
        laplace_highway_registry_ast_plan_destroy(&resources->ast_plan);
        RollbackAndReleaseCurrentSubTransaction();
        MemoryContextSwitchTo(outer_context);
        CurrentResourceOwner = outer_owner;
        PG_RE_THROW();
    }
    PG_END_TRY();
    pfree(resources);
    return HeapTupleGetDatum(result_tuple);
}
