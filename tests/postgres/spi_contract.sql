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

CREATE TEMP TABLE highway_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    receipt bytea NOT NULL,
    context_fingerprint bytea NOT NULL,
    program_fingerprint bytea NOT NULL,
    input_fingerprint bytea NOT NULL,
    output_fingerprint bytea NOT NULL,
    coordinates bytea[] NOT NULL,
    collision_fingerprints bytea[] NOT NULL
);

INSERT INTO highway_expected VALUES (
    true,
    decode(:'highway_receipt', 'hex'),
    decode(:'highway_context', 'hex'),
    decode(:'highway_program', 'hex'),
    decode(:'highway_input', 'hex'),
    decode(:'highway_output', 'hex'),
    ARRAY[
        decode(:'highway_coordinate_0', 'hex'),
        decode(:'highway_coordinate_1', 'hex')
    ],
    ARRAY[
        decode(:'highway_fingerprint_0', 'hex'),
        decode(:'highway_fingerprint_1', 'hex')
    ]
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

CREATE TEMP TABLE evidence_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    root_node bytea NOT NULL,
    copy_node bytea NOT NULL,
    independent_node bytea NOT NULL,
    root_source bytea NOT NULL,
    root_context bytea NOT NULL,
    copy_source bytea NOT NULL,
    copy_context bytea NOT NULL,
    independent_source bytea NOT NULL,
    independent_context bytea NOT NULL,
    lineage_receipt bytea NOT NULL,
    lineage_input bytea NOT NULL,
    lineage_output bytea NOT NULL,
    isa_receipt bytea NOT NULL
);

INSERT INTO evidence_expected VALUES (
    true,
    decode(:'evidence_root_node', 'hex'),
    decode(:'evidence_copy_node', 'hex'),
    decode(:'evidence_independent_node', 'hex'),
    decode(:'evidence_root_source', 'hex'),
    decode(:'evidence_root_context', 'hex'),
    decode(:'evidence_copy_source', 'hex'),
    decode(:'evidence_copy_context', 'hex'),
    decode(:'evidence_independent_source', 'hex'),
    decode(:'evidence_independent_context', 'hex'),
    decode(:'evidence_lineage_receipt', 'hex'),
    decode(:'evidence_lineage_input', 'hex'),
    decode(:'evidence_lineage_output', 'hex'),
    decode(:'evidence_isa_receipt', 'hex')
);

CREATE TEMP TABLE testimony_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    source_profile_id bytea NOT NULL,
    recipe_receipt_id bytea NOT NULL,
    trust_input_id bytea NOT NULL,
    testimony_ids bytea[] NOT NULL,
    evidence_node_ids bytea[] NOT NULL,
    outcome_detail_ids bytea[] NOT NULL,
    source_types integer[] NOT NULL,
    outcome_types integer[] NOT NULL,
    dispositions integer[] NOT NULL,
    uncertainty_numerators numeric[] NOT NULL,
    uncertainty_denominators numeric[] NOT NULL,
    sample_counts numeric[] NOT NULL,
    testimony_receipt_id bytea NOT NULL,
    input_fingerprint bytea NOT NULL,
    output_fingerprint bytea NOT NULL,
    isa_receipt_id bytea NOT NULL
);

INSERT INTO testimony_expected VALUES (
    true,
    decode(:'testimony_profile', 'hex'),
    decode(:'testimony_recipe', 'hex'),
    decode(:'testimony_trust', 'hex'),
    ARRAY[
        decode(:'testimony_0_id', 'hex'),
        decode(:'testimony_1_id', 'hex'),
        decode(:'testimony_2_id', 'hex')],
    ARRAY[
        decode(:'testimony_0_node', 'hex'),
        decode(:'testimony_1_node', 'hex'),
        decode(:'testimony_2_node', 'hex')],
    ARRAY[
        decode(:'testimony_0_outcome', 'hex'),
        decode(:'testimony_1_outcome', 'hex'),
        decode(:'testimony_2_outcome', 'hex')],
    ARRAY[
        :'testimony_0_source_type'::integer,
        :'testimony_1_source_type'::integer,
        :'testimony_2_source_type'::integer],
    ARRAY[
        :'testimony_0_outcome_type'::integer,
        :'testimony_1_outcome_type'::integer,
        :'testimony_2_outcome_type'::integer],
    ARRAY[
        :'testimony_0_disposition'::integer,
        :'testimony_1_disposition'::integer,
        :'testimony_2_disposition'::integer],
    ARRAY[
        :'testimony_0_uncertainty_numerator'::numeric,
        :'testimony_1_uncertainty_numerator'::numeric,
        :'testimony_2_uncertainty_numerator'::numeric],
    ARRAY[
        :'testimony_0_uncertainty_denominator'::numeric,
        :'testimony_1_uncertainty_denominator'::numeric,
        :'testimony_2_uncertainty_denominator'::numeric],
    ARRAY[
        :'testimony_0_sample_count'::numeric,
        :'testimony_1_sample_count'::numeric,
        :'testimony_2_sample_count'::numeric],
    decode(:'testimony_receipt', 'hex'),
    decode(:'testimony_input', 'hex'),
    decode(:'testimony_output', 'hex'),
    decode(:'testimony_isa_receipt', 'hex')
);

CREATE TEMP TABLE source_profile_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    profile_a_id bytea NOT NULL,
    profile_b_id bytea NOT NULL,
    receipt_id bytea NOT NULL,
    selected_boundary_fingerprint bytea NOT NULL,
    input_fingerprint bytea NOT NULL,
    output_fingerprint bytea NOT NULL,
    isa_receipt_id bytea NOT NULL
);

INSERT INTO source_profile_expected VALUES (
    true,
    decode(:'source_profile_a_id', 'hex'),
    decode(:'source_profile_b_id', 'hex'),
    decode(:'source_profile_receipt', 'hex'),
    decode(:'source_profile_boundary', 'hex'),
    decode(:'source_profile_input', 'hex'),
    decode(:'source_profile_output', 'hex'),
    decode(:'source_profile_isa_receipt', 'hex')
);

CREATE TEMP TABLE reference_mapping_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    mapping_ids bytea[] NOT NULL,
    proposition_ids bytea[] NOT NULL,
    occurrence_ids bytea[] NOT NULL,
    receipt_id bytea NOT NULL,
    boundary_id bytea NOT NULL,
    input_fingerprint bytea NOT NULL,
    output_fingerprint bytea NOT NULL,
    isa_receipt_id bytea NOT NULL
);

