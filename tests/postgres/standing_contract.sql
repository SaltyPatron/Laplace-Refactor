CREATE EXTENSION laplace;

\if :{?standing_disable_history_dependence}
ALTER TABLE laplace.standing_match_event
    DROP CONSTRAINT standing_match_event_one_root_per_participant;
\endif

CREATE FUNCTION pg_temp.execution_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('01',32),'hex'), decode(repeat('02',32),'hex'),
            decode(repeat('03',32),'hex'), decode(repeat('04',32),'hex'),
            decode(repeat('05',32),'hex'), decode(repeat('06',32),'hex'),
            decode(repeat('07',32),'hex'), decode(repeat('08',32),'hex'),
            decode(repeat('09',32),'hex'), decode(repeat('0a',32),'hex')
        ],
        decode(repeat('a0',32),'hex'), 1048576::bigint, 4, 1,
        1023::bigint, @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        @LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY@::integer
    )::laplace.execution_context
$context$;

CREATE FUNCTION pg_temp.stale_standing_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('01',32),'hex'), decode(repeat('02',32),'hex'),
            decode(repeat('03',32),'hex'), decode(repeat('ff',32),'hex'),
            decode(repeat('05',32),'hex'), decode(repeat('06',32),'hex'),
            decode(repeat('07',32),'hex'), decode(repeat('08',32),'hex'),
            decode(repeat('09',32),'hex'), decode(repeat('0a',32),'hex')
        ],
        decode(repeat('a0',32),'hex'), 1048576::bigint, 4, 1,
        1023::bigint, @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        @LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY@::integer
    )::laplace.execution_context
$context$;

CREATE FUNCTION pg_temp.seed_digest(seed integer)
RETURNS bytea
LANGUAGE SQL IMMUTABLE STRICT PARALLEL SAFE
AS $digest$
    SELECT decode(string_agg(lpad(to_hex((seed + ordinal) % 256), 2, '0'), ''
                             ORDER BY ordinal), 'hex')
    FROM generate_series(0, 31) ordinal
$digest$;

CREATE FUNCTION pg_temp.standing_state(
    state_id bytea,
    coordinate_id bytea,
    arena_scope_id bytea,
    recipe_id bytea,
    rating double precision,
    deviation double precision)
RETURNS laplace.standing_state
LANGUAGE SQL IMMUTABLE STRICT PARALLEL SAFE
AS $state$
    SELECT ROW(
        state_id, coordinate_id, arena_scope_id, decode(repeat('00',32),'hex'),
        pg_temp.seed_digest(128), recipe_id,
        rating, deviation, 0.06::double precision,
        0::numeric, 0::numeric, 1, 0
    )::laplace.standing_state
$state$;

CREATE FUNCTION pg_temp.standing_recipe(recipe_id bytea, authority_receipt_id bytea)
RETURNS laplace.standing_recipe
LANGUAGE SQL IMMUTABLE STRICT PARALLEL SAFE
AS $recipe$
    SELECT ROW(
        recipe_id, authority_receipt_id, pg_temp.seed_digest(32),
        pg_temp.seed_digest(48), pg_temp.seed_digest(64),
        pg_temp.seed_digest(80), pg_temp.seed_digest(96),
        1500.0::double precision, 200.0::double precision,
        0.06::double precision, 0.5::double precision,
        0.000001::double precision,
        ARRAY[1,0,1,1,0,0,0,0,0]::numeric[],
        ARRAY[1,1,2,2,0,0,0,0,0]::numeric[],
        15, 1, 1, 1, 0
    )::laplace.standing_recipe
$recipe$;

CREATE FUNCTION pg_temp.standing_event(
    event_id bytea,
    participant laplace.standing_state,
    opponent laplace.standing_state,
    period_seed integer,
    event_seed integer,
    outcome_mapping_id bytea,
    score numeric,
    outcome integer)
RETURNS laplace.standing_event
LANGUAGE SQL IMMUTABLE STRICT PARALLEL SAFE
AS $event$
    SELECT ROW(
        event_id, (participant).coordinate_id, (participant).state_id, opponent,
        pg_temp.seed_digest(period_seed), pg_temp.seed_digest(event_seed),
        outcome_mapping_id, pg_temp.seed_digest(event_seed + 2),
        pg_temp.seed_digest(event_seed + 3), score, 1::numeric, outcome, 0
    )::laplace.standing_event
$event$;

