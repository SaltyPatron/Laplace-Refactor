BEGIN TRANSACTION READ ONLY;

SELECT 'observation', clock_timestamp()::text, current_database(), current_user;

SELECT
    'relation',
    n.nspname,
    c.relname,
    c.relkind::text,
    pg_total_relation_size(c.oid)::text
FROM pg_catalog.pg_class AS c
JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
WHERE n.nspname !~ '^pg_'
  AND n.nspname <> 'information_schema'
  AND c.relkind IN ('r', 'p', 'v', 'm')
  AND c.relname ~* '(entit|physical|attest|consensus|occurr|lineage|depend|deriv|epoch)'
ORDER BY n.nspname, c.relname;

SELECT
    'column',
    n.nspname,
    c.relname,
    a.attnum::text,
    a.attname,
    pg_catalog.format_type(a.atttypid, a.atttypmod),
    a.attnotnull::text
FROM pg_catalog.pg_class AS c
JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
JOIN pg_catalog.pg_attribute AS a ON a.attrelid = c.oid
WHERE n.nspname !~ '^pg_'
  AND n.nspname <> 'information_schema'
  AND c.relkind IN ('r', 'p', 'v', 'm')
  AND a.attnum > 0
  AND NOT a.attisdropped
  AND (
      c.relname ~* '(entit|physical|attest|consensus|occurr|lineage|depend|deriv|epoch)'
      OR a.attname ~* '(physical|attest|consensus|occurr|lineage|depend|deriv|epoch|witness|source|valid|observed)'
  )
ORDER BY n.nspname, c.relname, a.attnum;

SELECT
    'routine',
    n.nspname,
    p.proname,
    p.prokind::text,
    pg_catalog.pg_get_function_identity_arguments(p.oid),
    pg_catalog.format_type(p.prorettype, NULL)
FROM pg_catalog.pg_proc AS p
JOIN pg_catalog.pg_namespace AS n ON n.oid = p.pronamespace
WHERE n.nspname !~ '^pg_'
  AND n.nspname <> 'information_schema'
  AND p.proname ~* '(physical|attest|consensus|occurr|lineage|depend|deriv|epoch|witness)'
ORDER BY n.nspname, p.proname, pg_catalog.pg_get_function_identity_arguments(p.oid);

SELECT
    'constraint',
    n.nspname,
    c.relname,
    con.conname,
    con.contype::text,
    pg_catalog.pg_get_constraintdef(con.oid, false)
FROM pg_catalog.pg_constraint AS con
JOIN pg_catalog.pg_class AS c ON c.oid = con.conrelid
JOIN pg_catalog.pg_namespace AS n ON n.oid = c.relnamespace
WHERE n.nspname !~ '^pg_'
  AND n.nspname <> 'information_schema'
  AND c.relname ~* '(entit|physical|attest|consensus|occurr|lineage|depend|deriv|epoch)'
ORDER BY n.nspname, c.relname, con.conname;

ROLLBACK;
