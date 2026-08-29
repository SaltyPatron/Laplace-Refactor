\timing on

CREATE EXTENSION laplace;

CREATE FUNCTION pg_temp.unicode_root_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
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
            decode(repeat('00', 32), 'hex'),
            decode(repeat('18', 32), 'hex'),
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

CREATE FUNCTION pg_temp.highway_registry_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
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
            decode(repeat('43', 32), 'hex'),
            decode(repeat('18', 32), 'hex'),
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

CREATE FUNCTION pg_temp.highway_registry_read_context()
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
        @LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY@::integer
    )::laplace.execution_context
$context$;

CREATE TEMP TABLE unicode_build_result AS
SELECT result.*
FROM laplace.unicode_root_build_and_activate(
    pg_temp.unicode_root_context(),
    :'unicode_source_root',
    :'unicode_spool_directory',
    :'unicode_tier0_path',
    :'unicode_reverse_path',
    decode(repeat('42', 16), 'hex'),
    decode(repeat('43', 32), 'hex'),
    0,
    false,
    decode(repeat('00', 16), 'hex'),
    decode(repeat('00', 32), 'hex'),
    33554432
) AS result;

DO $contract$
DECLARE
    build unicode_build_result%ROWTYPE;
    generation laplace.unicode_root_generation%ROWTYPE;
    deposit laplace.unicode_root_deposit_receipt%ROWTYPE;
BEGIN
    SELECT * INTO STRICT build FROM unicode_build_result;
    SELECT * INTO STRICT generation
    FROM laplace.unicode_root_generation
    WHERE root_receipt = build.root_receipt;
    SELECT * INTO STRICT deposit
    FROM laplace.unicode_root_deposit_receipt
    WHERE root_receipt = build.root_receipt;

    IF octet_length(build.root_receipt) <> 32
       OR octet_length(build.producer_receipt) <> 32
       OR octet_length(build.staged_stream_receipt) <> 32
       OR octet_length(build.sink_artifacts_fingerprint) <> 32
       OR octet_length(build.postgresql_artifact_fingerprint) <> 32
       OR octet_length(build.perfcache_artifact_set_fingerprint) <> 32
       OR octet_length(build.tier0_artifact_digest) <> 32
       OR octet_length(build.reverse_artifact_digest) <> 32
       OR octet_length(build.perfcache_manifest_fingerprint) <> 32
       OR octet_length(build.perfcache_encoded_manifest_fingerprint) <> 32
       OR octet_length(build.admission_receipt) <> 32
       OR octet_length(build.plan_manifest_fingerprint) <> 32
       OR octet_length(build.plan_sequence_fingerprint) <> 32
       OR build.plan_manifest_fingerprint <>
          decode('3a546d581afc1c4caf78c1c235aafd41b283984325d5717b188211d1091cb9e5', 'hex')
       OR build.activation_epoch_id <> decode(repeat('42', 16), 'hex')
       OR build.activation_epoch_fingerprint <> decode(repeat('43', 32), 'hex')
       OR build.total_frame_count <> 2230150
       OR build.total_encoded_bytes <= 0
       OR build.batch_count <= 1
       OR build.plan_count <= 0
       OR build.plan_count >= build.total_frame_count
       OR build.entity_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR build.physicality_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR build.atom_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR build.ducet_position_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR build.ducet_contraction_count <> 964
       OR build.normalization_composition_count <> 961
       OR build.tier0_artifact_digest <>
          decode('8950d9867428fd660f8a49377b0c4a693b57ef0a9807ea189e425d0bb847c291', 'hex')
       OR build.reverse_artifact_digest <>
          decode('6f33df84440a8f4bb19afa608befac11f17ad68123d06e043c2be7451f6ab7b1', 'hex')
       OR build.tier0_artifact_bytes <> 762586574
       OR build.reverse_artifact_bytes <> 117440896
       OR build.perfcache_artifact_count <> 2
       OR build.perfcache_dependency_count <> 1
       OR build.reverse_dependency_module_id <>
          decode('cb4d73fe1c7ad3784bdd69f9e22f5b3f', 'hex')
       OR build.reverse_dependency_artifact_digest <>
          build.tier0_artifact_digest THEN
        RAISE EXCEPTION 'Unicode root result violates the full-root contract'
            USING DETAIL = format(
                'frames=%s encoded_bytes=%s batches=%s plans=%s plan_manifest=%s tier0_bytes=%s reverse_bytes=%s tier0_digest=%s reverse_digest=%s artifact_count=%s dependency_count=%s dependency_module=%s dependency_digest=%s',
                build.total_frame_count, build.total_encoded_bytes,
                build.batch_count, build.plan_count,
                encode(build.plan_manifest_fingerprint, 'hex'),
                build.tier0_artifact_bytes,
                build.reverse_artifact_bytes,
                encode(build.tier0_artifact_digest, 'hex'),
                encode(build.reverse_artifact_digest, 'hex'),
                build.perfcache_artifact_count,
                build.perfcache_dependency_count,
                encode(build.reverse_dependency_module_id, 'hex'),
                encode(build.reverse_dependency_artifact_digest, 'hex'));
    END IF;

    IF generation.atom_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR generation.ducet_position_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR generation.ducet_contraction_count <> 964
       OR generation.normalization_composition_count <> 961
       OR generation.total_frame_count <> build.total_frame_count
       OR generation.total_encoded_bytes <> build.total_encoded_bytes
       OR generation.postgresql_artifact_fingerprint <>
          build.postgresql_artifact_fingerprint
       OR generation.plan_manifest_fingerprint <>
          build.plan_manifest_fingerprint
       OR deposit.producer_receipt <> build.producer_receipt
       OR deposit.staged_stream_receipt <> build.staged_stream_receipt
       OR deposit.sink_artifacts_fingerprint <>
          build.sink_artifacts_fingerprint
       OR deposit.postgresql_artifact_fingerprint <>
          build.postgresql_artifact_fingerprint
       OR deposit.perfcache_artifact_set_fingerprint <>
          build.perfcache_artifact_set_fingerprint
       OR deposit.perfcache_manifest_fingerprint <>
          build.perfcache_manifest_fingerprint
       OR deposit.perfcache_encoded_manifest_fingerprint <>
          build.perfcache_encoded_manifest_fingerprint
       OR deposit.admission_receipt <> build.admission_receipt
       OR deposit.plan_manifest_fingerprint <>
          build.plan_manifest_fingerprint
       OR deposit.plan_sequence_fingerprint <>
          build.plan_sequence_fingerprint
       OR deposit.entity_count <> build.entity_count
       OR deposit.physicality_count <> build.physicality_count
       OR deposit.atom_count <> build.atom_count
       OR deposit.ducet_position_count <> build.ducet_position_count
       OR deposit.ducet_contraction_count <> build.ducet_contraction_count
       OR deposit.normalization_composition_count <>
          build.normalization_composition_count
       OR deposit.plan_count <> build.plan_count THEN
        RAISE EXCEPTION 'Unicode root receipts do not bind the same execution';
    END IF;
