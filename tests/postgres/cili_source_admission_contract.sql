\if :{?source_skip_unicode}
\else
\ir unicode_root_contract.sql
\endif

CREATE TEMP TABLE cili_artifact_bytes (
    ordinal integer PRIMARY KEY,
    content bytea NOT NULL
);

INSERT INTO cili_artifact_bytes VALUES
    (0, pg_read_binary_file(
        :'cili_source_root' || '/' ||
        convert_from(decode(:'cili_artifact_0_local_path', 'hex'), 'UTF8'))),
    (1, pg_read_binary_file(
        :'cili_source_root' || '/' ||
        convert_from(decode(:'cili_artifact_1_local_path', 'hex'), 'UTF8'))),
    (2, pg_read_binary_file(
        :'cili_source_root' || '/' ||
        convert_from(decode(:'cili_artifact_2_local_path', 'hex'), 'UTF8')));

CREATE TEMP TABLE cili_profile_authority (
    kind integer NOT NULL,
    authority_id bytea NOT NULL,
    release_id bytea NOT NULL,
    namespace_id bytea NOT NULL,
    local_identifier_id bytea NOT NULL,
    version numeric NOT NULL,
    authority_release_fingerprint bytea NOT NULL,
    license_fingerprint bytea NOT NULL,
    artifact_graph_fingerprint bytea NOT NULL,
    syntax_authority_fingerprint bytea NOT NULL,
    recipe_program_fingerprint bytea NOT NULL,
    universal_ast_mapping_fingerprint bytea NOT NULL,
    highway_references_fingerprint bytea NOT NULL,
    epistemic_witnessing_fingerprint bytea NOT NULL,
    denominator_declaration_fingerprint bytea NOT NULL,
    conformance_fingerprint bytea NOT NULL,
    completion_law_fingerprint bytea NOT NULL,
    selected_boundary_fingerprint bytea NOT NULL,
    occurrence_context_fingerprint bytea NOT NULL,
    preferred_batch_bytes numeric NOT NULL,
    reconstruction_class integer NOT NULL,
    source_flags integer NOT NULL
);

INSERT INTO cili_profile_authority VALUES (
    :'cili_kind'::integer,
    decode(:'cili_authority_id', 'hex'),
    decode(:'cili_release_id', 'hex'),
    decode(:'cili_namespace_id', 'hex'),
    decode(:'cili_local_identifier_id', 'hex'),
    :'cili_version'::numeric,
    decode(:'cili_authority_release_fingerprint', 'hex'),
    decode(:'cili_license_fingerprint', 'hex'),
    decode(:'cili_artifact_graph_fingerprint', 'hex'),
    decode(:'cili_syntax_authority_fingerprint', 'hex'),
    decode(:'cili_recipe_program_fingerprint', 'hex'),
    decode(:'cili_universal_ast_mapping_fingerprint', 'hex'),
    decode(:'cili_highway_references_fingerprint', 'hex'),
    decode(:'cili_epistemic_witnessing_fingerprint', 'hex'),
    decode(:'cili_denominator_declaration_fingerprint', 'hex'),
    decode(:'cili_conformance_fingerprint', 'hex'),
    decode(:'cili_completion_law_fingerprint', 'hex'),
    decode(:'cili_selected_boundary_fingerprint', 'hex'),
    decode(:'cili_occurrence_context_fingerprint', 'hex'),
    :'cili_preferred_batch_bytes'::numeric,
    :'cili_reconstruction_class'::integer,
    :'cili_source_flags'::integer
);

CREATE TEMP TABLE cili_expected (
    artifact_count numeric NOT NULL,
    byte_count numeric NOT NULL,
    record_count numeric NOT NULL,
    field_count numeric NOT NULL,
    reference_count numeric NOT NULL,
    reference_coordinate_count numeric NOT NULL,
    claim_count numeric NOT NULL,
    mapping_count numeric NOT NULL,
    request_count numeric NOT NULL
);

