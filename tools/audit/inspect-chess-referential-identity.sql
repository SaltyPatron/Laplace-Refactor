\pset pager off
\pset format csv
\pset footer off
\pset tuples_only on

BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ READ ONLY;
SET LOCAL statement_timeout = '60s';

SELECT 'observation' AS section,
       jsonb_build_object(
           'database', current_database(),
           'server_version', current_setting('server_version'),
           'transaction_timestamp', transaction_timestamp(),
           'clock_timestamp', clock_timestamp(),
           'snapshot', txid_current_snapshot(),
           'transaction_read_only', current_setting('transaction_read_only'),
           'transaction_isolation', current_setting('transaction_isolation')
       ) AS payload;

SELECT 'function_contracts' AS section,
       jsonb_agg(
           jsonb_build_object(
               'schema', n.nspname,
               'name', p.proname,
               'arguments', pg_get_function_identity_arguments(p.oid),
               'result', pg_get_function_result(p.oid),
               'volatility', p.provolatile,
               'parallel_safety', p.proparallel,
               'definition_sha256', encode(
                   sha256(convert_to(pg_get_functiondef(p.oid), 'UTF8')),
                   'hex'
               )
           )
           ORDER BY p.proname, pg_get_function_identity_arguments(p.oid)
       ) AS payload
FROM pg_proc AS p
JOIN pg_namespace AS n ON n.oid = p.pronamespace
WHERE n.nspname = 'chess'
  AND p.proname IN (
      'player_games',
      'player_id',
      'player_ratings',
      'player_record'
  );

WITH name_inputs(name) AS (
    VALUES
        ('Fischer, Robert J'::text),
        ('Robert J Fischer'::text),
        ('Robert James Fischer'::text),
        ('Bobby Fischer'::text),
        ('Fischer'::text),
        ('BobbyFischer'::text)
), resolved AS (
    SELECT name,
           encode(chess.player_id(name), 'hex') AS player_id
    FROM name_inputs
)
SELECT 'name_resolution' AS section,
       jsonb_build_object(
           'inputs', jsonb_agg(
               jsonb_build_object('name', name, 'player_id', player_id)
               ORDER BY name
           ),
           'fischer_comma_robert_j_equals_robert_j_fischer',
               max(player_id) FILTER (WHERE name = 'Fischer, Robert J') =
               max(player_id) FILTER (WHERE name = 'Robert J Fischer'),
           'distinct_player_id_count', count(DISTINCT player_id)
       ) AS payload
FROM resolved;

WITH target AS (
    SELECT chess.player_id('Fischer, Robert J') AS player_id,
           encode(chess.player_id('Fischer, Robert J'), 'hex') AS player_id_hex
), games AS MATERIALIZED (
    SELECT g.*,
           CASE
               WHEN left(g.played_on, 4) ~ '^[0-9]{4}$'
                   THEN left(g.played_on, 4)::integer
               ELSE NULL
           END AS played_year
    FROM target AS t
    CROSS JOIN LATERAL chess.player_games(t.player_id, 5000, 0) AS g
), ranked_games AS (
    SELECT games.*,
           row_number() OVER (
               PARTITION BY played_year
               ORDER BY played_on, event, opponent
           ) AS year_rank
    FROM games
), selected_games AS (
    SELECT played_on,
           event,
           eco,
           as_white,
           opponent,
           result,
           outcome,
           played_year
    FROM ranked_games
    WHERE played_year > 2008
       OR event ILIKE '%Virginia%'
       OR event ILIKE '%Colonial%'
       OR (played_year IN (1956, 1972, 1992) AND year_rank <= 5)
    ORDER BY played_year NULLS LAST, played_on, event, opponent
    LIMIT 50
), year_bands AS (
    SELECT CASE
               WHEN played_year IS NULL THEN 'unknown'
               WHEN played_year <= 1972 THEN 'through_1972'
               WHEN played_year <= 2008 THEN '1973_through_2008'
               ELSE 'after_2008'
           END AS band,
           count(*) AS games
    FROM games
    GROUP BY 1
)
SELECT 'player_observation' AS section,
       jsonb_build_object(
           'query_name', 'Fischer, Robert J',
           'player_id', t.player_id_hex,
           'requested_game_limit', 5000,
           'returned_games', (SELECT count(*) FROM games),
           'minimum_parsed_year', (SELECT min(played_year) FROM games),
           'maximum_parsed_year', (SELECT max(played_year) FROM games),
           'games_in_2024', (
               SELECT count(*) FROM games WHERE played_year = 2024
           ),
           'distinct_events', (SELECT count(DISTINCT event) FROM games),
           'distinct_opponents', (SELECT count(DISTINCT opponent_id) FROM games),
           'year_bands', (
               SELECT jsonb_object_agg(band, games ORDER BY band)
               FROM year_bands
           ),
           'record', (
               SELECT jsonb_agg(to_jsonb(r) ORDER BY r.as_white DESC)
               FROM chess.player_record(t.player_id) AS r
           ),
           'ratings', (
               SELECT jsonb_agg(to_jsonb(r) ORDER BY r.rating)
               FROM chess.player_ratings(t.player_id) AS r
           ),
           'selected_games', (
               SELECT jsonb_agg(to_jsonb(g) ORDER BY g.played_year, g.played_on, g.event, g.opponent)
               FROM selected_games AS g
           )
       ) AS payload
FROM target AS t;

SELECT 'concurrent_chess_activity' AS section,
       jsonb_build_object(
           'matching_other_backends', count(*),
           'oldest_query_seconds', coalesce(
               max(floor(extract(epoch FROM (clock_timestamp() - query_start)))::bigint),
               0
           ),
           'states', coalesce(
               jsonb_object_agg(state, state_count ORDER BY state),
               '{}'::jsonb
           )
       ) AS payload
FROM (
    SELECT coalesce(state, '<null>') AS state,
           count(*) AS state_count,
           min(query_start) AS query_start
    FROM pg_stat_activity
    WHERE datname = current_database()
      AND pid <> pg_backend_pid()
      AND query ~* '(chess|pgn)'
    GROUP BY coalesce(state, '<null>')
) AS activity;

COMMIT;
