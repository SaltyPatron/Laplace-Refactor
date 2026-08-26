CREATE EXTENSION laplace;

CREATE FUNCTION pg_temp.composition_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('01', 32), 'hex'),
            decode(repeat('02', 32), 'hex'),
            decode(repeat('03', 32), 'hex'),
            decode(repeat('04', 32), 'hex'),
            decode(repeat('05', 32), 'hex'),
            decode(repeat('06', 32), 'hex'),
            decode(repeat('07', 32), 'hex'),
            decode(repeat('08', 32), 'hex'),
            decode(repeat('09', 32), 'hex'),
            decode(repeat('0a', 32), 'hex')
        ],
        decode(repeat('a0', 32), 'hex'),
        67108864::bigint,
        4,
        1,
        1023::bigint,
        @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        0
    )::laplace.execution_context
$context$;

CREATE TEMP TABLE composition_fixture (
    singleton boolean PRIMARY KEY DEFAULT true CHECK (singleton),
    entity_a bytea NOT NULL,
    entity_a_witness bytea NOT NULL,
    entity_b bytea NOT NULL,
    entity_b_witness bytea NOT NULL,
    expected_result_entity bytea NOT NULL,
    expected_result_physicality bytea NOT NULL,
    expected_result_tier smallint NOT NULL,
    expected_working_set_receipt bytea NOT NULL,
    expected_presence_semantic_receipt bytea NOT NULL,
    direct_presence_execution_receipt bytea NOT NULL,
    expected_candidate_fingerprint bytea NOT NULL,
    expected_disposition_fingerprint bytea NOT NULL,
    expected_stream_fingerprint bytea NOT NULL,
    expected_entity_dispositions bytea NOT NULL,
    expected_physicality_dispositions bytea NOT NULL
);

INSERT INTO composition_fixture VALUES (
    true,
    decode(:'persistence_entity_a', 'hex'),
    decode(:'persistence_entity_a_witness', 'hex'),
    decode(:'persistence_entity_b', 'hex'),
    decode(:'persistence_entity_b_witness', 'hex'),
    decode(:'composition_result_entity', 'hex'),
    decode(:'composition_result_physicality', 'hex'),
    get_byte(decode(:'composition_result_tier', 'hex'), 0)::smallint,
    decode(:'composition_working_set_receipt', 'hex'),
    decode(:'composition_presence_semantic_receipt', 'hex'),
    decode(:'composition_presence_execution_receipt', 'hex'),
    decode(:'composition_presence_candidate_fingerprint', 'hex'),
    decode(:'composition_presence_disposition_fingerprint', 'hex'),
    decode(:'composition_stream_fingerprint', 'hex'),
    decode(:'composition_entity_dispositions', 'hex'),
    decode(:'composition_physicality_dispositions', 'hex')
);

CREATE FUNCTION pg_temp.composition_fixture_deposit(
    source_ordinal numeric DEFAULT 1::numeric)
RETURNS laplace.composition_deposit_result
LANGUAGE SQL VOLATILE PARALLEL UNSAFE
AS $fixture$
    SELECT laplace.composition_deposit_batch(
        pg_temp.composition_context(),
        decode(repeat('91', 32), 'hex'),
        decode(repeat('a1', 32), 'hex'),
        ARRAY[
            ROW(
                entity_a, entity_a_witness,
                decode(repeat('e1', 32), 'hex'),
                1.0::double precision, 0.0::double precision,
                0.0::double precision, 0.0::double precision,
                0::bigint, 0::smallint, false
            )::laplace.composition_known_entity_record,
            ROW(
                entity_b, entity_b_witness,
                decode(repeat('e2', 32), 'hex'),
                0.0::double precision, 1.0::double precision,
                0.0::double precision, 0.0::double precision,
                0::bigint, 0::smallint, false
            )::laplace.composition_known_entity_record
        ],
        ARRAY[
            ROW(0::numeric, 1::numeric, 0::bigint, 1, 0)
                ::laplace.composition_operand_record,
            ROW(1::numeric, 1::numeric, 0::bigint, 1, 0)
                ::laplace.composition_operand_record
        ],
        ARRAY[
            ROW(
                0::numeric, 2::numeric, source_ordinal, 1, 0,
                decode(repeat('b1', 32), 'hex'),
                decode(repeat('c1', 32), 'hex'),
                decode(repeat('d1', 32), 'hex')
            )::laplace.composition_request_record
        ],
        256::numeric)
    FROM composition_fixture
    WHERE singleton
