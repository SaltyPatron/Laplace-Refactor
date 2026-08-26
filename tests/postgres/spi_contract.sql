CREATE EXTENSION laplace;

CREATE FUNCTION pg_temp.execution_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            '\x0101010101010101010101010101010101010101010101010101010101010101'::bytea,
            '\x0202020202020202020202020202020202020202020202020202020202020202'::bytea,
            '\x0303030303030303030303030303030303030303030303030303030303030303'::bytea,
            '\x0404040404040404040404040404040404040404040404040404040404040404'::bytea,
            '\x0505050505050505050505050505050505050505050505050505050505050505'::bytea,
            '\x0606060606060606060606060606060606060606060606060606060606060606'::bytea,
            '\x0707070707070707070707070707070707070707070707070707070707070707'::bytea,
            '\x0808080808080808080808080808080808080808080808080808080808080808'::bytea,
            '\x0909090909090909090909090909090909090909090909090909090909090909'::bytea,
            '\x0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a'::bytea
        ],
        '\xa0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0'::bytea,
        1048576::bigint,
        4,
        1,
        1023::bigint,
        @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        @LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY@::integer
    )::laplace.execution_context
$context$;

CREATE FUNCTION pg_temp.persistence_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        (pg_temp.execution_context()).epochs,
        (pg_temp.execution_context()).authority_fingerprint,
        16777216::bigint,
        4,
        1,
        1023::bigint,
        1::smallint,
        2::smallint,
        0
    )::laplace.execution_context
$context$;

CREATE TEMP TABLE native_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    identity_receipt bytea NOT NULL,
    context_fingerprint bytea NOT NULL,
    identity_program bytea NOT NULL,
    identity_input bytea NOT NULL,
    identity_output bytea NOT NULL,
    identity_entities bytea[] NOT NULL,
    trajectory_receipt bytea NOT NULL,
    trajectory_program bytea NOT NULL,
    trajectory_input bytea NOT NULL,
    trajectory_output bytea NOT NULL,
    trajectory_carrier bytea NOT NULL,
    trajectory_entity bytea NOT NULL
);

INSERT INTO native_expected (
    identity_receipt,
    context_fingerprint,
    identity_program,
    identity_input,
    identity_output,
    identity_entities,
    trajectory_receipt,
    trajectory_program,
    trajectory_input,
    trajectory_output,
    trajectory_carrier,
    trajectory_entity
)
VALUES (
    decode(:'identity_receipt', 'hex'),
    decode(:'identity_context', 'hex'),
    decode(:'identity_program', 'hex'),
    decode(:'identity_input', 'hex'),
    decode(:'identity_output', 'hex'),
    ARRAY[
        decode(:'identity_entity_0', 'hex'),
        decode(:'identity_entity_1', 'hex'),
        decode(:'identity_entity_2', 'hex')
    ],
    decode(:'trajectory_receipt', 'hex'),
    decode(:'trajectory_program', 'hex'),
    decode(:'trajectory_input', 'hex'),
    decode(:'trajectory_output', 'hex'),
    decode(:'trajectory_carrier', 'hex'),
    decode(:'trajectory_entity', 'hex')
);

CREATE TEMP TABLE persistence_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    source_fingerprint bytea NOT NULL,
    recipe_fingerprint bytea NOT NULL,
    entity_a bytea NOT NULL,
    entity_a_witness bytea NOT NULL,
    entity_b bytea NOT NULL,
    entity_b_witness bytea NOT NULL,
    physicality_id bytea NOT NULL,
    trajectory_fingerprint bytea NOT NULL,
    occurrence_id bytea NOT NULL,
    plan_sequence_fingerprint bytea NOT NULL,
    frames bytea[] NOT NULL,
    bulk_physicality_id bytea NOT NULL,
    bulk_occurrence_id bytea NOT NULL,
    bulk_stream bytea NOT NULL
);

