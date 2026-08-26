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
DECLARE
    result laplace.composition_deposit_result;
BEGIN
    result := pg_temp.composition_fixture_deposit();
    UPDATE laplace.physicality
    SET radius = radius + 0.25::double precision
    WHERE physicality_id = result.result_physicality_ids[1];
    PERFORM pg_temp.composition_fixture_deposit();
    RAISE EXCEPTION
        'semantic-drift composition-presence mutant accepted a changed radius';
END
$mutation$;