INSERT INTO reference_mapping_expected VALUES (
    true,
    ARRAY[
        decode(:'reference_mapping_id_0', 'hex'),
        decode(:'reference_mapping_id_1', 'hex'),
        decode(:'reference_mapping_id_2', 'hex')],
    ARRAY[
        decode(:'reference_mapping_proposition_0', 'hex'),
        decode(:'reference_mapping_proposition_1', 'hex'),
        decode(:'reference_mapping_proposition_2', 'hex')],
    ARRAY[
        decode(:'reference_mapping_occurrence_0', 'hex'),
        decode(:'reference_mapping_occurrence_1', 'hex'),
        decode(:'reference_mapping_occurrence_2', 'hex')],
    decode(:'reference_mapping_receipt', 'hex'),
    decode(:'reference_mapping_boundary', 'hex'),
    decode(:'reference_mapping_input', 'hex'),
    decode(:'reference_mapping_output', 'hex'),
    decode(:'reference_mapping_isa_receipt', 'hex')
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
                ('laplace.highway_coordinate_calculate_batch(laplace.execution_context,laplace.highway_key[])', 'i', 's'),
                ('laplace.highway_coordinate_execute_batch(laplace.execution_context,laplace.highway_key[])', 'v', 'u'),
                ('laplace.highway_registry_admit_and_activate(laplace.execution_context,numeric)', 'v', 'u'),
                ('laplace.highway_registry_resolve_active(laplace.execution_context)', 's', 'u'),
                ('laplace.evidence_record_lineage_batch(laplace.execution_context,laplace.evidence_lineage_record[],numeric)', 'v', 'u'),
                ('laplace.evidence_record_testimony_batch(laplace.execution_context,laplace.evidence_testimony_record[])', 'v', 'u'),
                ('laplace.canonical_deposit_batch(laplace.execution_context,bytea,bytea,bytea[])', 'v', 'u'),
                ('laplace.unicode_root_build_and_activate(laplace.execution_context,text,text,text,text,bytea,bytea,bigint,boolean,bytea,bytea,bigint)', 'v', 'u'),
                ('laplace.unicode_tier0_resolve_batch(bytea,bytea,integer[])', 's', 'u'),
                ('laplace.unicode_identity_reverse_resolve_batch(bytea,bytea,bytea[],bytea[])', 's', 'u')
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
              'highway_coordinate_calculate_batch',
              'highway_coordinate_execute_batch',
              'highway_registry_admit_and_activate',
              'highway_registry_resolve_active',
              'evidence_record_lineage_batch',
              'evidence_record_testimony_batch',
              'canonical_deposit_batch',
              'unicode_root_build_and_activate',
              'unicode_tier0_resolve_batch',
              'unicode_identity_reverse_resolve_batch'
          )
          AND privilege.grantee = 0
          AND privilege.privilege_type = 'EXECUTE'
    ) THEN
        RAISE EXCEPTION 'PUBLIC can execute a native Laplace PostgreSQL binding';
    END IF;
END
$contract$;

CREATE FUNCTION pg_temp.run_evidence_contract()
RETURNS void
LANGUAGE plpgsql
AS $evidence$
DECLARE
    expected evidence_expected%ROWTYPE;
    persistence persistence_expected%ROWTYPE;
    records laplace.evidence_lineage_record[];
    cycle_records laplace.evidence_lineage_record[];
    result laplace.evidence_lineage_result;
    replay laplace.evidence_lineage_result;
    receipt_xmin xid;
    receipt_ctid tid;
    nodes_before bigint;
    edges_before bigint;
    roots_before bigint;
BEGIN
    SELECT * INTO STRICT expected FROM evidence_expected;
    SELECT * INTO STRICT persistence FROM persistence_expected;
    SELECT array_agg(record ORDER BY (record).node_id)
    INTO STRICT records
    FROM (VALUES
        (ROW(expected.root_node, persistence.entity_a, persistence.occurrence_id,
             expected.root_source, expected.root_context, decode(repeat('00',32),'hex'),
             1::numeric, 1, 1, 0, 0)::laplace.evidence_lineage_record),
        (ROW(expected.copy_node, persistence.entity_a, persistence.occurrence_id,
             expected.copy_source, expected.copy_context, decode(repeat('00',32),'hex'),
             2::numeric, 1, 2, 0, 0)::laplace.evidence_lineage_record),
        (ROW(expected.independent_node, persistence.entity_a, persistence.occurrence_id,
             expected.independent_source, expected.independent_context,
             decode(repeat('00',32),'hex'),
             3::numeric, 1, 1, 0, 0)::laplace.evidence_lineage_record)
    ) AS nodes(record);
    records := records || ARRAY[
        ROW(expected.copy_node, decode(repeat('00',16),'hex'),
            decode(repeat('00',32),'hex'), decode(repeat('00',32),'hex'),
            decode(repeat('00',32),'hex'), expected.root_node,
            0::numeric, 2, 0, 0, 0)::laplace.evidence_lineage_record
    ];
    result := laplace.evidence_record_lineage_batch(
        pg_temp.persistence_context(), records, 3::numeric);
    IF result.lineage_receipt_id <> expected.lineage_receipt
       OR result.input_fingerprint <> expected.lineage_input
       OR result.output_fingerprint <> expected.lineage_output
       OR result.isa_receipt_id <> expected.isa_receipt
       OR result.input_record_count <> 4
       OR result.node_count <> 3
       OR result.edge_count <> 1
       OR result.root_relation_count <> 3
       OR cardinality(result.node_ids) <> 3
       OR cardinality(result.root_node_ids) <> 3
       OR (SELECT count(DISTINCT root_id) FROM unnest(result.root_node_ids) root_id) <> 2
       OR NOT EXISTS (
            SELECT FROM unnest(result.node_ids, result.root_node_ids) pair(node_id, root_id)
            WHERE pair.node_id = expected.copy_node
              AND pair.root_id = expected.root_node)
       OR NOT EXISTS (
            SELECT FROM unnest(result.node_ids, result.root_node_ids) pair(node_id, root_id)
            WHERE pair.node_id = expected.independent_node
              AND pair.root_id = expected.independent_node) THEN
        RAISE EXCEPTION 'PostgreSQL evidence lineage differs from the native ISA result'
            USING DETAIL = format(
                'actual=%s expected_lineage_receipt=%s expected_input=%s expected_output=%s expected_isa_receipt=%s',
                result::text,
                encode(expected.lineage_receipt, 'hex'),
                encode(expected.lineage_input, 'hex'),
                encode(expected.lineage_output, 'hex'),
                encode(expected.isa_receipt, 'hex'));
    END IF;
    IF (SELECT count(*) FROM laplace.evidence_node) <> 3
       OR (SELECT count(*) FROM laplace.evidence_dependence) <> 1
       OR (SELECT count(*) FROM laplace.evidence_root_projection) <> 3
       OR (SELECT count(*) FROM laplace.evidence_lineage_receipt) <> 1 THEN
        RAISE EXCEPTION 'evidence lineage did not persist its complete batch atomically';
    END IF;
    SELECT xmin, ctid INTO STRICT receipt_xmin, receipt_ctid
    FROM laplace.evidence_lineage_receipt
    WHERE receipt_id = result.lineage_receipt_id;
    replay := laplace.evidence_record_lineage_batch(
        pg_temp.persistence_context(), records, 3::numeric);
    IF replay IS DISTINCT FROM result OR NOT EXISTS (
        SELECT FROM laplace.evidence_lineage_receipt
        WHERE receipt_id = result.lineage_receipt_id
          AND xmin = receipt_xmin AND ctid = receipt_ctid) THEN
        RAISE EXCEPTION 'evidence replay changed its result or immutable receipt';
    END IF;
    SELECT count(*) INTO nodes_before FROM laplace.evidence_node;
    SELECT count(*) INTO edges_before FROM laplace.evidence_dependence;
    SELECT count(*) INTO roots_before FROM laplace.evidence_root_projection;
    SELECT array_agg(cycle ORDER BY cycle.record_kind,
                                    cycle.node_id,
                                    cycle.parent_node_id)
    INTO STRICT cycle_records
    FROM unnest(records || ARRAY[
        ROW(expected.root_node, decode(repeat('00',16),'hex'),
            decode(repeat('00',32),'hex'), decode(repeat('00',32),'hex'),
            decode(repeat('00',32),'hex'), expected.copy_node,
            0::numeric, 2, 0, 0, 0)::laplace.evidence_lineage_record
    ]) AS cycle;
    BEGIN
        PERFORM laplace.evidence_record_lineage_batch(
            pg_temp.persistence_context(), cycle_records, 4::numeric);
        RAISE EXCEPTION 'evidence dependence cycle was accepted';
    EXCEPTION
        WHEN invalid_recursion THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.evidence_node) <> nodes_before
       OR (SELECT count(*) FROM laplace.evidence_dependence) <> edges_before
       OR (SELECT count(*) FROM laplace.evidence_root_projection) <> roots_before
       OR (SELECT count(*) FROM laplace.evidence_lineage_receipt) <> 1 THEN
        RAISE EXCEPTION 'rejected evidence cycle published partial state';
    END IF;
    BEGIN
        UPDATE laplace.evidence_root_projection
        SET path_depth = path_depth + 1
        WHERE node_id = expected.copy_node AND root_node_id = expected.root_node;
        PERFORM laplace.evidence_record_lineage_batch(
            pg_temp.persistence_context(), records, 3::numeric);
        RAISE EXCEPTION 'conflicting durable evidence projection was accepted';
    EXCEPTION
        WHEN data_corrupted THEN NULL;
    END;
    IF NOT EXISTS (
        SELECT FROM laplace.evidence_root_projection
        WHERE node_id = expected.copy_node
          AND root_node_id = expected.root_node
          AND path_depth = 1) THEN
        RAISE EXCEPTION 'conflict subtransaction did not restore exact evidence state';
    END IF;
