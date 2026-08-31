\if :{?source_skip_unicode}
\else
\ir unicode_root_contract.sql
\endif

CREATE TEMP TABLE source_fixture_authority (
    artifact_graph bytea NOT NULL CHECK (octet_length(artifact_graph) = 32),
    archive_id bytea NOT NULL CHECK (octet_length(archive_id) = 32),
    text_id bytea NOT NULL CHECK (octet_length(text_id) = 32)
);

INSERT INTO source_fixture_authority
VALUES (
    decode(:'tabular_artifact_graph', 'hex'),
    decode(:'tabular_archive_id', 'hex'),
    decode(:'tabular_text_id', 'hex')
);

CREATE FUNCTION pg_temp.source_admission_context()
RETURNS laplace.execution_context
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('10', 32), 'hex'),
            decode(repeat('11', 32), 'hex'),
            decode(repeat('12', 32), 'hex'),
            decode(repeat('13', 32), 'hex'),
            decode(repeat('14', 32), 'hex'),
            decode(repeat('15', 32), 'hex'),
            decode(repeat('16', 32), 'hex'),
            (SELECT epoch_fingerprint
             FROM laplace.perfcache_active_control
             WHERE singleton AND active_present),
            (SELECT activation_epoch_fingerprint
             FROM laplace.highway_registry_active_control
             WHERE singleton AND active_present),
            decode(repeat('19', 32), 'hex')
        ],
        decode(repeat('a5', 32), 'hex'),
        1073741824::bigint,
        6,
        2,
        1023::bigint,
        @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        @LAPLACE_FRAMEWORK_CONTEXT_BOOTSTRAP@::integer
    )::laplace.execution_context
$context$;

CREATE FUNCTION pg_temp.source_profile_declaration()
RETURNS laplace.source_profile_manifest
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $profile$
    SELECT ROW(
        decode(repeat('00', 32), 'hex'),
        17,
        decode(repeat('10', 16), 'hex'),
        decode(repeat('30', 16), 'hex'),
        decode(repeat('50', 16), 'hex'),
        decode(repeat('70', 16), 'hex'),
        1::numeric,
        decode(repeat('90', 32), 'hex'),
        decode(repeat('91', 32), 'hex'),
        (SELECT artifact_graph FROM pg_temp.source_fixture_authority),
        decode(repeat('93', 32), 'hex'),
        decode(repeat('94', 32), 'hex'),
        decode(repeat('95', 32), 'hex'),
        decode(repeat('96', 32), 'hex'),
        decode(repeat('97', 32), 'hex'),
        decode(repeat('98', 32), 'hex'),
        decode(repeat('99', 32), 'hex'),
        decode(repeat('9a', 32), 'hex'),
        decode(repeat('9b', 32), 'hex'),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        2,
        34
    )::laplace.source_profile_manifest
$profile$;

CREATE FUNCTION pg_temp.source_artifacts(
    archive_content bytea DEFAULT decode('0001ff504b', 'hex'))
RETURNS laplace.tabular_source_artifact[]
LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $artifacts$
    SELECT ARRAY[
        ROW(
            authority.archive_id,
            decode(repeat('00', 32), 'hex'),
            authority.archive_id,
            archive_content,
            convert_to('release.zip', 'UTF8'),
            convert_to('application/zip', 'UTF8'),
            0, 0, 0,
            1, 0, 0, 0, 0,
            1,
            ARRAY[]::bytea[], 0
        )::laplace.tabular_source_artifact,
        ROW(
            authority.text_id,
            authority.archive_id,
            authority.text_id,
            convert_to(E'Id\tName\neng\tEnglish\njpn\t日本語\n', 'UTF8'),
            convert_to('tables/languages.tab', 'UTF8'),
            convert_to('text/tab-separated-values; charset=utf-8', 'UTF8'),
            3, 6, 3,
            2, 9, 1, 2, 5,
            6,
            ARRAY[convert_to('Id', 'UTF8'), convert_to('Name', 'UTF8')], 1
        )::laplace.tabular_source_artifact
    ]
    FROM pg_temp.source_fixture_authority AS authority