INSERT INTO cili_expected VALUES (
    :'cili_artifact_count'::numeric,
    :'cili_expected_bytes'::numeric,
    :'cili_expected_records'::numeric,
    :'cili_expected_fields'::numeric,
    :'cili_expected_references'::numeric,
    :'cili_expected_reference_coordinates'::numeric,
    :'cili_expected_claims'::numeric,
    :'cili_expected_mappings'::numeric,
    :'cili_expected_requests'::numeric
);

CREATE TEMP TABLE cili_reference_rule_authority (
    ordinal integer PRIMARY KEY,
    artifact_index numeric NOT NULL,
    column_index numeric NOT NULL,
    namespace_id bytea NOT NULL,
    kind integer NOT NULL,
    flags integer NOT NULL
);

INSERT INTO cili_reference_rule_authority VALUES
    (0, :'cili_reference_rule_0_artifact'::numeric,
        :'cili_reference_rule_0_column'::numeric,
        decode(:'cili_reference_rule_0_namespace', 'hex'),
        :'cili_reference_rule_0_kind'::integer,
        :'cili_reference_rule_0_flags'::integer),
    (1, :'cili_reference_rule_1_artifact'::numeric,
        :'cili_reference_rule_1_column'::numeric,
        decode(:'cili_reference_rule_1_namespace', 'hex'),
        :'cili_reference_rule_1_kind'::integer,
        :'cili_reference_rule_1_flags'::integer),
    (2, :'cili_reference_rule_2_artifact'::numeric,
        :'cili_reference_rule_2_column'::numeric,
        decode(:'cili_reference_rule_2_namespace', 'hex'),
        :'cili_reference_rule_2_kind'::integer,
        :'cili_reference_rule_2_flags'::integer),
    (3, :'cili_reference_rule_3_artifact'::numeric,
        :'cili_reference_rule_3_column'::numeric,
        decode(:'cili_reference_rule_3_namespace', 'hex'),
        :'cili_reference_rule_3_kind'::integer,
        :'cili_reference_rule_3_flags'::integer);

CREATE FUNCTION pg_temp.cili_reference_rules()
RETURNS laplace.tabular_reference_rule[]
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $rules$
    SELECT array_agg(
        ROW(artifact_index, column_index, namespace_id, kind, flags
        )::laplace.tabular_reference_rule
        ORDER BY ordinal)
    FROM cili_reference_rule_authority
$rules$;

CREATE TEMP TABLE cili_mapping_rule_authority (
    ordinal integer PRIMARY KEY,
    artifact_index numeric NOT NULL,
    left_column_index numeric NOT NULL,
    right_column_index numeric NOT NULL,
    relation_content bytea NOT NULL,
    relation_version numeric NOT NULL,
    relation_kind integer NOT NULL,
    flags integer NOT NULL
);

INSERT INTO cili_mapping_rule_authority VALUES
    (0, :'cili_mapping_rule_0_artifact'::numeric,
        :'cili_mapping_rule_0_left_column'::numeric,
        :'cili_mapping_rule_0_right_column'::numeric,
        decode(:'cili_mapping_rule_0_relation', 'hex'),
        :'cili_mapping_rule_0_version'::numeric,
        :'cili_mapping_rule_0_kind'::integer,
        :'cili_mapping_rule_0_flags'::integer),
    (1, :'cili_mapping_rule_1_artifact'::numeric,
        :'cili_mapping_rule_1_left_column'::numeric,
        :'cili_mapping_rule_1_right_column'::numeric,
        decode(:'cili_mapping_rule_1_relation', 'hex'),
        :'cili_mapping_rule_1_version'::numeric,
        :'cili_mapping_rule_1_kind'::integer,
        :'cili_mapping_rule_1_flags'::integer);

CREATE FUNCTION pg_temp.cili_mapping_rules()
RETURNS laplace.tabular_mapping_rule[]
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $rules$
    SELECT array_agg(
        ROW(artifact_index, left_column_index, right_column_index,
            relation_content, relation_version, relation_kind, flags
        )::laplace.tabular_mapping_rule
        ORDER BY ordinal)
    FROM cili_mapping_rule_authority