INSERT INTO persistence_expected VALUES (
    true,
    decode(:'persistence_source', 'hex'),
    decode(:'persistence_recipe', 'hex'),
    decode(:'persistence_entity_a', 'hex'),
    decode(:'persistence_entity_a_witness', 'hex'),
    decode(:'persistence_entity_b', 'hex'),
    decode(:'persistence_entity_b_witness', 'hex'),
    decode(:'persistence_physicality', 'hex'),
    decode(:'persistence_trajectory', 'hex'),
    decode(:'persistence_occurrence', 'hex'),
    decode(:'persistence_plan_sequence', 'hex'),
    ARRAY[
        decode(:'persistence_frame_0', 'hex'),
        decode(:'persistence_frame_1', 'hex'),
        decode(:'persistence_frame_2', 'hex'),
        decode(:'persistence_frame_3', 'hex'),
        decode(:'persistence_frame_4', 'hex'),
        decode(:'persistence_frame_5', 'hex'),
        decode(:'persistence_frame_6', 'hex')
    ],
    decode(:'persistence_bulk_physicality', 'hex'),
    decode(:'persistence_bulk_occurrence', 'hex'),
    decode(:'persistence_bulk_stream', 'hex')
);

DO $contract$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM (
            VALUES
                ('laplace.identity_codepoint_calculate_batch(laplace.execution_context,integer[])', 'i', 's'),
                ('laplace.identity_codepoint_execute_batch(laplace.execution_context,integer[])', 'v', 'u'),
                ('laplace.trajectory_composition_decode_calculate_batch(laplace.execution_context,bytea[])', 'i', 's'),
                ('laplace.trajectory_composition_decode_execute_batch(laplace.execution_context,bytea[])', 'v', 'u'),
                ('laplace.canonical_deposit_batch(laplace.execution_context,bytea,bytea,bytea[])', 'v', 'u'),
                ('laplace.unicode_root_build_and_activate(laplace.execution_context,text,text,text,text,bytea,bytea,bigint,boolean,bytea,bytea,bigint,integer)', 'v', 'u')
        ) AS expected(signature, volatility, parallel_safety)
        LEFT JOIN pg_catalog.pg_proc AS procedure
            ON procedure.oid = pg_catalog.to_regprocedure(expected.signature)
        WHERE procedure.oid IS NULL
           OR procedure.provolatile::text <> expected.volatility
           OR procedure.proparallel::text <> expected.parallel_safety
    ) THEN
        RAISE EXCEPTION 'PostgreSQL calculation/execution planner contract differs from the binding';
    END IF;
    IF EXISTS (
        SELECT 1
        FROM pg_catalog.pg_proc AS procedure
        JOIN pg_catalog.pg_namespace AS namespace
          ON namespace.oid = procedure.pronamespace
        CROSS JOIN LATERAL pg_catalog.aclexplode(
            COALESCE(procedure.proacl, pg_catalog.acldefault('f', procedure.proowner)))
            AS privilege
        WHERE namespace.nspname = 'laplace'
          AND procedure.proname IN (
              'identity_codepoint_calculate_batch',
              'identity_codepoint_execute_batch',
              'trajectory_composition_decode_calculate_batch',
              'trajectory_composition_decode_execute_batch',
              'canonical_deposit_batch',
              'unicode_root_build_and_activate'
          )
          AND privilege.grantee = 0
          AND privilege.privilege_type = 'EXECUTE'
    ) THEN
        RAISE EXCEPTION 'PUBLIC can execute a native Laplace PostgreSQL binding';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    batch_positions integer[];
    calculated laplace.identity_batch_result;
    batch_result laplace.identity_batch_result;
    batch_receipt_count bigint;
    result laplace.identity_batch_result;
    repeated laplace.identity_batch_result;
    changed laplace.identity_batch_result;
    changed_context laplace.execution_context;
    before_xmin xid;
    before_ctid tid;
    expected native_expected%ROWTYPE;
    receipt_count bigint;