$artifacts$;

CREATE FUNCTION pg_temp.source_reference_rules()
RETURNS laplace.tabular_reference_rule[]
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $rules$
    SELECT ARRAY[
        ROW(
            1::numeric,
            0::numeric,
            decode(repeat('50', 16), 'hex'),
            7,
            3
        )::laplace.tabular_reference_rule,
        ROW(
            1::numeric,
            1::numeric,
            decode(repeat('51', 16), 'hex'),
            7,
            3
        )::laplace.tabular_reference_rule
    ]
$rules$;

CREATE FUNCTION pg_temp.source_mapping_rules()
RETURNS laplace.tabular_mapping_rule[]
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $rules$
    SELECT ARRAY[
        ROW(
            1::numeric,
            0::numeric,
            1::numeric,
            convert_to('=', 'UTF8'),
            1::numeric,
            8,
            1
        )::laplace.tabular_mapping_rule
    ]
$rules$;

CREATE FUNCTION pg_temp.admit_source(
    artifacts laplace.tabular_source_artifact[] DEFAULT pg_temp.source_artifacts(),
    admission_context laplace.execution_context
        DEFAULT pg_temp.source_admission_context())
RETURNS laplace.tabular_source_admission_result
LANGUAGE SQL VOLATILE PARALLEL UNSAFE
AS $admit$
    SELECT laplace.source_admit_tabular(
        admission_context,
        pg_temp.source_profile_declaration(),
        decode(repeat('c0', 32), 'hex'),
        decode(repeat('d0', 32), 'hex'),
        artifacts,
        pg_temp.source_reference_rules(),
        pg_temp.source_mapping_rules(),
        4096)
$admit$;

CREATE FUNCTION pg_temp.physicality_occurrences(candidate_id bytea)
RETURNS laplace.composition_occurrence[]
LANGUAGE SQL STABLE PARALLEL RESTRICTED
AS $decode$
    SELECT (laplace.trajectory_composition_decode_calculate_batch(
        pg_temp.source_admission_context(),
        array_agg(substring(p.trajectory FROM segment_offset + 1 FOR 32)
                  ORDER BY segment_offset))).occurrences
    FROM laplace.physicality AS p
    CROSS JOIN LATERAL generate_series(
        0, octet_length(p.trajectory) - 32, 32) AS segment(segment_offset)
    WHERE p.physicality_id = candidate_id
$decode$;

CREATE TEMP TABLE source_admission_before AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT COALESCE(sum(vertex_count), 0) FROM laplace.physicality) AS vertex_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.reference_coordinate) AS reference_coordinate_count,
    (SELECT count(*) FROM laplace.reference_occurrence) AS reference_occurrence_count,
    (SELECT count(*) FROM laplace.reference_topology_receipt) AS reference_receipt_count,
    (SELECT count(*) FROM laplace.reference_mapping_proposition) AS mapping_proposition_count,
    (SELECT count(*) FROM laplace.reference_mapping_occurrence) AS mapping_occurrence_count,
    (SELECT count(*) FROM laplace.reference_mapping_receipt) AS mapping_receipt_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE source_admission_first AS
WITH admission AS MATERIALIZED (
    SELECT pg_temp.admit_source() AS result
)
SELECT (result).* FROM admission;

DO $contract$
DECLARE
    admitted source_admission_first%ROWTYPE;
    profile laplace.source_profile%ROWTYPE;
    world laplace.world_admission%ROWTYPE;
    source_occurrences bigint;