$rules$;

CREATE TEMP TABLE cili_artifact_authority (
    ordinal integer PRIMARY KEY,
    artifact_id bytea NOT NULL,
    parent_artifact_id bytea NOT NULL,
    name bytea NOT NULL,
    media_type bytea NOT NULL,
    expected_record_count numeric NOT NULL,
    expected_field_count numeric NOT NULL,
    reference_column_mask numeric NOT NULL,
    mode integer NOT NULL,
    delimiter integer NOT NULL,
    line_terminator integer NOT NULL,
    expected_column_count integer NOT NULL,
    outcome_type integer NOT NULL,
    flags integer NOT NULL,
    column_names bytea[] NOT NULL,
    header_record_count integer NOT NULL
);

INSERT INTO cili_artifact_authority VALUES
    (0, decode(:'cili_artifact_0_id', 'hex'),
        decode(:'cili_artifact_0_parent_id', 'hex'),
        decode(:'cili_artifact_0_name', 'hex'), decode(:'cili_artifact_0_media_type', 'hex'),
        :'cili_artifact_0_records'::numeric,
        :'cili_artifact_0_fields'::numeric,
        :'cili_artifact_0_reference_mask'::numeric,
        :'cili_artifact_0_mode'::integer,
        :'cili_artifact_0_delimiter'::integer,
        :'cili_artifact_0_terminator'::integer,
        :'cili_artifact_0_columns'::integer,
        :'cili_artifact_0_outcome'::integer,
        :'cili_artifact_0_flags'::integer,
        ARRAY[]::bytea[], :'cili_artifact_0_header_records'::integer),
    (1, decode(:'cili_artifact_1_id', 'hex'),
        decode(:'cili_artifact_1_parent_id', 'hex'),
        decode(:'cili_artifact_1_name', 'hex'), decode(:'cili_artifact_1_media_type', 'hex'),
        :'cili_artifact_1_records'::numeric,
        :'cili_artifact_1_fields'::numeric,
        :'cili_artifact_1_reference_mask'::numeric,
        :'cili_artifact_1_mode'::integer,
        :'cili_artifact_1_delimiter'::integer,
        :'cili_artifact_1_terminator'::integer,
        :'cili_artifact_1_columns'::integer,
        :'cili_artifact_1_outcome'::integer,
        :'cili_artifact_1_flags'::integer,
        ARRAY[decode(:'cili_artifact_1_column_0', 'hex'),
              decode(:'cili_artifact_1_column_1', 'hex')],
        :'cili_artifact_1_header_records'::integer),
    (2, decode(:'cili_artifact_2_id', 'hex'),
        decode(:'cili_artifact_2_parent_id', 'hex'),
        decode(:'cili_artifact_2_name', 'hex'), decode(:'cili_artifact_2_media_type', 'hex'),
        :'cili_artifact_2_records'::numeric,
        :'cili_artifact_2_fields'::numeric,
        :'cili_artifact_2_reference_mask'::numeric,
        :'cili_artifact_2_mode'::integer,
        :'cili_artifact_2_delimiter'::integer,
        :'cili_artifact_2_terminator'::integer,
        :'cili_artifact_2_columns'::integer,
        :'cili_artifact_2_outcome'::integer,
        :'cili_artifact_2_flags'::integer,
        ARRAY[decode(:'cili_artifact_2_column_0', 'hex'),
              decode(:'cili_artifact_2_column_1', 'hex')],
        :'cili_artifact_2_header_records'::integer);

