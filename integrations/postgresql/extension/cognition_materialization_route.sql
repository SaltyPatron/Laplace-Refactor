-- Exact realization materialization/readback route.
--
-- This reconciliation program is intentionally safe in both contexts in which it
-- is installed:
--   * while PostgreSQL is executing the Laplace extension upgrade script; and
--   * as the packaged reconciliation program against an already-active 1.0.2
--     cluster whose binary has been replaced by the successor package.
--
-- Objects created by an extension script are members automatically.  Objects
-- created by the standalone reconciliation path are attached explicitly after
-- exact owner/shape checks, so the installed cluster never accumulates shadow
-- functions or unowned schema state.

DO $laplace_materialization_types$
DECLARE
    extension_oid oid;
    extension_owner oid;
    type_oid oid;
    names text[];
    types text[];
BEGIN
    SELECT e.oid, e.extowner
      INTO STRICT extension_oid, extension_owner
      FROM pg_catalog.pg_extension AS e
     WHERE e.extname = 'laplace';
    IF pg_catalog.pg_get_userbyid(extension_owner) <> current_user THEN
        RAISE EXCEPTION 'materialization route reconciliation requires the Laplace extension owner';
    END IF;

    type_oid := pg_catalog.to_regtype('laplace.cognition_realization_result');
    IF type_oid IS NULL THEN
        EXECUTE $ddl$
            CREATE TYPE laplace.cognition_realization_result AS (
                content_id bytea,
                language_id bytea,
                candidate_receipt_id bytea,
                realization_recipe_id bytea,
                missing_obligation_fingerprint bytea,
                preference_rank numeric(20,0),
                reused_subtree_count numeric(20,0),
                generated_composition_count numeric(20,0),
                structural_tier integer,
                match_class integer,
                missing_obligation_count integer,
                disposition integer,
                flags integer,
                version integer)
        $ddl$;
        type_oid := pg_catalog.to_regtype('laplace.cognition_realization_result');
    END IF;
    SELECT pg_catalog.array_agg(a.attname::text ORDER BY a.attnum),
           pg_catalog.array_agg(pg_catalog.format_type(a.atttypid, a.atttypmod) ORDER BY a.attnum)
      INTO names, types
      FROM pg_catalog.pg_attribute AS a
     WHERE a.attrelid = type_oid::pg_catalog.regclass
       AND a.attnum > 0 AND NOT a.attisdropped;
    IF names <> ARRAY[
            'content_id','language_id','candidate_receipt_id','realization_recipe_id',
            'missing_obligation_fingerprint','preference_rank','reused_subtree_count',
            'generated_composition_count','structural_tier','match_class',
            'missing_obligation_count','disposition','flags','version']::text[]
       OR types <> ARRAY[
            'bytea','bytea','bytea','bytea','bytea','numeric(20,0)','numeric(20,0)',
            'numeric(20,0)','integer','integer','integer','integer','integer','integer']::text[]
       OR NOT EXISTS (
            SELECT 1 FROM pg_catalog.pg_type AS t
             WHERE t.oid = type_oid AND t.typowner = extension_owner AND t.typtype = 'c') THEN
        RAISE EXCEPTION 'incompatible cognition_realization_result composite';
    END IF;

    type_oid := pg_catalog.to_regtype('laplace.cognition_materialization_request');
    IF type_oid IS NULL THEN
        EXECUTE $ddl$
            CREATE TYPE laplace.cognition_materialization_request AS (
                maximum_nodes numeric(20,0),
                maximum_trajectory_carriers numeric(20,0),
                maximum_output_bytes numeric(20,0),
                maximum_depth integer,
                version integer)
        $ddl$;
        type_oid := pg_catalog.to_regtype('laplace.cognition_materialization_request');
    END IF;
    SELECT pg_catalog.array_agg(a.attname::text ORDER BY a.attnum),
           pg_catalog.array_agg(pg_catalog.format_type(a.atttypid, a.atttypmod) ORDER BY a.attnum)
      INTO names, types
      FROM pg_catalog.pg_attribute AS a
     WHERE a.attrelid = type_oid::pg_catalog.regclass
       AND a.attnum > 0 AND NOT a.attisdropped;
    IF names <> ARRAY[
            'maximum_nodes','maximum_trajectory_carriers','maximum_output_bytes',
            'maximum_depth','version']::text[]
       OR types <> ARRAY[
            'numeric(20,0)','numeric(20,0)','numeric(20,0)','integer','integer']::text[]
       OR NOT EXISTS (
            SELECT 1 FROM pg_catalog.pg_type AS t
             WHERE t.oid = type_oid AND t.typowner = extension_owner AND t.typtype = 'c') THEN
        RAISE EXCEPTION 'incompatible cognition_materialization_request composite';
    END IF;

    type_oid := pg_catalog.to_regtype('laplace.cognition_materialization_result');
    IF type_oid IS NULL THEN
        EXECUTE $ddl$
            CREATE TYPE laplace.cognition_materialization_result AS (
                output bytea,
                materialization_id bytea,
                source_candidate_receipt_id bytea,
                source_recipe_id bytea,
                provider_fingerprint bytea,
                readset_fingerprint bytea,
                output_fingerprint bytea,
                root_content_id bytea,
                resolved_node_count numeric(20,0),
                trajectory_carrier_count numeric(20,0),
                codepoint_count numeric(20,0),
                output_bytes numeric(20,0),
                maximum_depth_observed integer,
                status integer,
                version integer)
        $ddl$;
        type_oid := pg_catalog.to_regtype('laplace.cognition_materialization_result');
    END IF;
    SELECT pg_catalog.array_agg(a.attname::text ORDER BY a.attnum),
           pg_catalog.array_agg(pg_catalog.format_type(a.atttypid, a.atttypmod) ORDER BY a.attnum)
      INTO names, types
      FROM pg_catalog.pg_attribute AS a
     WHERE a.attrelid = type_oid::pg_catalog.regclass
       AND a.attnum > 0 AND NOT a.attisdropped;
    IF names <> ARRAY[
            'output','materialization_id','source_candidate_receipt_id','source_recipe_id',
            'provider_fingerprint','readset_fingerprint','output_fingerprint','root_content_id',
            'resolved_node_count','trajectory_carrier_count','codepoint_count','output_bytes',
            'maximum_depth_observed','status','version']::text[]
       OR types <> ARRAY[
            'bytea','bytea','bytea','bytea','bytea','bytea','bytea','bytea',
            'numeric(20,0)','numeric(20,0)','numeric(20,0)','numeric(20,0)',
            'integer','integer','integer']::text[]
       OR NOT EXISTS (
            SELECT 1 FROM pg_catalog.pg_type AS t
             WHERE t.oid = type_oid AND t.typowner = extension_owner AND t.typtype = 'c') THEN
        RAISE EXCEPTION 'incompatible cognition_materialization_result composite';
    END IF;