END
$evidence$;

CREATE FUNCTION pg_temp.run_testimony_contract()
RETURNS void
LANGUAGE plpgsql
AS $testimony$
DECLARE
    expected testimony_expected%ROWTYPE;
    records laplace.evidence_testimony_record[];
    invalid_records laplace.evidence_testimony_record[];
    result laplace.evidence_testimony_result;
    replay laplace.evidence_testimony_result;
    receipt_xmin xid;
    receipt_ctid tid;
    testimony_before bigint;
    receipt_before bigint;
BEGIN
    SELECT * INTO STRICT expected FROM testimony_expected;
    SELECT array_agg(
        ROW(
            expected.testimony_ids[ordinal],
            expected.evidence_node_ids[ordinal],
            expected.source_profile_id,
            expected.recipe_receipt_id,
            expected.trust_input_id,
            expected.outcome_detail_ids[ordinal],
            expected.uncertainty_numerators[ordinal],
            expected.uncertainty_denominators[ordinal],
            expected.sample_counts[ordinal],
            expected.source_types[ordinal],
            expected.outcome_types[ordinal],
            expected.dispositions[ordinal],
            0)::laplace.evidence_testimony_record
        ORDER BY expected.testimony_ids[ordinal])
    INTO STRICT records
    FROM generate_subscripts(expected.testimony_ids, 1) ordinal;

    result := laplace.evidence_record_testimony_batch(
        pg_temp.persistence_context(), records);
    IF result.testimony_ids IS DISTINCT FROM expected.testimony_ids
       OR result.testimony_receipt_id <> expected.testimony_receipt_id
       OR result.source_profile_id <> expected.source_profile_id
       OR result.input_fingerprint <> expected.input_fingerprint
       OR result.output_fingerprint <> expected.output_fingerprint
       OR result.isa_receipt_id <> expected.isa_receipt_id
       OR result.testimony_count <> 3
       OR result.sample_count <> 13
       OR result.uncertain_count <> 2
       OR result.negative_disposition_count <> 1 THEN
        RAISE EXCEPTION 'PostgreSQL evidence testimony differs from native ISA result'
            USING DETAIL = format(
                'actual=%s expected_receipt=%s expected_input=%s expected_output=%s expected_isa=%s',
                result::text,
                encode(expected.testimony_receipt_id, 'hex'),
                encode(expected.input_fingerprint, 'hex'),
                encode(expected.output_fingerprint, 'hex'),
                encode(expected.isa_receipt_id, 'hex'));
    END IF;
    IF (SELECT count(*) FROM laplace.evidence_testimony) <> 3
       OR (SELECT count(*) FROM laplace.evidence_testimony_receipt) <> 1 THEN
        RAISE EXCEPTION 'testimony did not persist its complete batch atomically';
    END IF;
    IF EXISTS (
        SELECT 1
        FROM unnest(
            expected.testimony_ids,
            expected.evidence_node_ids,
            expected.outcome_detail_ids,
            expected.source_types,
            expected.outcome_types,
            expected.dispositions,
            expected.uncertainty_numerators,
            expected.uncertainty_denominators,
            expected.sample_counts)
            expected_row(
                testimony_id, evidence_node_id, outcome_detail_id,
                source_type, outcome_type, disposition,
                uncertainty_numerator, uncertainty_denominator, sample_count)
        LEFT JOIN laplace.evidence_testimony durable
          ON durable.testimony_id = expected_row.testimony_id
         AND durable.evidence_node_id = expected_row.evidence_node_id
         AND durable.source_profile_id = expected.source_profile_id
         AND durable.recipe_receipt_id = expected.recipe_receipt_id
         AND durable.trust_input_id = expected.trust_input_id
         AND durable.outcome_detail_id = expected_row.outcome_detail_id
         AND durable.source_type = expected_row.source_type
         AND durable.outcome_type = expected_row.outcome_type
         AND durable.disposition = expected_row.disposition
         AND durable.uncertainty_numerator = expected_row.uncertainty_numerator
         AND durable.uncertainty_denominator = expected_row.uncertainty_denominator
         AND durable.sample_count = expected_row.sample_count
         AND durable.flags = 0
        WHERE durable.testimony_id IS NULL
    ) THEN
        RAISE EXCEPTION 'durable testimony rows differ from exact native inputs';
    END IF;

    SELECT xmin, ctid INTO STRICT receipt_xmin, receipt_ctid
    FROM laplace.evidence_testimony_receipt
    WHERE receipt_id = result.testimony_receipt_id;
    replay := laplace.evidence_record_testimony_batch(
        pg_temp.persistence_context(), records);
    IF replay IS DISTINCT FROM result OR NOT EXISTS (
        SELECT FROM laplace.evidence_testimony_receipt
        WHERE receipt_id = result.testimony_receipt_id
          AND xmin = receipt_xmin AND ctid = receipt_ctid) THEN
        RAISE EXCEPTION 'testimony replay changed its result or immutable receipt';
    END IF;

    BEGIN
        UPDATE laplace.evidence_testimony
        SET disposition = CASE disposition WHEN 1 THEN 2 ELSE 1 END
        WHERE testimony_id = expected.testimony_ids[1];
        PERFORM laplace.evidence_record_testimony_batch(
            pg_temp.persistence_context(), records);
        RAISE EXCEPTION 'conflicting durable testimony was accepted';
    EXCEPTION
        WHEN data_corrupted THEN NULL;
    END;
    IF NOT EXISTS (
        SELECT FROM laplace.evidence_testimony
        WHERE testimony_id = expected.testimony_ids[1]
          AND disposition = expected.dispositions[1]) THEN
        RAISE EXCEPTION 'conflict subtransaction did not restore exact testimony state';
    END IF;

    SELECT array_agg(
        ROW(
            (records[ordinal]).testimony_id,
            (records[ordinal]).evidence_node_id,
            (records[ordinal]).source_profile_id,
            (records[ordinal]).recipe_receipt_id,
            (records[ordinal]).trust_input_id,
            (records[ordinal]).outcome_detail_id,
            (records[ordinal]).uncertainty_numerator,
            (records[ordinal]).uncertainty_denominator,
            CASE ordinal WHEN 1 THEN 0 ELSE (records[ordinal]).sample_count END,
            (records[ordinal]).source_type,
            (records[ordinal]).outcome_type,
            (records[ordinal]).disposition,
            (records[ordinal]).flags)::laplace.evidence_testimony_record
        ORDER BY ordinal)
    INTO STRICT invalid_records
    FROM generate_subscripts(records, 1) ordinal;
    SELECT count(*) INTO testimony_before FROM laplace.evidence_testimony;
    SELECT count(*) INTO receipt_before FROM laplace.evidence_testimony_receipt;
    BEGIN
        PERFORM laplace.evidence_record_testimony_batch(
            pg_temp.persistence_context(), invalid_records);
        RAISE EXCEPTION 'zero-sample testimony was accepted';
    EXCEPTION
        WHEN data_exception THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.evidence_testimony) <> testimony_before
       OR (SELECT count(*) FROM laplace.evidence_testimony_receipt) <> receipt_before THEN
        RAISE EXCEPTION 'rejected testimony published partial durable state';
    END IF;