CREATE FUNCTION pg_temp.cili_source_context()
RETURNS laplace.execution_context
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('30', 32), 'hex'),
            decode(repeat('31', 32), 'hex'),
            decode(repeat('32', 32), 'hex'),
            decode(repeat('33', 32), 'hex'),
            decode(repeat('34', 32), 'hex'),
            decode(repeat('35', 32), 'hex'),
            decode(repeat('36', 32), 'hex'),
            (SELECT epoch_fingerprint
             FROM laplace.perfcache_active_control
             WHERE singleton AND active_present),
            (SELECT activation_epoch_fingerprint
             FROM laplace.highway_registry_active_control
             WHERE singleton AND active_present),
            decode(repeat('39', 32), 'hex')
        ],
        decode(repeat('c5', 32), 'hex'),
        12884901888::bigint,
        6,
        2,
        1023::bigint,
        @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        @LAPLACE_FRAMEWORK_CONTEXT_BOOTSTRAP@::integer
    )::laplace.execution_context
$context$;

CREATE FUNCTION pg_temp.cili_profile_declaration()
RETURNS laplace.source_profile_manifest
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $profile$
    SELECT ROW(
        decode(repeat('00', 32), 'hex'),
        authority.kind,
        authority.authority_id,
        authority.release_id,
        authority.namespace_id,
        authority.local_identifier_id,
        authority.version,
        authority.authority_release_fingerprint,
        authority.license_fingerprint,
        authority.artifact_graph_fingerprint,
        authority.syntax_authority_fingerprint,
        authority.recipe_program_fingerprint,
        authority.universal_ast_mapping_fingerprint,
        authority.highway_references_fingerprint,
        authority.epistemic_witnessing_fingerprint,
        authority.denominator_declaration_fingerprint,
        authority.conformance_fingerprint,
        authority.completion_law_fingerprint,
        authority.selected_boundary_fingerprint,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        authority.reconstruction_class,
        authority.source_flags
    )::laplace.source_profile_manifest
    FROM cili_profile_authority AS authority
$profile$;

CREATE FUNCTION pg_temp.cili_source_artifacts(
    corrupt_table boolean DEFAULT false,
    corrupt_parent boolean DEFAULT false)