BEGIN
    SELECT * INTO STRICT expected FROM native_expected;
    SELECT count(*) INTO receipt_count FROM laplace.execution_receipt;
    calculated := laplace.identity_codepoint_calculate_batch(
        pg_temp.execution_context(), ARRAY[50, 53, 53]);
    IF (SELECT count(*) FROM laplace.execution_receipt) <> receipt_count THEN
        RAISE EXCEPTION 'pure identity calculation published durable state';
    END IF;
    result := laplace.identity_codepoint_execute_batch(
        pg_temp.execution_context(), ARRAY[50, 53, 53]);
    IF calculated IS DISTINCT FROM result THEN
        RAISE EXCEPTION 'pure and durable identity routes produced different results';
    END IF;
    IF result.entity_ids IS DISTINCT FROM expected.identity_entities THEN
        RAISE EXCEPTION 'SPI identity output differs from the native batch';
    END IF;
    IF result.receipt_id <> expected.identity_receipt
       OR result.context_fingerprint <> expected.context_fingerprint
       OR result.program_fingerprint <> expected.identity_program
       OR result.input_fingerprint <> expected.identity_input
       OR result.output_fingerprint <> expected.identity_output
       OR result.instruction_count <> 1
       OR result.executed_instruction_count <> 1
       OR result.isa_major <> 1
       OR result.isa_minor <> 2
       OR result.status <> 0
       OR result.item_count <> 3 THEN
        RAISE EXCEPTION 'SPI identity receipt differs from the native receipt';
    END IF;

    SELECT xmin, ctid INTO before_xmin, before_ctid
    FROM laplace.execution_receipt
    WHERE receipt_id = result.receipt_id;
    repeated := laplace.identity_codepoint_execute_batch(
        pg_temp.execution_context(), ARRAY[50, 53, 53]);
    IF repeated.receipt_id <> result.receipt_id THEN
        RAISE EXCEPTION 'deterministic replay changed receipt identity';
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM laplace.execution_receipt
        WHERE receipt_id = result.receipt_id
          AND xmin = before_xmin
          AND ctid = before_ctid
    ) THEN
        RAISE EXCEPTION 'deterministic replay rewrote the durable receipt';
    END IF;

    changed_context := pg_temp.execution_context();
    changed_context.memory_bytes := changed_context.memory_bytes + 1;
    changed := laplace.identity_codepoint_calculate_batch(
        changed_context, ARRAY[50, 53, 53]);
    IF changed.entity_ids IS DISTINCT FROM result.entity_ids
       OR changed.context_fingerprint = result.context_fingerprint
       OR changed.program_fingerprint = result.program_fingerprint
       OR changed.receipt_id = result.receipt_id THEN
        RAISE EXCEPTION 'explicit PostgreSQL execution context was not bound to the ISA receipt';
    END IF;

    SELECT count(*) INTO receipt_count FROM laplace.execution_receipt;
    BEGIN
        PERFORM laplace.identity_codepoint_execute_batch(
            pg_temp.execution_context(), ARRAY[50, 1114112]);
        RAISE EXCEPTION 'invalid later identity element was accepted';
    EXCEPTION
        WHEN invalid_parameter_value THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.execution_receipt) <> receipt_count THEN
        RAISE EXCEPTION 'invalid identity batch published a receipt';
    END IF;

    changed_context.memory_bytes := 0;
    BEGIN
        PERFORM laplace.identity_codepoint_execute_batch(
            changed_context, ARRAY[50, 53, 53]);
        RAISE EXCEPTION 'invalid execution context was accepted';
    EXCEPTION
        WHEN invalid_parameter_value THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.execution_receipt) <> receipt_count THEN
        RAISE EXCEPTION 'invalid execution context published a receipt';
    END IF;

    SELECT count(*) INTO batch_receipt_count FROM laplace.execution_receipt;
    SELECT array_agg(position ORDER BY position)
    INTO STRICT batch_positions
    FROM generate_series(0, 4095) AS positions(position);
    batch_result := laplace.identity_codepoint_calculate_batch(
        pg_temp.execution_context(), batch_positions);
    IF cardinality(batch_result.entity_ids) <> 4096
       OR batch_result.item_count <> 4096 THEN
        RAISE EXCEPTION 'set-generated identity batch was not executed as one batch';
    END IF;
    IF (SELECT count(*) FROM laplace.execution_receipt) <> batch_receipt_count THEN
        RAISE EXCEPTION 'set-generated pure batch published durable state';
    END IF;
    calculated := batch_result;
    batch_result := laplace.identity_codepoint_execute_batch(
        pg_temp.execution_context(), batch_positions);
    IF batch_result IS DISTINCT FROM calculated THEN
        RAISE EXCEPTION 'set-generated pure and durable batches differ';
    END IF;
    IF (SELECT count(*) FROM laplace.execution_receipt) <> batch_receipt_count + 1 THEN
        RAISE EXCEPTION 'one identity batch did not publish exactly one receipt';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    calculated laplace.trajectory_batch_result;
    result laplace.trajectory_batch_result;
    occurrence laplace.composition_occurrence;
    expected native_expected%ROWTYPE;
    receipt_count bigint;
