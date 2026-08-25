CREATE EXTENSION laplace;

CREATE FUNCTION laplace._test_perfcache_admit_mutant(
    bigint, boolean, bytea, bytea, bytea)
RETURNS void
AS :'perfcache_mutant_module', 'laplace_pg_test_perfcache_admit_expected_epoch_mutant'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

SELECT laplace._test_perfcache_admit_mutant(
    1, false,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'perfcache_manifest_1', 'hex'));

SELECT laplace._test_perfcache_admit_mutant(
    0, false,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'perfcache_manifest_2', 'hex'));

DO $mutation$
BEGIN
    RAISE EXCEPTION 'perfcache expected-epoch mutant accepted a stale activation';
END
$mutation$;