END
$contract$;

DO $contract$
BEGIN
    IF EXISTS (
        SELECT 1
        FROM information_schema.columns
        WHERE table_schema = 'laplace'
          AND table_name IN (
              'attestation', 'unicode_ducet_position',
              'unicode_ducet_contraction',
              'unicode_normalization_composition')
          AND column_name = 'canonical_record') THEN
        RAISE EXCEPTION
            'canonical transport records were duplicated into normalized storage';
    END IF;
END
$contract$;

CREATE TEMP TABLE unicode_table_counts AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.attestation
     WHERE source_fingerprint = (SELECT root_receipt FROM unicode_build_result)
       AND attestation_kind = 3) AS atom_count,
    (SELECT count(*) FROM laplace.unicode_ducet_position) AS ducet_position_count,
    (SELECT count(*) FROM laplace.unicode_ducet_contraction) AS contraction_count,
    (SELECT count(*) FROM laplace.unicode_normalization_composition) AS normalization_count;

DO $contract$
DECLARE
    counts unicode_table_counts%ROWTYPE;
BEGIN
    SELECT * INTO STRICT counts FROM unicode_table_counts;
    IF counts.entity_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR counts.physicality_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR counts.atom_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR counts.ducet_position_count <> @LAPLACE_PG_UNICODE_ROOT_POPULATION@
       OR counts.contraction_count <> 964
       OR counts.normalization_count <> 961 THEN
        RAISE EXCEPTION 'Unicode root deposition is incomplete';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    build unicode_build_result%ROWTYPE;
    normalized_heap_bytes bigint;
