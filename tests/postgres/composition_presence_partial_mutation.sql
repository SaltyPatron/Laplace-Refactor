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
   'laplace_pg_composition_deposit_presence_partial_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
BEGIN
    PERFORM pg_temp.composition_mutant_deposit(
        inputs.execution_context,
        inputs.source_fingerprint,
        inputs.calculation_recipe_fingerprint,
        inputs.known_entities,
        inputs.operands,
        inputs.requests,
        inputs.preferred_batch_bytes)
    FROM pg_temp.composition_fixture_pair_inputs() AS inputs;
    RAISE EXCEPTION
        'partial composition-presence mutant escaped complete-result validation';
END
$mutation$;
