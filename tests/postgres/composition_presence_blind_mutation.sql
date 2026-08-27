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
   'laplace_pg_composition_deposit_presence_blind_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
DECLARE
    fixture composition_fixture%ROWTYPE;
    result laplace.composition_deposit_result;
BEGIN
    SELECT * INTO STRICT fixture FROM composition_fixture;
    SELECT pg_temp.composition_mutant_deposit(
        inputs.execution_context,
        inputs.source_fingerprint,
        inputs.calculation_recipe_fingerprint,
        inputs.known_entities,
        inputs.operands,
        inputs.requests,
        inputs.preferred_batch_bytes)
    INTO STRICT result
    FROM pg_temp.composition_fixture_pair_inputs() AS inputs;
    IF fixture.expected_result_entity = ANY(result.result_entity_ids)
       AND fixture.expected_result_physicality = ANY(result.result_physicality_ids)
       AND result.unique_entity_count > 0
       AND result.unique_physicality_count > 0
       AND result.novel_entity_count = result.unique_entity_count
       AND result.novel_physicality_count = result.unique_physicality_count
       AND result.entity_presence_round_count = 0
       AND result.physicality_presence_round_count = 0
       AND cardinality(result.entity_presence_dispositions) =
            result.unique_entity_count
       AND cardinality(result.physicality_presence_dispositions) =
            result.unique_physicality_count
       AND 0::smallint = ALL(result.entity_presence_dispositions)
       AND 0::smallint = ALL(result.physicality_presence_dispositions)
       AND result.entity_inserted < result.novel_entity_count
       AND result.physicality_inserted < result.novel_physicality_count THEN
        RAISE EXCEPTION
            'blind composition-presence mutant published without observing canonical state';
    END IF;
    RAISE EXCEPTION 'blind composition-presence mutant did not create its intended defect';
END
$mutation$;