CREATE TEMP TABLE standing_expected AS
SELECT
    decode(:'standing_participant_state','hex') participant_state,
    decode(:'standing_recipe','hex') recipe,
    decode(:'standing_authority_receipt','hex') authority_receipt,
    decode(:'standing_confirm_mapping','hex') confirm_mapping,
    decode(:'standing_refute_mapping','hex') refute_mapping,
    decode(:'standing_participant_coordinate','hex') participant_coordinate,
    decode(:'standing_arena','hex') arena,
    decode(:'standing_opponent_a_state','hex') opponent_a_state,
    decode(:'standing_opponent_a_coordinate','hex') opponent_a_coordinate,
    decode(:'standing_opponent_b_state','hex') opponent_b_state,
    decode(:'standing_opponent_b_coordinate','hex') opponent_b_coordinate,
    decode(:'standing_event_a','hex') event_a,
    decode(:'standing_event_b','hex') event_b,
    decode(:'standing_successor_state','hex') successor_state,
    decode(:'standing_receipt','hex') receipt,
    decode(:'standing_input','hex') input_fingerprint,
    decode(:'standing_output','hex') output_fingerprint,
    decode(:'standing_isa_receipt','hex') isa_receipt,
    decode(:'standing_repeated_root_event','hex') repeated_root_event,
    :'standing_rating'::double precision rating,
    :'standing_deviation'::double precision deviation,
    :'standing_volatility'::double precision volatility,
    decode(:'standing_second_event','hex') second_event,
    decode(:'standing_second_state','hex') second_state,
    decode(:'standing_second_receipt','hex') second_receipt,
    decode(:'standing_second_input','hex') second_input,
    decode(:'standing_second_output','hex') second_output,
    decode(:'standing_second_isa_receipt','hex') second_isa_receipt,
    :'standing_second_rating'::double precision second_rating,
    :'standing_second_deviation'::double precision second_deviation,
    :'standing_second_volatility'::double precision second_volatility;

\if :{?standing_admission_mutant}
CREATE FUNCTION pg_temp.standing_unadmitted_probe(
    laplace.execution_context, laplace.standing_period_input[])
RETURNS laplace.standing_period_result
AS :'standing_mutant_module',
   'laplace_pg_evidence_calculate_standing_admission_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;
\else
CREATE FUNCTION pg_temp.standing_unadmitted_probe(
    laplace.execution_context, laplace.standing_period_input[])
RETURNS laplace.standing_period_result
LANGUAGE SQL VOLATILE STRICT PARALLEL UNSAFE
AS $probe$
    SELECT laplace.evidence_calculate_standing_batch($1, $2)
$probe$;
\endif

DO $contract$
DECLARE
    standing_recipe laplace.standing_recipe;
    participant laplace.standing_state;
    opponent_a laplace.standing_state;
    opponent_b laplace.standing_state;
    first_inputs laplace.standing_period_input[];
    second_inputs laplace.standing_period_input[];
    first_result laplace.standing_period_result;
    replay_result laplace.standing_period_result;
    second_prior laplace.standing_state;
    repeated_root_inputs laplace.standing_period_input[];
    second_result laplace.standing_period_result;
    rejected boolean := false;
    expected standing_expected%ROWTYPE;