END
$testimony$;

CREATE FUNCTION pg_temp.run_source_profile_contract()
RETURNS void
LANGUAGE plpgsql
AS $source_profile$
DECLARE
    expected source_profile_expected%ROWTYPE;
    profiles laplace.source_profile_manifest[];
    reversed laplace.source_profile_manifest[];
    result laplace.source_profile_result;
    replay laplace.source_profile_result;
    expected_ids bytea[];
    receipt_xmin xid;
    receipt_ctid tid;
    profile_before bigint;
    receipt_before bigint;
BEGIN
    SELECT * INTO STRICT expected FROM source_profile_expected;
    SELECT array_agg(profile ORDER BY (profile).profile_id)
    INTO STRICT profiles
    FROM (VALUES
        (ROW(
            expected.profile_a_id, 17,
            decode('101112131415161718191a1b1c1d1e1f','hex'),
            decode('303132333435363738393a3b3c3d3e3f','hex'),
            decode('505152535455565758595a5b5c5d5e5f','hex'),
            decode('707172737475767778797a7b7c7d7e7f','hex'),
            1::numeric,
            decode(repeat('a0',32),'hex'), decode(repeat('a1',32),'hex'),
            decode(repeat('a2',32),'hex'), decode(repeat('a3',32),'hex'),
            decode(repeat('a4',32),'hex'), decode(repeat('a5',32),'hex'),
            decode(repeat('a6',32),'hex'), decode(repeat('a7',32),'hex'),
            decode(repeat('a8',32),'hex'), decode(repeat('a9',32),'hex'),
            decode(repeat('aa',32),'hex'), expected.selected_boundary_fingerprint,
            64::numeric, 1::numeric, 1::numeric, 1::numeric, 1::numeric,
            2::numeric, 3::numeric, 2::numeric, 0::numeric, 0::numeric,
            2::numeric, 0::numeric, 0::numeric, 0::numeric, 0::numeric,
            0::numeric, 1::numeric,
            1::numeric, 0::numeric, 0::numeric, 0::numeric, 0::numeric,
            0::numeric, 0::numeric, 0::numeric, 0::numeric, 0::numeric,
            1::numeric, 0::numeric, 64256::numeric, 1, 0
        )::laplace.source_profile_manifest),
        (ROW(
            expected.profile_b_id, 17,
            decode('1112131415161718191a1b1c1d1e1f20','hex'),
            decode('3132333435363738393a3b3c3d3e3f40','hex'),
            decode('5152535455565758595a5b5c5d5e5f60','hex'),
            decode('7172737475767778797a7b7c7d7e7f80','hex'),
            1::numeric,
            decode(repeat('b0',32),'hex'), decode(repeat('b1',32),'hex'),
            decode(repeat('b2',32),'hex'), decode(repeat('b3',32),'hex'),
            decode(repeat('b4',32),'hex'), decode(repeat('b5',32),'hex'),
            decode(repeat('b6',32),'hex'), decode(repeat('b7',32),'hex'),
            decode(repeat('b8',32),'hex'), decode(repeat('b9',32),'hex'),
            decode(repeat('ba',32),'hex'), expected.selected_boundary_fingerprint,
            64::numeric, 1::numeric, 1::numeric, 1::numeric, 1::numeric,
            2::numeric, 3::numeric, 2::numeric, 0::numeric, 0::numeric,
            2::numeric, 0::numeric, 0::numeric, 0::numeric, 0::numeric,
            0::numeric, 1::numeric,
            1::numeric, 0::numeric, 0::numeric, 0::numeric, 0::numeric,
            0::numeric, 0::numeric, 0::numeric, 0::numeric, 0::numeric,
            1::numeric, 0::numeric, 64256::numeric, 1, 0
        )::laplace.source_profile_manifest)
    ) source_profiles(profile);
    SELECT array_agg(value ORDER BY value)
    INTO STRICT expected_ids
    FROM unnest(ARRAY[expected.profile_a_id, expected.profile_b_id]) value;

    result := laplace.source_profile_validate_batch(
        pg_temp.persistence_context(), profiles);
    IF result.profile_ids IS DISTINCT FROM expected_ids
       OR result.source_profile_receipt_id <> expected.receipt_id
       OR result.selected_boundary_fingerprint <>
            expected.selected_boundary_fingerprint
       OR result.input_fingerprint <> expected.input_fingerprint
       OR result.output_fingerprint <> expected.output_fingerprint
       OR result.isa_receipt_id <> expected.isa_receipt_id
       OR result.profile_count <> 2
       OR result.closure_subject_count <> 2
       OR result.persisted_count <> 2
       OR result.negative_count <> 0
       OR result.exact_reconstruction_count <> 2
       OR result.semantic_reconstruction_count <> 0
       OR result.no_reconstruction_count <> 0 THEN
        RAISE EXCEPTION 'PostgreSQL source-profile result differs from native ISA result'
            USING DETAIL = format(
                'actual=%s expected_receipt=%s expected_input=%s expected_output=%s expected_isa=%s',
                result::text,
                encode(expected.receipt_id, 'hex'),
                encode(expected.input_fingerprint, 'hex'),
                encode(expected.output_fingerprint, 'hex'),
                encode(expected.isa_receipt_id, 'hex'));
    END IF;
    IF (SELECT count(*) FROM laplace.source_profile) <> 2
       OR (SELECT count(*) FROM laplace.source_profile_receipt) <> 1
       OR EXISTS (
            SELECT FROM unnest(profiles) input
            WHERE NOT EXISTS (
                SELECT FROM laplace.source_profile durable
                WHERE durable.profile_id = (input).profile_id
                  AND ROW(durable.*) IS NOT DISTINCT FROM ROW((input).*))) THEN
        RAISE EXCEPTION 'source-profile batch did not persist exact manifests atomically';
    END IF;
    SELECT xmin, ctid INTO STRICT receipt_xmin, receipt_ctid
    FROM laplace.source_profile_receipt
    WHERE receipt_id = result.source_profile_receipt_id;
    replay := laplace.source_profile_validate_batch(
        pg_temp.persistence_context(), profiles);
    IF replay IS DISTINCT FROM result OR NOT EXISTS (
        SELECT FROM laplace.source_profile_receipt
        WHERE receipt_id = result.source_profile_receipt_id
          AND xmin = receipt_xmin AND ctid = receipt_ctid) THEN
        RAISE EXCEPTION 'source-profile replay changed its result or immutable receipt';
    END IF;

    BEGIN
        UPDATE laplace.source_profile
        SET license_fingerprint = decode(repeat('ff',32),'hex')
        WHERE profile_id = expected.profile_a_id;
        PERFORM laplace.source_profile_validate_batch(
            pg_temp.persistence_context(), profiles);
        RAISE EXCEPTION 'conflicting durable source profile was accepted';
    EXCEPTION
        WHEN data_corrupted THEN NULL;
    END;
    IF NOT EXISTS (
        SELECT FROM laplace.source_profile
        WHERE profile_id = expected.profile_a_id
          AND license_fingerprint = decode(repeat('a1',32),'hex')) THEN
        RAISE EXCEPTION 'source-profile conflict subtransaction did not restore state';
    END IF;

    SELECT array_agg(profile ORDER BY (profile).profile_id DESC)
    INTO STRICT reversed FROM unnest(profiles) profile;
    SELECT count(*) INTO profile_before FROM laplace.source_profile;
    SELECT count(*) INTO receipt_before FROM laplace.source_profile_receipt;
    BEGIN
        PERFORM laplace.source_profile_validate_batch(
            pg_temp.persistence_context(), reversed);
        RAISE EXCEPTION 'unsorted source-profile batch was accepted';
    EXCEPTION
        WHEN data_exception THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.source_profile) <> profile_before
       OR (SELECT count(*) FROM laplace.source_profile_receipt) <> receipt_before THEN
        RAISE EXCEPTION 'rejected source-profile batch published partial state';
    END IF;