BEGIN
    SELECT * INTO STRICT build FROM unicode_build_result;
    SELECT sum(pg_table_size(format('laplace.%I', table_name)::regclass))
    INTO STRICT normalized_heap_bytes
    FROM (VALUES
        ('entity'), ('physicality'), ('attestation'),
        ('unicode_ducet_position'), ('unicode_ducet_contraction'),
        ('unicode_normalization_composition')) AS tables(table_name);
    IF normalized_heap_bytes >
       build.total_encoded_bytes + build.total_encoded_bytes / 10 THEN
        RAISE EXCEPTION
            'normalized Unicode heap exceeds its 110 percent transport bound'
            USING DETAIL = format(
                'normalized_heap_bytes=%s canonical_transport_bytes=%s',
                normalized_heap_bytes, build.total_encoded_bytes);
    END IF;
END
$contract$;

CREATE TEMP TABLE unicode_identity_samples AS
WITH calculated AS (
    SELECT (laplace.identity_codepoint_calculate_batch(
        pg_temp.unicode_root_context(), ARRAY[0, 65, 1114111])).entity_ids
), deposited AS (
    SELECT array_agg(entity_id::bytea ORDER BY source_ordinal) AS entity_ids
    FROM laplace.attestation
    WHERE source_fingerprint = (SELECT root_receipt FROM unicode_build_result)
      AND attestation_kind = 3
      AND source_ordinal IN (1, 66, 1114112)
)
SELECT calculated.entity_ids AS calculated, deposited.entity_ids AS deposited
FROM calculated CROSS JOIN deposited;

DO $contract$
DECLARE
    samples unicode_identity_samples%ROWTYPE;
BEGIN
    SELECT * INTO STRICT samples FROM unicode_identity_samples;
    IF samples.calculated <> samples.deposited THEN
        RAISE EXCEPTION 'Unicode identity calculation and deposited root diverged';
    END IF;
END
$contract$;

DO $contract$
DECLARE
    active laplace.perfcache_active_control%ROWTYPE;
BEGIN
    SELECT * INTO STRICT active
    FROM laplace.perfcache_active_control
    WHERE singleton;
    IF active.sequence <> 1
       OR NOT active.active_present
       OR active.activation_epoch_id <> decode(repeat('42', 16), 'hex')
       OR active.epoch_fingerprint <> decode(repeat('43', 32), 'hex')
       OR active.manifest_fingerprint <>
          (SELECT perfcache_manifest_fingerprint FROM unicode_build_result)
       OR active.admission_receipt_id <>
          (SELECT admission_receipt FROM unicode_build_result) THEN
        RAISE EXCEPTION 'Unicode Tier-0 epoch was not atomically activated';
    END IF;
END
$contract$;

CREATE TEMP TABLE highway_registry_counts_before AS
SELECT
    (SELECT count(*) FROM laplace.entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT COALESCE(sum(vertex_count), 0) FROM laplace.physicality) AS vertex_count,
    (SELECT count(*) FROM laplace.attestation WHERE attestation_kind = 1) AS occurrence_count;

CREATE TEMP TABLE highway_registry_result AS
SELECT deposited.*
FROM laplace.highway_registry_admit_and_activate(
    pg_temp.highway_registry_context(), 1048576::numeric) AS deposited;

CREATE TEMP TABLE highway_registry_replay AS
SELECT deposited.*
FROM laplace.highway_registry_admit_and_activate(
    pg_temp.highway_registry_context(), 1048576::numeric) AS deposited;

CREATE TEMP TABLE highway_registry_active AS
SELECT active.*
FROM laplace.highway_registry_resolve_active(
    pg_temp.highway_registry_read_context()) AS active;

DO $contract$
DECLARE
    deposited highway_registry_result%ROWTYPE;
    replay highway_registry_replay%ROWTYPE;
    active highway_registry_active%ROWTYPE;
    before_counts highway_registry_counts_before%ROWTYPE;
