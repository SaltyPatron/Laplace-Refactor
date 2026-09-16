-- Run after unicode_root_contract.sql in its disposable PostgreSQL backend.
-- The real Unicode/Highway activation, prompt admission, persistence, native
-- candidate providers, firmware and materialization owners remain in control.
CREATE TEMP TABLE product_cognition_program AS
SELECT decode(:'product_cognition_program_id','hex') AS id,
       decode(:'product_cognition_image','hex') AS image;

CREATE FUNCTION pg_temp.product_cognition_context(selected_program bytea)
RETURNS laplace.execution_context
LANGUAGE plpgsql STABLE PARALLEL UNSAFE
AS $context$
DECLARE result laplace.execution_context;
BEGIN
    result := pg_temp.highway_registry_read_context();
    result.epochs[5] := selected_program;
    result.flags := @LAPLACE_FRAMEWORK_CONTEXT_BOOTSTRAP@;
    RETURN result;
END
$context$;

CREATE TEMP TABLE product_cognition_input AS
SELECT program.id, program.image, authority.context,
       ROW(
           (authority.context).epochs[1], (authority.context).epochs[2],
           (authority.context).epochs[3], (authority.context).epochs[3],
           (authority.context).epochs[4], (authority.context).epochs[10],
           (authority.context).epochs[7], (authority.context).epochs[6],
           (authority.context).epochs[1], (authority.context).epochs[9],
           0::numeric,0,1::numeric,1048576::numeric
       )::laplace.cognition_prompt_scope AS scope,
       ROW(
           program.id, (authority.context).epochs[4],
           (authority.context).epochs[6], decode(repeat('00',32),'hex'),false,
           ROW(64::numeric,256::numeric,128::numeric,64::numeric,1048576::numeric,
               64::numeric,64::numeric,64::numeric,8,1,8,64)
               ::laplace.cognition_observation_search_budget,
           ROW(16::numeric,32::numeric,32::numeric,32::numeric,16::numeric,8192::numeric,
               128::numeric,128::numeric,4,4)
               ::laplace.cognition_observation_forward_limits,
           ROW(64::numeric,64::numeric,4096::numeric,16,1)
               ::laplace.cognition_firmware_materialization_limits,
           4096::numeric,8192::numeric,8,1
       )::laplace.cognition_firmware_product_request AS request
FROM product_cognition_program program
CROSS JOIN LATERAL (
    SELECT pg_temp.product_cognition_context(program.id) AS context
) authority;

CREATE FUNCTION pg_temp.product_evidence_counts()
RETURNS jsonb LANGUAGE SQL STABLE PARALLEL UNSAFE
AS $counts$
    SELECT jsonb_build_object(
        'nodes',(SELECT count(*) FROM laplace.evidence_node),
        'dependences',(SELECT count(*) FROM laplace.evidence_dependence),
        'root_projections',(SELECT count(*) FROM laplace.evidence_root_projection))
$counts$;

CREATE TEMP TABLE product_cognition_evidence_before AS
SELECT pg_temp.product_evidence_counts() AS counts;

CREATE FUNCTION pg_temp.require_product_cognition(
    actual laplace.cognition_firmware_product_result,
    selected_program bytea, require_durable boolean)
RETURNS void LANGUAGE plpgsql VOLATILE PARALLEL UNSAFE
AS $check$
BEGIN
    IF actual.status IS DISTINCT FROM 0
       OR actual.program_id IS DISTINCT FROM selected_program
       OR actual.output IS DISTINCT FROM decode('41','hex')
       OR actual.completed_steps IS DISTINCT FROM 2
       OR actual.emitted_parts IS DISTINCT FROM 1
       OR actual.layer_count IS DISTINCT FROM 1::numeric
       OR actual.provider_call_count IS DISTINCT FROM 2::numeric
       OR actual.physical_provider_batches IS DISTINCT FROM 1::numeric
       OR actual.physical_provider_rows IS NULL
       OR actual.physical_provider_rows < 0
       OR (require_durable AND actual.physical_provider_rows = 0)
       OR actual.semantic_rows_examined IS DISTINCT FROM 0::numeric
       OR actual.semantic_database_operations IS DISTINCT FROM 0::numeric
       OR actual.materialization_resolved_nodes IS DISTINCT FROM 1::numeric
       OR actual.materialization_trajectory_reads IS DISTINCT FROM 0::numeric
       OR actual.materialization_trajectory_bytes IS DISTINCT FROM 0::numeric
       OR actual.materialization_database_operations IS NULL
       OR actual.materialization_database_operations <= 0
       OR octet_length(actual.trunk_entity_id) IS DISTINCT FROM 16
       OR octet_length(actual.prompt_admission_receipt_id) IS DISTINCT FROM 32
       OR actual.prompt_admission_receipt_id = decode(repeat('00',32),'hex')
       OR octet_length(actual.execution_receipt_id) IS DISTINCT FROM 32
       OR actual.execution_receipt_id = decode(repeat('00',32),'hex')
       OR octet_length(actual.trace_fingerprint) IS DISTINCT FROM 32
       OR octet_length(actual.next_checkpoint_fingerprint) IS DISTINCT FROM 32
       OR octet_length(actual.checkpoint) IS NULL
       OR octet_length(actual.checkpoint) = 0 THEN
        RAISE EXCEPTION 'real product retained-prompt cognition did not complete: %',
            row_to_json(actual);
    END IF;