BEGIN
    SELECT * INTO STRICT admitted FROM source_admission_first;
    SELECT * INTO STRICT profile
    FROM laplace.source_profile
    WHERE profile_id = admitted.profile_id;
    SELECT * INTO STRICT world
    FROM laplace.world_admission
    WHERE admission_id = admitted.world_admission_id;
    SELECT count(*) INTO STRICT source_occurrences
    FROM laplace.attestation
    WHERE source_fingerprint = admitted.source_fingerprint
      AND attestation_kind = 1;

    IF admitted.status <> 0
       OR admitted.artifact_count <> 2
       OR admitted.claim_count <> 2
       OR admitted.evidence_node_count <> 2
       OR admitted.testimony_count <> 2
       OR admitted.request_count <= admitted.claim_count
       OR admitted.occurrence_count <= 0
       OR admitted.logical_occurrence_count <= 0
       OR admitted.reference_occurrence_count <> 4
       OR admitted.reference_coordinate_count <> 4
       OR admitted.reference_present_count <> 4
       OR admitted.reference_retired_count <> 0
       OR admitted.reference_unresolved_count <> 0
       OR admitted.reference_mapping_receipt_id IS NULL
       OR admitted.reference_mapping_isa_receipt_id IS NULL
       OR admitted.reference_mapping_occurrence_count <> 2
       OR admitted.reference_mapping_proposition_count <> 2
       OR admitted.reference_mapping_resolved_count <> 2
       OR admitted.reference_mapping_unresolved_count <> 0
       OR admitted.reference_mapping_retired_count <> 0
       OR admitted.durable_stream_record_count <= 0
       OR source_occurrences <> admitted.occurrence_count
       OR profile.reconstruction_class <> 2
       OR profile.flags <> 34
       OR profile.mapping_count <> 2
       OR profile.transformation_count <> admitted.request_count
       OR profile.transformed_count <> admitted.request_count
       OR profile.unresolved_count <>
          profile.reference_count + profile.mapping_count
       OR profile.closure_subject_count <>
          admitted.request_count + profile.reference_count +
              profile.mapping_count
       OR profile.persisted_count <> 0
       OR profile.artifact_graph_fingerprint <>
          (SELECT artifact_graph FROM pg_temp.source_fixture_authority)
       OR world.reconstruction_class <> 2
       OR world.profile_claim_count <> 2
       OR world.evidence_node_count <> 2
       OR world.testimony_count <> 2
       OR world.closure_subject_count <>
          admitted.request_count + profile.reference_count +
              profile.mapping_count
       OR world.closed_subject_count <> world.closure_subject_count
       OR NOT EXISTS (
            SELECT 1 FROM laplace.source_profile_receipt
            WHERE receipt_id = admitted.source_profile_receipt_id
              AND negative_count =
                  profile.reference_count + profile.mapping_count
              AND closure_subject_count =
                  admitted.request_count + profile.reference_count +
                      profile.mapping_count)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.composition_execution_receipt
            WHERE working_set_receipt = admitted.composition_working_set_receipt_id)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.reference_topology_receipt
            WHERE receipt_id = admitted.reference_topology_receipt_id
              AND isa_receipt_id = admitted.reference_topology_isa_receipt_id
              AND occurrence_count = 4
              AND coordinate_count = 4
              AND present_count = 4
              AND retired_count = 0
              AND unresolved_count = 0)
       OR (SELECT count(*) FROM laplace.reference_occurrence
           WHERE source_profile_id = admitted.profile_id
             AND disposition = 1
             AND field_entity_id <> value_entity_id) <> 4
       OR NOT EXISTS (
            SELECT 1 FROM laplace.reference_mapping_receipt
            WHERE receipt_id = admitted.reference_mapping_receipt_id
              AND isa_receipt_id = admitted.reference_mapping_isa_receipt_id
              AND occurrence_count = 2
              AND proposition_count = 2
              AND resolved_count = 2
              AND unresolved_count = 0
              AND retired_count = 0)
       OR (SELECT count(*) FROM laplace.reference_mapping_occurrence
           WHERE source_profile_id = admitted.profile_id
             AND disposition = 1) <> 2
       OR NOT EXISTS (
            SELECT 1 FROM laplace.evidence_lineage_receipt
            WHERE receipt_id = admitted.evidence_lineage_receipt_id)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.evidence_testimony_receipt
            WHERE receipt_id = admitted.evidence_testimony_receipt_id)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.world_admission_receipt
            WHERE receipt_id = admitted.world_admission_receipt_id) THEN
        RAISE EXCEPTION
            'tabular source did not traverse the complete durable admission lifecycle: %',
            admitted;
    END IF;
