CREATE EXTENSION laplace;

CREATE FUNCTION pg_temp.world_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('01', 32), 'hex'), decode(repeat('02', 32), 'hex'),
            decode(repeat('03', 32), 'hex'), decode(repeat('04', 32), 'hex'),
            decode(repeat('05', 32), 'hex'), decode(repeat('06', 32), 'hex'),
            decode(repeat('07', 32), 'hex'), decode(repeat('08', 32), 'hex'),
            decode(repeat('09', 32), 'hex'), decode(repeat('0a', 32), 'hex')
        ],
        decode(repeat('a0', 32), 'hex'),
        67108864::bigint, 4, 1, 1023::bigint,
        @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        0
    )::laplace.execution_context
$context$;

CREATE TEMP TABLE world_expected (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    entity_a bytea NOT NULL,
    witness_a bytea NOT NULL,
    entity_b bytea NOT NULL,
    witness_b bytea NOT NULL,
    profile_id bytea NOT NULL,
    occurrence_id bytea NOT NULL,
    evidence_node bytea NOT NULL,
    evidence_source bytea NOT NULL,
    evidence_context bytea NOT NULL,
    testimony_id bytea NOT NULL,
    testimony_trust bytea NOT NULL,
    testimony_outcome bytea NOT NULL
);

INSERT INTO world_expected VALUES (
    true,
    decode(:'persistence_entity_a', 'hex'),
    decode(:'persistence_entity_a_witness', 'hex'),
    decode(:'persistence_entity_b', 'hex'),
    decode(:'persistence_entity_b_witness', 'hex'),
    decode(:'world_profile_id', 'hex'),
    decode(:'world_occurrence_id', 'hex'),
    decode(:'world_evidence_node', 'hex'),
    decode(:'world_evidence_source', 'hex'),
    decode(:'world_evidence_context', 'hex'),
    decode(:'world_testimony_id', 'hex'),
    decode(:'world_testimony_trust', 'hex'),
    decode(:'world_testimony_outcome', 'hex')
);

DO $world$
DECLARE
    expected world_expected%ROWTYPE;
    entity_a bytea;
    witness_a bytea;
    entity_b bytea;
    witness_b bytea;
    profile_id bytea;
    expected_occurrence bytea;
    evidence_node bytea;
    evidence_source bytea;
    evidence_context bytea;
    testimony_id bytea;
    testimony_trust bytea;
    testimony_outcome bytea;
    persisted_occurrence bytea;
    recipe bytea := decode(repeat('a1', 32), 'hex');
    profile laplace.source_profile_manifest;
    profile_result laplace.source_profile_result;
    composition_result laplace.composition_deposit_result;
    lineage_result laplace.evidence_lineage_result;
    testimony_result laplace.evidence_testimony_result;
    admission_result laplace.world_admission_result;
    replay_result laplace.world_admission_result;
    request laplace.world_admission_request;
    admission_xmin xid;
    admission_ctid tid;
    world_rows bigint;
    world_receipts bigint;