RETURNS laplace.tabular_source_artifact[]
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $artifacts$
    SELECT array_agg(
        ROW(
            authority.artifact_id,
            CASE WHEN corrupt_parent AND authority.ordinal = 1
                 THEN set_byte(authority.parent_artifact_id, 0, 255)
                 ELSE authority.parent_artifact_id END,
            authority.artifact_id,
            CASE WHEN corrupt_table AND authority.ordinal = 1
                 THEN set_byte(bytes.content, 0, get_byte(bytes.content, 0) # 1)
                 ELSE bytes.content END,
            authority.name,
            authority.media_type,
            authority.expected_record_count,
            authority.expected_field_count,
            authority.reference_column_mask,
            authority.mode,
            authority.delimiter,
            authority.line_terminator,
            authority.expected_column_count,
            authority.outcome_type,
            authority.flags,
            authority.column_names,
            authority.header_record_count
        )::laplace.tabular_source_artifact
        ORDER BY authority.ordinal)
    FROM cili_artifact_authority AS authority
    JOIN cili_artifact_bytes AS bytes USING (ordinal)
$artifacts$;

CREATE FUNCTION pg_temp.admit_cili_source(
    artifacts laplace.tabular_source_artifact[]
        DEFAULT pg_temp.cili_source_artifacts(),
    mappings laplace.tabular_mapping_rule[]
        DEFAULT pg_temp.cili_mapping_rules())
RETURNS laplace.tabular_source_admission_result
LANGUAGE SQL VOLATILE PARALLEL UNSAFE
AS $admit$
    SELECT laplace.source_admit_tabular(
        pg_temp.cili_source_context(),
        pg_temp.cili_profile_declaration(),
        (SELECT geometry_epoch
         FROM laplace.unicode_root_generation
         ORDER BY recorded_at DESC
         LIMIT 1),
        (SELECT occurrence_context_fingerprint FROM cili_profile_authority),
        artifacts,
        pg_temp.cili_reference_rules(),
        mappings,
        (SELECT preferred_batch_bytes FROM cili_profile_authority))
$admit$;

CREATE FUNCTION pg_temp.cili_has_exact_text(source bytea, positions integer[])
RETURNS boolean
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $exact$
    WITH expected AS (
        SELECT array_agg(binding.entity_id::bytea ORDER BY value.ordinality) AS ids
        FROM unnest(positions) WITH ORDINALITY AS value(codepoint, ordinality)
        CROSS JOIN LATERAL (
            SELECT root_receipt
            FROM laplace.unicode_root_generation
            ORDER BY recorded_at DESC
            LIMIT 1) AS root
        JOIN laplace.attestation AS binding
          ON binding.source_fingerprint = root.root_receipt
         AND binding.attestation_kind = 3
         AND binding.source_ordinal = value.codepoint + 1
    ), candidate_physicalities AS (
        SELECT DISTINCT attestation.physicality_id
        FROM laplace.attestation AS attestation
        JOIN laplace.physicality AS physicality
          ON physicality.physicality_id = attestation.physicality_id
        WHERE attestation.source_fingerprint = source
          AND attestation.attestation_kind = 1
          AND physicality.logical_count = cardinality(positions)
    ), candidates AS (
        SELECT candidate.physicality_id,
               array_agg((occurrence).entity_id::bytea
                         ORDER BY (occurrence).logical_ordinal) AS ids
        FROM candidate_physicalities AS candidate
        JOIN laplace.physicality AS physicality
          ON physicality.physicality_id = candidate.physicality_id
        CROSS JOIN LATERAL unnest((
            laplace.trajectory_composition_decode_calculate_batch(
                pg_temp.cili_source_context(),
                ARRAY(
                    SELECT substring(
                        physicality.trajectory FROM segment_offset + 1 FOR 32)
                    FROM generate_series(
                        0, octet_length(physicality.trajectory) - 32, 32)
                        AS segment(segment_offset)
                    ORDER BY segment_offset))).occurrences) AS occurrence
        GROUP BY candidate.physicality_id
    )
    SELECT EXISTS (
        SELECT 1 FROM candidates, expected
        WHERE candidates.ids = expected.ids)
$exact$;

CREATE TEMP TABLE cili_before AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.reference_coordinate) AS reference_coordinate_count,
    (SELECT count(*) FROM laplace.reference_occurrence) AS reference_occurrence_count,
    (SELECT count(*) FROM laplace.reference_mapping_proposition) AS mapping_proposition_count,
    (SELECT count(*) FROM laplace.reference_mapping_occurrence) AS mapping_occurrence_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE cili_first AS
WITH admission AS MATERIALIZED (
    SELECT pg_temp.admit_cili_source() AS result
)
SELECT (result).* FROM admission;

DO $contract$
DECLARE
    admitted cili_first%ROWTYPE;
    expected cili_expected%ROWTYPE;
    profile laplace.source_profile%ROWTYPE;
    world laplace.world_admission%ROWTYPE;