BEGIN
    SELECT * INTO STRICT deposited FROM highway_registry_result;
    SELECT * INTO STRICT replay FROM highway_registry_replay;
    SELECT * INTO STRICT active FROM highway_registry_active;
    SELECT * INTO STRICT before_counts FROM highway_registry_counts_before;
    IF deposited.status <> 0
       OR deposited.registry_version <> 2
       OR deposited.registry_fingerprint <> deposited.source_fingerprint
       OR deposited.registry_fingerprint <>
          decode('1b5a1f5a2177c18256ec55bdc403da557ab3dc0003fcdee21784c21ea85f56b5', 'hex')
       OR octet_length(deposited.root_entity_id) <> 16
       OR octet_length(deposited.root_physicality_id) <> 32
       OR octet_length(deposited.registry_epoch_id) <> 16
       OR octet_length(deposited.registry_epoch_fingerprint) <> 32
       OR octet_length(deposited.isa_receipt) <> 32
       OR octet_length(deposited.recipe_fingerprint) <> 32
       OR octet_length(deposited.admission_receipt) <> 32
       OR octet_length(deposited.activation_receipt) <> 32
       OR octet_length(deposited.activation_fingerprint) <> 32
       OR deposited.activation_sequence <> 1
       OR deposited.effect_disposition <> 3
       OR deposited.unicode_root_receipt <>
          (SELECT root_receipt FROM unicode_build_result)
       OR deposited.unicode_activation_epoch_id <>
          decode(repeat('42', 16), 'hex')
       OR deposited.unicode_activation_epoch_fingerprint <>
          decode(repeat('43', 32), 'hex')
       OR cardinality(deposited.kind_name_entity_ids) <> 17
       OR cardinality(deposited.alias_name_entity_ids) <> 0
       OR cardinality(deposited.disposition_name_entity_ids) <> 8
       OR deposited.atom_count <= 0
       OR deposited.request_count <= 24
       OR deposited.unique_entity_count <= 24
       OR deposited.unique_physicality_count <= 24
       OR deposited.novel_entity_count <= 0
       OR deposited.novel_physicality_count <= 0
       OR deposited.entity_inserted <= 0
       OR deposited.physicality_inserted <= 0
       OR deposited.trajectory_vertex_inserted <= 0
       OR deposited.occurrence_inserted <= 0
       OR deposited.plan_count <= 0 THEN
        RAISE EXCEPTION
            'Highway registry did not traverse the canonical Unicode/AST/deposition lifecycle: %',
            deposited;
    END IF;
    IF active.status <> 0
       OR active.registry_version <> deposited.registry_version
       OR active.registry_fingerprint <> deposited.registry_fingerprint
       OR active.registry_epoch_id <> deposited.registry_epoch_id
       OR active.registry_epoch_fingerprint <> deposited.registry_epoch_fingerprint
       OR active.root_entity_id <> deposited.root_entity_id
       OR active.root_physicality_id <> deposited.root_physicality_id
       OR active.activation_sequence <> deposited.activation_sequence
       OR active.activation_receipt <> deposited.activation_receipt
       OR active.activation_fingerprint <> deposited.activation_fingerprint
       OR cardinality(active.kind_ids) <> 17
       OR cardinality(active.kind_name_entity_ids) <> 17
       OR cardinality(active.alias_kind_ids) <> 0
       OR cardinality(active.alias_name_entity_ids) <> 0
       OR cardinality(active.disposition_ids) <> 8
       OR cardinality(active.disposition_name_entity_ids) <> 8
       OR active.unicode_activation_epoch_id <> deposited.unicode_activation_epoch_id
       OR active.unicode_activation_epoch_fingerprint <>
          deposited.unicode_activation_epoch_fingerprint THEN
        RAISE EXCEPTION
            'Active Highway public readback differs from admitted canonical state: %',
            active;
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM laplace.entity
        WHERE entity_id = deposited.root_entity_id)
       OR NOT EXISTS (
        SELECT 1 FROM laplace.physicality
        WHERE physicality_id = deposited.root_physicality_id
          AND entity_id = deposited.root_entity_id)
       OR NOT EXISTS (
        SELECT 1 FROM laplace.execution_receipt
        WHERE receipt_id = deposited.isa_receipt
          AND opcode = 262146)
       OR NOT EXISTS (
        SELECT 1 FROM laplace.canonical_deposit_receipt
        WHERE stream_fingerprint = deposited.stream_fingerprint
          AND source_fingerprint = deposited.source_fingerprint
          AND recipe_fingerprint = deposited.recipe_fingerprint
          AND plan_sequence_fingerprint = deposited.plan_sequence_fingerprint)
       OR NOT EXISTS (
        SELECT 1 FROM laplace.highway_registry_generation
        WHERE activation_epoch_id = deposited.registry_epoch_id
          AND activation_epoch_fingerprint =
              deposited.registry_epoch_fingerprint
          AND root_entity_id = deposited.root_entity_id
          AND root_physicality_id = deposited.root_physicality_id
          AND registry_fingerprint = deposited.registry_fingerprint)
       OR (SELECT count(*) FROM laplace.highway_registry_kind_projection
           WHERE activation_epoch_id = deposited.registry_epoch_id
             AND activation_epoch_fingerprint =
                 deposited.registry_epoch_fingerprint) <> 17
       OR (SELECT count(*) FROM laplace.highway_registry_alias_projection
           WHERE activation_epoch_id = deposited.registry_epoch_id
             AND activation_epoch_fingerprint =
                 deposited.registry_epoch_fingerprint) <> 0
       OR (SELECT count(*) FROM laplace.highway_registry_disposition_projection
           WHERE activation_epoch_id = deposited.registry_epoch_id
             AND activation_epoch_fingerprint =
                 deposited.registry_epoch_fingerprint) <> 8
       OR NOT EXISTS (
        SELECT 1 FROM laplace.highway_registry_active_control
        WHERE singleton AND active_present AND sequence = 1
          AND activation_epoch_id = deposited.registry_epoch_id
          AND activation_epoch_fingerprint =
              deposited.registry_epoch_fingerprint
          AND admission_receipt = deposited.admission_receipt
          AND activation_receipt = deposited.activation_receipt
          AND activation_fingerprint = deposited.activation_fingerprint)
       OR NOT EXISTS (
        SELECT 1 FROM laplace.highway_registry_activation_event
        WHERE sequence = 1
          AND activation_epoch_id = deposited.registry_epoch_id
          AND activation_epoch_fingerprint =
              deposited.registry_epoch_fingerprint
          AND admission_receipt = deposited.admission_receipt
          AND activation_receipt = deposited.activation_receipt
          AND activation_fingerprint = deposited.activation_fingerprint)
       OR (SELECT count(*) FROM laplace.entity) <=
          before_counts.entity_count
       OR (SELECT count(*) FROM laplace.physicality) <=
          before_counts.physicality_count
       OR (SELECT COALESCE(sum(vertex_count), 0) FROM laplace.physicality) <=
          before_counts.vertex_count
       OR (SELECT count(*) FROM laplace.attestation
           WHERE attestation_kind = 1) <=
          before_counts.occurrence_count THEN
        RAISE EXCEPTION
            'Highway registry receipts do not identify durable canonical state';
    END IF;
    IF replay.root_entity_id <> deposited.root_entity_id
       OR replay.root_physicality_id <> deposited.root_physicality_id
       OR replay.admission_receipt <> deposited.admission_receipt
       OR replay.activation_receipt <> deposited.activation_receipt
       OR replay.activation_fingerprint <> deposited.activation_fingerprint
       OR replay.activation_sequence <> deposited.activation_sequence
       OR replay.effect_disposition <> deposited.effect_disposition
       OR replay.entity_inserted <> 0
       OR replay.physicality_inserted <> 0
       OR replay.trajectory_vertex_inserted <> 0
       OR replay.occurrence_inserted <> 0 THEN
        RAISE EXCEPTION
            'Highway registry replay changed identity, receipts, or durable rows: first=% replay=%',
            deposited, replay;
    END IF;