BEGIN
    SELECT * INTO STRICT expected FROM standing_expected;
    standing_recipe := pg_temp.standing_recipe(
        expected.recipe, expected.authority_receipt);
    participant := pg_temp.standing_state(
        expected.participant_state, expected.participant_coordinate,
        expected.arena, expected.recipe, 1500.0, 200.0);
    opponent_a := pg_temp.standing_state(
        expected.opponent_a_state, expected.opponent_a_coordinate,
        expected.arena, expected.recipe, 1500.0, 200.0);
    opponent_b := pg_temp.standing_state(
        expected.opponent_b_state, expected.opponent_b_coordinate,
        expected.arena, expected.recipe, 1500.0, 200.0);
    first_inputs := ARRAY[
        ROW(standing_recipe, participant, pg_temp.standing_event(
            expected.event_a, participant, opponent_a,
            144, 160, expected.confirm_mapping, 1, 1))::laplace.standing_period_input,
        ROW(standing_recipe, participant, pg_temp.standing_event(
            expected.event_b, participant, opponent_b,
            144, 176, expected.refute_mapping, 0, 2))::laplace.standing_period_input
    ];

    INSERT INTO laplace.standing_recipe_history(
        recipe_id, authority_receipt_id, evaluation_law_id, world_context_id,
        language_modality_id, valid_time_scope_id, evidence_boundary_id,
        default_rating, default_rating_deviation, default_volatility,
        volatility_constraint, convergence_tolerance, score_numerator,
        score_denominator, rateable_outcome_mask, participant_role,
        arena_kind, version, flags)
    VALUES (
        (standing_recipe).recipe_id, (standing_recipe).authority_receipt_id,
        (standing_recipe).evaluation_law_id, (standing_recipe).world_context_id,
        (standing_recipe).language_modality_id, (standing_recipe).valid_time_scope_id,
        (standing_recipe).evidence_boundary_id, (standing_recipe).default_rating,
        (standing_recipe).default_rating_deviation, (standing_recipe).default_volatility,
        (standing_recipe).volatility_constraint, (standing_recipe).convergence_tolerance,
        (standing_recipe).score_numerator, (standing_recipe).score_denominator,
        (standing_recipe).rateable_outcome_mask, (standing_recipe).participant_role,
        (standing_recipe).arena_kind, (standing_recipe).version, (standing_recipe).flags);

    BEGIN
        PERFORM pg_temp.standing_unadmitted_probe(
            pg_temp.execution_context(), first_inputs);
    EXCEPTION WHEN insufficient_privilege THEN
        rejected := true;
    END;
    IF NOT rejected THEN
        RAISE EXCEPTION 'standing admission mutant accepted an unadmitted recipe';
    END IF;
    rejected := false;

    INSERT INTO laplace.standing_recipe_admission(
        recipe_id, authority_receipt_id, authority_fingerprint,
        evidence_epoch, admission_sequence)
    VALUES (
        (standing_recipe).recipe_id, (standing_recipe).authority_receipt_id,
        (pg_temp.execution_context()).authority_fingerprint,
        (pg_temp.execution_context()).epochs[4], 1);

    BEGIN
        PERFORM laplace.evidence_calculate_standing_batch(
            pg_temp.stale_standing_context(), first_inputs);
    EXCEPTION WHEN insufficient_privilege THEN
        rejected := true;
    END;
    IF NOT rejected THEN
        RAISE EXCEPTION 'standing accepted an admission from a stale evidence epoch';
    END IF;
    rejected := false;

    first_result := laplace.evidence_calculate_standing_batch(
        pg_temp.execution_context(), first_inputs);
    IF first_result.state_id <> expected.successor_state
       OR first_result.receipt_id <> expected.receipt
       OR first_result.input_fingerprint <> expected.input_fingerprint
       OR first_result.output_fingerprint <> expected.output_fingerprint
       OR first_result.isa_receipt_id <> expected.isa_receipt
       OR first_result.rating IS DISTINCT FROM expected.rating
       OR first_result.rating_deviation IS DISTINCT FROM expected.deviation
       OR first_result.volatility IS DISTINCT FROM expected.volatility
       OR first_result.eligible_match_count <> 2
       OR first_result.period_ordinal <> 1
       OR first_result.eligible_event_count <> 2
       OR first_result.prior_match_count <> 0
       OR first_result.successor_match_count <> 2 THEN
        RAISE EXCEPTION 'first persistent standing result diverged from native and ISA execution';
    END IF;
    replay_result := laplace.evidence_calculate_standing_batch(
        pg_temp.execution_context(), ARRAY[first_inputs[2], first_inputs[1]]);
    IF replay_result.state_id <> first_result.state_id
       OR replay_result.receipt_id <> first_result.receipt_id
       OR replay_result.isa_receipt_id <> first_result.isa_receipt_id THEN
        RAISE EXCEPTION 'standing replay changed canonical identity';
    END IF;
    IF (SELECT count(*) FROM laplace.standing_recipe_history) <> 1
       OR (SELECT count(*) FROM laplace.standing_recipe_admission) <> 1
       OR (SELECT count(*) FROM laplace.standing_state_history) <> 4
       OR (SELECT count(*) FROM laplace.standing_match_event) <> 2
       OR (SELECT count(*) FROM laplace.standing_period_receipt) <> 1
       OR (SELECT count(*) FROM laplace.standing_period_receipt_member) <> 2
       OR (SELECT count(*) FROM laplace.execution_receipt
           WHERE opcode = 327683) <> 1 THEN
        RAISE EXCEPTION 'standing replay changed durable cardinality';
    END IF;

    second_prior := ROW(
        first_result.state_id, first_result.coordinate_id,
        first_result.arena_scope_id, first_result.prior_state_id,
        first_result.epoch_id, first_result.rating_recipe_id,
        first_result.rating, first_result.rating_deviation,
        first_result.volatility, first_result.eligible_match_count,
        first_result.period_ordinal, first_result.rating_recipe_version,
        first_result.state_flags)::laplace.standing_state;

    repeated_root_inputs := ARRAY[
        ROW(standing_recipe, second_prior, pg_temp.standing_event(
            expected.repeated_root_event, second_prior, opponent_a,
            191, 160, expected.confirm_mapping, 1, 1))::laplace.standing_period_input
    ];
    BEGIN
        PERFORM laplace.evidence_calculate_standing_batch(
            pg_temp.execution_context(), repeated_root_inputs);
    EXCEPTION WHEN OTHERS THEN
        IF SQLSTATE = 'XX001' THEN
            rejected := true;
        ELSE
            RAISE;
        END IF;
    END;
    IF NOT rejected THEN
        RAISE EXCEPTION 'standing accepted reuse of a dependence root across periods';
    END IF;
    rejected := false;

    second_inputs := ARRAY[
        ROW(standing_recipe, second_prior, pg_temp.standing_event(
            expected.second_event, second_prior, opponent_a,
            145, 192, expected.confirm_mapping, 1, 1))::laplace.standing_period_input
    ];
    second_result := laplace.evidence_calculate_standing_batch(
        pg_temp.execution_context(), second_inputs);
    IF second_result.state_id <> expected.second_state
       OR second_result.receipt_id <> expected.second_receipt
       OR second_result.input_fingerprint <> expected.second_input
       OR second_result.output_fingerprint <> expected.second_output
       OR second_result.isa_receipt_id <> expected.second_isa_receipt
       OR second_result.rating IS DISTINCT FROM expected.second_rating
       OR second_result.rating_deviation IS DISTINCT FROM expected.second_deviation
       OR second_result.volatility IS DISTINCT FROM expected.second_volatility
       OR second_result.eligible_match_count <> 3
       OR second_result.period_ordinal <> 2
       OR second_result.prior_match_count <> 2
       OR second_result.successor_match_count <> 3 THEN
        RAISE EXCEPTION 'earned standing did not continue from its durable predecessor';
    END IF;
    IF (SELECT count(*) FROM laplace.standing_recipe_history) <> 1
       OR (SELECT count(*) FROM laplace.standing_recipe_admission) <> 1
       OR (SELECT count(*) FROM laplace.standing_state_history) <> 5
       OR (SELECT count(*) FROM laplace.standing_match_event) <> 3
       OR (SELECT count(*) FROM laplace.standing_period_receipt) <> 2
       OR (SELECT count(*) FROM laplace.standing_period_receipt_member) <> 3
       OR (SELECT count(*) FROM laplace.execution_receipt
           WHERE opcode = 327683) <> 2 THEN
        RAISE EXCEPTION 'second period did not publish one exact durable continuation';
    END IF;

    UPDATE laplace.standing_match_event
    SET context_id = pg_temp.seed_digest(254)
    WHERE event_id = expected.event_a;
    BEGIN
        PERFORM laplace.evidence_calculate_standing_batch(
            pg_temp.execution_context(), first_inputs);
    EXCEPTION WHEN data_corrupted THEN
        rejected := true;
    END;
    IF NOT rejected THEN
        RAISE EXCEPTION 'standing replay accepted a corrupted durable event';
    END IF;
    UPDATE laplace.standing_match_event
    SET context_id = pg_temp.seed_digest(162)
    WHERE event_id = expected.event_a;

    rejected := false;
    UPDATE laplace.standing_recipe_history
    SET default_rating = default_rating + 1.0
    WHERE recipe_id = expected.recipe;
    BEGIN
        PERFORM laplace.evidence_calculate_standing_batch(
            pg_temp.execution_context(), first_inputs);
    EXCEPTION WHEN data_corrupted THEN
        rejected := true;
    END;
    IF NOT rejected THEN
        RAISE EXCEPTION 'standing replay accepted corrupted durable recipe history';
    END IF;
    UPDATE laplace.standing_recipe_history
    SET default_rating = 1500.0
    WHERE recipe_id = expected.recipe;

    rejected := false;
    UPDATE laplace.standing_state_history
    SET rating = rating + 1.0
    WHERE state_id = expected.successor_state;
    BEGIN
        PERFORM laplace.evidence_calculate_standing_batch(
            pg_temp.execution_context(), first_inputs);
    EXCEPTION WHEN data_corrupted THEN
        rejected := true;
    END;
    IF NOT rejected THEN
        RAISE EXCEPTION 'standing replay accepted a corrupted durable successor';
    END IF;
    UPDATE laplace.standing_state_history
    SET rating = expected.rating
    WHERE state_id = expected.successor_state;
END
$contract$;

SELECT encode(state_id,'hex') AS state_id,
       encode(prior_state_id,'hex') AS prior_state_id,
       period_ordinal, eligible_match_count, rating
FROM laplace.standing_state_history
ORDER BY coordinate_id, period_ordinal;