END
$check$;

-- Snapshot the actual stream-receipt owner before this product call. The
-- product returns a producer receipt; it does not write the separate
-- composition_execution_receipt table or expose its stream receipt ID.
CREATE TEMP TABLE product_cognition_deposit_before AS
SELECT receipt_id FROM laplace.canonical_deposit_receipt;

-- No expected answer is supplied to the product. 'A' is checked only after
-- executing the independently compiled constituent/emit program against 'AA'.
CREATE TEMP TABLE product_cognition_first AS
SELECT result.*
FROM product_cognition_input input
CROSS JOIN LATERAL laplace.cognition_firmware_execute_product(
    input.context,input.image,'AA',input.scope,input.request,''::bytea,8388608::bigint) result;

SELECT pg_temp.require_product_cognition(ROW(first.*)::laplace.cognition_firmware_product_result,program.id,false)
FROM product_cognition_first first CROSS JOIN product_cognition_program program;

CREATE TEMP TABLE product_cognition_root_before AS
SELECT result.trunk_entity_id AS id,
       (SELECT pg_catalog.record_send(e) FROM laplace.entity e
        WHERE e.entity_id=result.trunk_entity_id) AS entity,
       (SELECT array_agg(pg_catalog.record_send(p) ORDER BY p.physicality_id)
        FROM laplace.physicality p WHERE p.entity_id=result.trunk_entity_id) AS physicalities
FROM product_cognition_first result;

CREATE TEMP TABLE product_cognition_new_deposit AS
SELECT deposit.*
FROM laplace.canonical_deposit_receipt deposit
WHERE NOT EXISTS (
    SELECT 1 FROM product_cognition_deposit_before before_state
    WHERE before_state.receipt_id=deposit.receipt_id);

CREATE TEMP TABLE product_cognition_deposit_after_first AS
SELECT deposit.receipt_id,pg_catalog.record_send(deposit) AS body
FROM laplace.canonical_deposit_receipt deposit;

DO $stored$
DECLARE result product_cognition_first%ROWTYPE; atom bytea;
        records bigint; new_receipts bigint;
BEGIN
    SELECT * INTO STRICT result FROM product_cognition_first;
    SELECT ((laplace.identity_codepoint_calculate_batch(
        input.context,ARRAY[65])).entity_ids)[1]
        INTO STRICT atom FROM product_cognition_input input;
    IF octet_length(atom) IS DISTINCT FROM 16 THEN
        RAISE EXCEPTION 'native atom identity was not returned for the retained prompt check';
    END IF;
    SELECT count(*) INTO records FROM laplace.physicality
        WHERE entity_id=result.trunk_entity_id;
    IF records < 1 OR records > 16 THEN
        RAISE EXCEPTION 'product root physicality count is outside the fixture bound: %',records;
    END IF;
    IF NOT EXISTS(SELECT 1 FROM product_cognition_root_before WHERE entity IS NOT NULL) THEN
        RAISE EXCEPTION 'product root canonical entity is absent';
    END IF;
    IF NOT EXISTS(
        SELECT 1 FROM laplace.physicality
        WHERE entity_id=result.trunk_entity_id AND physicality_type=1
          AND logical_count=2 AND vertex_count>0
          AND laplace.trajectory_entity_ids(trajectory)=ARRAY[atom]) THEN
        RAISE EXCEPTION 'product root has no native two-A composition physicality';
    END IF;

    SELECT count(*) INTO new_receipts FROM product_cognition_new_deposit;
    IF result.prompt_persistence_receipt_id IS NULL THEN
        -- Exact presence can require no publication even on this fixture's
        -- first call. Canonical readback above still has to succeed.
        IF new_receipts <> 0 THEN
            RAISE EXCEPTION 'product reported reuse but added canonical deposit receipts: %',new_receipts;
        END IF;
    ELSE
        IF octet_length(result.prompt_persistence_receipt_id) <> 32
           OR result.prompt_persistence_receipt_id=decode(repeat('00',32),'hex') THEN
            RAISE EXCEPTION 'product returned a malformed persistence producer receipt';
        END IF;
        IF new_receipts <> 1 THEN
            RAISE EXCEPTION 'product publication did not add one canonical deposit receipt: %',new_receipts;
        END IF;
        IF NOT EXISTS(
            SELECT 1 FROM product_cognition_new_deposit deposit
            CROSS JOIN product_cognition_input input
            WHERE deposit.source_fingerprint=(input.scope).source_fingerprint
              AND deposit.recipe_fingerprint=(input.scope).calculation_recipe_fingerprint
              AND deposit.status=0 AND deposit.physicality_count>0
              AND deposit.occurrence_count=0) THEN
            RAISE EXCEPTION 'product canonical deposit source, recipe, status or native counts differ';
        END IF;
    END IF;
END
$stored$;