BEGIN
    SELECT * INTO STRICT admitted FROM cili_first;
    SELECT * INTO STRICT expected FROM cili_expected;
    SELECT * INTO STRICT profile FROM laplace.source_profile
    WHERE profile_id = admitted.profile_id;
    SELECT * INTO STRICT world FROM laplace.world_admission
    WHERE admission_id = admitted.world_admission_id;
    IF admitted.status <> 0
       OR admitted.artifact_count <> expected.artifact_count
       OR admitted.claim_count <> expected.claim_count
       OR admitted.request_count <> expected.request_count
       OR admitted.evidence_node_count <> expected.claim_count
       OR admitted.testimony_count <> expected.claim_count
       OR admitted.reference_occurrence_count <> expected.reference_count
       OR admitted.reference_coordinate_count <>
          expected.reference_coordinate_count
       OR admitted.reference_present_count <> 0
       OR admitted.reference_retired_count <> 0
       OR admitted.reference_unresolved_count <> expected.reference_count
       OR admitted.reference_persistence_batch_count <= 0
       OR admitted.reference_maximum_persistence_batch_records <= 0
       OR admitted.reference_maximum_encoded_persistence_batch_bytes >
          (SELECT preferred_batch_bytes FROM cili_profile_authority)
       OR admitted.reference_minimum_encoded_persistence_record_bytes <= 0
       OR (admitted.reference_persistence_batch_count > 1
           AND (SELECT preferred_batch_bytes FROM cili_profile_authority) -
               admitted.reference_maximum_encoded_persistence_batch_bytes >=
               admitted.reference_minimum_encoded_persistence_record_bytes)
       OR admitted.reference_mapping_occurrence_count <> expected.mapping_count
       OR admitted.reference_mapping_proposition_count <> expected.mapping_count
       OR admitted.reference_mapping_resolved_count <> 0
       OR admitted.reference_mapping_unresolved_count <> expected.mapping_count
       OR admitted.reference_mapping_retired_count <> 0
       OR admitted.reference_mapping_persistence_batch_count <= 0
       OR admitted.reference_mapping_maximum_persistence_batch_records <= 0
       OR admitted.reference_mapping_maximum_encoded_persistence_batch_bytes >
          (SELECT preferred_batch_bytes FROM cili_profile_authority)
       OR admitted.reference_mapping_minimum_encoded_persistence_record_bytes <= 0
       OR (admitted.reference_mapping_persistence_batch_count > 1
           AND (SELECT preferred_batch_bytes FROM cili_profile_authority) -
               admitted.reference_mapping_maximum_encoded_persistence_batch_bytes >=
               admitted.reference_mapping_minimum_encoded_persistence_record_bytes)
       OR profile.byte_count <> expected.byte_count
       OR profile.container_count <> 1
       OR profile.member_count <> 2
       OR profile.file_count <> 3
       OR profile.record_count <> expected.record_count
       OR profile.field_count <> expected.field_count
       OR profile.reference_count <> expected.reference_count
       OR profile.claim_count <> expected.claim_count
       OR profile.mapping_count <> expected.mapping_count
       OR profile.reconstruction_class <> 2
       OR profile.unresolved_count <>
          expected.reference_count + expected.mapping_count
       OR profile.closure_subject_count <>
          admitted.request_count + expected.reference_count + expected.mapping_count
       OR world.profile_claim_count <> expected.claim_count
       OR world.evidence_node_count <> expected.claim_count
       OR world.testimony_count <> expected.claim_count
       OR world.closure_subject_count <> profile.closure_subject_count
       OR world.closed_subject_count <> world.closure_subject_count
       OR NOT EXISTS (
            SELECT 1 FROM laplace.reference_topology_receipt
            WHERE receipt_id = admitted.reference_topology_receipt_id
              AND occurrence_count = expected.reference_count
              AND coordinate_count = expected.reference_coordinate_count
              AND present_count = 0
              AND retired_count = 0
              AND unresolved_count = expected.reference_count)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.reference_mapping_receipt
            WHERE receipt_id = admitted.reference_mapping_receipt_id
              AND occurrence_count = expected.mapping_count
              AND proposition_count = expected.mapping_count
              AND resolved_count = 0
              AND unresolved_count = expected.mapping_count
              AND retired_count = 0)
       OR (SELECT count(*) FROM laplace.reference_mapping_occurrence
           WHERE source_profile_id = admitted.profile_id
             AND left_disposition = 6
             AND right_disposition = 6
             AND disposition = 4) <> expected.mapping_count THEN
        RAISE EXCEPTION 'CILI mapping release did not close the durable route: %', admitted;
    END IF;
    IF NOT pg_temp.cili_has_exact_text(
            admitted.source_fingerprint, ARRAY[105,49])
       OR NOT pg_temp.cili_has_exact_text(
            admitted.source_fingerprint,
            ARRAY[48,48,48,48,49,55,52,48,45,97])
       OR NOT EXISTS (
            SELECT 1 FROM laplace.reference_mapping_occurrence
            WHERE source_profile_id = admitted.profile_id
              AND artifact_ordinal = 2
              AND row_ordinal = 1) THEN
        RAISE EXCEPTION 'first headerless CILI mapping row is not exactly queryable';
    END IF;
    IF EXISTS (
        SELECT 1
        FROM laplace.attestation AS occurrence
        JOIN laplace.physicality AS physicality USING (physicality_id)
        WHERE occurrence.source_fingerprint = admitted.source_fingerprint
          AND occurrence.attestation_kind = 1
          AND physicality.logical_count = 14960316) THEN
        RAISE EXCEPTION 'CILI ZIP wrapper was falsely promoted to canonical content';
    END IF;
