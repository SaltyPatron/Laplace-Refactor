-- Existing persistent clusters may already be at 1.0.1. Product readback
-- reconciliation runs after the verified 1.0.0 -> 1.0.1 indexed-cognition update,
-- so advance that same installed extension to the product cognition generation
-- before exposing any readback surface. Fresh 1.0.3 installations never execute
-- the version transition below; the recurring reconciliation still installs and
-- verifies additive native product entrypoints introduced within the 1.0.2 ABI.
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
        ALTER EXTENSION laplace UPDATE TO '1.0.3';
    ELSIF version = '1.0.2' THEN
        ALTER EXTENSION laplace UPDATE TO '1.0.3';
    ELSIF version <> '1.0.3' THEN
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

-- Reconcile the filter-first physical providers on already-persistent 1.0.3
-- clusters as well as on fresh installs.  The semantic candidate query addresses
-- mappings by (boundary, endpoint); without these indexes an otherwise set-wise
-- frontier can degrade into occurrence scans.  They are rebuildable accelerators
-- and do not change semantic authority.
CREATE INDEX IF NOT EXISTS reference_mapping_occurrence_left_semantic_idx
    ON laplace.reference_mapping_occurrence
    (boundary_id, left_value_entity_id, proposition_id, source_profile_id, occurrence_id)
    INCLUDE (right_value_entity_id, row_entity_id)
    WHERE disposition = 1;

CREATE INDEX IF NOT EXISTS reference_mapping_occurrence_right_semantic_idx
    ON laplace.reference_mapping_occurrence
    (boundary_id, right_value_entity_id, proposition_id, source_profile_id, occurrence_id)
    INCLUDE (left_value_entity_id, row_entity_id)
    WHERE disposition = 1;

CREATE OR REPLACE FUNCTION laplace.cognition_conversation_execute_product(
    laplace.execution_context,
    text,
    laplace.cognition_prompt_scope,
    laplace.cognition_firmware_product_request,
    bytea,
    bigint)
RETURNS laplace.cognition_firmware_product_result
AS 'laplace_pg', 'laplace_pg_cognition_conversation_product_execute'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

REVOKE EXECUTE ON FUNCTION laplace.cognition_conversation_execute_product(
    laplace.execution_context,
    text,
    laplace.cognition_prompt_scope,
    laplace.cognition_firmware_product_request,
    bytea,
    bigint) FROM PUBLIC;

DO $laplace_product_conversation_reconcile$
DECLARE
    procedure_oid oid;
    owner name;
BEGIN
    SELECT pg_catalog.pg_get_userbyid(e.extowner)
      INTO STRICT owner
      FROM pg_catalog.pg_extension AS e
     WHERE e.extname = 'laplace';
    IF owner <> current_user THEN
        RAISE EXCEPTION 'product conversation reconciliation requires the extension owner';
    END IF;

    procedure_oid := pg_catalog.to_regprocedure(
        'laplace.cognition_conversation_execute_product('
        'laplace.execution_context,text,laplace.cognition_prompt_scope,'
        'laplace.cognition_firmware_product_request,bytea,bigint)');
    IF procedure_oid IS NULL THEN
        RAISE EXCEPTION 'product conversation route was not created';
    END IF;

    IF NOT EXISTS (
        SELECT 1
          FROM pg_catalog.pg_depend AS d
          JOIN pg_catalog.pg_extension AS e ON e.oid = d.refobjid
         WHERE d.classid = 'pg_catalog.pg_proc'::pg_catalog.regclass
           AND d.objid = procedure_oid
           AND d.refclassid = 'pg_catalog.pg_extension'::pg_catalog.regclass
           AND d.deptype = 'e'
           AND e.extname = 'laplace'
    ) THEN
        ALTER EXTENSION laplace ADD FUNCTION laplace.cognition_conversation_execute_product(
            laplace.execution_context,
            text,
            laplace.cognition_prompt_scope,
            laplace.cognition_firmware_product_request,
            bytea,
            bigint);
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
         WHERE p.oid = procedure_oid
           AND e.extname = 'laplace'
           AND p.proowner = e.extowner
           AND l.lanname = 'c'
           AND p.probin = 'laplace_pg'
           AND p.prosrc = 'laplace_pg_cognition_conversation_product_execute'
           AND p.provolatile = 'v'
           AND p.proisstrict
           AND NOT p.prosecdef
    ) THEN
        RAISE EXCEPTION 'product conversation route is not owned native extension code';
    END IF;
END
$laplace_product_conversation_reconcile$;