END
$contract$;

DO $contract$
DECLARE
    admitted source_admission_first%ROWTYPE;
    japanese_count bigint;
    raw_archive_count bigint;
BEGIN
    SELECT * INTO STRICT admitted FROM source_admission_first;

    WITH expected_positions(codepoint_position, ordinality) AS (
        VALUES (26085, 1), (26412, 2), (35486, 3)
    ), expected AS (
        SELECT array_agg(binding.entity_id::bytea ORDER BY positions.ordinality)
                   AS ids
        FROM expected_positions AS positions
        JOIN laplace.attestation AS binding
          ON binding.source_fingerprint = admitted.unicode_root_receipt_id
         AND binding.attestation_kind = 3
         AND binding.source_ordinal = positions.codepoint_position + 1
    ), candidates AS (
        SELECT p.physicality_id,
               array_agg((occurrence).entity_id::bytea
                         ORDER BY (occurrence).logical_ordinal) AS ids
        FROM laplace.physicality AS p
        JOIN laplace.attestation AS a
          ON a.physicality_id = p.physicality_id
         AND a.source_fingerprint = admitted.source_fingerprint
         AND a.attestation_kind = 1
        CROSS JOIN LATERAL unnest(
            pg_temp.physicality_occurrences(p.physicality_id)) AS occurrence
        GROUP BY p.physicality_id
        HAVING count(*) = 3
    )
    SELECT count(*) INTO STRICT japanese_count
    FROM candidates, expected
    WHERE candidates.ids = expected.ids;

    WITH expected_positions(codepoint_position, ordinality) AS (
        VALUES (0, 1), (1, 2), (255, 3), (80, 4), (75, 5)
    ), expected AS (
        SELECT array_agg(binding.entity_id::bytea ORDER BY positions.ordinality)
                   AS ids
        FROM expected_positions AS positions
        JOIN laplace.attestation AS binding
          ON binding.source_fingerprint = admitted.unicode_root_receipt_id
         AND binding.attestation_kind = 3
         AND binding.source_ordinal = positions.codepoint_position + 1
    ), candidates AS (
        SELECT p.physicality_id,
               array_agg((occurrence).entity_id::bytea
                         ORDER BY (occurrence).logical_ordinal) AS ids
        FROM laplace.physicality AS p
        JOIN laplace.attestation AS a
          ON a.physicality_id = p.physicality_id
         AND a.source_fingerprint = admitted.source_fingerprint
         AND a.attestation_kind = 1
        CROSS JOIN LATERAL unnest(
            pg_temp.physicality_occurrences(p.physicality_id)) AS occurrence
        GROUP BY p.physicality_id
        HAVING count(*) = 5
    )
    SELECT count(*) INTO STRICT raw_archive_count
    FROM candidates, expected
    WHERE candidates.ids = expected.ids;

    IF japanese_count < 1 THEN
        RAISE EXCEPTION
            'exact selected Japanese content was not deposited as a reusable composition';
    END IF;
    IF raw_archive_count <> 0 THEN
        RAISE EXCEPTION
            'non-invertible source distribution was falsely deposited as exact raw content';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    admitted source_admission_first%ROWTYPE;
    witnessed_record_count bigint;
    witnessed_nonrecord_count bigint;
    structurally_retained_nonclaim_count bigint;
