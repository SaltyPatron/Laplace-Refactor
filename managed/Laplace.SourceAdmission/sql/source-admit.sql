\set ON_ERROR_STOP on
\set source_copy @@SOURCE_COPY@@
BEGIN;
SET LOCAL synchronous_commit=on;
SET LOCAL statement_timeout=@@TIMEOUT_MS@@;
\if :source_copy
CREATE TEMP TABLE laplace_source_input (
    artifact_index integer PRIMARY KEY, content bytea NOT NULL
) ON COMMIT DROP;
\copy pg_temp.laplace_source_input FROM '@@COPY_PATH@@' WITH (FORMAT binary)
\endif

CREATE TEMP TABLE laplace_source_declaration ON COMMIT PRESERVE ROWS AS
SELECT @@SOURCE_GRAPH@@ AS graph, @@SELECTED_BOUNDARY@@ AS boundary,
       @@DENOMINATORS@@::jsonb AS denominators;

CREATE TEMP TABLE laplace_source_result ON COMMIT PRESERVE ROWS AS
SELECT admission.* FROM laplace.source_admit_tabular(@@ARGUMENTS@@) AS admission;

CREATE TEMP VIEW laplace_source_profile_matches AS
SELECT result.profile_id FROM pg_temp.laplace_source_result AS result
JOIN laplace.source_profile AS profile USING(profile_id)
CROSS JOIN pg_temp.laplace_source_declaration AS declaration
WHERE profile.artifact_graph_fingerprint=declaration.graph
  AND profile.selected_boundary_fingerprint=declaration.boundary
  AND profile.byte_count=(declaration.denominators->>'bytes')::numeric
  AND profile.container_count=(declaration.denominators->>'containers')::numeric
  AND profile.member_count=(declaration.denominators->>'members')::numeric
  AND profile.file_count=(declaration.denominators->>'files')::numeric
  AND profile.record_count=(declaration.denominators->>'records')::numeric
  AND profile.field_count=(declaration.denominators->>'fields')::numeric
  AND profile.claim_count=(declaration.denominators->>'claims')::numeric
  AND profile.mapping_count=(declaration.denominators->>'mappings')::numeric;

DO $closure$
BEGIN
    IF NOT EXISTS (SELECT FROM pg_temp.laplace_source_profile_matches) THEN
        RAISE EXCEPTION 'Source admission differs from its declared graph, boundary, or denominators';
    END IF;
END
$closure$;
COMMIT;

BEGIN READ ONLY;
SET LOCAL statement_timeout=@@TIMEOUT_MS@@;
SELECT json_build_object(
    'database',current_database(),'database_user',current_user,
    'postmaster_started_at',pg_postmaster_start_time(), 'admission',to_jsonb(result),
    'declared_profile_persisted',EXISTS (
        SELECT FROM pg_temp.laplace_source_profile_matches AS matched WHERE matched.profile_id=result.profile_id),
    'root_entity_persisted',EXISTS (
        SELECT FROM laplace.entity WHERE entity_id=result.root_entity_id),
    'root_physicality_persisted',EXISTS (
        SELECT FROM laplace.physicality WHERE physicality_id=result.root_physicality_id AND entity_id=result.root_entity_id),
    'world_admission_persisted',EXISTS (
        SELECT FROM laplace.world_admission WHERE admission_id=result.world_admission_id AND source_profile_id=result.profile_id)
) FROM pg_temp.laplace_source_result AS result;
COMMIT;
