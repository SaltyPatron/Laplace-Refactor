\if :{?source_skip_unicode}
\else
\ir unicode_root_contract.sql
\endif

CREATE TEMP TABLE iso_artifact_bytes (
    ordinal integer PRIMARY KEY,
    content bytea NOT NULL
);

INSERT INTO iso_artifact_bytes VALUES
    (0, pg_read_binary_file(
        :'iso_source_root' || '/' ||
        convert_from(decode(:'iso_artifact_0_local_path', 'hex'), 'UTF8'))),
    (1, pg_read_binary_file(
        :'iso_source_root' || '/' ||
        convert_from(decode(:'iso_artifact_1_local_path', 'hex'), 'UTF8'))),
    (2, pg_read_binary_file(
        :'iso_source_root' || '/' ||
        convert_from(decode(:'iso_artifact_2_local_path', 'hex'), 'UTF8'))),
    (3, pg_read_binary_file(
        :'iso_source_root' || '/' ||
        convert_from(decode(:'iso_artifact_3_local_path', 'hex'), 'UTF8'))),
    (4, pg_read_binary_file(
        :'iso_source_root' || '/' ||
        convert_from(decode(:'iso_artifact_4_local_path', 'hex'), 'UTF8')));

CREATE TEMP TABLE iso_profile_authority (
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

INSERT INTO iso_profile_authority VALUES (
    :'iso_kind'::integer,
    decode(:'iso_authority_id', 'hex'),
    decode(:'iso_release_id', 'hex'),
    decode(:'iso_namespace_id', 'hex'),
    decode(:'iso_local_identifier_id', 'hex'),
    :'iso_version'::numeric,
    decode(:'iso_authority_release_fingerprint', 'hex'),
    decode(:'iso_license_fingerprint', 'hex'),
    decode(:'iso_artifact_graph_fingerprint', 'hex'),
    decode(:'iso_syntax_authority_fingerprint', 'hex'),
    decode(:'iso_recipe_program_fingerprint', 'hex'),
    decode(:'iso_universal_ast_mapping_fingerprint', 'hex'),
    decode(:'iso_highway_references_fingerprint', 'hex'),
    decode(:'iso_epistemic_witnessing_fingerprint', 'hex'),
    decode(:'iso_denominator_declaration_fingerprint', 'hex'),
    decode(:'iso_conformance_fingerprint', 'hex'),
    decode(:'iso_completion_law_fingerprint', 'hex'),
    decode(:'iso_selected_boundary_fingerprint', 'hex'),
    decode(:'iso_occurrence_context_fingerprint', 'hex'),
    :'iso_preferred_batch_bytes'::numeric,
    :'iso_reconstruction_class'::integer,
    :'iso_source_flags'::integer
);

CREATE TEMP TABLE iso_expected (
    artifact_count numeric NOT NULL,
    byte_count numeric NOT NULL,
    record_count numeric NOT NULL,
    field_count numeric NOT NULL,
    reference_count numeric NOT NULL,
    reference_coordinate_count numeric NOT NULL,
    claim_count numeric NOT NULL,
    request_count numeric NOT NULL
);

INSERT INTO iso_expected VALUES (
    :'iso_artifact_count'::numeric,
    :'iso_expected_bytes'::numeric,
    :'iso_expected_records'::numeric,
    :'iso_expected_fields'::numeric,
    :'iso_expected_references'::numeric,
    :'iso_expected_reference_coordinates'::numeric,
    :'iso_expected_claims'::numeric,
    :'iso_expected_requests'::numeric
);

CREATE TEMP TABLE iso_reference_rule_authority (
    ordinal integer PRIMARY KEY,
    artifact_index numeric NOT NULL,
    column_index numeric NOT NULL,
    namespace bytea NOT NULL CHECK (octet_length(namespace) = 16),
    kind integer NOT NULL,
    flags integer NOT NULL
);

INSERT INTO iso_reference_rule_authority VALUES
    (0, :'iso_reference_rule_0_artifact'::numeric, :'iso_reference_rule_0_column'::numeric, decode(:'iso_reference_rule_0_namespace', 'hex'), :'iso_reference_rule_0_kind'::integer, :'iso_reference_rule_0_flags'::integer),
    (1, :'iso_reference_rule_1_artifact'::numeric, :'iso_reference_rule_1_column'::numeric, decode(:'iso_reference_rule_1_namespace', 'hex'), :'iso_reference_rule_1_kind'::integer, :'iso_reference_rule_1_flags'::integer),
    (2, :'iso_reference_rule_2_artifact'::numeric, :'iso_reference_rule_2_column'::numeric, decode(:'iso_reference_rule_2_namespace', 'hex'), :'iso_reference_rule_2_kind'::integer, :'iso_reference_rule_2_flags'::integer),
    (3, :'iso_reference_rule_3_artifact'::numeric, :'iso_reference_rule_3_column'::numeric, decode(:'iso_reference_rule_3_namespace', 'hex'), :'iso_reference_rule_3_kind'::integer, :'iso_reference_rule_3_flags'::integer),
    (4, :'iso_reference_rule_4_artifact'::numeric, :'iso_reference_rule_4_column'::numeric, decode(:'iso_reference_rule_4_namespace', 'hex'), :'iso_reference_rule_4_kind'::integer, :'iso_reference_rule_4_flags'::integer),
    (5, :'iso_reference_rule_5_artifact'::numeric, :'iso_reference_rule_5_column'::numeric, decode(:'iso_reference_rule_5_namespace', 'hex'), :'iso_reference_rule_5_kind'::integer, :'iso_reference_rule_5_flags'::integer),
    (6, :'iso_reference_rule_6_artifact'::numeric, :'iso_reference_rule_6_column'::numeric, decode(:'iso_reference_rule_6_namespace', 'hex'), :'iso_reference_rule_6_kind'::integer, :'iso_reference_rule_6_flags'::integer),
    (7, :'iso_reference_rule_7_artifact'::numeric, :'iso_reference_rule_7_column'::numeric, decode(:'iso_reference_rule_7_namespace', 'hex'), :'iso_reference_rule_7_kind'::integer, :'iso_reference_rule_7_flags'::integer),
    (8, :'iso_reference_rule_8_artifact'::numeric, :'iso_reference_rule_8_column'::numeric, decode(:'iso_reference_rule_8_namespace', 'hex'), :'iso_reference_rule_8_kind'::integer, :'iso_reference_rule_8_flags'::integer);

CREATE FUNCTION pg_temp.iso_reference_rules()
RETURNS laplace.tabular_reference_rule[]
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $rules$
    SELECT array_agg(
        ROW(
            artifact_index,
            column_index,
            namespace,
            kind,
            flags
        )::laplace.tabular_reference_rule
        ORDER BY ordinal)
    FROM iso_reference_rule_authority
$rules$;

CREATE TEMP TABLE iso_artifact_authority (
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

INSERT INTO iso_artifact_authority VALUES
    (0, decode(:'iso_artifact_0_id', 'hex'), decode(:'iso_artifact_0_parent_id', 'hex'), decode(:'iso_artifact_0_name', 'hex'), decode(:'iso_artifact_0_media_type', 'hex'), :'iso_artifact_0_records'::numeric, :'iso_artifact_0_fields'::numeric, :'iso_artifact_0_reference_mask'::numeric, :'iso_artifact_0_mode'::integer, :'iso_artifact_0_delimiter'::integer, :'iso_artifact_0_terminator'::integer, :'iso_artifact_0_columns'::integer, :'iso_artifact_0_outcome'::integer, :'iso_artifact_0_flags'::integer, ARRAY[]::bytea[], :'iso_artifact_0_header_records'::integer),
    (1, decode(:'iso_artifact_1_id', 'hex'), decode(:'iso_artifact_1_parent_id', 'hex'), decode(:'iso_artifact_1_name', 'hex'), decode(:'iso_artifact_1_media_type', 'hex'), :'iso_artifact_1_records'::numeric, :'iso_artifact_1_fields'::numeric, :'iso_artifact_1_reference_mask'::numeric, :'iso_artifact_1_mode'::integer, :'iso_artifact_1_delimiter'::integer, :'iso_artifact_1_terminator'::integer, :'iso_artifact_1_columns'::integer, :'iso_artifact_1_outcome'::integer, :'iso_artifact_1_flags'::integer, ARRAY[decode(:'iso_artifact_1_column_0', 'hex'), decode(:'iso_artifact_1_column_1', 'hex'), decode(:'iso_artifact_1_column_2', 'hex')], :'iso_artifact_1_header_records'::integer),
    (2, decode(:'iso_artifact_2_id', 'hex'), decode(:'iso_artifact_2_parent_id', 'hex'), decode(:'iso_artifact_2_name', 'hex'), decode(:'iso_artifact_2_media_type', 'hex'), :'iso_artifact_2_records'::numeric, :'iso_artifact_2_fields'::numeric, :'iso_artifact_2_reference_mask'::numeric, :'iso_artifact_2_mode'::integer, :'iso_artifact_2_delimiter'::integer, :'iso_artifact_2_terminator'::integer, :'iso_artifact_2_columns'::integer, :'iso_artifact_2_outcome'::integer, :'iso_artifact_2_flags'::integer, ARRAY[decode(:'iso_artifact_2_column_0', 'hex'), decode(:'iso_artifact_2_column_1', 'hex'), decode(:'iso_artifact_2_column_2', 'hex'), decode(:'iso_artifact_2_column_3', 'hex'), decode(:'iso_artifact_2_column_4', 'hex'), decode(:'iso_artifact_2_column_5', 'hex'), decode(:'iso_artifact_2_column_6', 'hex'), decode(:'iso_artifact_2_column_7', 'hex')], :'iso_artifact_2_header_records'::integer),
    (3, decode(:'iso_artifact_3_id', 'hex'), decode(:'iso_artifact_3_parent_id', 'hex'), decode(:'iso_artifact_3_name', 'hex'), decode(:'iso_artifact_3_media_type', 'hex'), :'iso_artifact_3_records'::numeric, :'iso_artifact_3_fields'::numeric, :'iso_artifact_3_reference_mask'::numeric, :'iso_artifact_3_mode'::integer, :'iso_artifact_3_delimiter'::integer, :'iso_artifact_3_terminator'::integer, :'iso_artifact_3_columns'::integer, :'iso_artifact_3_outcome'::integer, :'iso_artifact_3_flags'::integer, ARRAY[decode(:'iso_artifact_3_column_0', 'hex'), decode(:'iso_artifact_3_column_1', 'hex'), decode(:'iso_artifact_3_column_2', 'hex')], :'iso_artifact_3_header_records'::integer),
    (4, decode(:'iso_artifact_4_id', 'hex'), decode(:'iso_artifact_4_parent_id', 'hex'), decode(:'iso_artifact_4_name', 'hex'), decode(:'iso_artifact_4_media_type', 'hex'), :'iso_artifact_4_records'::numeric, :'iso_artifact_4_fields'::numeric, :'iso_artifact_4_reference_mask'::numeric, :'iso_artifact_4_mode'::integer, :'iso_artifact_4_delimiter'::integer, :'iso_artifact_4_terminator'::integer, :'iso_artifact_4_columns'::integer, :'iso_artifact_4_outcome'::integer, :'iso_artifact_4_flags'::integer, ARRAY[decode(:'iso_artifact_4_column_0', 'hex'), decode(:'iso_artifact_4_column_1', 'hex'), decode(:'iso_artifact_4_column_2', 'hex'), decode(:'iso_artifact_4_column_3', 'hex'), decode(:'iso_artifact_4_column_4', 'hex'), decode(:'iso_artifact_4_column_5', 'hex')], :'iso_artifact_4_header_records'::integer);

CREATE FUNCTION pg_temp.iso_source_context()
RETURNS laplace.execution_context
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('20', 32), 'hex'),
            decode(repeat('21', 32), 'hex'),
            decode(repeat('22', 32), 'hex'),
            decode(repeat('23', 32), 'hex'),
            decode(repeat('24', 32), 'hex'),
            decode(repeat('25', 32), 'hex'),
            decode(repeat('26', 32), 'hex'),
            (SELECT epoch_fingerprint
             FROM laplace.perfcache_active_control
             WHERE singleton AND active_present),
            (SELECT activation_epoch_fingerprint
             FROM laplace.highway_registry_active_control
             WHERE singleton AND active_present),
            decode(repeat('29', 32), 'hex')
        ],
        decode(repeat('b5', 32), 'hex'),
        8589934592::bigint,
        6,
        2,
        1023::bigint,
        @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        @LAPLACE_FRAMEWORK_CONTEXT_BOOTSTRAP@::integer
    )::laplace.execution_context
