\ir spi_contract.sql

CREATE OR REPLACE FUNCTION laplace.canonical_deposit_batch(
    laplace.execution_context,
    bytea,
    bytea,
    bytea[])
RETURNS laplace.canonical_deposit_result
AS :'persistence_mutant_module', 'laplace_pg_canonical_deposit_batch'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

DO $mutation$
DECLARE
    expected persistence_expected%ROWTYPE;
    collision_frames bytea[];
BEGIN
    SELECT * INTO STRICT expected FROM persistence_expected;
    SELECT array_agg(
        CASE
            WHEN substring(frame FROM 9 FOR 16) = expected.entity_a
            THEN set_byte(frame, octet_length(frame) - 1,
                          get_byte(frame, octet_length(frame) - 1) # 1)
            ELSE frame
        END
        ORDER BY ordinal)
    INTO STRICT collision_frames
    FROM unnest(expected.frames) WITH ORDINALITY AS input(frame, ordinal);

    PERFORM laplace.canonical_deposit_batch(
        pg_temp.persistence_context(), expected.source_fingerprint,
        expected.recipe_fingerprint, collision_frames);
    RAISE EXCEPTION 'blind conflict mutant accepted a same-128/different-witness identity';
END
$mutation$;
