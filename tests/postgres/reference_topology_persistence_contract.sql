CREATE EXTENSION laplace;

CREATE FUNCTION pg_temp.topology_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('01', 32), 'hex'),
            decode(repeat('02', 32), 'hex'),
            decode(repeat('03', 32), 'hex'),
            decode(repeat('04', 32), 'hex'),
            decode(repeat('05', 32), 'hex'),
            decode(repeat('06', 32), 'hex'),
            decode(repeat('07', 32), 'hex'),
            decode(repeat('08', 32), 'hex'),
            decode(repeat('09', 32), 'hex'),
            decode(repeat('0a', 32), 'hex')
        ],
        decode(repeat('a0', 32), 'hex'),
        1073741824::bigint,
        6,
        2,
        1023::bigint,
        1::smallint,
        6::smallint,
        1
    )::laplace.execution_context
$context$;

CREATE TEMP TABLE topology_measurement AS
SELECT
    pg_current_wal_lsn() AS wal_before,
    pg_database_size(current_database()) AS database_bytes_before,
    clock_timestamp() AS started_at;

CREATE TEMP TABLE topology_candidates AS
SELECT array_agg(
    ROW(
        decode(repeat('10', 32), 'hex'),
        7,
        decode(repeat('30', 16), 'hex'),
        decode(repeat('50', 16), 'hex'),
        decode(repeat('70', 16), 'hex'),
        decode(repeat('00', 8), 'hex') || int8send(candidate_ordinal),
        1::numeric,
        decode(repeat('00', 8), 'hex') || int8send(candidate_ordinal),
        decode(repeat('80', 16), 'hex'),
        decode(repeat('00', 8), 'hex') || int8send(candidate_ordinal),
        candidate_ordinal::numeric,
        1::numeric,
        candidate_ordinal::numeric,
        1::numeric,
        3
    )::laplace.reference_candidate
    ORDER BY candidate_ordinal) AS records
FROM generate_series(1, 40000) candidate_ordinal;

SET session_replication_role = replica;

CREATE TEMP TABLE topology_invocation AS
SELECT laplace.reference_topology_resolve_batch(
    pg_temp.topology_context(), records, 8388608::numeric) AS result
FROM topology_candidates;

CREATE TEMP TABLE topology_result AS
SELECT (result).* FROM topology_invocation;

CREATE TEMP TABLE topology_tuple_state AS
SELECT 1 AS relation_kind, coordinate AS identity,
       xmin::text AS tuple_xmin, ctid::text AS tuple_ctid
FROM laplace.reference_coordinate
UNION ALL
SELECT 2, reference_id, xmin::text, ctid::text
FROM laplace.reference_occurrence
UNION ALL
SELECT 3, receipt_id || occurrence_id, xmin::text, ctid::text
FROM laplace.reference_topology_receipt_member;

CREATE TEMP TABLE topology_replay_measurement AS
SELECT pg_current_wal_lsn() AS wal_before, clock_timestamp() AS started_at;

CREATE TEMP TABLE topology_replay_invocation AS
SELECT laplace.reference_topology_resolve_batch(
    pg_temp.topology_context(), records, 8388608::numeric) AS result
FROM topology_candidates;

CREATE TEMP TABLE topology_replay_result AS
SELECT (result).* FROM topology_replay_invocation;

RESET session_replication_role;

DO $contract$
DECLARE
    result topology_result%ROWTYPE;
    replay topology_replay_result%ROWTYPE;
BEGIN
    SELECT * INTO STRICT result FROM topology_result;
    SELECT * INTO STRICT replay FROM topology_replay_result;
    IF result.occurrence_count <> 40000 OR
       result.coordinate_count <> 40000 OR
       result.present_count <> 40000 OR
       result.retired_count <> 0 OR
       result.unresolved_count <> 0 THEN
        RAISE EXCEPTION 'topology persistence changed exact native semantics';
    END IF;
    IF result.persistence_batch_count < 2 OR
       result.maximum_encoded_persistence_batch_bytes > 8388608 OR
       result.maximum_persistence_batch_records >= 40000 OR
       result.minimum_encoded_persistence_record_bytes <= 0 OR
       8388608 - result.maximum_encoded_persistence_batch_bytes >=
           result.minimum_encoded_persistence_record_bytes THEN
        RAISE EXCEPTION 'topology persistence did not saturate its byte-derived batches';
    END IF;
    IF (SELECT count(*) FROM laplace.reference_coordinate) <> 40000 OR
       (SELECT count(*) FROM laplace.reference_occurrence) <> 40000 OR
       (SELECT count(*) FROM laplace.reference_topology_receipt_member) <> 40000 THEN
        RAISE EXCEPTION 'topology persistence durable counts are incomplete';
    END IF;
    IF result IS DISTINCT FROM replay THEN
        RAISE EXCEPTION 'topology replay changed its exact public result';
    END IF;
    IF EXISTS (
        SELECT 1
        FROM topology_tuple_state before
        FULL JOIN (
            SELECT 1 AS relation_kind, coordinate AS identity,
                   xmin::text AS tuple_xmin, ctid::text AS tuple_ctid
            FROM laplace.reference_coordinate
            UNION ALL
            SELECT 2, reference_id, xmin::text, ctid::text
            FROM laplace.reference_occurrence
            UNION ALL
            SELECT 3, receipt_id || occurrence_id, xmin::text, ctid::text
            FROM laplace.reference_topology_receipt_member
        ) after USING (relation_kind, identity)
        WHERE before.identity IS NULL OR after.identity IS NULL OR
              before.tuple_xmin <> after.tuple_xmin OR
              before.tuple_ctid <> after.tuple_ctid
    ) THEN
        RAISE EXCEPTION 'topology replay rewrote durable exact-set rows';
    END IF;
END
$contract$;

SELECT
    r.occurrence_count,
    r.persistence_batch_count,
    r.maximum_persistence_batch_records,
    r.maximum_encoded_persistence_batch_bytes,
    pg_wal_lsn_diff(pg_current_wal_lsn(), m.wal_before)::bigint AS wal_bytes,
    pg_database_size(current_database()) - m.database_bytes_before AS database_growth_bytes,
    round(extract(epoch FROM rm.started_at - m.started_at)::numeric, 3) AS first_elapsed_seconds,
    pg_wal_lsn_diff(pg_current_wal_lsn(), rm.wal_before)::bigint AS replay_wal_bytes,
    round(extract(epoch FROM clock_timestamp() - rm.started_at)::numeric, 3) AS replay_elapsed_seconds
FROM topology_result r
CROSS JOIN topology_measurement m
CROSS JOIN topology_replay_measurement rm;