$context$;

CREATE FUNCTION pg_temp.iso_profile_declaration()
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
    FROM iso_profile_authority AS authority
$profile$;

CREATE FUNCTION pg_temp.iso_source_artifacts(
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
            CASE WHEN corrupt_table AND authority.ordinal = 2
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
    FROM iso_artifact_authority AS authority
    JOIN iso_artifact_bytes AS bytes USING (ordinal)
$artifacts$;

CREATE FUNCTION pg_temp.admit_iso_source(
    artifacts laplace.tabular_source_artifact[]
        DEFAULT pg_temp.iso_source_artifacts())
RETURNS laplace.tabular_source_admission_result
LANGUAGE SQL VOLATILE PARALLEL UNSAFE
AS $admit$
    SELECT laplace.source_admit_tabular(
        pg_temp.iso_source_context(),
        pg_temp.iso_profile_declaration(),
        (SELECT geometry_epoch
         FROM laplace.unicode_root_generation
         ORDER BY recorded_at DESC
         LIMIT 1),
        (SELECT occurrence_context_fingerprint FROM iso_profile_authority),
        artifacts,
        pg_temp.iso_reference_rules(),
        ARRAY[]::laplace.tabular_mapping_rule[],
        (SELECT preferred_batch_bytes FROM iso_profile_authority))
$admit$;

CREATE FUNCTION pg_temp.source_has_exact_text(
    source bytea,
    positions integer[])
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
                pg_temp.iso_source_context(),
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
        SELECT 1
        FROM candidates, expected
        WHERE candidates.ids = expected.ids)