END
$source_profile$;

CREATE FUNCTION pg_temp.run_reference_mapping_contract()
RETURNS void
LANGUAGE plpgsql
AS $reference_mapping$
DECLARE
    expected reference_mapping_expected%ROWTYPE;
    profiles source_profile_expected%ROWTYPE;
    keys laplace.highway_key[] := ARRAY[
        ROW(
            7,
            decode('1112131415161718191a1b1c1d1e1f20', 'hex'),
            decode('2122232425262728292a2b2c2d2e2f30', 'hex'),
            decode('3132333435363738393a3b3c3d3e3f40', 'hex'),
            decode('4142434445464748494a4b4c4d4e4f50', 'hex'),
            1)::laplace.highway_key,
        ROW(
            7,
            decode('12131415161718191a1b1c1d1e1f2021', 'hex'),
            decode('22232425262728292a2b2c2d2e2f3031', 'hex'),
            decode('32333435363738393a3b3c3d3e3f4041', 'hex'),
            decode('42434445464748494a4b4c4d4e4f5051', 'hex'),
            1)::laplace.highway_key,
        ROW(
            7,
            decode('131415161718191a1b1c1d1e1f202122', 'hex'),
            decode('232425262728292a2b2c2d2e2f303132', 'hex'),
            decode('333435363738393a3b3c3d3e3f404142', 'hex'),
            decode('434445464748494a4b4c4d4e4f505152', 'hex'),
            1)::laplace.highway_key];
    coordinates laplace.highway_batch_result;
    candidates laplace.reference_mapping_candidate[];
    invalid_candidates laplace.reference_mapping_candidate[];
    invalid_candidate laplace.reference_mapping_candidate;
    rollback_candidates laplace.reference_mapping_candidate[];
    bulk_candidates laplace.reference_mapping_candidate[];
    bulk_result laplace.reference_mapping_result;
    result laplace.reference_mapping_result;
    replay laplace.reference_mapping_result;
    receipt_xmin xid;
    receipt_ctid tid;
    mapping_before bigint;
    proposition_before bigint;
    receipt_before bigint;
    execution_before bigint;
