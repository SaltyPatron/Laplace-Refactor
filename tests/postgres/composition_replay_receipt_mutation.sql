\ir composition_contract.sql

CREATE FUNCTION pg_temp.composition_mutant_deposit(
    laplace.execution_context,
    bytea,
    bytea,
    laplace.composition_known_entity_record[],
    laplace.composition_operand_record[],
    laplace.composition_request_record[],
    numeric)
RETURNS laplace.composition_deposit_result
AS :'persistence_mutant_module',
   'laplace_pg_composition_deposit_replay_receipt_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
DECLARE
    result laplace.composition_deposit_result;
BEGIN
    SELECT (pg_temp.composition_mutant_deposit(
        inputs.execution_context,
        inputs.source_fingerprint,
        inputs.calculation_recipe_fingerprint,
        inputs.known_entities,
        inputs.operands,
        inputs.requests,
        inputs.preferred_batch_bytes)).*
    INTO STRICT result
    FROM pg_temp.composition_fixture_inputs() AS inputs;
    UPDATE laplace.composition_execution_receipt
    SET stream_fingerprint = set_byte(stream_fingerprint, 0,
        get_byte(stream_fingerprint, 0) # 1)
    WHERE working_set_receipt = result.working_set_receipt;
    PERFORM pg_temp.composition_mutant_deposit(
        inputs.execution_context,
        inputs.source_fingerprint,
        inputs.calculation_recipe_fingerprint,
        inputs.known_entities,
        inputs.operands,
        inputs.requests,
        inputs.preferred_batch_bytes)
    FROM pg_temp.composition_fixture_inputs() AS inputs;
    RAISE EXCEPTION
        'composition replay-receipt mutant accepted colliding durable fields';
END
$mutation$;