END
$laplace_materialization_types$;

DO $laplace_materialization_function_collision$
DECLARE
    function_oid oid;
    extension_owner oid;
BEGIN
    SELECT e.extowner INTO STRICT extension_owner
      FROM pg_catalog.pg_extension AS e
     WHERE e.extname = 'laplace';
    function_oid := pg_catalog.to_regprocedure(
        'laplace.cognition_realization_materialize_utf8('
        'laplace.execution_context,laplace.cognition_realization_result,'
        'laplace.cognition_materialization_request)');
    IF function_oid IS NOT NULL AND NOT EXISTS (
        SELECT 1
          FROM pg_catalog.pg_proc AS p
          JOIN pg_catalog.pg_language AS l ON l.oid = p.prolang
         WHERE p.oid = function_oid
           AND p.proowner = extension_owner
           AND l.lanname = 'c'
           AND p.probin = 'laplace_pg'
           AND p.prosrc = 'laplace_pg_cognition_realization_materialize_utf8'
           AND p.prorettype = pg_catalog.to_regtype('laplace.cognition_materialization_result')
           AND p.provolatile = 'v'
           AND p.proisstrict
           AND p.proparallel = 'u'
           AND NOT p.prosecdef
           AND NOT p.proretset) THEN
        RAISE EXCEPTION 'incompatible cognition_realization_materialize_utf8 function';
    END IF;
END
$laplace_materialization_function_collision$;

CREATE OR REPLACE FUNCTION laplace.cognition_realization_materialize_utf8(
    laplace.execution_context,
    laplace.cognition_realization_result,
    laplace.cognition_materialization_request)
RETURNS laplace.cognition_materialization_result
AS 'laplace_pg', 'laplace_pg_cognition_realization_materialize_utf8'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

REVOKE ALL ON FUNCTION laplace.cognition_realization_materialize_utf8(
    laplace.execution_context,
    laplace.cognition_realization_result,
    laplace.cognition_materialization_request) FROM PUBLIC;

DO $laplace_materialization_extension_membership$
DECLARE
    extension_oid oid;
    type_oid oid;
    function_oid oid;
BEGIN
    SELECT e.oid INTO STRICT extension_oid
      FROM pg_catalog.pg_extension AS e
     WHERE e.extname = 'laplace';

    FOREACH type_oid IN ARRAY ARRAY[
        pg_catalog.to_regtype('laplace.cognition_realization_result')::oid,
        pg_catalog.to_regtype('laplace.cognition_materialization_request')::oid,
        pg_catalog.to_regtype('laplace.cognition_materialization_result')::oid]
    LOOP
        IF NOT EXISTS (
            SELECT 1 FROM pg_catalog.pg_depend AS d
             WHERE d.classid = 'pg_catalog.pg_type'::pg_catalog.regclass
               AND d.objid = type_oid
               AND d.refclassid = 'pg_catalog.pg_extension'::pg_catalog.regclass
               AND d.refobjid = extension_oid
               AND d.deptype = 'e') THEN
            EXECUTE pg_catalog.format(
                'ALTER EXTENSION laplace ADD TYPE %s', type_oid::pg_catalog.regtype);
        END IF;
    END LOOP;

    function_oid := pg_catalog.to_regprocedure(
        'laplace.cognition_realization_materialize_utf8('
        'laplace.execution_context,laplace.cognition_realization_result,'
        'laplace.cognition_materialization_request)');
    IF NOT EXISTS (
        SELECT 1 FROM pg_catalog.pg_depend AS d
         WHERE d.classid = 'pg_catalog.pg_proc'::pg_catalog.regclass
           AND d.objid = function_oid
           AND d.refclassid = 'pg_catalog.pg_extension'::pg_catalog.regclass
           AND d.refobjid = extension_oid
           AND d.deptype = 'e') THEN
        EXECUTE pg_catalog.format(
            'ALTER EXTENSION laplace ADD FUNCTION %s',
            function_oid::pg_catalog.regprocedure);
    END IF;
END
$laplace_materialization_extension_membership$;
