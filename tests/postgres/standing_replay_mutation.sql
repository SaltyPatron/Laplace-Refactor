\ir standing_contract.sql

CREATE FUNCTION pg_temp.standing_replay_mutant(
    laplace.execution_context,
    laplace.standing_period_input[])
RETURNS laplace.standing_period_result
AS :'standing_mutant_module',
   'laplace_pg_evidence_calculate_standing_replay_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
DECLARE
    expected standing_expected%ROWTYPE;
    participant laplace.standing_state;
    opponent_a laplace.standing_state;
    opponent_b laplace.standing_state;
    recipe laplace.standing_recipe;
    inputs laplace.standing_period_input[];
BEGIN
    SELECT * INTO STRICT expected FROM standing_expected;
    recipe := pg_temp.standing_recipe(
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
    inputs := ARRAY[
        ROW(recipe, participant, pg_temp.standing_event(
            expected.event_a, participant, opponent_a,
            144, 160, expected.confirm_mapping, 1, 1))::laplace.standing_period_input,
        ROW(recipe, participant, pg_temp.standing_event(
            expected.event_b, participant, opponent_b,
            144, 176, expected.refute_mapping, 0, 2))::laplace.standing_period_input
    ];
    UPDATE laplace.standing_state_history
    SET rating = rating + 1.0
    WHERE state_id = expected.participant_state;
    PERFORM pg_temp.standing_replay_mutant(
        pg_temp.execution_context(), inputs);
    RAISE EXCEPTION
        'standing replay mutant accepted a corrupted durable initial state';
END
$mutation$;