BEGIN
    SELECT * INTO STRICT admitted FROM source_admission_first;
    WITH witnessed AS (
        SELECT e.node_id,
               array_agg((item).metadata >> 6 ORDER BY (item).logical_ordinal)
                   AS roles
        FROM laplace.evidence_node AS e
        JOIN laplace.attestation AS occurrence
          ON occurrence.attestation_id = e.occurrence_id
        JOIN laplace.physicality AS p
          ON p.physicality_id = occurrence.physicality_id
        CROSS JOIN LATERAL unnest(
            pg_temp.physicality_occurrences(p.physicality_id)) AS item
        WHERE e.source_id = admitted.source_fingerprint
        GROUP BY e.node_id
    )
    SELECT count(*) FILTER (
               WHERE roles = ARRAY[1, 7, 7, 10]::bigint[]),
           count(*) FILTER (
               WHERE roles <> ARRAY[1, 7, 7, 10]::bigint[]),
           admitted.occurrence_count - count(*)
    INTO STRICT witnessed_record_count, witnessed_nonrecord_count,
        structurally_retained_nonclaim_count
    FROM witnessed;

    IF witnessed_record_count <> admitted.claim_count
       OR witnessed_nonrecord_count <> 0
       OR structurally_retained_nonclaim_count <= 0 THEN
        RAISE EXCEPTION
            'structural AST tiers were promoted into record testimony';
    END IF;
END
$contract$;

CREATE TEMP TABLE source_admission_after_first AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT COALESCE(sum(vertex_count), 0) FROM laplace.physicality) AS vertex_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.reference_coordinate) AS reference_coordinate_count,
    (SELECT count(*) FROM laplace.reference_occurrence) AS reference_occurrence_count,
    (SELECT count(*) FROM laplace.reference_topology_receipt) AS reference_receipt_count,
    (SELECT count(*) FROM laplace.reference_mapping_proposition) AS mapping_proposition_count,
    (SELECT count(*) FROM laplace.reference_mapping_occurrence) AS mapping_occurrence_count,
    (SELECT count(*) FROM laplace.reference_mapping_receipt) AS mapping_receipt_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE source_admission_replay AS
WITH admission AS MATERIALIZED (
    SELECT pg_temp.admit_source() AS result
)
SELECT (result).* FROM admission;

DO $contract$
DECLARE
    first source_admission_first%ROWTYPE;
    replay source_admission_replay%ROWTYPE;
    expected source_admission_after_first%ROWTYPE;
    actual source_admission_after_first%ROWTYPE;
