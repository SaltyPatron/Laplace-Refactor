\pset pager off
\pset format csv
\pset footer off

BEGIN TRANSACTION READ ONLY;

SELECT 'server' AS section,
       current_database() AS database_name,
       current_setting('server_version') AS server_version,
       pg_is_in_recovery() AS is_in_recovery,
       pg_database_size(current_database()) AS database_bytes,
       current_timestamp AS observed_at;

SELECT 'extension' AS section,
       extension_name,
       extension_version,
       schema_name
FROM (
    SELECT e.extname AS extension_name,
           e.extversion AS extension_version,
           n.nspname AS schema_name
    FROM pg_extension AS e
    JOIN pg_namespace AS n ON n.oid = e.extnamespace
) AS installed
ORDER BY extension_name;

SELECT 'activity' AS section,
       pid,
       backend_type,
       application_name,
       state,
       wait_event_type,
       wait_event,
       floor(extract(epoch FROM (clock_timestamp() - query_start)))::bigint AS query_seconds,
       floor(extract(epoch FROM (clock_timestamp() - xact_start)))::bigint AS transaction_seconds,
       left(regexp_replace(query, '[[:space:]]+', ' ', 'g'), 500) AS query_excerpt
FROM pg_stat_activity
WHERE datname = current_database()
  AND pid <> pg_backend_pid()
ORDER BY query_start NULLS LAST, pid;

SELECT 'relation' AS section,
       n.nspname AS schema_name,
       c.relname AS relation_name,
       c.relkind AS relation_kind,
       c.reltuples::bigint AS estimated_rows,
       pg_total_relation_size(c.oid) AS total_bytes,
       pg_relation_size(c.oid) AS heap_bytes,
       pg_indexes_size(c.oid) AS index_bytes
FROM pg_class AS c
JOIN pg_namespace AS n ON n.oid = c.relnamespace
WHERE c.relkind IN ('r', 'm', 'p')
  AND n.nspname NOT IN ('pg_catalog', 'information_schema')
  AND n.nspname !~ '^pg_toast'
ORDER BY pg_total_relation_size(c.oid) DESC, n.nspname, c.relname
LIMIT 100;

SELECT 'index' AS section,
       n.nspname AS schema_name,
       table_class.relname AS table_name,
       index_class.relname AS index_name,
       pg_relation_size(index_class.oid) AS index_bytes,
       coalesce(stats.idx_scan, 0) AS scan_count,
       pg_get_indexdef(index_class.oid) AS definition
FROM pg_index AS index_catalog
JOIN pg_class AS index_class ON index_class.oid = index_catalog.indexrelid
JOIN pg_class AS table_class ON table_class.oid = index_catalog.indrelid
JOIN pg_namespace AS n ON n.oid = table_class.relnamespace
LEFT JOIN pg_stat_user_indexes AS stats ON stats.indexrelid = index_class.oid
WHERE n.nspname NOT IN ('pg_catalog', 'information_schema')
ORDER BY pg_relation_size(index_class.oid) DESC, n.nspname, table_class.relname, index_class.relname
LIMIT 100;

SELECT 'surface-summary' AS section,
       n.nspname AS schema_name,
       count(*) FILTER (WHERE p.prokind = 'f') AS function_count,
       count(*) FILTER (WHERE p.prokind = 'p') AS procedure_count,
       count(*) FILTER (WHERE p.provolatile = 'i') AS immutable_count,
       count(*) FILTER (WHERE p.provolatile = 's') AS stable_count,
       count(*) FILTER (WHERE p.provolatile = 'v') AS volatile_count,
       count(*) FILTER (WHERE p.proconfig IS NOT NULL) AS configured_count
FROM pg_proc AS p
JOIN pg_namespace AS n ON n.oid = p.pronamespace
WHERE n.nspname NOT IN ('pg_catalog', 'information_schema')
GROUP BY n.nspname
ORDER BY n.nspname;

SELECT 'configured-surface' AS section,
       n.nspname AS schema_name,
       p.proname AS routine_name,
       p.prokind AS routine_kind,
       p.provolatile AS volatility,
       p.proparallel AS parallel_safety,
       pg_get_function_identity_arguments(p.oid) AS identity_arguments,
       p.proconfig AS attached_configuration
FROM pg_proc AS p
JOIN pg_namespace AS n ON n.oid = p.pronamespace
WHERE n.nspname NOT IN ('pg_catalog', 'information_schema')
  AND p.proconfig IS NOT NULL
ORDER BY n.nspname, p.proname, pg_get_function_identity_arguments(p.oid);

SELECT 'type' AS section,
       n.nspname AS schema_name,
       t.typname AS type_name,
       t.typtype AS type_kind,
       t.typcategory AS type_category,
       t.typlen AS internal_length,
       t.typbyval AS passed_by_value,
       t.typalign AS alignment,
       t.typstorage AS storage
FROM pg_type AS t
JOIN pg_namespace AS n ON n.oid = t.typnamespace
WHERE n.nspname NOT IN ('pg_catalog', 'information_schema')
  AND t.typtype IN ('b', 'd', 'e', 'r', 'm')
  AND t.typrelid = 0
  AND t.typelem = 0
ORDER BY n.nspname, t.typname;

SELECT 'operator-class' AS section,
       n.nspname AS schema_name,
       opclass.opcname AS operator_class,
       access_method.amname AS access_method,
       opclass.opcdefault AS is_default,
       pg_catalog.format_type(opclass.opcintype, NULL) AS indexed_type
FROM pg_opclass AS opclass
JOIN pg_namespace AS n ON n.oid = opclass.opcnamespace
JOIN pg_am AS access_method ON access_method.oid = opclass.opcmethod
WHERE n.nspname NOT IN ('pg_catalog', 'information_schema')
ORDER BY n.nspname, opclass.opcname;

SELECT count(*) = 1 AS has_pg_stat_statements,
       coalesce(max(format('%I.%I', n.nspname, c.relname)), '') AS pg_stat_statements_relation
FROM pg_extension AS e
JOIN pg_namespace AS n ON n.oid = e.extnamespace
LEFT JOIN pg_class AS c
       ON c.relnamespace = n.oid
      AND c.relname = 'pg_stat_statements'
WHERE e.extname = 'pg_stat_statements'
  AND c.oid IS NOT NULL
\gset
\if :has_pg_stat_statements
  SELECT 'statement' AS section,
         queryid,
         calls,
         round(total_exec_time::numeric, 3) AS total_exec_ms,
         round(mean_exec_time::numeric, 3) AS mean_exec_ms,
         rows,
         shared_blks_hit,
         shared_blks_read,
         temp_blks_read,
         temp_blks_written,
         left(regexp_replace(query, '[[:space:]]+', ' ', 'g'), 1000) AS query_excerpt
  FROM :pg_stat_statements_relation
  WHERE dbid = (SELECT oid FROM pg_database WHERE datname = current_database())
  ORDER BY total_exec_time DESC, calls DESC
  LIMIT 100;
\else
  SELECT 'statement-statistics-unavailable' AS section,
         'pg_stat_statements is not installed in this database' AS observation;
\endif

COMMIT;