BEGIN
    SELECT * INTO STRICT expected FROM reference_mapping_expected;
    SELECT * INTO STRICT profiles FROM source_profile_expected;
    coordinates := laplace.highway_coordinate_calculate_batch(
        pg_temp.persistence_context(), keys);

    INSERT INTO laplace.entity(entity_id, identity_witness)
    SELECT DISTINCT entity_id,
           entity_id || decode(repeat('00', 16), 'hex')
    FROM unnest(ARRAY[
        decode(repeat('50',16),'hex'), decode(repeat('51',16),'hex'),
        decode(repeat('60',16),'hex'), decode(repeat('61',16),'hex'),
        decode(repeat('62',16),'hex'), decode(repeat('63',16),'hex'),
        decode(repeat('70',16),'hex'), decode(repeat('90',16),'hex'),
        decode(repeat('80',16),'hex'), decode(repeat('81',16),'hex'),
        decode(repeat('82',16),'hex'), decode(repeat('83',16),'hex'),
        decode(repeat('a0',16),'hex'), decode(repeat('a1',16),'hex'),
        decode(repeat('a2',16),'hex'), decode(repeat('a3',16),'hex')
    ]) entity(entity_id)
    ON CONFLICT DO NOTHING;

    INSERT INTO laplace.reference_coordinate(
        kind, authority, release, namespace, local_identifier,
        version, coordinate, collision_fingerprint)
    SELECT
        (keys[ordinal]).kind,
        (keys[ordinal]).authority,
        (keys[ordinal]).release,
        (keys[ordinal]).namespace,
        (keys[ordinal]).local_identifier,
        (keys[ordinal]).version,
        coordinates.coordinates[ordinal],
        coordinates.collision_fingerprints[ordinal]
    FROM generate_subscripts(keys, 1) ordinal;

    INSERT INTO laplace.reference_occurrence(
        reference_id, occurrence_id, source_profile_id, coordinate,
        row_entity_id, field_entity_id, value_entity_id,
        source_ordinal, artifact_ordinal, row_ordinal, column_ordinal,
        rule_flags, disposition)
    VALUES
        (decode(repeat('31',32),'hex'), decode(repeat('21',32),'hex'),
         profiles.profile_a_id, coordinates.coordinates[1],
         decode(repeat('60',16),'hex'), decode(repeat('70',16),'hex'),
         decode(repeat('80',16),'hex'), 1, 1, 1, 1, 1, 1),
        (decode(repeat('41',32),'hex'), decode(repeat('22',32),'hex'),
         profiles.profile_a_id, coordinates.coordinates[2],
         decode(repeat('60',16),'hex'), decode(repeat('90',16),'hex'),
         decode(repeat('a0',16),'hex'), 1, 1, 1, 2, 1, 1),
        (decode(repeat('32',32),'hex'), decode(repeat('23',32),'hex'),
         profiles.profile_b_id, coordinates.coordinates[1],
         decode(repeat('61',16),'hex'), decode(repeat('70',16),'hex'),
         decode(repeat('81',16),'hex'), 1, 1, 1, 1, 1, 1),
        (decode(repeat('42',32),'hex'), decode(repeat('24',32),'hex'),
         profiles.profile_b_id, coordinates.coordinates[2],
         decode(repeat('61',16),'hex'), decode(repeat('90',16),'hex'),
         decode(repeat('a1',16),'hex'), 1, 1, 1, 2, 1, 1),
        (decode(repeat('33',32),'hex'), decode(repeat('25',32),'hex'),
         profiles.profile_a_id, coordinates.coordinates[1],
         decode(repeat('62',16),'hex'), decode(repeat('70',16),'hex'),
         decode(repeat('82',16),'hex'), 2, 1, 2, 1, 1, 1),
        (decode(repeat('43',32),'hex'), decode(repeat('26',32),'hex'),
         profiles.profile_a_id, coordinates.coordinates[3],
         decode(repeat('62',16),'hex'), decode(repeat('90',16),'hex'),
         decode(repeat('a2',16),'hex'), 2, 1, 2, 2, 1, 6),
        (decode(repeat('34',32),'hex'), decode(repeat('27',32),'hex'),
         profiles.profile_a_id, coordinates.coordinates[1],
         decode(repeat('63',16),'hex'), decode(repeat('70',16),'hex'),
         decode(repeat('83',16),'hex'), 3, 1, 3, 1, 1, 1),
        (decode(repeat('44',32),'hex'), decode(repeat('28',32),'hex'),
         profiles.profile_a_id, coordinates.coordinates[2],
         decode(repeat('63',16),'hex'), decode(repeat('90',16),'hex'),
         decode(repeat('a3',16),'hex'), 3, 1, 3, 2, 1, 1);

    candidates := ARRAY[
        ROW(
            expected.boundary_id, profiles.profile_a_id,
            decode(repeat('31',32),'hex'), decode(repeat('41',32),'hex'),
            coordinates.coordinates[1], coordinates.collision_fingerprints[1], 7, 1,
            coordinates.coordinates[2], coordinates.collision_fingerprints[2], 7, 1,
            decode(repeat('50',16),'hex'), decode(repeat('60',16),'hex'),
            decode(repeat('70',16),'hex'), decode(repeat('80',16),'hex'),
            decode(repeat('90',16),'hex'), decode(repeat('a0',16),'hex'),
            1, 1, 1, 1, 8, 1, 1, 1)::laplace.reference_mapping_candidate,
        ROW(
            expected.boundary_id, profiles.profile_b_id,
            decode(repeat('32',32),'hex'), decode(repeat('42',32),'hex'),
            coordinates.coordinates[1], coordinates.collision_fingerprints[1], 7, 1,
            coordinates.coordinates[2], coordinates.collision_fingerprints[2], 7, 1,
            decode(repeat('50',16),'hex'), decode(repeat('61',16),'hex'),
            decode(repeat('70',16),'hex'), decode(repeat('81',16),'hex'),
            decode(repeat('90',16),'hex'), decode(repeat('a1',16),'hex'),
            1, 1, 1, 1, 8, 1, 1, 1)::laplace.reference_mapping_candidate,
        ROW(
            expected.boundary_id, profiles.profile_a_id,
            decode(repeat('33',32),'hex'), decode(repeat('43',32),'hex'),
            coordinates.coordinates[1], coordinates.collision_fingerprints[1], 7, 1,
            coordinates.coordinates[3], coordinates.collision_fingerprints[3], 7, 1,
            decode(repeat('50',16),'hex'), decode(repeat('62',16),'hex'),
            decode(repeat('70',16),'hex'), decode(repeat('82',16),'hex'),
            decode(repeat('90',16),'hex'), decode(repeat('a2',16),'hex'),
            2, 1, 2, 1, 8, 1, 1, 6)::laplace.reference_mapping_candidate];

    result := laplace.reference_mapping_resolve_batch(
        pg_temp.persistence_context(), candidates, 1048576::numeric);
    IF result.mapping_ids IS DISTINCT FROM expected.mapping_ids
       OR result.proposition_ids IS DISTINCT FROM expected.proposition_ids
       OR result.occurrence_ids IS DISTINCT FROM expected.occurrence_ids
       OR result.dispositions IS DISTINCT FROM ARRAY[1, 1, 3]
       OR result.reference_mapping_receipt_id <> expected.receipt_id
       OR result.boundary_id <> expected.boundary_id
       OR result.input_fingerprint <> expected.input_fingerprint
       OR result.output_fingerprint <> expected.output_fingerprint
       OR result.isa_receipt_id <> expected.isa_receipt_id
       OR result.occurrence_count <> 3
       OR result.proposition_count <> 2
       OR result.resolved_count <> 2
       OR result.unresolved_count <> 1
       OR result.retired_count <> 0
       OR result.persistence_batch_count <> 1
       OR result.maximum_persistence_batch_records <> 3
       OR result.maximum_encoded_persistence_batch_bytes > 1048576
       OR result.minimum_encoded_persistence_record_bytes <= 0 THEN
        RAISE EXCEPTION 'PostgreSQL reference mapping differs from direct native ISA'
            USING DETAIL = result::text;
    END IF;
    IF expected.proposition_ids[1] <> expected.proposition_ids[2]
       OR expected.proposition_ids[1] = expected.proposition_ids[3]
       OR (SELECT count(*) FROM laplace.reference_mapping_proposition) <> 2
       OR (SELECT count(*) FROM laplace.reference_mapping_occurrence) <> 3
       OR (SELECT count(*) FROM laplace.reference_mapping_receipt) <> 1
       OR (SELECT count(*) FROM laplace.reference_mapping_receipt_member) <> 3
       OR NOT EXISTS (
            SELECT FROM laplace.execution_receipt
            WHERE receipt_id = expected.isa_receipt_id) THEN
        RAISE EXCEPTION 'reference mapping did not persist exact folded propositions and witnessed occurrences';
    END IF;

    SELECT xmin, ctid INTO STRICT receipt_xmin, receipt_ctid
    FROM laplace.reference_mapping_receipt
    WHERE receipt_id = result.reference_mapping_receipt_id;
    replay := laplace.reference_mapping_resolve_batch(
        pg_temp.persistence_context(), candidates, 1048576::numeric);
    IF replay IS DISTINCT FROM result OR NOT EXISTS (
        SELECT FROM laplace.reference_mapping_receipt
        WHERE receipt_id = result.reference_mapping_receipt_id
          AND xmin = receipt_xmin AND ctid = receipt_ctid) THEN
        RAISE EXCEPTION 'reference mapping replay changed its result or immutable receipt';
    END IF;

    BEGIN
        UPDATE laplace.reference_mapping_occurrence
        SET left_disposition = 2
        WHERE occurrence_id = expected.occurrence_ids[1];
        PERFORM laplace.reference_mapping_resolve_batch(
            pg_temp.persistence_context(), candidates, 1048576::numeric);
        RAISE EXCEPTION 'conflicting durable reference mapping was accepted';
    EXCEPTION
        WHEN data_corrupted THEN NULL;
    END;
    IF NOT EXISTS (
        SELECT FROM laplace.reference_mapping_occurrence
        WHERE occurrence_id = expected.occurrence_ids[1]
          AND left_disposition = 1) THEN
        RAISE EXCEPTION 'reference mapping conflict subtransaction did not restore state';
    END IF;

    invalid_candidates := candidates;
    invalid_candidate := invalid_candidates[1];
    invalid_candidate.left_coordinate := coordinates.coordinates[2];
    invalid_candidates[1] := invalid_candidate;
    SELECT count(*) INTO execution_before FROM laplace.execution_receipt;
    BEGIN
        PERFORM laplace.reference_mapping_resolve_batch(
            pg_temp.persistence_context(), invalid_candidates, 1048576::numeric);
        RAISE EXCEPTION 'reference mapping accepted a coordinate that conflicts with its durable reference';
    EXCEPTION
        WHEN data_corrupted THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.execution_receipt) <> execution_before THEN
        RAISE EXCEPTION 'rejected reference mapping published an ISA receipt';
    END IF;

    rollback_candidates := ARRAY[
        ROW(
            expected.boundary_id, profiles.profile_a_id,
            decode(repeat('34',32),'hex'), decode(repeat('44',32),'hex'),
            coordinates.coordinates[1], coordinates.collision_fingerprints[1], 7, 1,
            coordinates.coordinates[2], coordinates.collision_fingerprints[2], 7, 1,
            decode(repeat('51',16),'hex'), decode(repeat('63',16),'hex'),
            decode(repeat('70',16),'hex'), decode(repeat('83',16),'hex'),
            decode(repeat('90',16),'hex'), decode(repeat('a3',16),'hex'),
            3, 1, 3, 1, 8, 1, 1, 1)::laplace.reference_mapping_candidate];
    SELECT count(*) INTO mapping_before FROM laplace.reference_mapping_occurrence;
    SELECT count(*) INTO proposition_before FROM laplace.reference_mapping_proposition;
    SELECT count(*) INTO receipt_before FROM laplace.reference_mapping_receipt;
    SELECT count(*) INTO execution_before FROM laplace.execution_receipt;
    BEGIN
        PERFORM laplace.reference_mapping_resolve_batch(
            pg_temp.persistence_context(), rollback_candidates, 1048576::numeric);
        RAISE EXCEPTION 'force reference mapping transaction rollback';
    EXCEPTION
        WHEN raise_exception THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.reference_mapping_occurrence) <> mapping_before
       OR (SELECT count(*) FROM laplace.reference_mapping_proposition) <> proposition_before
       OR (SELECT count(*) FROM laplace.reference_mapping_receipt) <> receipt_before
       OR (SELECT count(*) FROM laplace.execution_receipt) <> execution_before THEN
        RAISE EXCEPTION 'reference mapping rollback published partial durable state';
    END IF;

    WITH generated AS (
        SELECT candidate_ordinal,
               int8send(candidate_ordinal) || int8send(1) AS left_coordinate,
               int8send(candidate_ordinal) || int8send(2) AS right_coordinate,
               decode(repeat('d1', 16), 'hex') ||
                   int8send(candidate_ordinal) || int8send(1) AS left_reference_id,
               decode(repeat('d1', 16), 'hex') ||
                   int8send(candidate_ordinal) || int8send(2) AS right_reference_id
        FROM generate_series(1, 4096) candidate_ordinal
    ), coordinates_to_insert AS (
        SELECT 7 AS kind,
               decode(repeat('b1', 16), 'hex') AS authority,
               decode(repeat('b2', 16), 'hex') AS release,
               decode(repeat('b3', 16), 'hex') AS namespace,
               coordinate AS local_identifier,
               1::numeric AS version,
               coordinate,
               coordinate || coordinate AS collision_fingerprint
        FROM generated
        CROSS JOIN LATERAL unnest(
            ARRAY[left_coordinate, right_coordinate]) coordinate
    )
    INSERT INTO laplace.reference_coordinate(
        kind, authority, release, namespace, local_identifier,
        version, coordinate, collision_fingerprint)
    SELECT * FROM coordinates_to_insert;

    WITH generated AS (
        SELECT candidate_ordinal,
               side,
               int8send(candidate_ordinal) || int8send(side) AS coordinate,
               decode(repeat('d1', 16), 'hex') ||
                   int8send(candidate_ordinal) || int8send(side) AS reference_id,
               decode(repeat('d2', 16), 'hex') ||
                   int8send(candidate_ordinal) || int8send(side) AS occurrence_id
        FROM generate_series(1, 4096) candidate_ordinal
        CROSS JOIN generate_series(1, 2) side
    )
    INSERT INTO laplace.reference_occurrence(
        reference_id, occurrence_id, source_profile_id, coordinate,
        row_entity_id, field_entity_id, value_entity_id,
        source_ordinal, artifact_ordinal, row_ordinal, column_ordinal,
        rule_flags, disposition)
    SELECT reference_id, occurrence_id, profiles.profile_a_id, coordinate,
           decode(repeat('60', 16), 'hex'),
           CASE side WHEN 1 THEN decode(repeat('70', 16), 'hex')
                     ELSE decode(repeat('90', 16), 'hex') END,
           CASE side WHEN 1 THEN decode(repeat('80', 16), 'hex')
                     ELSE decode(repeat('a0', 16), 'hex') END,
           candidate_ordinal + 100, 1, candidate_ordinal + 100, side,
           1, 1
    FROM generated;

    WITH generated AS (
        SELECT candidate_ordinal,
               int8send(candidate_ordinal) || int8send(1) AS left_coordinate,
               int8send(candidate_ordinal) || int8send(2) AS right_coordinate,
               decode(repeat('d1', 16), 'hex') ||
                   int8send(candidate_ordinal) || int8send(1) AS left_reference_id,
               decode(repeat('d1', 16), 'hex') ||
                   int8send(candidate_ordinal) || int8send(2) AS right_reference_id
        FROM generate_series(1, 4096) candidate_ordinal
    )
    SELECT array_agg(
        ROW(
            expected.boundary_id, profiles.profile_a_id,
            left_reference_id, right_reference_id,
            left_coordinate, left_coordinate || left_coordinate, 7, 1,
            right_coordinate, right_coordinate || right_coordinate, 7, 1,
            decode(repeat('50', 16), 'hex'),
            decode(repeat('60', 16), 'hex'),
            decode(repeat('70', 16), 'hex'),
            decode(repeat('80', 16), 'hex'),
            decode(repeat('90', 16), 'hex'),
            decode(repeat('a0', 16), 'hex'),
            candidate_ordinal + 100, 1, candidate_ordinal + 100,
            1, 8, 1, 1, 1
        )::laplace.reference_mapping_candidate
        ORDER BY candidate_ordinal)
    INTO STRICT bulk_candidates
    FROM generated;

    bulk_result := laplace.reference_mapping_resolve_batch(
        pg_temp.persistence_context(), bulk_candidates, 1048576::numeric);
    IF bulk_result.occurrence_count <> 4096
       OR bulk_result.proposition_count <> 4096
       OR bulk_result.resolved_count <> 4096
       OR bulk_result.unresolved_count <> 0
       OR bulk_result.retired_count <> 0
       OR bulk_result.persistence_batch_count <= 1
       OR bulk_result.maximum_persistence_batch_records >= 4096
       OR bulk_result.maximum_encoded_persistence_batch_bytes > 1048576
       OR 1048576 - bulk_result.maximum_encoded_persistence_batch_bytes >=
          bulk_result.minimum_encoded_persistence_record_bytes THEN
        RAISE EXCEPTION 'reference mapping byte-derived persistence changed exact bulk semantics'
            USING DETAIL = bulk_result::text;
    END IF;