$exact$;

CREATE TEMP TABLE iso_before AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.reference_coordinate) AS reference_coordinate_count,
    (SELECT count(*) FROM laplace.reference_occurrence) AS reference_occurrence_count,
    (SELECT count(*) FROM laplace.reference_topology_receipt) AS reference_receipt_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE iso_first AS
WITH admission AS MATERIALIZED (
    SELECT pg_temp.admit_iso_source() AS result
)
SELECT (result).* FROM admission;

DO $contract$
DECLARE
    admitted iso_first%ROWTYPE;
    profile laplace.source_profile%ROWTYPE;
    world laplace.world_admission%ROWTYPE;
    expected iso_expected%ROWTYPE;
BEGIN
    SELECT * INTO STRICT admitted FROM iso_first;
    SELECT * INTO STRICT expected FROM iso_expected;
    SELECT * INTO STRICT profile
    FROM laplace.source_profile WHERE profile_id = admitted.profile_id;
    SELECT * INTO STRICT world
    FROM laplace.world_admission WHERE admission_id = admitted.world_admission_id;
    IF admitted.status <> 0
       OR admitted.artifact_count <> expected.artifact_count
       OR admitted.claim_count <> expected.claim_count
       OR admitted.request_count <> expected.request_count
       OR admitted.evidence_node_count <> expected.claim_count
       OR admitted.testimony_count <> expected.claim_count
       OR admitted.reference_occurrence_count <> expected.reference_count
       OR admitted.reference_coordinate_count <>
          expected.reference_coordinate_count
       OR admitted.reference_present_count <> 18404
       OR admitted.reference_retired_count <> 401
       OR admitted.reference_unresolved_count <> 0
       OR admitted.reference_persistence_batch_count <= 0
       OR admitted.reference_maximum_persistence_batch_records <= 0
       OR admitted.reference_maximum_encoded_persistence_batch_bytes >
          (SELECT preferred_batch_bytes FROM iso_profile_authority)
       OR admitted.reference_minimum_encoded_persistence_record_bytes <= 0
       OR (admitted.reference_persistence_batch_count > 1
           AND (SELECT preferred_batch_bytes FROM iso_profile_authority) -
               admitted.reference_maximum_encoded_persistence_batch_bytes >=
               admitted.reference_minimum_encoded_persistence_record_bytes)
       OR admitted.reference_mapping_persistence_batch_count <> 0
       OR admitted.reference_mapping_maximum_persistence_batch_records <> 0
       OR admitted.reference_mapping_maximum_encoded_persistence_batch_bytes <> 0
       OR admitted.reference_mapping_minimum_encoded_persistence_record_bytes <> 0
       OR profile.byte_count <> expected.byte_count
       OR profile.container_count <> 1
       OR profile.member_count <> 4
       OR profile.file_count <> 5
       OR profile.record_count <> expected.record_count
       OR profile.field_count <> expected.field_count
       OR profile.reference_count <> expected.reference_count
       OR profile.claim_count <> expected.claim_count
       OR profile.mapping_count <> 0
       OR profile.reconstruction_class <> 2
       OR profile.transformation_count <> admitted.request_count
       OR profile.transformed_count <> admitted.request_count
       OR profile.unresolved_count <> expected.reference_count
       OR profile.closure_subject_count <>
          admitted.request_count + expected.reference_count
       OR profile.persisted_count <> 0
       OR world.profile_claim_count <> expected.claim_count
       OR world.evidence_node_count <> expected.claim_count
       OR world.testimony_count <> expected.claim_count
       OR world.closure_subject_count <>
          admitted.request_count + expected.reference_count
       OR world.closed_subject_count <> world.closure_subject_count
       OR NOT EXISTS (
            SELECT 1 FROM laplace.source_profile_receipt
            WHERE receipt_id = admitted.source_profile_receipt_id
              AND negative_count = expected.reference_count
              AND closure_subject_count =
                  admitted.request_count + expected.reference_count)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.reference_topology_receipt
            WHERE receipt_id = admitted.reference_topology_receipt_id
              AND isa_receipt_id = admitted.reference_topology_isa_receipt_id
              AND occurrence_count = expected.reference_count
              AND coordinate_count = expected.reference_coordinate_count
              AND present_count = 18404
              AND retired_count = 401
              AND unresolved_count = 0)
       OR (SELECT count(*) FROM laplace.reference_occurrence
           WHERE source_profile_id = admitted.profile_id
             AND field_entity_id <> value_entity_id) <>
          expected.reference_count
       OR NOT EXISTS (
            SELECT 1 FROM laplace.evidence_lineage_receipt
            WHERE receipt_id = admitted.evidence_lineage_receipt_id)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.evidence_testimony_receipt
            WHERE receipt_id = admitted.evidence_testimony_receipt_id)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.world_admission_receipt
            WHERE receipt_id = admitted.world_admission_receipt_id) THEN
        RAISE EXCEPTION 'ISO 639 release did not close the durable route: %', admitted;
    END IF;
    IF NOT pg_temp.source_has_exact_text(
            admitted.source_fingerprint, ARRAY[106,112,110])
       OR NOT pg_temp.source_has_exact_text(
            admitted.source_fingerprint, ARRAY[74,97,112,97,110,101,115,101])
       OR NOT pg_temp.source_has_exact_text(
            admitted.source_fingerprint, ARRAY[101,110,103])
       OR NOT pg_temp.source_has_exact_text(
            admitted.source_fingerprint, ARRAY[69,110,103,108,105,115,104]) THEN
        RAISE EXCEPTION 'exact ISO identifier or reference-name content is not queryable';
    END IF;
    IF EXISTS (
        SELECT 1
        FROM laplace.attestation AS occurrence
        JOIN laplace.physicality AS physicality
          ON physicality.physicality_id = occurrence.physicality_id
        WHERE occurrence.source_fingerprint = admitted.source_fingerprint
          AND occurrence.attestation_kind = 1
          AND physicality.logical_count = 153962) THEN
        RAISE EXCEPTION 'ZIP wrapper was falsely promoted to exact selected content';
    END IF;