BEGIN
    SELECT * INTO STRICT expected FROM native_expected;
    SELECT count(*) INTO receipt_count FROM laplace.execution_receipt;
    calculated := laplace.trajectory_composition_decode_calculate_batch(
        pg_temp.execution_context(), ARRAY[expected.trajectory_carrier]);
    IF (SELECT count(*) FROM laplace.execution_receipt) <> receipt_count THEN
        RAISE EXCEPTION 'pure trajectory calculation published durable state';
    END IF;
    result := laplace.trajectory_composition_decode_execute_batch(
        pg_temp.execution_context(), ARRAY[expected.trajectory_carrier]);
    IF calculated IS DISTINCT FROM result THEN
        RAISE EXCEPTION 'pure and durable trajectory routes produced different results';
    END IF;
    occurrence := result.occurrences[1];
    IF array_length(result.occurrences, 1) <> 1
       OR occurrence.entity_id <> expected.trajectory_entity
       OR occurrence.logical_ordinal <> 1
       OR occurrence.metadata <> 105226698753
       OR occurrence.atom <> 49
       OR occurrence.packed_ordinal <> 1
       OR occurrence.run_length <> 1
       OR occurrence.tier <> 0
       OR occurrence.has_atom IS NOT TRUE
       OR result.logical_count <> 1 THEN
        RAISE EXCEPTION 'SPI trajectory output differs from exact native decode';
    END IF;
    IF result.receipt_id <> expected.trajectory_receipt
       OR result.context_fingerprint <> expected.context_fingerprint
       OR result.program_fingerprint <> expected.trajectory_program
       OR result.input_fingerprint <> expected.trajectory_input
       OR result.output_fingerprint <> expected.trajectory_output
       OR result.item_count <> 1 THEN
        RAISE EXCEPTION 'SPI trajectory receipt differs from the native receipt';
    END IF;

    SELECT count(*) INTO receipt_count FROM laplace.execution_receipt;
    BEGIN
        PERFORM laplace.trajectory_composition_decode_execute_batch(
            pg_temp.execution_context(),
            ARRAY[expected.trajectory_carrier, '\x0000000000000000000000000000000000000000000000000000000000000000'::bytea]);
        RAISE EXCEPTION 'invalid later trajectory carrier was accepted';
    EXCEPTION
        WHEN invalid_binary_representation THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.execution_receipt) <> receipt_count THEN
        RAISE EXCEPTION 'invalid trajectory batch published a receipt';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    expected persistence_expected%ROWTYPE;
    result laplace.canonical_deposit_result;
    replay laplace.canonical_deposit_result;
    incomplete_frames bytea[];
    collision_frames bytea[];
    before_receipt_xmin xid;
    before_receipt_ctid tid;
    candidate_physicality bytea;
    carriers bytea[];
    decoded laplace.trajectory_batch_result;
    bulk_frames bytea[];
    bulk_result laplace.canonical_deposit_result;
    plan_line text;
    plan_text text := '';
    maximum_vertex_bytes integer;
    carrier_storage_bytes bigint;
    table_count bigint;
    low_memory_context laplace.execution_context;
