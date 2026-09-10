-- Existing persistent clusters may already be at 1.0.1. Product readback
-- reconciliation runs after the verified 1.0.0 -> 1.0.1 indexed-cognition update,
-- so advance that same installed extension to the product cognition generation
-- before exposing any readback surface. Fresh 1.0.2 installations never execute
-- this standalone reconciliation prefix.
DO $laplace_product_cognition_upgrade$
DECLARE
    version text;
    owner name;
BEGIN
    SELECT e.extversion, pg_catalog.pg_get_userbyid(e.extowner)
      INTO STRICT version, owner
      FROM pg_catalog.pg_extension AS e
     WHERE e.extname = 'laplace';
    IF owner <> current_user THEN
        RAISE EXCEPTION 'product cognition update requires the extension owner';
    END IF;
    IF version = '1.0.1' THEN
        ALTER EXTENSION laplace UPDATE TO '1.0.2';
    ELSIF version <> '1.0.2' THEN
        RAISE EXCEPTION 'unsupported product cognition predecessor version: %', version;
    END IF;

    IF NOT EXISTS (
        SELECT 1
          FROM pg_catalog.pg_proc AS p
          JOIN pg_catalog.pg_language AS l ON l.oid = p.prolang
          JOIN pg_catalog.pg_depend AS d
            ON d.classid = 'pg_catalog.pg_proc'::pg_catalog.regclass
           AND d.objid = p.oid
           AND d.refclassid = 'pg_catalog.pg_extension'::pg_catalog.regclass
           AND d.deptype = 'e'
          JOIN pg_catalog.pg_extension AS e ON e.oid = d.refobjid
         WHERE p.oid = pg_catalog.to_regprocedure(
             'laplace.cognition_firmware_execute_product('
             'laplace.execution_context,bytea,text,laplace.cognition_prompt_scope,'
             'laplace.cognition_firmware_product_request,bytea,bigint)')
           AND e.extname = 'laplace'
           AND p.proowner = e.extowner
           AND l.lanname = 'c'
           AND p.probin = 'laplace_pg'
           AND p.prosrc = 'laplace_pg_cognition_product_execute'
           AND p.provolatile = 'v'
           AND p.proisstrict
           AND NOT p.prosecdef
    ) THEN
        RAISE EXCEPTION 'product cognition route is not owned native extension code';
    END IF;
END
$laplace_product_cognition_upgrade$;