END
$contract$;

CREATE TEMP TABLE cili_after_first AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.reference_coordinate) AS reference_coordinate_count,
    (SELECT count(*) FROM laplace.reference_occurrence) AS reference_occurrence_count,
    (SELECT count(*) FROM laplace.reference_mapping_proposition) AS mapping_proposition_count,
    (SELECT count(*) FROM laplace.reference_mapping_occurrence) AS mapping_occurrence_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE cili_replay AS
WITH admission AS MATERIALIZED (
    SELECT pg_temp.admit_cili_source() AS result
)
SELECT (result).* FROM admission;

DO $contract$
DECLARE
    first cili_first%ROWTYPE;
    replay cili_replay%ROWTYPE;
    expected cili_after_first%ROWTYPE;
    actual cili_after_first%ROWTYPE;
BEGIN
    SELECT * INTO STRICT first FROM cili_first;
    SELECT * INTO STRICT replay FROM cili_replay;
    SELECT * INTO STRICT expected FROM cili_after_first;
    SELECT
        (SELECT count(*) FROM laplace.entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1),
        (SELECT count(*) FROM laplace.source_profile),
        (SELECT count(*) FROM laplace.reference_coordinate),
        (SELECT count(*) FROM laplace.reference_occurrence),
        (SELECT count(*) FROM laplace.reference_mapping_proposition),
        (SELECT count(*) FROM laplace.reference_mapping_occurrence),
        (SELECT count(*) FROM laplace.evidence_node),
        (SELECT count(*) FROM laplace.evidence_testimony),
        (SELECT count(*) FROM laplace.world_admission)
    INTO STRICT actual;
    IF (to_jsonb(first) - ARRAY[
            'composition_working_set_receipt_id',
            'composition_presence_receipt_id',
            'composition_producer_receipt_id',
            'composition_stream_receipt_id',
            'durable_stream_record_count',
            'world_admission_id',
            'world_admission_receipt_id',
            'world_admission_isa_receipt_id'
        ]) IS DISTINCT FROM
       (to_jsonb(replay) - ARRAY[
            'composition_working_set_receipt_id',
            'composition_presence_receipt_id',
            'composition_producer_receipt_id',
            'composition_stream_receipt_id',
            'durable_stream_record_count',
            'world_admission_id',
            'world_admission_receipt_id',
            'world_admission_isa_receipt_id'
        ])
       OR first.composition_working_set_receipt_id =
          replay.composition_working_set_receipt_id
       OR first.composition_presence_receipt_id =
          replay.composition_presence_receipt_id
       OR first.composition_producer_receipt_id =
          replay.composition_producer_receipt_id
       OR first.composition_stream_receipt_id =
          replay.composition_stream_receipt_id
       OR first.world_admission_id = replay.world_admission_id
       OR first.world_admission_receipt_id =
          replay.world_admission_receipt_id
       OR expected.entity_count <> actual.entity_count
       OR expected.physicality_count <> actual.physicality_count
       OR expected.occurrence_count <> actual.occurrence_count
       OR expected.profile_count <> actual.profile_count
       OR expected.reference_coordinate_count <>
          actual.reference_coordinate_count
       OR expected.reference_occurrence_count <>
          actual.reference_occurrence_count
       OR expected.mapping_proposition_count <>
          actual.mapping_proposition_count
       OR expected.mapping_occurrence_count <>
          actual.mapping_occurrence_count
       OR expected.evidence_count <> actual.evidence_count
       OR expected.testimony_count <> actual.testimony_count
       OR expected.world_count + 1 <> actual.world_count THEN
        RAISE EXCEPTION
            'CILI replay changed semantics or canonical cardinality instead of recording one distinct physical execution';
    END IF;