$fixture$;

DO $contract$
DECLARE
    fixture composition_fixture%ROWTYPE;
    first_result laplace.composition_deposit_result;
    replay_result laplace.composition_deposit_result;
    repeated_result laplace.composition_deposit_result;
    semantic_drift_rejected boolean := false;
    replay_collision_rejected boolean := false;
    before_entities bigint;
    before_physicalities bigint;
    before_vertices bigint;
    before_occurrences bigint;
BEGIN
    SELECT * INTO STRICT fixture FROM composition_fixture;
    IF pg_catalog.to_regprocedure(
        'laplace.composition_deposit_batch(laplace.execution_context,bytea,bytea,laplace.composition_known_entity_record[],laplace.composition_operand_record[],laplace.composition_request_record[],numeric)')
        IS NULL THEN
        RAISE EXCEPTION 'composition deposit PostgreSQL binding is missing';
    END IF;

    first_result := pg_temp.composition_fixture_deposit();
    IF first_result.status <> 0
       OR cardinality(first_result.result_entity_ids) <> 1
       OR cardinality(first_result.result_physicality_ids) <> 1
       OR first_result.unique_entity_count <> 3
       OR first_result.unique_physicality_count <> 1
       OR first_result.novel_entity_count <> 3
       OR first_result.novel_physicality_count <> 1
       OR first_result.trajectory_vertex_count <> 2
       OR first_result.novel_trajectory_vertex_count <> 2
       OR first_result.occurrence_count <> 1
       OR first_result.entity_inserted <> 3
       OR first_result.physicality_inserted <> 1
       OR first_result.trajectory_vertex_inserted <> 2
       OR first_result.occurrence_inserted <> 1
       OR first_result.entity_presence_round_count <> 2
       OR first_result.physicality_presence_round_count <> 1
       OR first_result.result_entity_ids[1] <> fixture.expected_result_entity
       OR first_result.result_physicality_ids[1] <>
            fixture.expected_result_physicality
       OR first_result.result_tier_floors[1] <> fixture.expected_result_tier
       OR first_result.working_set_receipt <>
            fixture.expected_working_set_receipt
       OR first_result.presence_semantic_receipt <>
            fixture.expected_presence_semantic_receipt
       OR first_result.presence_execution_receipt =
            fixture.direct_presence_execution_receipt
       OR first_result.presence_candidate_fingerprint <>
            fixture.expected_candidate_fingerprint
       OR first_result.presence_disposition_fingerprint <>
            fixture.expected_disposition_fingerprint
       OR first_result.stream_fingerprint <> fixture.expected_stream_fingerprint
       OR first_result.entity_presence_dispositions IS DISTINCT FROM
            (SELECT array_agg(
                 get_byte(fixture.expected_entity_dispositions, ordinal)::smallint
                 ORDER BY ordinal)
             FROM generate_series(
                 0, octet_length(fixture.expected_entity_dispositions) - 1)
                 ordinal)
       OR first_result.physicality_presence_dispositions IS DISTINCT FROM
            (SELECT array_agg(
                 get_byte(
                     fixture.expected_physicality_dispositions,
                     ordinal)::smallint
                 ORDER BY ordinal)
             FROM generate_series(
                 0,
                 octet_length(fixture.expected_physicality_dispositions) - 1)
                 ordinal)
       THEN
        RAISE EXCEPTION 'first whole-working-set composition deposition differs from contract: %',
            first_result;
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM laplace.canonical_entity
        WHERE entity_id = first_result.result_entity_ids[1])
       OR NOT EXISTS (
        SELECT 1 FROM laplace.physicality
        WHERE physicality_id = first_result.result_physicality_ids[1])
       OR (SELECT count(*) FROM laplace.composition_trajectory_vertex
           WHERE physicality_id = first_result.result_physicality_ids[1]) <> 2 THEN
        RAISE EXCEPTION 'composition result is not exactly readable from canonical state';
    END IF;

    BEGIN
        UPDATE laplace.physicality
        SET radius = radius + 0.25::double precision
        WHERE physicality_id = first_result.result_physicality_ids[1];
        PERFORM pg_temp.composition_fixture_deposit();
        UPDATE laplace.physicality
        SET radius = radius - 0.25::double precision
        WHERE physicality_id = first_result.result_physicality_ids[1];
    EXCEPTION
        WHEN SQLSTATE 'XX001' THEN
            semantic_drift_rejected := true;
    END;
    IF semantic_drift_rejected IS NOT TRUE THEN
        RAISE EXCEPTION
            'composition presence accepted same identity with drifted semantic fields';
    END IF;

    replay_result := pg_temp.composition_fixture_deposit();
    IF replay_result.result_entity_ids IS DISTINCT FROM first_result.result_entity_ids
       OR replay_result.result_physicality_ids IS DISTINCT FROM first_result.result_physicality_ids
       OR replay_result.novel_entity_count <> 0
       OR replay_result.novel_physicality_count <> 0
       OR replay_result.novel_trajectory_vertex_count <> 0
       OR replay_result.stream_record_count <> 1
       OR replay_result.entity_inserted <> 0
       OR replay_result.physicality_inserted <> 0
       OR replay_result.trajectory_vertex_inserted <> 0
       OR replay_result.occurrence_inserted <> 0 THEN
        RAISE EXCEPTION 'composition replay did not filter exact canonical state: %',
            replay_result;
    END IF;
    repeated_result := pg_temp.composition_fixture_deposit();
    IF repeated_result IS DISTINCT FROM replay_result THEN
        RAISE EXCEPTION 'steady-state composition replay receipt is not deterministic';
    END IF;
    BEGIN
        UPDATE laplace.canonical_deposit_receipt
        SET total_records = total_records + 1
        WHERE receipt_id = replay_result.staged_stream_receipt;
        PERFORM pg_temp.composition_fixture_deposit();
        UPDATE laplace.canonical_deposit_receipt
        SET total_records = total_records - 1
        WHERE receipt_id = replay_result.staged_stream_receipt;
    EXCEPTION
        WHEN SQLSTATE 'XX001' THEN
            replay_collision_rejected := true;
    END;
    IF replay_collision_rejected IS NOT TRUE THEN
        RAISE EXCEPTION
            'composition replay accepted a colliding durable deposit receipt';
    END IF;

    SELECT count(*) INTO before_entities FROM laplace.canonical_entity;
    SELECT count(*) INTO before_physicalities FROM laplace.physicality;
    SELECT count(*) INTO before_vertices FROM laplace.composition_trajectory_vertex;
    SELECT count(*) INTO before_occurrences FROM laplace.observed_occurrence;
    BEGIN
        PERFORM pg_temp.composition_fixture_deposit(2::numeric);
        RAISE EXCEPTION 'force composition transaction rollback';
    EXCEPTION
        WHEN raise_exception THEN
            NULL;
    END;
    IF (SELECT count(*) FROM laplace.canonical_entity) <> before_entities
       OR (SELECT count(*) FROM laplace.physicality) <> before_physicalities
       OR (SELECT count(*) FROM laplace.composition_trajectory_vertex) <> before_vertices
       OR (SELECT count(*) FROM laplace.observed_occurrence) <> before_occurrences THEN
        RAISE EXCEPTION 'composition deposition escaped transaction rollback';
    END IF;
END
$contract$;

SELECT 'postgres.composition-working-set-contract passed' AS result;