END
$contract$;

CREATE TEMP TABLE iso_after_first AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.reference_coordinate) AS reference_coordinate_count,
    (SELECT count(*) FROM laplace.reference_occurrence) AS reference_occurrence_count,
    (SELECT count(*) FROM laplace.reference_topology_receipt) AS reference_receipt_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count;

CREATE TEMP TABLE iso_replay AS
WITH admission AS MATERIALIZED (
    SELECT pg_temp.admit_iso_source() AS result
)
SELECT (result).* FROM admission;

DO $contract$
DECLARE
    first iso_first%ROWTYPE;
    replay iso_replay%ROWTYPE;
    expected iso_after_first%ROWTYPE;
    actual iso_after_first%ROWTYPE;
BEGIN
    SELECT * INTO STRICT first FROM iso_first;
    SELECT * INTO STRICT replay FROM iso_replay;
    SELECT * INTO STRICT expected FROM iso_after_first;
    SELECT
        (SELECT count(*) FROM laplace.entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1),
        (SELECT count(*) FROM laplace.source_profile),
        (SELECT count(*) FROM laplace.reference_coordinate),
        (SELECT count(*) FROM laplace.reference_occurrence),
        (SELECT count(*) FROM laplace.reference_topology_receipt),
        (SELECT count(*) FROM laplace.evidence_node),
        (SELECT count(*) FROM laplace.evidence_testimony)
    INTO STRICT actual;
    IF first.profile_id <> replay.profile_id
       OR first.source_fingerprint <> replay.source_fingerprint
       OR first.reconstruction_fingerprint <> replay.reconstruction_fingerprint
       OR first.root_entity_id <> replay.root_entity_id
       OR first.root_physicality_id <> replay.root_physicality_id
       OR first.source_profile_receipt_id <> replay.source_profile_receipt_id
       OR first.reference_topology_receipt_id <>
          replay.reference_topology_receipt_id
       OR first.evidence_lineage_receipt_id <> replay.evidence_lineage_receipt_id
       OR first.evidence_testimony_receipt_id <> replay.evidence_testimony_receipt_id
       OR expected IS DISTINCT FROM actual THEN
        RAISE EXCEPTION 'ISO 639 replay changed semantic state or durable cardinality';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    before_counts iso_after_first%ROWTYPE;
    after_counts iso_after_first%ROWTYPE;