-- A separate statement makes the retained prompt visible to the durable
-- provider independently of same-statement snapshot behavior.
CREATE TEMP TABLE product_cognition_warm AS
SELECT result.*
FROM product_cognition_input input
CROSS JOIN LATERAL laplace.cognition_firmware_execute_product(
    input.context,input.image,'AA',input.scope,input.request,''::bytea,8388608::bigint) result;

SELECT pg_temp.require_product_cognition(ROW(warm.*)::laplace.cognition_firmware_product_result,program.id,true)
FROM product_cognition_warm warm CROSS JOIN product_cognition_program program;

CREATE TEMP TABLE product_cognition_replay AS
SELECT result.*
FROM product_cognition_input input
CROSS JOIN LATERAL laplace.cognition_firmware_execute_product(
    input.context,input.image,'AA',input.scope,input.request,''::bytea,8388608::bigint) result;

SELECT pg_temp.require_product_cognition(ROW(replay.*)::laplace.cognition_firmware_product_result,program.id,true)
FROM product_cognition_replay replay CROSS JOIN product_cognition_program program;

DO $replay$
DECLARE before_state product_cognition_root_before%ROWTYPE;
        warm product_cognition_warm%ROWTYPE; replay product_cognition_replay%ROWTYPE;
        current_entity bytea; current_physicalities bytea[];
BEGIN
    SELECT * INTO STRICT before_state FROM product_cognition_root_before;
    SELECT * INTO STRICT warm FROM product_cognition_warm;
    SELECT * INTO STRICT replay FROM product_cognition_replay;
    SELECT pg_catalog.record_send(e) INTO STRICT current_entity FROM laplace.entity e WHERE entity_id=before_state.id;
    SELECT array_agg(pg_catalog.record_send(p) ORDER BY physicality_id) INTO current_physicalities
        FROM laplace.physicality p WHERE entity_id=before_state.id;
    IF warm.trunk_entity_id IS DISTINCT FROM before_state.id
       OR replay.trunk_entity_id IS DISTINCT FROM before_state.id
       OR current_entity IS DISTINCT FROM before_state.entity
       OR current_physicalities IS DISTINCT FROM before_state.physicalities
       OR to_jsonb(warm) IS DISTINCT FROM to_jsonb(replay)
       OR pg_temp.product_evidence_counts() IS DISTINCT FROM
          (SELECT counts FROM product_cognition_evidence_before) THEN
        RAISE EXCEPTION 'durable product replay changed canonical prompt, execution, or evidence counts';
    END IF;
    IF warm.prompt_persistence_receipt_id IS NOT NULL
       OR replay.prompt_persistence_receipt_id IS NOT NULL THEN
        RAISE EXCEPTION 'warm product replay requested publication for already-present canonical state';
    END IF;
    IF EXISTS (
        (SELECT deposit.receipt_id,pg_catalog.record_send(deposit)
         FROM laplace.canonical_deposit_receipt deposit
         EXCEPT SELECT receipt_id,body FROM product_cognition_deposit_after_first)
        UNION ALL
        (SELECT receipt_id,body FROM product_cognition_deposit_after_first
         EXCEPT SELECT deposit.receipt_id,pg_catalog.record_send(deposit)
         FROM laplace.canonical_deposit_receipt deposit)) THEN
        RAISE EXCEPTION 'warm product replay changed retained canonical deposit receipts';
    END IF;
END
$replay$;

\pset format unaligned
\pset tuples_only on
SELECT 'LAPLACE_QA_RECEIPT product_cognition_retained_prompt ' || jsonb_build_object(
    'schema','laplace.product-cognition-retained-prompt-test/v1',
    'product_calls',3,'durable_replay_calls',2,'output_hex',encode(warm.output,'hex'),
    'canonical_root_unchanged',true,'exact_warm_replay',true,
    'warm_publication_reused',true,
    'first_persistence_producer_receipt_id',encode(first.prompt_persistence_receipt_id,'hex'),
    'first_new_canonical_deposit_receipt_ids',
        (SELECT coalesce(jsonb_agg(encode(receipt_id,'hex') ORDER BY receipt_id),'[]'::jsonb)
         FROM product_cognition_new_deposit),
    'evidence_lineage_counts_unchanged',true,
    'trunk_entity_id',encode(warm.trunk_entity_id,'hex'),
    'prompt_admission_receipt_id',encode(warm.prompt_admission_receipt_id,'hex'),
    'execution_receipt_id',encode(warm.execution_receipt_id,'hex'),
    'first_physical_provider_rows',first.physical_provider_rows,
    'replay_physical_provider_rows',warm.physical_provider_rows,
    'replay_physical_provider_batches',warm.physical_provider_batches,
    'replay_materialization_nodes',warm.materialization_resolved_nodes,
    'replay_materialization_database_operations',warm.materialization_database_operations,
    'completed_steps',warm.completed_steps,
    'evidence_lineage_counts',(SELECT counts FROM product_cognition_evidence_before)
)::text
FROM product_cognition_warm warm CROSS JOIN product_cognition_first first;
\pset tuples_only off
\pset format aligned
