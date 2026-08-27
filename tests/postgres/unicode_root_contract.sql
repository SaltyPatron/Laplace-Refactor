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
    33554432,
    32768
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
          decode('74499f893b359982962c3dbeb5572965c8d89f22609f47c41dbd7143d08034ab', 'hex')
       OR build.activation_epoch_id <> decode(repeat('42', 16), 'hex')
       OR build.activation_epoch_fingerprint <> decode(repeat('43', 32), 'hex')
       OR build.total_frame_count <> 2230150
       OR build.total_encoded_bytes <= 0
       OR build.batch_count <= 1
       OR build.plan_count > build.batch_count * 12 + 5
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
              'unicode_atom_binding', 'unicode_ducet_position',
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
    (SELECT count(*) FROM laplace.canonical_entity) AS entity_count,
    (SELECT count(*) FROM laplace.physicality) AS physicality_count,
    (SELECT count(*) FROM laplace.unicode_atom_binding) AS atom_count,
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
        ('canonical_entity'), ('physicality'), ('unicode_atom_binding'),
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
    SELECT array_agg(entity_id::bytea ORDER BY codepoint_position) AS entity_ids
    FROM laplace.unicode_atom_binding
    WHERE codepoint_position IN (0, 65, 1114111)
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
        array_agg(binding.placement_rank ORDER BY binding.codepoint_position)
            AS placement_ranks,
        array_agg(binding.position_class ORDER BY binding.codepoint_position)
            AS position_classes,
        array_agg(binding.lup_v1_bytes ORDER BY binding.codepoint_position)
            AS lup_v1_bytes,
        array_agg(binding.entity_id::bytea ORDER BY binding.codepoint_position)
            AS entity_ids,
        array_agg(binding.identity_preimage_fingerprint::bytea
                  ORDER BY binding.codepoint_position)
            AS identity_preimage_fingerprints,
        array_agg(binding.physicality_id::bytea ORDER BY binding.codepoint_position)
            AS physicality_ids,
        array_agg(binding.coordinate_x ORDER BY binding.codepoint_position)
            AS coordinate_x,
        array_agg(binding.coordinate_y ORDER BY binding.codepoint_position)
            AS coordinate_y,
        array_agg(binding.coordinate_z ORDER BY binding.codepoint_position)
            AS coordinate_z,
        array_agg(binding.coordinate_m ORDER BY binding.codepoint_position)
            AS coordinate_m,
        array_agg(binding.hilbert_key ORDER BY binding.codepoint_position)
            AS hilbert_keys
    INTO STRICT expected
    FROM laplace.unicode_atom_binding AS binding
    WHERE binding.codepoint_position IN (0, 65, 1114111);

    IF tier0.codepoint_positions <> ARRAY[0, 65, 1114111]
       OR tier0.found <> ARRAY[true, true, true]
       OR tier0.placement_ranks <> expected.placement_ranks
       OR tier0.position_classes <> expected.position_classes
       OR tier0.lup_v1_bytes <> expected.lup_v1_bytes
       OR tier0.entity_ids <> expected.entity_ids
       OR tier0.identity_preimage_fingerprints <>
          expected.identity_preimage_fingerprints
       OR tier0.physicality_ids <> expected.physicality_ids
       OR tier0.coordinate_x <> expected.coordinate_x
       OR tier0.coordinate_y <> expected.coordinate_y
       OR tier0.coordinate_z <> expected.coordinate_z
       OR tier0.coordinate_m <> expected.coordinate_m
       OR tier0.hilbert_keys <> expected.hilbert_keys
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
SELECT codepoint_position, entity_id, physicality_id
FROM laplace.unicode_atom_binding
WHERE root_receipt = (SELECT root_receipt FROM unicode_build_result)
  AND codepoint_position IN (0, 65, 1114111)
ORDER BY codepoint_position;