BEGIN
    SELECT * INTO STRICT first FROM source_admission_first;
    SELECT * INTO STRICT replay FROM source_admission_replay;
    SELECT * INTO STRICT expected FROM source_admission_after_first;
    SELECT
        (SELECT count(*) FROM laplace.entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT COALESCE(sum(vertex_count), 0) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1),
        (SELECT count(*) FROM laplace.source_profile),
        (SELECT count(*) FROM laplace.reference_coordinate),
        (SELECT count(*) FROM laplace.reference_occurrence),
        (SELECT count(*) FROM laplace.reference_topology_receipt),
        (SELECT count(*) FROM laplace.reference_mapping_proposition),
        (SELECT count(*) FROM laplace.reference_mapping_occurrence),
        (SELECT count(*) FROM laplace.reference_mapping_receipt),
        (SELECT count(*) FROM laplace.evidence_node),
        (SELECT count(*) FROM laplace.evidence_testimony),
        (SELECT count(*) FROM laplace.world_admission)
    INTO STRICT actual;

    IF first.profile_id <> replay.profile_id
       OR first.source_profile_receipt_id <> replay.source_profile_receipt_id
       OR first.reference_topology_receipt_id <>
          replay.reference_topology_receipt_id
       OR first.reference_mapping_receipt_id <>
          replay.reference_mapping_receipt_id
       OR first.source_fingerprint <> replay.source_fingerprint
       OR first.reconstruction_fingerprint <> replay.reconstruction_fingerprint
       OR first.root_entity_id <> replay.root_entity_id
       OR first.root_physicality_id <> replay.root_physicality_id
       OR first.evidence_lineage_receipt_id <> replay.evidence_lineage_receipt_id
       OR first.evidence_testimony_receipt_id <> replay.evidence_testimony_receipt_id
       OR first.composition_working_set_receipt_id =
          replay.composition_working_set_receipt_id
       OR first.world_admission_id = replay.world_admission_id
       OR first.world_admission_receipt_id = replay.world_admission_receipt_id
       OR expected.entity_count <> actual.entity_count
       OR expected.physicality_count <> actual.physicality_count
       OR expected.vertex_count <> actual.vertex_count
       OR expected.occurrence_count <> actual.occurrence_count
       OR expected.profile_count <> actual.profile_count
       OR expected.reference_coordinate_count <>
          actual.reference_coordinate_count
       OR expected.reference_occurrence_count <>
          actual.reference_occurrence_count
       OR expected.reference_receipt_count <>
          actual.reference_receipt_count
       OR expected.mapping_proposition_count <>
          actual.mapping_proposition_count
       OR expected.mapping_occurrence_count <>
          actual.mapping_occurrence_count
       OR expected.mapping_receipt_count <>
          actual.mapping_receipt_count
       OR expected.evidence_count <> actual.evidence_count
       OR expected.testimony_count <> actual.testimony_count
       OR expected.world_count + 1 <> actual.world_count THEN
        RAISE EXCEPTION
            'tabular source replay did not preserve semantics while recording its distinct physical execution'
            USING DETAIL = concat_ws(', ',
                CASE WHEN first.profile_id <> replay.profile_id
                    THEN 'profile_id' END,
                CASE WHEN first.source_profile_receipt_id <>
                               replay.source_profile_receipt_id
                    THEN 'source_profile_receipt_id' END,
                CASE WHEN first.reference_topology_receipt_id <>
                               replay.reference_topology_receipt_id
                    THEN 'reference_topology_receipt_id' END,
                CASE WHEN first.reference_mapping_receipt_id <>
                               replay.reference_mapping_receipt_id
                    THEN 'reference_mapping_receipt_id' END,
                CASE WHEN first.source_fingerprint <>
                               replay.source_fingerprint
                    THEN 'source_fingerprint' END,
                CASE WHEN first.reconstruction_fingerprint <>
                               replay.reconstruction_fingerprint
                    THEN 'reconstruction_fingerprint' END,
                CASE WHEN first.root_entity_id <> replay.root_entity_id
                    THEN 'root_entity_id' END,
                CASE WHEN first.root_physicality_id <>
                               replay.root_physicality_id
                    THEN 'root_physicality_id' END,
                CASE WHEN first.evidence_lineage_receipt_id <>
                               replay.evidence_lineage_receipt_id
                    THEN 'evidence_lineage_receipt_id' END,
                CASE WHEN first.evidence_testimony_receipt_id <>
                               replay.evidence_testimony_receipt_id
                    THEN 'evidence_testimony_receipt_id' END,
                CASE WHEN first.composition_working_set_receipt_id =
                               replay.composition_working_set_receipt_id
                    THEN 'composition_working_set_receipt_id' END,
                CASE WHEN first.world_admission_id = replay.world_admission_id
                    THEN 'world_admission_id' END,
                CASE WHEN first.world_admission_receipt_id =
                               replay.world_admission_receipt_id
                    THEN 'world_admission_receipt_id' END,
                CASE WHEN expected.entity_count <> actual.entity_count
                    THEN 'entity_count' END,
                CASE WHEN expected.physicality_count <>
                               actual.physicality_count
                    THEN 'physicality_count' END,
                CASE WHEN expected.vertex_count <> actual.vertex_count
                    THEN 'vertex_count' END,
                CASE WHEN expected.occurrence_count <> actual.occurrence_count
                    THEN 'occurrence_count' END,
                CASE WHEN expected.profile_count <> actual.profile_count
                    THEN 'profile_count' END,
                CASE WHEN expected.reference_coordinate_count <>
                               actual.reference_coordinate_count
                    THEN 'reference_coordinate_count' END,
                CASE WHEN expected.reference_occurrence_count <>
                               actual.reference_occurrence_count
                    THEN 'reference_occurrence_count' END,
                CASE WHEN expected.reference_receipt_count <>
                               actual.reference_receipt_count
                    THEN 'reference_receipt_count' END,
                CASE WHEN expected.mapping_proposition_count <>
                               actual.mapping_proposition_count
                    THEN 'mapping_proposition_count' END,
                CASE WHEN expected.mapping_occurrence_count <>
                               actual.mapping_occurrence_count
                    THEN 'mapping_occurrence_count' END,
                CASE WHEN expected.mapping_receipt_count <>
                               actual.mapping_receipt_count
                    THEN 'mapping_receipt_count' END,
                CASE WHEN expected.evidence_count <> actual.evidence_count
                    THEN 'evidence_count' END,
                CASE WHEN expected.testimony_count <> actual.testimony_count
                    THEN 'testimony_count' END,
                CASE WHEN expected.world_count + 1 <> actual.world_count
                    THEN 'world_count' END);
    END IF;
