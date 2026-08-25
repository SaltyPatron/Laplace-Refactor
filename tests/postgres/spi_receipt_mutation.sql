CREATE EXTENSION laplace;

DO $mutation$
DECLARE
    result laplace.identity_batch_result;
    collision_detected boolean := false;
BEGIN
    result := laplace.identity_codepoint_execute_batch(ARRAY[50, 53, 53]);
    UPDATE laplace.execution_receipt
    SET output_fingerprint = decode(repeat('00', 32), 'hex')
    WHERE receipt_id = result.receipt_id;

    BEGIN
        PERFORM laplace.identity_codepoint_execute_batch(ARRAY[50, 53, 53]);
    EXCEPTION
        WHEN SQLSTATE 'XX001' THEN collision_detected := true;
    END;
    IF collision_detected IS NOT TRUE THEN
        RAISE EXCEPTION 'corrupted durable receipt was silently accepted';
    END IF;
END
$mutation$;

SELECT 'postgres.mutation-receipt-collision-detected passed' AS result;