END
$reference_mapping$;

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
       OR result.isa_minor <> @LAPLACE_ISA_MINOR@
       OR result.status <> 0
       OR result.item_count <> 3 THEN
        RAISE EXCEPTION USING MESSAGE = format(
            'SPI identity receipt differs from native: receipt=%s/%s context=%s/%s program=%s/%s input=%s/%s output=%s/%s instructions=%s/%s isa=%s.%s status=%s items=%s',
            encode(result.receipt_id, 'hex'), encode(expected.identity_receipt, 'hex'),
            encode(result.context_fingerprint, 'hex'), encode(expected.context_fingerprint, 'hex'),
            encode(result.program_fingerprint, 'hex'), encode(expected.identity_program, 'hex'),
            encode(result.input_fingerprint, 'hex'), encode(expected.identity_input, 'hex'),
            encode(result.output_fingerprint, 'hex'), encode(expected.identity_output, 'hex'),
            result.instruction_count, result.executed_instruction_count,
            result.isa_major, result.isa_minor, result.status, result.item_count);
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
    keys laplace.highway_key[] := ARRAY[
        ROW(
            3,
            decode('101112131415161718191a1b1c1d1e1f', 'hex'),
            decode('303132333435363738393a3b3c3d3e3f', 'hex'),
            decode('505152535455565758595a5b5c5d5e5f', 'hex'),
            decode('707172737475767778797a7b7c7d7e7f', 'hex'),
            1
        )::laplace.highway_key,
        ROW(
            16,
            decode('101112131415161718191a1b1c1d1e1f', 'hex'),
            decode('3132333435363738393a3b3c3d3e3f40', 'hex'),
            decode('505152535455565758595a5b5c5d5e5f', 'hex'),
            decode('707172737475767778797a7b7c7d7e7f', 'hex'),
            1
        )::laplace.highway_key
    ];
    calculated laplace.highway_batch_result;
    result laplace.highway_batch_result;
    repeated laplace.highway_batch_result;
    expected highway_expected%ROWTYPE;
    receipt_count bigint;
    before_xmin xid;
    before_ctid tid;
