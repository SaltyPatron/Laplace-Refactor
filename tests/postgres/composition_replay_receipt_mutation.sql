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
    UPDATE laplace.canonical_deposit_receipt
    SET total_records = total_records + 1
    WHERE receipt_id = result.staged_stream_receipt;
    PERFORM pg_temp.composition_fixture_deposit();
    RAISE EXCEPTION
        'composition replay-receipt mutant accepted colliding durable fields';
END
$mutation$;
