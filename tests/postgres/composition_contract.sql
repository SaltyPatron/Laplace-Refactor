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
    entity_b_witness bytea NOT NULL
);

INSERT INTO composition_fixture VALUES (
    true,
    decode(:'persistence_entity_a', 'hex'),
    decode(:'persistence_entity_a_witness', 'hex'),
    decode(:'persistence_entity_b', 'hex'),
    decode(:'persistence_entity_b_witness', 'hex')
);

DO $contract$
DECLARE
    fixture composition_fixture%ROWTYPE;
    source_fingerprint bytea := decode(repeat('91', 32), 'hex');
    calculation_recipe bytea := decode(repeat('a1', 32), 'hex');
    recipe_fingerprint bytea := decode(repeat('b1', 32), 'hex');
    geometry_epoch bytea := decode(repeat('c1', 32), 'hex');
    occurrence_context bytea := decode(repeat('d1', 32), 'hex');
    known laplace.composition_known_entity_record[];
    operands laplace.composition_operand_record[];
    requests laplace.composition_request_record[];
    first_result laplace.composition_deposit_result;
    replay_result laplace.composition_deposit_result;
    repeated_result laplace.composition_deposit_result;
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

    known := ARRAY[
        ROW(
            fixture.entity_a,
            fixture.entity_a_witness,
            decode(repeat('e1', 32), 'hex'),
            1.0::double precision, 0.0::double precision,
            0.0::double precision, 0.0::double precision,
            0::bigint, 0::smallint, false
        )::laplace.composition_known_entity_record,
        ROW(
            fixture.entity_b,
            fixture.entity_b_witness,
            decode(repeat('e2', 32), 'hex'),
            0.0::double precision, 1.0::double precision,
            0.0::double precision, 0.0::double precision,
            0::bigint, 0::smallint, false
        )::laplace.composition_known_entity_record
    ];
    operands := ARRAY[
        ROW(0::numeric, 1::numeric, 0::bigint, 1, 0)
            ::laplace.composition_operand_record,
        ROW(1::numeric, 1::numeric, 0::bigint, 1, 0)
            ::laplace.composition_operand_record
    ];
    requests := ARRAY[
        ROW(
            0::numeric, 2::numeric, 1::numeric, 1, 0,
            recipe_fingerprint, geometry_epoch, occurrence_context
        )::laplace.composition_request_record
    ];

    first_result := laplace.composition_deposit_batch(
        pg_temp.composition_context(), source_fingerprint,
        calculation_recipe, known, operands, requests, 256::numeric);
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
       OR first_result.physicality_presence_round_count <> 1 THEN
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

    replay_result := laplace.composition_deposit_batch(
        pg_temp.composition_context(), source_fingerprint,
        calculation_recipe, known, operands, requests, 256::numeric);
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
    repeated_result := laplace.composition_deposit_batch(
        pg_temp.composition_context(), source_fingerprint,
        calculation_recipe, known, operands, requests, 256::numeric);
    IF repeated_result IS DISTINCT FROM replay_result THEN
        RAISE EXCEPTION 'steady-state composition replay receipt is not deterministic';
    END IF;

    SELECT count(*) INTO before_entities FROM laplace.canonical_entity;
    SELECT count(*) INTO before_physicalities FROM laplace.physicality;
    SELECT count(*) INTO before_vertices FROM laplace.composition_trajectory_vertex;
    SELECT count(*) INTO before_occurrences FROM laplace.observed_occurrence;
    BEGIN
        requests[1].source_ordinal := 2::numeric;
        PERFORM laplace.composition_deposit_batch(
            pg_temp.composition_context(), source_fingerprint,
            calculation_recipe, known, operands, requests, 256::numeric);
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