BEGIN
    SELECT * INTO STRICT expected FROM persistence_expected;

    SELECT array_agg(frame ORDER BY ordinal)
    INTO STRICT incomplete_frames
    FROM unnest(expected.frames) WITH ORDINALITY AS input(frame, ordinal)
    WHERE substring(frame FROM 9 FOR 16) <> expected.entity_b;
    BEGIN
        PERFORM laplace.canonical_deposit_batch(
            pg_temp.persistence_context(), expected.source_fingerprint,
            expected.recipe_fingerprint, incomplete_frames);
        RAISE EXCEPTION 'unresolved in-stream entity reference was accepted';
    EXCEPTION
        WHEN foreign_key_violation THEN NULL;
    END;
    IF EXISTS (SELECT FROM laplace.canonical_entity)
       OR EXISTS (SELECT FROM laplace.physicality)
       OR EXISTS (SELECT FROM laplace.composition_trajectory_vertex)
       OR EXISTS (SELECT FROM laplace.observed_occurrence)
       OR EXISTS (SELECT FROM laplace.canonical_deposit_receipt) THEN
        RAISE EXCEPTION 'failed reference preflight published partial canonical state';
    END IF;

    low_memory_context := pg_temp.persistence_context();
    low_memory_context.memory_bytes := 1024;
    BEGIN
        PERFORM laplace.canonical_deposit_batch(
            low_memory_context, expected.source_fingerprint,
            expected.recipe_fingerprint, expected.frames);
        RAISE EXCEPTION 'persistence working set exceeded its resource grant';
    EXCEPTION
        WHEN data_exception THEN NULL;
    END;
    IF EXISTS (SELECT FROM laplace.canonical_entity)
       OR EXISTS (SELECT FROM laplace.physicality)
       OR EXISTS (SELECT FROM laplace.composition_trajectory_vertex)
       OR EXISTS (SELECT FROM laplace.observed_occurrence)
       OR EXISTS (SELECT FROM laplace.canonical_deposit_receipt) THEN
        RAISE EXCEPTION 'resource rejection published partial canonical state';
    END IF;

    result := laplace.canonical_deposit_batch(
        pg_temp.persistence_context(), expected.source_fingerprint,
        expected.recipe_fingerprint, expected.frames);
    IF result.entity_count <> 2
       OR result.physicality_count <> 1
       OR result.trajectory_vertex_count <> 3
       OR result.occurrence_count <> 1
       OR result.logical_occurrence_count <> 5
       OR result.entity_inserted <> 2
       OR result.physicality_inserted <> 1
       OR result.trajectory_vertex_inserted <> 3
       OR result.occurrence_inserted <> 1
       OR result.plan_count <> 11
       OR result.plan_sequence_fingerprint <> expected.plan_sequence_fingerprint
       OR result.effect_disposition <> 1
       OR result.status <> 0 THEN
        RAISE EXCEPTION 'canonical persistence result differs from native stream contract';
    END IF;
    IF (SELECT count(*) FROM laplace.canonical_entity) <> 2
       OR (SELECT count(*) FROM laplace.physicality) <> 1
       OR (SELECT count(*) FROM laplace.composition_trajectory_vertex) <> 3
       OR (SELECT count(*) FROM laplace.observed_occurrence) <> 1
       OR (SELECT count(*) FROM laplace.canonical_deposit_receipt) <> 1 THEN
        RAISE EXCEPTION 'one canonical stream did not deposit all four record families atomically';
    END IF;
    IF NOT EXISTS (
        SELECT FROM laplace.canonical_entity
        WHERE entity_id = expected.entity_a
          AND identity_witness = expected.entity_a_witness
    ) OR NOT EXISTS (
        SELECT FROM laplace.physicality
        WHERE physicality_id = expected.physicality_id
          AND entity_id = expected.entity_a
          AND trajectory_fingerprint = expected.trajectory_fingerprint
    ) OR NOT EXISTS (
        SELECT FROM laplace.observed_occurrence
        WHERE occurrence_id = expected.occurrence_id
          AND entity_id = expected.entity_a
          AND physicality_id = expected.physicality_id
    ) THEN
        RAISE EXCEPTION 'normalized persistence rows lost native identity or reference fields';
    END IF;
    IF EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = 'laplace'
          AND table_name IN (
              'canonical_entity', 'physicality',
              'composition_trajectory_vertex', 'observed_occurrence')
          AND column_name = 'canonical_frame'
    ) THEN
        RAISE EXCEPTION 'transport frames were duplicated into normalized durable state';
    END IF;
    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_schema = 'laplace'
          AND table_name = 'canonical_entity'
          AND column_name IN ('tier', 'source', 'role', 'language', 'modality')
    ) THEN
        RAISE EXCEPTION 'canonical identity was coupled to source, use or structural altitude';
    END IF;

    SELECT physicality_id INTO STRICT candidate_physicality
    FROM laplace.composition_trajectory_vertex
    WHERE constituent_entity_id = expected.entity_a
    ORDER BY physicality_id, vertex_index
    LIMIT 1;
    SELECT array_agg(carrier ORDER BY vertex_index)
    INTO STRICT carriers
    FROM laplace.composition_trajectory_vertex
    WHERE physicality_id = candidate_physicality;
    decoded := laplace.trajectory_composition_decode_calculate_batch(
        pg_temp.execution_context(), carriers);
    IF decoded.logical_count <> 5
       OR cardinality(decoded.occurrences) <> 3 THEN
        RAISE EXCEPTION 'indexed candidate did not survive exact native trajectory proof';
    END IF;

    SELECT xmin, ctid INTO before_receipt_xmin, before_receipt_ctid
    FROM laplace.canonical_deposit_receipt
    WHERE receipt_id = result.receipt_id;
    replay := laplace.canonical_deposit_batch(
        pg_temp.persistence_context(), expected.source_fingerprint,
        expected.recipe_fingerprint, expected.frames);
    IF replay.receipt_id <> result.receipt_id
       OR replay.entity_inserted <> 0
       OR replay.physicality_inserted <> 0
       OR replay.trajectory_vertex_inserted <> 0
       OR replay.occurrence_inserted <> 0
       OR replay.plan_sequence_fingerprint <> result.plan_sequence_fingerprint THEN
        RAISE EXCEPTION 'deterministic canonical replay changed receipt or rewrote records';
    END IF;
    IF NOT EXISTS (
        SELECT FROM laplace.canonical_deposit_receipt
        WHERE receipt_id = result.receipt_id
          AND xmin = before_receipt_xmin
          AND ctid = before_receipt_ctid
    ) THEN
        RAISE EXCEPTION 'canonical replay rewrote its immutable receipt';
    END IF;

    SELECT array_agg(
        CASE
            WHEN substring(frame FROM 9 FOR 16) = expected.entity_a
            THEN set_byte(frame, octet_length(frame) - 1,
                          get_byte(frame, octet_length(frame) - 1) # 1)
            ELSE frame
        END
        ORDER BY ordinal)
    INTO STRICT collision_frames
    FROM unnest(expected.frames) WITH ORDINALITY AS input(frame, ordinal);
    BEGIN
        PERFORM laplace.canonical_deposit_batch(
            pg_temp.persistence_context(), expected.source_fingerprint,
            expected.recipe_fingerprint, collision_frames);
        RAISE EXCEPTION 'same 128-bit identity with a different full witness was accepted';
    EXCEPTION
        WHEN data_corrupted THEN NULL;
    END;
    SELECT count(*) INTO table_count FROM laplace.canonical_entity;
    IF table_count <> 2 THEN
        RAISE EXCEPTION 'identity collision changed canonical entity cardinality';
    END IF;

    bulk_frames := ARRAY[
        substring(expected.bulk_stream FROM 1 FOR 56),
        substring(expected.bulk_stream FROM 57 FOR 56),
        substring(expected.bulk_stream FROM 113 FOR 232)
    ] || ARRAY(
        SELECT substring(expected.bulk_stream FROM 345 + vertex * 80 FOR 80)
        FROM generate_series(0, 4095) AS vertices(vertex)
        ORDER BY vertex
    ) || ARRAY[
        substring(expected.bulk_stream FROM 328025 FOR 168)
    ];
    IF cardinality(bulk_frames) <> 4100 THEN
        RAISE EXCEPTION 'bulk native stream was not split at complete frame boundaries';
    END IF;
    bulk_result := laplace.canonical_deposit_batch(
        pg_temp.persistence_context(), expected.source_fingerprint,
        expected.recipe_fingerprint, bulk_frames);
    IF bulk_result.total_records <> 4100
       OR bulk_result.entity_inserted <> 0
       OR bulk_result.physicality_inserted <> 1
       OR bulk_result.trajectory_vertex_inserted <> 4096
       OR bulk_result.occurrence_inserted <> 1
       OR bulk_result.plan_count <> 11
       OR bulk_result.plan_sequence_fingerprint <> expected.plan_sequence_fingerprint THEN
        RAISE EXCEPTION 'bulk stream was not deposited through one bounded plan sequence';
    END IF;
    IF NOT EXISTS (
        SELECT FROM laplace.physicality
        WHERE physicality_id = expected.bulk_physicality_id
          AND vertex_count = 4096
          AND logical_count = 4096
    ) OR NOT EXISTS (
        SELECT FROM laplace.observed_occurrence
        WHERE occurrence_id = expected.bulk_occurrence_id
          AND physicality_id = expected.bulk_physicality_id
    ) THEN
        RAISE EXCEPTION 'bulk physicality or occurrence did not close atomically';
    END IF;

    ANALYZE laplace.composition_trajectory_vertex;
    FOR plan_line IN EXECUTE
        'EXPLAIN (ANALYZE, BUFFERS, WAL, FORMAT TEXT) '
        'SELECT physicality_id, vertex_index '
        'FROM laplace.composition_trajectory_vertex '
        'WHERE constituent_entity_id = $1 '
        'ORDER BY physicality_id, vertex_index'
        USING expected.entity_a
    LOOP
        plan_text := plan_text || plan_line || E'\n';
    END LOOP;
    IF position('composition_trajectory_vertex_constituent_idx' IN plan_text) = 0
       OR position('actual time=' IN plan_text) = 0
       OR position('Buffers:' IN plan_text) = 0 THEN
        RAISE EXCEPTION 'indexed candidate plan lacks analyzed index/buffer evidence: %', plan_text;
    END IF;

    SELECT max(pg_column_size(vertex)), sum(octet_length(carrier))
    INTO STRICT maximum_vertex_bytes, carrier_storage_bytes
    FROM laplace.composition_trajectory_vertex AS vertex;
    IF maximum_vertex_bytes > 192
       OR carrier_storage_bytes <>
          32 * (SELECT count(*) FROM laplace.composition_trajectory_vertex) THEN
        RAISE EXCEPTION 'normalized trajectory storage duplicated its transport frame';
    END IF;
END
$contract$;

CREATE TEMP TABLE zero_pattern_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    source_fingerprint bytea NOT NULL,
    recipe_fingerprint bytea NOT NULL,
    entity_id bytea NOT NULL,
    physicality_id bytea NOT NULL,
    occurrence_id bytea NOT NULL,
    zero_digest bytea NOT NULL,
    plan_sequence_fingerprint bytea NOT NULL,
    frames bytea[] NOT NULL
);

