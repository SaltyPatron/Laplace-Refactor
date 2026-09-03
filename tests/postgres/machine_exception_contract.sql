CREATE EXTENSION laplace;

CREATE TEMP TABLE machine_exception_expected AS
WITH encoded AS (
    SELECT trim(trailing E'\n' FROM convert_from(
        decode(:'machine_exception_expected_hex', 'hex'), 'UTF8')) AS rows
), lines AS (
    SELECT line, ordinal
    FROM encoded,
         regexp_split_to_table(rows, E'\n') WITH ORDINALITY AS value(line, ordinal)
), fields AS (
    SELECT ordinal, string_to_array(line, ',') AS value
    FROM lines
)
SELECT ordinal,
       value[1]::bigint AS condition,
       value[2]::bigint AS kind,
       value[3]::bigint AS priority,
       value[4]::bigint AS capability_flags,
       value[5]::bigint AS recovery_disposition,
       value[6]::bigint AS publication_disposition
FROM fields;

DO $contract$
DECLARE
    mismatch_count bigint;
BEGIN
    WITH actual AS (
        SELECT row_number() OVER () AS ordinal, descriptor.*
        FROM laplace.machine_exception_registry() AS descriptor
    ), differences AS (
        (SELECT * FROM actual EXCEPT ALL SELECT * FROM machine_exception_expected)
        UNION ALL
        (SELECT * FROM machine_exception_expected EXCEPT ALL SELECT * FROM actual)
    )
    SELECT count(*) INTO mismatch_count FROM differences;
    IF mismatch_count <> 0 OR
       (SELECT count(*) FROM machine_exception_expected) = 0 OR
       (SELECT count(DISTINCT condition) FROM machine_exception_expected) <>
           (SELECT count(*) FROM machine_exception_expected) THEN
        RAISE EXCEPTION
            'PostgreSQL machine-exception registry differs from ordered native descriptors';
    END IF;
END
$contract$;

\if :{?machine_exception_mutant_module}
CREATE FUNCTION pg_temp.machine_exception_registry_mutant()
RETURNS SETOF laplace.machine_exception_descriptor
AS :'machine_exception_mutant_module', 'laplace_pg_machine_exception_registry'
LANGUAGE C STABLE PARALLEL SAFE;

DO $mutation$
DECLARE
    rejected boolean := false;
BEGIN
    BEGIN
        PERFORM * FROM pg_temp.machine_exception_registry_mutant();
    EXCEPTION WHEN data_exception THEN
        rejected := true;
    END;
    IF NOT rejected THEN
        RAISE EXCEPTION
            'PostgreSQL route accepted a descriptor with capability drift';
    END IF;
END
$mutation$;
\endif
