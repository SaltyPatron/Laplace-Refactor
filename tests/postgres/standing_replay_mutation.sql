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
    inputs laplace.standing_period_input[];
BEGIN
    SELECT * INTO STRICT expected FROM standing_expected;
    participant := pg_temp.standing_state(
        expected.participant_state, expected.participant_coordinate,
        expected.arena, 1500.0, 200.0);
    opponent_a := pg_temp.standing_state(
        expected.opponent_a_state, expected.opponent_a_coordinate,
        expected.arena, 1400.0, 80.0);
    opponent_b := pg_temp.standing_state(
        expected.opponent_b_state, expected.opponent_b_coordinate,
        expected.arena, 1600.0, 80.0);
    inputs := ARRAY[
        ROW(participant, pg_temp.standing_event(
            expected.event_a, participant, opponent_a,
            144, 160, 1, 1), 0.5, 0.000001)::laplace.standing_period_input,
        ROW(participant, pg_temp.standing_event(
            expected.event_b, participant, opponent_b,
            144, 176, 0, 2), 0.5, 0.000001)::laplace.standing_period_input
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