END
$contract$;

CREATE TEMP TABLE source_admission_after_replay AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT COALESCE(sum(vertex_count), 0) FROM laplace.physicality) AS vertex_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.reference_coordinate) AS reference_coordinate_count,
    (SELECT count(*) FROM laplace.reference_occurrence) AS reference_occurrence_count,
    (SELECT count(*) FROM laplace.reference_topology_receipt) AS reference_receipt_count,
    (SELECT count(*) FROM laplace.reference_mapping_proposition) AS mapping_proposition_count,
    (SELECT count(*) FROM laplace.reference_mapping_occurrence) AS mapping_occurrence_count,
    (SELECT count(*) FROM laplace.reference_mapping_receipt) AS mapping_receipt_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE source_admission_steady_replay AS
WITH admission AS MATERIALIZED (
    SELECT pg_temp.admit_source() AS result
)
SELECT (result).* FROM admission;

DO $contract$
DECLARE
    replay source_admission_replay%ROWTYPE;
    steady source_admission_steady_replay%ROWTYPE;
    expected source_admission_after_replay%ROWTYPE;
    actual source_admission_after_replay%ROWTYPE;
BEGIN
    SELECT * INTO STRICT replay FROM source_admission_replay;
    SELECT * INTO STRICT steady FROM source_admission_steady_replay;
    SELECT * INTO STRICT expected FROM source_admission_after_replay;
    SELECT
        (SELECT count(*) FROM laplace.entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT COALESCE(sum(vertex_count), 0) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1),
        (SELECT count(*) FROM laplace.source_profile),
        (SELECT count(*) FROM laplace.reference_coordinate),
        (SELECT count(*) FROM laplace.reference_occurrence),
        (SELECT count(*) FROM laplace.reference_topology_receipt),
        (SELECT count(*) FROM laplace.reference_mapping_proposition),
        (SELECT count(*) FROM laplace.reference_mapping_occurrence),
        (SELECT count(*) FROM laplace.reference_mapping_receipt),
        (SELECT count(*) FROM laplace.evidence_node),
        (SELECT count(*) FROM laplace.evidence_testimony),
        (SELECT count(*) FROM laplace.world_admission)
    INTO STRICT actual;
    IF to_jsonb(replay) IS DISTINCT FROM to_jsonb(steady)
       OR expected IS DISTINCT FROM actual THEN
        RAISE EXCEPTION
            'steady-state tabular source replay changed receipts or durable cardinality';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    before_counts source_admission_after_replay%ROWTYPE;
    after_counts source_admission_after_replay%ROWTYPE;
    pinned_context laplace.execution_context;