BEGIN
    SELECT * INTO STRICT before_counts FROM iso_after_first;
    BEGIN
        PERFORM pg_temp.admit_iso_source(
            pg_temp.iso_source_artifacts(true, false));
        RAISE EXCEPTION 'changed ISO table byte was accepted';
    EXCEPTION WHEN data_exception THEN NULL;
    END;
    BEGIN
        PERFORM pg_temp.admit_iso_source(
            pg_temp.iso_source_artifacts(false, true));
        RAISE EXCEPTION 'broken ISO artifact parent was accepted';
    EXCEPTION WHEN data_exception THEN NULL;
    END;
    SELECT
        (SELECT count(*) FROM laplace.entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1),
        (SELECT count(*) FROM laplace.source_profile),
        (SELECT count(*) FROM laplace.reference_coordinate),
        (SELECT count(*) FROM laplace.reference_occurrence),
        (SELECT count(*) FROM laplace.reference_topology_receipt),
        (SELECT count(*) FROM laplace.evidence_node),
        (SELECT count(*) FROM laplace.evidence_testimony)
    INTO STRICT after_counts;
    IF before_counts IS DISTINCT FROM after_counts THEN
        RAISE EXCEPTION 'rejected ISO mutation published partial state';
    END IF;
END
$contract$;

EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT)
SELECT profile.profile_id, evidence.node_id, testimony.testimony_id
FROM iso_first AS admitted
JOIN laplace.source_profile AS profile
  ON profile.profile_id = admitted.profile_id
JOIN laplace.evidence_node AS evidence
  ON evidence.source_id = admitted.source_fingerprint
JOIN laplace.evidence_testimony AS testimony
  ON testimony.evidence_node_id = evidence.node_id
 AND testimony.source_profile_id = admitted.profile_id
ORDER BY evidence.node_id
LIMIT 32;
