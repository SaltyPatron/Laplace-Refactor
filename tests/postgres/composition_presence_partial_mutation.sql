\ir composition_contract.sql

CREATE OR REPLACE FUNCTION laplace.composition_deposit_batch(
    laplace.execution_context,
    bytea,
    bytea,
    laplace.composition_known_entity_record[],
    laplace.composition_operand_record[],
    laplace.composition_request_record[],
    numeric)
RETURNS laplace.composition_deposit_result
AS :'persistence_mutant_module', 'laplace_pg_composition_deposit_batch'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
BEGIN
    PERFORM pg_temp.composition_fixture_deposit();
    RAISE EXCEPTION
        'partial composition-presence mutant escaped complete-result validation';
END
$mutation$;