BEGIN
    SELECT * INTO STRICT expected FROM highway_expected;
    SELECT count(*) INTO receipt_count FROM laplace.execution_receipt;
    calculated := laplace.highway_coordinate_calculate_batch(
        pg_temp.execution_context(), keys);
    IF (SELECT count(*) FROM laplace.execution_receipt) <> receipt_count THEN
        RAISE EXCEPTION 'pure highway calculation published durable state';
    END IF;
    result := laplace.highway_coordinate_execute_batch(
        pg_temp.execution_context(), keys);
    IF calculated IS DISTINCT FROM result THEN
        RAISE EXCEPTION 'pure and durable highway routes differ';
    END IF;
    IF result.coordinates IS DISTINCT FROM expected.coordinates
       OR result.collision_fingerprints IS DISTINCT FROM expected.collision_fingerprints
       OR result.receipt_id <> expected.receipt
       OR result.context_fingerprint <> expected.context_fingerprint
       OR result.program_fingerprint <> expected.program_fingerprint
       OR result.input_fingerprint <> expected.input_fingerprint
       OR result.output_fingerprint <> expected.output_fingerprint
       OR result.instruction_count <> 1
       OR result.executed_instruction_count <> 1
       OR result.isa_major <> 1
       OR result.isa_minor <> @LAPLACE_ISA_MINOR@
       OR result.status <> 0
       OR result.item_count <> 2 THEN
        RAISE EXCEPTION 'PostgreSQL highway result differs from direct native ISA';
    END IF;
    IF result.coordinates[1] = result.coordinates[2] THEN
        RAISE EXCEPTION 'release-scoped coordinates collapsed';
    END IF;

    SELECT xmin, ctid INTO before_xmin, before_ctid
    FROM laplace.execution_receipt WHERE receipt_id = result.receipt_id;
    repeated := laplace.highway_coordinate_execute_batch(
        pg_temp.execution_context(), keys);
    IF repeated IS DISTINCT FROM result OR NOT EXISTS (
        SELECT 1 FROM laplace.execution_receipt
        WHERE receipt_id = result.receipt_id
          AND xmin = before_xmin AND ctid = before_ctid
    ) THEN
        RAISE EXCEPTION 'highway replay changed its result or durable receipt';
    END IF;

    SELECT count(*) INTO receipt_count FROM laplace.execution_receipt;
    keys[2].release := decode(repeat('00', 16), 'hex');
    BEGIN
        PERFORM laplace.highway_coordinate_execute_batch(
            pg_temp.execution_context(), keys);
        RAISE EXCEPTION 'zero release scope was accepted';
    EXCEPTION
        WHEN invalid_parameter_value THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.execution_receipt) <> receipt_count THEN
        RAISE EXCEPTION 'invalid highway batch published a receipt';
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
    IF EXISTS (SELECT FROM laplace.entity)
       OR EXISTS (SELECT FROM laplace.physicality)
       OR EXISTS (SELECT FROM laplace.attestation)
       OR EXISTS (SELECT FROM laplace.consensus)
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
    IF EXISTS (SELECT FROM laplace.entity)
       OR EXISTS (SELECT FROM laplace.physicality)
       OR EXISTS (SELECT FROM laplace.attestation)
       OR EXISTS (SELECT FROM laplace.consensus)
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
    IF (SELECT count(*) FROM laplace.entity) <> 2
       OR (SELECT count(*) FROM laplace.physicality) <> 1
       OR (SELECT count(*) FROM laplace.attestation) <> 1
       OR (SELECT count(*) FROM laplace.consensus) <> 0
       OR (SELECT count(*) FROM laplace.canonical_deposit_receipt) <> 1 THEN
        RAISE EXCEPTION 'one canonical stream did not deposit all four record families atomically';
    END IF;
    IF NOT EXISTS (
        SELECT FROM laplace.entity
        WHERE entity_id = expected.entity_a
          AND identity_witness = expected.entity_a_witness
    ) OR NOT EXISTS (
        SELECT FROM laplace.physicality
        WHERE physicality_id = expected.physicality_id
          AND entity_id = expected.entity_a
          AND trajectory_fingerprint = expected.trajectory_fingerprint
          AND octet_length(trajectory) = 96
    ) OR NOT EXISTS (
        SELECT FROM laplace.attestation
        WHERE attestation_id = expected.occurrence_id
          AND entity_id = expected.entity_a
          AND physicality_id = expected.physicality_id
          AND attestation_kind = 1
    ) THEN
        RAISE EXCEPTION 'normalized persistence rows lost native identity or reference fields';
    END IF;
    IF EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = 'laplace'
          AND table_name IN (
              'entity', 'physicality', 'attestation', 'consensus')
          AND column_name = 'canonical_frame'
    ) THEN
        RAISE EXCEPTION 'transport frames were duplicated into normalized durable state';
    END IF;
    IF EXISTS (
        SELECT 1 FROM information_schema.columns
        WHERE table_schema = 'laplace'
          AND table_name = 'entity'
          AND column_name IN ('tier', 'source', 'role', 'language', 'modality')
    ) THEN
        RAISE EXCEPTION 'canonical identity was coupled to source, use or structural altitude';
    END IF;

    SELECT physicality_id INTO STRICT candidate_physicality
    FROM laplace.physicality
    WHERE physicality_id = expected.physicality_id;
    SELECT array_agg(substring(trajectory FROM segment_offset + 1 FOR 32)
                     ORDER BY segment_offset)
    INTO STRICT carriers
    FROM laplace.physicality
    CROSS JOIN LATERAL generate_series(
        0, octet_length(trajectory) - 32, 32) AS segment(segment_offset)
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
    SELECT count(*) INTO table_count FROM laplace.entity;
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
        SELECT FROM laplace.attestation
        WHERE attestation_id = expected.bulk_occurrence_id
          AND physicality_id = expected.bulk_physicality_id
    ) THEN
        RAISE EXCEPTION 'bulk physicality or occurrence did not close atomically';
    END IF;

    SELECT max(pg_column_size(physicality)), sum(octet_length(trajectory))
    INTO STRICT maximum_vertex_bytes, carrier_storage_bytes
    FROM laplace.physicality;
    IF carrier_storage_bytes <> 32 * (
        SELECT sum(vertex_count) FROM laplace.physicality) THEN
        RAISE EXCEPTION 'physicality did not retain exactly one packed trajectory carrier stream';
    END IF;
END
$contract$;

SELECT pg_temp.run_evidence_contract();
SELECT pg_temp.run_testimony_contract();
SELECT pg_temp.run_source_profile_contract();
SELECT pg_temp.run_reference_mapping_contract();

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
        SELECT FROM laplace.entity
        WHERE entity_id = expected.entity_id
          AND identity_witness = expected.zero_digest
    ) OR NOT EXISTS (
        SELECT FROM laplace.physicality
        WHERE physicality_id = expected.physicality_id
          AND entity_id = expected.entity_id
          AND recipe_fingerprint = expected.zero_digest
          AND geometry_epoch = expected.zero_digest
    ) OR NOT EXISTS (
        SELECT FROM laplace.physicality
        WHERE physicality_id = expected.physicality_id
          AND trajectory = substring(expected.frames[3] FROM 49 FOR 32)
    ) OR NOT EXISTS (
        SELECT FROM laplace.attestation
        WHERE attestation_id = expected.occurrence_id
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