BEGIN
    SELECT * INTO STRICT expected FROM world_expected;
    entity_a := expected.entity_a;
    witness_a := expected.witness_a;
    entity_b := expected.entity_b;
    witness_b := expected.witness_b;
    profile_id := expected.profile_id;
    expected_occurrence := expected.occurrence_id;
    evidence_node := expected.evidence_node;
    evidence_source := expected.evidence_source;
    evidence_context := expected.evidence_context;
    testimony_id := expected.testimony_id;
    testimony_trust := expected.testimony_trust;
    testimony_outcome := expected.testimony_outcome;
    profile := ROW(
        profile_id, 17,
        decode('101112131415161718191a1b1c1d1e1f','hex'),
        decode('303132333435363738393a3b3c3d3e3f','hex'),
        decode('505152535455565758595a5b5c5d5e5f','hex'),
        decode('707172737475767778797a7b7c7d7e7f','hex'),
        1::numeric,
        decode(repeat('a0',32),'hex'), decode(repeat('a1',32),'hex'),
        decode(repeat('a2',32),'hex'), decode(repeat('a3',32),'hex'),
        recipe, decode(repeat('a5',32),'hex'),
        decode(repeat('a6',32),'hex'), decode(repeat('a7',32),'hex'),
        decode(repeat('a8',32),'hex'), decode(repeat('a9',32),'hex'),
        decode(repeat('aa',32),'hex'), decode(repeat('d0',32),'hex'),
        64::numeric, 1::numeric, 1::numeric, 1::numeric, 1::numeric,
        2::numeric, 3::numeric, 2::numeric, 0::numeric, 0::numeric,
        2::numeric, 1::numeric, 0::numeric, 0::numeric, 0::numeric,
        0::numeric, 1::numeric,
        1::numeric, 0::numeric, 0::numeric, 0::numeric, 0::numeric,
        0::numeric, 0::numeric, 0::numeric, 0::numeric, 0::numeric,
        1::numeric, 0::numeric, 62208::numeric, 1, 0
    )::laplace.source_profile_manifest;
    profile_result := laplace.source_profile_validate_batch(
        pg_temp.world_context(), ARRAY[profile]);
    IF profile_result.profile_ids <> ARRAY[profile_id]
       OR profile_result.profile_count <> 1
       OR profile_result.closure_subject_count <> 1 THEN
        RAISE EXCEPTION 'world source profile did not persist as one exact boundary';
    END IF;

    composition_result := laplace.composition_deposit_batch(
        pg_temp.world_context(),
        decode(repeat('91', 32), 'hex'), recipe,
        ARRAY[
            ROW(entity_a, witness_a, decode(repeat('e1',32),'hex'),
                1.0::double precision, 0.0::double precision,
                0.0::double precision, 0.0::double precision,
                0::bigint, 0::smallint, false)
                ::laplace.composition_known_entity_record,
            ROW(entity_b, witness_b, decode(repeat('e2',32),'hex'),
                0.0::double precision, 1.0::double precision,
                0.0::double precision, 0.0::double precision,
                0::bigint, 0::smallint, false)
                ::laplace.composition_known_entity_record
        ],
        ARRAY[
            ROW(0::numeric,1::numeric,0::bigint,1,0)
                ::laplace.composition_operand_record,
            ROW(1::numeric,1::numeric,0::bigint,1,0)
                ::laplace.composition_operand_record
        ],
        ARRAY[
            ROW(0::numeric,2::numeric,1::numeric,1,0,
                decode(repeat('b1',32),'hex'),
                decode(repeat('c1',32),'hex'),
                decode(repeat('d1',32),'hex'))
                ::laplace.composition_request_record
        ],
        256::numeric);
    SELECT m.occurrence_id INTO persisted_occurrence
    FROM laplace.composition_execution_occurrence_member m
    WHERE m.working_set_receipt=composition_result.working_set_receipt
      AND m.member_ordinal=1;
    IF composition_result.status <> 0
       OR composition_result.occurrence_count <> 1
       OR composition_result.logical_occurrence_count <> 2
       OR NOT EXISTS (
            SELECT FROM laplace.composition_execution_occurrence_member m
            WHERE m.working_set_receipt=composition_result.working_set_receipt
              AND m.occurrence_id=expected_occurrence
              AND m.member_ordinal=1) THEN
        RAISE EXCEPTION 'composition did not publish its exact durable occurrence membership'
            USING DETAIL=format(
                'expected=%s persisted=%s status=%s occurrence_count=%s logical_occurrence_count=%s',
                encode(expected_occurrence,'hex'),
                COALESCE(encode(persisted_occurrence,'hex'),'<missing>'),
                composition_result.status,
                composition_result.occurrence_count,
                composition_result.logical_occurrence_count);
    END IF;

    lineage_result := laplace.evidence_record_lineage_batch(
        pg_temp.world_context(),
        ARRAY[ROW(
            evidence_node, composition_result.result_entity_ids[1],
            expected_occurrence, evidence_source, evidence_context,
            decode(repeat('00',32),'hex'),
            1::numeric, 1, 1, 0, 0
        )::laplace.evidence_lineage_record],
        1::numeric);
    IF lineage_result.node_ids <> ARRAY[evidence_node]
       OR lineage_result.node_count <> 1 THEN
        RAISE EXCEPTION 'world evidence lineage did not persist one exact node';
    END IF;

    testimony_result := laplace.evidence_record_testimony_batch(
        pg_temp.world_context(),
        ARRAY[ROW(
            testimony_id, evidence_node, profile_id, recipe,
            testimony_trust, testimony_outcome,
            0::numeric, 1::numeric, 1::numeric, 1, 5, 1, 0
        )::laplace.evidence_testimony_record]);
    IF testimony_result.testimony_ids <> ARRAY[testimony_id]
       OR testimony_result.testimony_count <> 1 THEN
        RAISE EXCEPTION 'world testimony did not persist one exact witnessed claim';
    END IF;

    request := ROW(
        profile_id,
        profile_result.source_profile_receipt_id,
        recipe,
        composition_result.working_set_receipt,
        lineage_result.lineage_receipt_id,
        testimony_result.testimony_receipt_id
    )::laplace.world_admission_request;
    admission_result := laplace.world_admission_close_batch(
        pg_temp.world_context(), ARRAY[request]);
    IF admission_result.admission_count <> 1
       OR admission_result.occurrence_count <> 2
       OR admission_result.claim_count <> 1
       OR admission_result.evidence_node_count <> 1
       OR admission_result.testimony_count <> 1
       OR admission_result.closure_subject_count <> 1
       OR cardinality(admission_result.admission_ids) <> 1
       OR NOT EXISTS (
            SELECT FROM laplace.world_admission a
            WHERE a.admission_id=admission_result.admission_ids[1]
              AND a.source_profile_id=profile_id
              AND a.recipe_receipt_id=recipe
              AND a.composition_working_set_receipt_id=
                  composition_result.working_set_receipt
              AND a.evidence_lineage_receipt_id=lineage_result.lineage_receipt_id
              AND a.evidence_testimony_receipt_id=
                  testimony_result.testimony_receipt_id
              AND a.readback_fingerprint<>decode(repeat('00',32),'hex'))
       OR NOT EXISTS (
            SELECT FROM laplace.world_admission_receipt_member m
            WHERE m.receipt_id=admission_result.world_admission_receipt_id
              AND m.admission_id=admission_result.admission_ids[1]
              AND m.member_ordinal=1) THEN
        RAISE EXCEPTION 'world admission did not close and persist the complete public route';
    END IF;
    SELECT xmin,ctid INTO STRICT admission_xmin,admission_ctid
    FROM laplace.world_admission_receipt
    WHERE receipt_id=admission_result.world_admission_receipt_id;
    replay_result := laplace.world_admission_close_batch(
        pg_temp.world_context(), ARRAY[request]);
    IF replay_result IS DISTINCT FROM admission_result OR NOT EXISTS (
        SELECT FROM laplace.world_admission_receipt
        WHERE receipt_id=admission_result.world_admission_receipt_id
          AND xmin=admission_xmin AND ctid=admission_ctid) THEN
        RAISE EXCEPTION 'world admission replay changed its logical result or immutable receipt';
    END IF;

    SELECT count(*) INTO world_rows FROM laplace.world_admission;
    SELECT count(*) INTO world_receipts FROM laplace.world_admission_receipt;
    BEGIN
        DELETE FROM laplace.evidence_lineage_receipt_member
        WHERE receipt_id=lineage_result.lineage_receipt_id;
        PERFORM laplace.world_admission_close_batch(
            pg_temp.world_context(), ARRAY[request]);
        RAISE EXCEPTION 'world admission accepted a lineage receipt without its durable member set';
    EXCEPTION
        WHEN data_corrupted THEN NULL;
    END;
    IF (SELECT count(*) FROM laplace.world_admission) <> world_rows
       OR (SELECT count(*) FROM laplace.world_admission_receipt) <> world_receipts
       OR NOT EXISTS (
            SELECT FROM laplace.evidence_lineage_receipt_member
            WHERE receipt_id=lineage_result.lineage_receipt_id
              AND node_id=evidence_node) THEN
        RAISE EXCEPTION 'rejected world admission published state or failed rollback';
    END IF;
END
$world$;

SELECT 'postgres.world-admission-contract passed' AS result;
