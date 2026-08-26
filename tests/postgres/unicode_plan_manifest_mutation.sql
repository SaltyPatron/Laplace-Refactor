CREATE EXTENSION laplace;

CREATE FUNCTION pg_temp.mutated_unicode_plan_manifest()
RETURNS bytea
AS :'persistence_mutant_module',
   'laplace_test_unicode_postgresql_plan_manifest'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

DO $mutation$
BEGIN
    IF pg_temp.mutated_unicode_plan_manifest() <>
       decode(
           '9f331f03785b878c03fa36c4d78384695c3bb69a573995021281fb05c7e6c72a',
           'hex') THEN
        RAISE EXCEPTION
            'exact plan manifest detected the substituted SQL plan';
    END IF;
    RAISE EXCEPTION
        'substituted SQL plan retained the canonical plan manifest';
END
$mutation$;