END
$contract$;

CREATE TEMP TABLE cili_after_replay AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.reference_coordinate) AS reference_coordinate_count,
    (SELECT count(*) FROM laplace.reference_occurrence) AS reference_occurrence_count,
    (SELECT count(*) FROM laplace.reference_mapping_proposition) AS mapping_proposition_count,
    (SELECT count(*) FROM laplace.reference_mapping_occurrence) AS mapping_occurrence_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

DO $contract$
DECLARE
    expected cili_after_replay%ROWTYPE;
    actual cili_after_replay%ROWTYPE;
    bad_mappings laplace.tabular_mapping_rule[];
BEGIN
    SELECT * INTO STRICT expected FROM cili_after_replay;
    BEGIN
        PERFORM pg_temp.admit_cili_source(
            pg_temp.cili_source_artifacts(true, false));
        RAISE EXCEPTION 'changed CILI table byte was accepted';
    EXCEPTION WHEN data_exception THEN NULL;
    END;
    BEGIN
        PERFORM pg_temp.admit_cili_source(
            pg_temp.cili_source_artifacts(false, true));
        RAISE EXCEPTION 'broken CILI artifact parent was accepted';
    EXCEPTION WHEN data_exception THEN NULL;
    END;
    SELECT array_agg(
        CASE WHEN ordinal = 1 THEN
            ROW(artifact_index, left_column_index,
                right_column_index, convert_to('invented', 'UTF8'),
                relation_version, relation_kind, flags
            )::laplace.tabular_mapping_rule
        ELSE ROW(artifact_index, left_column_index, right_column_index,
                 relation_content, relation_version, relation_kind, flags
             )::laplace.tabular_mapping_rule
        END ORDER BY ordinal)
    INTO STRICT bad_mappings
    FROM unnest(pg_temp.cili_mapping_rules()) WITH ORDINALITY AS source(
        artifact_index, left_column_index, right_column_index,
        relation_content, relation_version, relation_kind, flags, ordinal);
    BEGIN
        PERFORM pg_temp.admit_cili_source(
            pg_temp.cili_source_artifacts(), bad_mappings);
        RAISE EXCEPTION 'changed CILI mapping relation was accepted';
    EXCEPTION WHEN data_exception THEN NULL;
    END;
    SELECT
        (SELECT count(*) FROM laplace.entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1),
        (SELECT count(*) FROM laplace.source_profile),
        (SELECT count(*) FROM laplace.reference_coordinate),
        (SELECT count(*) FROM laplace.reference_occurrence),
        (SELECT count(*) FROM laplace.reference_mapping_proposition),
        (SELECT count(*) FROM laplace.reference_mapping_occurrence),
        (SELECT count(*) FROM laplace.evidence_node),
        (SELECT count(*) FROM laplace.evidence_testimony),
        (SELECT count(*) FROM laplace.world_admission)
    INTO STRICT actual;
    IF expected IS DISTINCT FROM actual THEN
        RAISE EXCEPTION 'rejected CILI mutation published partial state';
    END IF;
END
$contract$;

EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT)
SELECT mapping.proposition_id, mapping.row_ordinal,
       left_reference.disposition, right_reference.disposition
FROM cili_first AS admitted
JOIN laplace.reference_mapping_occurrence AS mapping
  ON mapping.source_profile_id = admitted.profile_id
JOIN laplace.reference_occurrence AS left_reference
  ON left_reference.reference_id = mapping.left_reference_id
JOIN laplace.reference_occurrence AS right_reference
  ON right_reference.reference_id = mapping.right_reference_id
ORDER BY mapping.source_ordinal
LIMIT 32;