END
$contract$;

DO $contract$
DECLARE
    tier0 laplace.unicode_tier0_batch_result;
    reverse laplace.unicode_identity_reverse_batch_result;
    expected record;
BEGIN
    SELECT (laplace.unicode_tier0_resolve_batch(
        decode(repeat('42', 16), 'hex'),
        decode(repeat('43', 32), 'hex'),
        ARRAY[0, 65, 1114111])).*
    INTO STRICT tier0;

    SELECT
        array_agg(binding.entity_id::bytea ORDER BY binding.source_ordinal)
            AS entity_ids,
        array_agg(entity.identity_witness::bytea ORDER BY binding.source_ordinal)
            AS identity_preimage_fingerprints,
        array_agg(binding.physicality_id::bytea ORDER BY binding.source_ordinal)
            AS physicality_ids,
        array_agg(physicality.centroid_x ORDER BY binding.source_ordinal)
            AS coordinate_x,
        array_agg(physicality.centroid_y ORDER BY binding.source_ordinal)
            AS coordinate_y,
        array_agg(physicality.centroid_z ORDER BY binding.source_ordinal)
            AS coordinate_z,
        array_agg(physicality.centroid_m ORDER BY binding.source_ordinal)
            AS coordinate_m
    INTO STRICT expected
    FROM laplace.attestation AS binding
    JOIN laplace.entity AS entity ON entity.entity_id = binding.entity_id
    JOIN laplace.physicality AS physicality
      ON physicality.physicality_id = binding.physicality_id
    WHERE binding.source_fingerprint =
          (SELECT root_receipt FROM unicode_build_result)
      AND binding.attestation_kind = 3
      AND binding.source_ordinal IN (1, 66, 1114112);

    IF tier0.codepoint_positions <> ARRAY[0, 65, 1114111]
       OR tier0.found <> ARRAY[true, true, true]
       OR tier0.entity_ids <> expected.entity_ids
       OR tier0.identity_preimage_fingerprints <>
          expected.identity_preimage_fingerprints
       OR tier0.physicality_ids <> expected.physicality_ids
       OR tier0.coordinate_x <> expected.coordinate_x
       OR tier0.coordinate_y <> expected.coordinate_y
       OR tier0.coordinate_z <> expected.coordinate_z
       OR tier0.coordinate_m <> expected.coordinate_m
       OR cardinality(tier0.placement_ranks) <> 3
       OR cardinality(tier0.position_classes) <> 3
       OR cardinality(tier0.lup_v1_bytes) <> 3
       OR cardinality(tier0.hilbert_keys) <> 3
       OR EXISTS (
            SELECT 1 FROM unnest(tier0.placement_ranks) AS rank(value)
            WHERE rank.value < 0 OR rank.value > 1114111)
       OR EXISTS (
            SELECT 1 FROM unnest(tier0.position_classes) AS class(value)
            WHERE class.value < 0 OR class.value > 4)
       OR EXISTS (
            SELECT 1 FROM unnest(tier0.lup_v1_bytes) AS lup(value)
            WHERE octet_length(lup.value) NOT BETWEEN 1 AND 4)
       OR EXISTS (
            SELECT 1 FROM unnest(tier0.hilbert_keys) AS hilbert(value)
            WHERE octet_length(hilbert.value) <> 16)
       OR tier0.activation_epoch_id <> decode(repeat('42', 16), 'hex')
       OR tier0.activation_epoch_fingerprint <> decode(repeat('43', 32), 'hex')
       OR cardinality(tier0.geometry_epochs) <> 3
       OR cardinality(tier0.encoded_records) <> 3
       OR EXISTS (
            SELECT 1
            FROM unnest(tier0.encoded_records) AS encoded(record)
            WHERE octet_length(encoded.record) <= 196) THEN
        RAISE EXCEPTION
            'production Unicode Tier-0 batch access diverged from deposited state';
    END IF;

    SELECT (laplace.unicode_identity_reverse_resolve_batch(
        tier0.activation_epoch_id,
        tier0.activation_epoch_fingerprint,
        tier0.entity_ids,
        tier0.identity_preimage_fingerprints)).*
    INTO STRICT reverse;
    IF reverse.content_ids <> tier0.entity_ids
       OR reverse.identity_preimage_fingerprints <>
          tier0.identity_preimage_fingerprints
       OR reverse.found <> ARRAY[true, true, true]
       OR reverse.codepoint_positions <> ARRAY[0, 65, 1114111]
       OR reverse.activation_epoch_id <> tier0.activation_epoch_id
       OR reverse.activation_epoch_fingerprint <>
          tier0.activation_epoch_fingerprint THEN
        RAISE EXCEPTION
            'production Unicode identity reverse access does not invert Tier-0';
    END IF;

    BEGIN
        PERFORM laplace.unicode_tier0_resolve_batch(
            decode(repeat('41', 16), 'hex'),
            decode(repeat('43', 32), 'hex'),
            ARRAY[65]);
        RAISE EXCEPTION 'stale Unicode access epoch was accepted';
    EXCEPTION
        WHEN object_not_in_prerequisite_state THEN NULL;
    END;
END
$contract$;

EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT)
SELECT (source_ordinal - 1)::integer AS codepoint_position,
       entity_id, physicality_id
FROM laplace.attestation
WHERE source_fingerprint = (SELECT root_receipt FROM unicode_build_result)
  AND attestation_kind = 3
  AND source_ordinal IN (1, 66, 1114112)
ORDER BY source_ordinal;
