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
    IF result.novel_entity_count = 3
       AND result.novel_physicality_count = 1
       AND result.entity_presence_round_count = 0
       AND result.physicality_presence_round_count = 0
       AND result.entity_presence_dispositions = ARRAY[0, 0, 0]::smallint[]
       AND result.physicality_presence_dispositions = ARRAY[0]::smallint[] THEN
        RAISE EXCEPTION
            'blind composition-presence mutant published without observing canonical state';
    END IF;
    RAISE EXCEPTION 'blind composition-presence mutant did not create its intended defect';
END
$mutation$;
