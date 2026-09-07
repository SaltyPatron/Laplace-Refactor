\set ON_ERROR_STOP on
BEGIN;
SET LOCAL synchronous_commit=on;
SET LOCAL statement_timeout=:'timeout_ms';
SET LOCAL lock_timeout='30s';

CREATE TEMP TABLE unicode_activation_input ON COMMIT PRESERVE ROWS AS
SELECT :'activation_request'::jsonb AS request,
       :'activation_contract'::jsonb AS contract;

DO $initial$
DECLARE active laplace.perfcache_active_control%ROWTYPE;
BEGIN
    SELECT * INTO STRICT active FROM laplace.perfcache_active_control
    WHERE singleton FOR UPDATE;
    IF active.sequence<>0 OR active.active_present
       OR active.activation_epoch_id<>decode(repeat('00',16),'hex')
       OR active.epoch_fingerprint<>decode(repeat('00',32),'hex')
       OR EXISTS (SELECT FROM laplace.unicode_root_generation)
       OR EXISTS (SELECT FROM laplace.unicode_root_deposit_receipt)
       OR EXISTS (SELECT FROM laplace.entity)
       OR EXISTS (SELECT FROM laplace.physicality)
       OR EXISTS (SELECT FROM laplace.attestation)
       OR EXISTS (SELECT FROM laplace.consensus)
       OR EXISTS (SELECT FROM laplace.perfcache_generation)
       OR EXISTS (SELECT FROM laplace.perfcache_activation_event) THEN
        RAISE EXCEPTION 'Initial Unicode activation requires an empty product state';
    END IF;
END
$initial$;

-- The sole construction operation is the native C entry point. Its native
-- sibling sinks publish the durable root and the tier-zero/reverse cache pair.
CREATE TEMP TABLE unicode_product_build ON COMMIT PRESERVE ROWS AS
SELECT result.* FROM unicode_activation_input AS input
CROSS JOIN LATERAL laplace.unicode_root_build_and_activate(
    :execution_context,
    input.request->>'source_root', input.request->>'spool_directory',
    input.request->>'tier0_path', input.request->>'reverse_path',
    decode(input.request->>'activation_epoch_id','hex'),
    decode(input.request->>'activation_epoch_fingerprint','hex'),
    0,false,decode(repeat('00',16),'hex'),decode(repeat('00',32),'hex'),
    (input.contract->'operation'->>'maximum_batch_bytes')::bigint
) AS result;

DO $closure$
DECLARE
    build unicode_product_build%ROWTYPE;
    active laplace.perfcache_active_control%ROWTYPE;
    request jsonb;
    expected jsonb;
    actual jsonb;
    field text;
BEGIN
    SELECT * INTO STRICT build FROM unicode_product_build;
    SELECT * INTO STRICT active FROM laplace.perfcache_active_control WHERE singleton;
    SELECT input.request, input.contract->'expected_result'
    INTO STRICT request, expected FROM unicode_activation_input AS input;
    actual := to_jsonb(build);
    FOREACH field IN ARRAY ARRAY[
        'total_frame_count','entity_count','physicality_count','atom_count',
        'ducet_position_count','ducet_contraction_count','normalization_composition_count',
        'tier0_artifact_bytes','reverse_artifact_bytes','perfcache_artifact_count','perfcache_dependency_count'
    ] LOOP
        IF actual->field IS DISTINCT FROM expected->field THEN
            RAISE EXCEPTION 'Native Unicode result differs for %', field;
        END IF;
    END LOOP;
    IF build.activation_epoch_id IS DISTINCT FROM decode(request->>'activation_epoch_id','hex')
       OR build.activation_epoch_fingerprint IS DISTINCT FROM decode(request->>'activation_epoch_fingerprint','hex')
       OR build.tier0_artifact_digest IS DISTINCT FROM decode(expected->>'tier0_artifact_digest','hex')
       OR build.reverse_artifact_digest IS DISTINCT FROM decode(expected->>'reverse_artifact_digest','hex')
       OR build.plan_manifest_fingerprint IS DISTINCT FROM decode(expected->>'plan_manifest_fingerprint','hex')
       OR build.reverse_dependency_module_id IS DISTINCT FROM decode(expected->>'reverse_dependency_module_id','hex')
       OR build.reverse_dependency_artifact_digest IS DISTINCT FROM build.tier0_artifact_digest
       OR active.sequence<>1 OR NOT active.active_present
       OR active.activation_epoch_id IS DISTINCT FROM build.activation_epoch_id
       OR active.epoch_fingerprint IS DISTINCT FROM build.activation_epoch_fingerprint
       OR (SELECT count(*) FROM laplace.entity)<>(expected->>'entity_count')::numeric
       OR (SELECT count(*) FROM laplace.physicality)<>(expected->>'physicality_count')::numeric
       OR (SELECT count(*) FROM laplace.attestation WHERE source_fingerprint=build.root_receipt AND attestation_kind=3)<>(expected->>'atom_count')::numeric
       OR (SELECT count(*) FROM laplace.unicode_ducet_position)<>(expected->>'ducet_position_count')::numeric
       OR (SELECT count(*) FROM laplace.unicode_ducet_contraction)<>(expected->>'ducet_contraction_count')::numeric
       OR (SELECT count(*) FROM laplace.unicode_normalization_composition)<>(expected->>'normalization_composition_count')::numeric
       OR NOT EXISTS (SELECT FROM laplace.unicode_root_generation AS generation
           WHERE generation.root_receipt=build.root_receipt
             AND generation.plan_manifest_fingerprint=build.plan_manifest_fingerprint
             AND generation.postgresql_artifact_fingerprint=build.postgresql_artifact_fingerprint)
       OR NOT EXISTS (SELECT FROM laplace.unicode_root_deposit_receipt AS deposit
           WHERE deposit.root_receipt=build.root_receipt AND deposit.producer_receipt=build.producer_receipt
             AND deposit.staged_stream_receipt=build.staged_stream_receipt AND deposit.admission_receipt=build.admission_receipt) THEN
        RAISE EXCEPTION 'Native Unicode activation differs from its declared contract';
    END IF;
END
$closure$;
COMMIT;

BEGIN READ ONLY;
SET LOCAL statement_timeout=:'timeout_ms';
SELECT to_jsonb(build) FROM unicode_product_build AS build
JOIN laplace.unicode_root_generation AS generation USING(root_receipt)
JOIN laplace.perfcache_active_control AS active
  ON active.singleton AND active.active_present
 AND active.activation_epoch_id=build.activation_epoch_id
 AND active.epoch_fingerprint=build.activation_epoch_fingerprint;
COMMIT;
