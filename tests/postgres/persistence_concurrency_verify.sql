CREATE TEMP TABLE concurrency_expected (
    physicality bytea,
    occurrence bytea
);
INSERT INTO concurrency_expected VALUES (
    decode(:'persistence_concurrent_physicality', 'hex'),
    decode(:'persistence_concurrent_occurrence', 'hex')
);

DO $verify$
DECLARE
    expected concurrency_expected%ROWTYPE;
BEGIN
    SELECT * INTO STRICT expected FROM concurrency_expected;
    IF (SELECT count(*) FROM laplace.physicality
        WHERE physicality_id = expected.physicality) <> 1
       OR (SELECT count(*) FROM laplace.composition_trajectory_vertex
           WHERE physicality_id = expected.physicality) <> 3
       OR (SELECT count(*) FROM laplace.observed_occurrence
           WHERE occurrence_id = expected.occurrence
             AND physicality_id = expected.physicality) <> 1
       OR (SELECT count(*) FROM laplace.canonical_deposit_receipt) <> 4 THEN
        RAISE EXCEPTION 'concurrent exact-stream deposit did not converge to one immutable state';
    END IF;
END
$verify$;
