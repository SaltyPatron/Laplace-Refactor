DO $contract$
DECLARE
    values bigint[];
BEGIN
    values := laplace._test_perfcache_lookup(ARRAY[0, 2]);
    IF laplace._test_perfcache_metric(1) <> 2
       OR (SELECT count(*) FROM laplace.perfcache_generation) <> 2
       OR (SELECT count(*) FROM laplace.perfcache_activation_event) <> 2
       OR values NOT IN (
           ARRAY[200, 202]::bigint[],
           ARRAY[700, 702]::bigint[]) THEN
        RAISE EXCEPTION 'competing perfcache activation did not converge exactly';
    END IF;
END
$contract$;

SELECT 'postgres.perfcache-single-concurrent-winner passed' AS result;
