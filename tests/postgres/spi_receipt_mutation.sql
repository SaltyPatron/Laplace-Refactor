CREATE EXTENSION laplace;

CREATE FUNCTION pg_temp.execution_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            '\x0101010101010101010101010101010101010101010101010101010101010101'::bytea,
            '\x0202020202020202020202020202020202020202020202020202020202020202'::bytea,
            '\x0303030303030303030303030303030303030303030303030303030303030303'::bytea,
            '\x0404040404040404040404040404040404040404040404040404040404040404'::bytea,
            '\x0505050505050505050505050505050505050505050505050505050505050505'::bytea,
            '\x0606060606060606060606060606060606060606060606060606060606060606'::bytea,
            '\x0707070707070707070707070707070707070707070707070707070707070707'::bytea,
            '\x0808080808080808080808080808080808080808080808080808080808080808'::bytea,
            '\x0909090909090909090909090909090909090909090909090909090909090909'::bytea,
            '\x0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a'::bytea
        ],
        '\xa0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0'::bytea,
        1048576::bigint, 4, 1, 1023::bigint, 1::smallint, 0::smallint, 2
    )::laplace.execution_context
$context$;

DO $mutation$
DECLARE
    result laplace.identity_batch_result;
    collision_detected boolean := false;
BEGIN
    result := laplace.identity_codepoint_execute_batch(
        pg_temp.execution_context(), ARRAY[50, 53, 53]);
    UPDATE laplace.execution_receipt
    SET output_fingerprint =
        '\x0000000000000000000000000000000000000000000000000000000000000000'::bytea
    WHERE receipt_id = result.receipt_id;

    BEGIN
        PERFORM laplace.identity_codepoint_execute_batch(
            pg_temp.execution_context(), ARRAY[50, 53, 53]);
    EXCEPTION
        WHEN SQLSTATE 'XX001' THEN collision_detected := true;
    END;
    IF collision_detected IS NOT TRUE THEN
        RAISE EXCEPTION 'corrupted durable receipt was silently accepted';
    END IF;
END
$mutation$;

SELECT 'postgres.mutation-receipt-collision-detected passed' AS result;
