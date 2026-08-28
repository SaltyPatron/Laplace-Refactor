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
        0
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
            0, 0, 0,
            1, 0, 0, 0, 0,
            1
        )::laplace.tabular_source_artifact,
        ROW(
            authority.text_id,
            authority.archive_id,
            authority.text_id,
            convert_to(E'Id\tName\neng\tEnglish\njpn\t日本語\n', 'UTF8'),
            convert_to('tables/languages.tab', 'UTF8'),
            3, 6, 1,
            2, 9, 1, 2, 5,
            6
        )::laplace.tabular_source_artifact
    ]
    FROM pg_temp.source_fixture_authority AS authority
$artifacts$;

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
        4096)
$admit$;

CREATE TEMP TABLE source_admission_before AS
SELECT
    (SELECT count(*) FROM laplace.canonical_entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.composition_trajectory_vertex) AS vertex_count,
    (SELECT count(*) FROM laplace.observed_occurrence) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE source_admission_first AS
SELECT (pg_temp.admit_source()).*;

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
    FROM laplace.observed_occurrence
    WHERE source_fingerprint = admitted.source_fingerprint;

    IF admitted.status <> 0
       OR admitted.artifact_count <> 2
       OR admitted.claim_count <> 2
       OR admitted.evidence_node_count <> 2
       OR admitted.testimony_count <> 2
       OR admitted.request_count <= admitted.claim_count
       OR admitted.occurrence_count <= 0
       OR admitted.logical_occurrence_count <= 0
       OR admitted.durable_stream_record_count <= 0
       OR source_occurrences <> admitted.occurrence_count
       OR profile.reconstruction_class <> 2
       OR profile.transformation_count <> admitted.request_count
       OR profile.transformed_count <> admitted.request_count
       OR profile.unresolved_count <> profile.reference_count
       OR profile.closure_subject_count <>
          admitted.request_count + profile.reference_count
       OR profile.persisted_count <> 0
       OR profile.artifact_graph_fingerprint <>
          (SELECT artifact_graph FROM pg_temp.source_fixture_authority)
       OR world.reconstruction_class <> 2
       OR world.profile_claim_count <> 2
       OR world.evidence_node_count <> 2
       OR world.testimony_count <> 2
       OR world.closure_subject_count <>
          admitted.request_count + profile.reference_count
       OR world.closed_subject_count <> world.closure_subject_count
       OR NOT EXISTS (
            SELECT 1 FROM laplace.source_profile_receipt
            WHERE receipt_id = admitted.source_profile_receipt_id
              AND negative_count = profile.reference_count
              AND closure_subject_count =
                  admitted.request_count + profile.reference_count)
       OR NOT EXISTS (
            SELECT 1 FROM laplace.composition_execution_receipt
            WHERE working_set_receipt = admitted.composition_working_set_receipt_id)
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
        JOIN laplace.unicode_atom_binding AS binding
          ON binding.root_receipt = admitted.unicode_root_receipt_id
         AND binding.codepoint_position = positions.codepoint_position
    ), candidates AS (
        SELECT v.physicality_id,
               array_agg(v.constituent_entity_id::bytea
                         ORDER BY v.logical_ordinal) AS ids
        FROM laplace.composition_trajectory_vertex AS v
        JOIN laplace.observed_occurrence AS o
          ON o.physicality_id = v.physicality_id
         AND o.source_fingerprint = admitted.source_fingerprint
        GROUP BY v.physicality_id
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
        JOIN laplace.unicode_atom_binding AS binding
          ON binding.root_receipt = admitted.unicode_root_receipt_id
         AND binding.codepoint_position = positions.codepoint_position
    ), candidates AS (
        SELECT v.physicality_id,
               array_agg(v.constituent_entity_id::bytea
                         ORDER BY v.logical_ordinal) AS ids
        FROM laplace.composition_trajectory_vertex AS v
        JOIN laplace.observed_occurrence AS o
          ON o.physicality_id = v.physicality_id
         AND o.source_fingerprint = admitted.source_fingerprint
        GROUP BY v.physicality_id
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

CREATE TEMP TABLE source_admission_after_first AS
SELECT
    (SELECT count(*) FROM laplace.canonical_entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.composition_trajectory_vertex) AS vertex_count,
    (SELECT count(*) FROM laplace.observed_occurrence) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE source_admission_replay AS
SELECT (pg_temp.admit_source()).*;

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
        (SELECT count(*) FROM laplace.canonical_entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.composition_trajectory_vertex),
        (SELECT count(*) FROM laplace.observed_occurrence),
        (SELECT count(*) FROM laplace.source_profile),
        (SELECT count(*) FROM laplace.evidence_node),
        (SELECT count(*) FROM laplace.evidence_testimony),
        (SELECT count(*) FROM laplace.world_admission)
    INTO STRICT actual;

    IF first.profile_id <> replay.profile_id
       OR first.source_profile_receipt_id <> replay.source_profile_receipt_id
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
       OR expected.evidence_count <> actual.evidence_count
       OR expected.testimony_count <> actual.testimony_count
       OR expected.world_count + 1 <> actual.world_count THEN
        RAISE EXCEPTION
            'tabular source replay did not preserve semantics while recording its distinct physical execution';
    END IF;
END
$contract$;

CREATE TEMP TABLE source_admission_after_replay AS
SELECT
    (SELECT count(*) FROM laplace.canonical_entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.composition_trajectory_vertex) AS vertex_count,
    (SELECT count(*) FROM laplace.observed_occurrence) AS occurrence_count,
    (SELECT count(*) FROM laplace.source_profile) AS profile_count,
    (SELECT count(*) FROM laplace.evidence_node) AS evidence_count,
    (SELECT count(*) FROM laplace.evidence_testimony) AS testimony_count,
    (SELECT count(*) FROM laplace.world_admission) AS world_count;

CREATE TEMP TABLE source_admission_steady_replay AS
SELECT (pg_temp.admit_source()).*;

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
        (SELECT count(*) FROM laplace.canonical_entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.composition_trajectory_vertex),
        (SELECT count(*) FROM laplace.observed_occurrence),
        (SELECT count(*) FROM laplace.source_profile),
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
        (SELECT count(*) FROM laplace.canonical_entity),
        (SELECT count(*) FROM laplace.physicality),
        (SELECT count(*) FROM laplace.composition_trajectory_vertex),
        (SELECT count(*) FROM laplace.observed_occurrence),
        (SELECT count(*) FROM laplace.source_profile),
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
