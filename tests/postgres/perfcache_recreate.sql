DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 3 THEN
        RAISE EXCEPTION 'pre-recreate backend did not load the active epoch';
    END IF;
END
$contract$;

DROP EXTENSION laplace CASCADE;
CREATE EXTENSION laplace;

CREATE FUNCTION laplace._test_perfcache_admit(
    bigint, boolean, bytea, bytea, bytea)
RETURNS void
AS 'laplace_pg', 'laplace_pg_test_perfcache_admit'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace._test_perfcache_metric(integer)
RETURNS bigint
AS 'laplace_pg', 'laplace_pg_test_perfcache_metric'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace._test_perfcache_lookup(integer[])
RETURNS bigint[]
AS 'laplace_pg', 'laplace_pg_test_perfcache_lookup'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

SELECT laplace._test_perfcache_admit(
    0, false,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'perfcache_manifest_1', 'hex'));

DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 1
       OR laplace._test_perfcache_lookup(ARRAY[0, 2]) <>
          ARRAY[100, 102]::bigint[]
       OR (SELECT count(*) FROM laplace.perfcache_generation) <> 1
       OR (SELECT count(*) FROM laplace.perfcache_activation_event) <> 1 THEN
        RAISE EXCEPTION 'same-postmaster extension recreation retained stale state';
    END IF;
END
$contract$;

SELECT 'postgres.perfcache-same-postmaster-recreate passed' AS result;