INSERT INTO zero_pattern_expected
SELECT
    true,
    source_fingerprint,
    recipe_fingerprint,
    decode(:'persistence_zero_entity', 'hex'),
    decode(:'persistence_zero_physicality', 'hex'),
    decode(:'persistence_zero_occurrence', 'hex'),
    decode(repeat('00', 32), 'hex'),
    plan_sequence_fingerprint,
    ARRAY[
        decode(:'persistence_zero_frame_0', 'hex'),
        decode(:'persistence_zero_frame_1', 'hex'),
        decode(:'persistence_zero_frame_2', 'hex'),
        decode(:'persistence_zero_frame_3', 'hex')
    ]
FROM persistence_expected;

DO $zero_patterns$
DECLARE
    expected zero_pattern_expected%ROWTYPE;
    result laplace.canonical_deposit_result;
BEGIN
    SELECT * INTO STRICT expected FROM zero_pattern_expected;
    result := laplace.canonical_deposit_batch(
        pg_temp.persistence_context(), expected.source_fingerprint,
        expected.recipe_fingerprint, expected.frames);
    IF result.entity_count <> 1
       OR result.physicality_count <> 1
       OR result.trajectory_vertex_count <> 1
       OR result.occurrence_count <> 1
       OR result.entity_inserted <> 1
       OR result.physicality_inserted <> 1
       OR result.trajectory_vertex_inserted <> 1
       OR result.occurrence_inserted <> 1
       OR result.plan_count <> 11
       OR result.plan_sequence_fingerprint <> expected.plan_sequence_fingerprint THEN
        RAISE EXCEPTION 'zero-pattern native stream did not use the canonical set sink';
    END IF;
    IF NOT EXISTS (
        SELECT FROM laplace.canonical_entity
        WHERE entity_id = expected.entity_id
          AND identity_witness = expected.zero_digest
    ) OR NOT EXISTS (
        SELECT FROM laplace.physicality
        WHERE physicality_id = expected.physicality_id
          AND entity_id = expected.entity_id
          AND recipe_fingerprint = expected.zero_digest
          AND geometry_epoch = expected.zero_digest
    ) OR NOT EXISTS (
        SELECT FROM laplace.composition_trajectory_vertex
        WHERE physicality_id = expected.physicality_id
          AND constituent_entity_id = expected.entity_id
    ) OR NOT EXISTS (
        SELECT FROM laplace.observed_occurrence
        WHERE occurrence_id = expected.occurrence_id
          AND entity_id = expected.entity_id
          AND physicality_id = expected.physicality_id
          AND source_fingerprint = expected.zero_digest
          AND context_fingerprint = expected.zero_digest
    ) THEN
        RAISE EXCEPTION 'zero identity/digest bit patterns were treated as absence';
    END IF;
END
$zero_patterns$;

SELECT 'postgres.spi-isa-contract passed' AS result;
