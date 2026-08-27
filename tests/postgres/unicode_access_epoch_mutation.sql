\ir unicode_root_contract.sql

CREATE OR REPLACE FUNCTION laplace.unicode_tier0_resolve_batch(
    bytea, bytea, integer[])
RETURNS laplace.unicode_tier0_batch_result
AS :'unicode_access_mutant_module', 'laplace_pg_unicode_tier0_resolve_batch'
LANGUAGE C STABLE STRICT PARALLEL UNSAFE;

DO $mutation$
BEGIN
    PERFORM laplace.unicode_tier0_resolve_batch(
        decode(repeat('41', 16), 'hex'),
        decode(repeat('43', 32), 'hex'),
        ARRAY[65]);
    RAISE EXCEPTION
        'Unicode access expected-epoch mutant accepted a stale generation';
END
$mutation$;
