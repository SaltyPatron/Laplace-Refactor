CREATE TEMP TABLE concurrency_expected (
    physicality bytea,
    occurrence bytea,
    receipt_count bigint
);
INSERT INTO concurrency_expected VALUES (
    decode(:'persistence_concurrent_physicality', 'hex'),
    decode(:'persistence_concurrent_occurrence', 'hex'),
    :'persistence_expected_receipt_count'::bigint
);

DO $verify$
DECLARE
    expected concurrency_expected%ROWTYPE;
BEGIN
    SELECT * INTO STRICT expected FROM concurrency_expected;
    IF (SELECT count(*) FROM laplace.physicality
        WHERE physicality_id = expected.physicality
          AND vertex_count = 3
          AND octet_length(trajectory) = 3 * 32) <> 1
       OR (SELECT count(*) FROM laplace.attestation
           WHERE attestation_id = expected.occurrence
             AND physicality_id = expected.physicality
             AND attestation_kind = 1) <> 1
       OR (SELECT count(*) FROM laplace.canonical_deposit_receipt) <>
          expected.receipt_count THEN
        RAISE EXCEPTION 'concurrent exact-stream deposit did not converge to one immutable state';
    END IF;
END
$verify$;
