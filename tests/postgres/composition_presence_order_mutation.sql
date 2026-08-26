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
    fixture composition_fixture%ROWTYPE;
BEGIN
    SELECT * INTO STRICT fixture FROM composition_fixture;
    PERFORM laplace.composition_deposit_batch(
        pg_temp.composition_context(),
        decode(repeat('91', 32), 'hex'),
        decode(repeat('a1', 32), 'hex'),
        ARRAY[
            ROW(
                fixture.entity_a, fixture.entity_a_witness,
                decode(repeat('e1', 32), 'hex'),
                1.0::double precision, 0.0::double precision,
                0.0::double precision, 0.0::double precision,
                0::bigint, 0::smallint, false
            )::laplace.composition_known_entity_record,
            ROW(
                fixture.entity_b, fixture.entity_b_witness,
                decode(repeat('e2', 32), 'hex'),
                0.0::double precision, 1.0::double precision,
                0.0::double precision, 0.0::double precision,
                0::bigint, 0::smallint, false
            )::laplace.composition_known_entity_record
        ],
        ARRAY[
            ROW(0::numeric, 1::numeric, 0::bigint, 1, 0)
                ::laplace.composition_operand_record,
            ROW(1::numeric, 1::numeric, 0::bigint, 1, 0)
                ::laplace.composition_operand_record
        ],
        ARRAY[
            ROW(
                0::numeric, 2::numeric, 1::numeric, 1, 0,
                decode(repeat('b1', 32), 'hex'),
                decode(repeat('c1', 32), 'hex'),
                decode(repeat('d1', 32), 'hex')
            )::laplace.composition_request_record
        ],
        256::numeric);
    RAISE EXCEPTION
        'reordered composition-presence mutant escaped ordinal validation';
END
$mutation$;
