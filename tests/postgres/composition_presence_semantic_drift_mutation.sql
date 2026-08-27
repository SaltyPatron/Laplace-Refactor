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
   'laplace_pg_composition_deposit_presence_semantic_drift_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
DECLARE
    result laplace.composition_deposit_result;
BEGIN
    SELECT pg_temp.composition_mutant_deposit(
        inputs.execution_context,
        inputs.source_fingerprint,
        inputs.calculation_recipe_fingerprint,
        inputs.known_entities,
        inputs.operands,
        inputs.requests,
        inputs.preferred_batch_bytes)
    INTO STRICT result
    FROM pg_temp.composition_fixture_inputs() AS inputs;
    UPDATE laplace.physicality
    SET radius = radius + 0.25::double precision
    WHERE physicality_id = result.result_physicality_ids[1];
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
        'semantic-drift composition-presence mutant accepted a changed radius';
END
$mutation$;