BEGIN
    SELECT * INTO STRICT before_counts FROM source_admission_after_replay;
    pinned_context := pg_temp.source_admission_context();
    BEGIN
        PERFORM pg_temp.admit_source(
            pg_temp.source_artifacts(decode('0001ff504a', 'hex')));
        RAISE EXCEPTION 'artifact digest defect was accepted';
    EXCEPTION
        WHEN data_exception THEN NULL;
    END;
    SELECT
        (SELECT count(*) FROM laplace.entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT COALESCE(sum(vertex_count), 0) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1),
        (SELECT count(*) FROM laplace.source_profile),
        (SELECT count(*) FROM laplace.reference_coordinate),
        (SELECT count(*) FROM laplace.reference_occurrence),
        (SELECT count(*) FROM laplace.reference_topology_receipt),
        (SELECT count(*) FROM laplace.reference_mapping_proposition),
        (SELECT count(*) FROM laplace.reference_mapping_occurrence),
        (SELECT count(*) FROM laplace.reference_mapping_receipt),
        (SELECT count(*) FROM laplace.evidence_node),
        (SELECT count(*) FROM laplace.evidence_testimony),
        (SELECT count(*) FROM laplace.world_admission)
    INTO STRICT after_counts;
    IF before_counts IS DISTINCT FROM after_counts THEN
        RAISE EXCEPTION 'rejected artifact mutation published partial state';
    END IF;

    BEGIN
        UPDATE laplace.perfcache_active_control
        SET active_present = false
        WHERE singleton;
        PERFORM pg_temp.admit_source(
            pg_temp.source_artifacts(), pinned_context);
        RAISE EXCEPTION 'absent active Unicode root was accepted';
    EXCEPTION
        WHEN object_not_in_prerequisite_state THEN NULL;
    END;
    IF NOT EXISTS (
        SELECT 1 FROM laplace.perfcache_active_control
        WHERE singleton AND active_present) THEN
        RAISE EXCEPTION 'Unicode mutation subtransaction did not roll back';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    baseline source_admission_steady_replay%ROWTYPE;
    replay laplace.tabular_source_admission_result;
    changed bigint;
BEGIN
    SELECT * INTO STRICT baseline FROM source_admission_steady_replay;
    BEGIN
        UPDATE laplace.physicality AS physicality
        SET centroid_x = CASE
            WHEN centroid_x = 0.5 THEN 0.25
            ELSE 0.5
        END
        FROM laplace.attestation AS binding
        WHERE binding.source_fingerprint = baseline.unicode_root_receipt_id
          AND binding.attestation_kind = 3
          AND binding.source_ordinal = 102
          AND binding.physicality_id = physicality.physicality_id;
        GET DIAGNOSTICS changed = ROW_COUNT;
        IF changed <> 1 THEN
            RAISE EXCEPTION
                'mapped Unicode leaf defect fixture did not select one Tier-0 physicality: %',
                changed;
        END IF;

        SELECT (admission.result).*
        INTO STRICT replay
        FROM (SELECT pg_temp.admit_source() AS result) AS admission;
        IF replay.root_entity_id IS DISTINCT FROM baseline.root_entity_id
           OR replay.root_physicality_id IS DISTINCT FROM baseline.root_physicality_id THEN
            RAISE EXCEPTION USING
                ERRCODE = 'LP001',
                MESSAGE = 'source admission followed relational Unicode leaf drift instead of the mapped plane';
        END IF;
        RAISE EXCEPTION USING
            ERRCODE = 'LP000',
            MESSAGE = 'rollback mapped Unicode leaf drift fixture';
    EXCEPTION
        WHEN SQLSTATE 'LP000' THEN NULL;
    END;
END
$contract$;

EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT)
SELECT e.node_id, e.proposition_id, t.testimony_id, w.admission_id
FROM source_admission_first AS admitted
JOIN laplace.evidence_node AS e
  ON e.source_id = admitted.source_fingerprint
JOIN laplace.evidence_testimony AS t
  ON t.evidence_node_id = e.node_id
 AND t.source_profile_id = admitted.profile_id
JOIN laplace.world_admission AS w
  ON w.admission_id = admitted.world_admission_id
ORDER BY e.node_id;