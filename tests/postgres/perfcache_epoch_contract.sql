CREATE FUNCTION laplace._test_perfcache_admit(
    bigint, boolean, bytea, bytea, bytea)
RETURNS void
AS 'laplace_pg', 'laplace_pg_test_perfcache_admit'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace._test_perfcache_active_epoch()
RETURNS bytea
AS 'laplace_pg', 'laplace_pg_test_perfcache_active_epoch'
LANGUAGE C VOLATILE PARALLEL UNSAFE;

CREATE FUNCTION laplace._test_perfcache_metric(integer)
RETURNS bigint
AS 'laplace_pg', 'laplace_pg_test_perfcache_metric'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace._test_perfcache_lookup(integer[])
RETURNS bigint[]
AS 'laplace_pg', 'laplace_pg_test_perfcache_lookup'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace._test_perfcache_hold(bytea, bytea, integer)
RETURNS bytea
AS 'laplace_pg', 'laplace_pg_test_perfcache_hold'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace._test_perfcache_pin_then_error(bytea, bytea)
RETURNS void
AS 'laplace_pg', 'laplace_pg_test_perfcache_pin_then_error'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

CREATE FUNCTION laplace._test_perfcache_replay_committing(xid)
RETURNS void
AS 'laplace_pg', 'laplace_pg_test_perfcache_replay_committing'
LANGUAGE C VOLATILE STRICT PARALLEL UNSAFE;

REVOKE EXECUTE ON FUNCTION laplace._test_perfcache_admit(
    bigint, boolean, bytea, bytea, bytea) FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION laplace._test_perfcache_active_epoch() FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION laplace._test_perfcache_metric(integer) FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION laplace._test_perfcache_lookup(integer[]) FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION laplace._test_perfcache_hold(bytea, bytea, integer) FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION laplace._test_perfcache_pin_then_error(bytea, bytea) FROM PUBLIC;
REVOKE EXECUTE ON FUNCTION laplace._test_perfcache_replay_committing(xid) FROM PUBLIC;

CREATE TEMP TABLE native_perfcache_fixture(
    generation integer PRIMARY KEY,
    encoded_manifest bytea NOT NULL
);
INSERT INTO native_perfcache_fixture(generation, encoded_manifest)
VALUES
    (2, decode(:'perfcache_manifest_2', 'hex')),
    (5, decode(:'perfcache_manifest_5', 'hex')),
    (6, decode(:'perfcache_manifest_6', 'hex'));

SELECT laplace._test_perfcache_admit(
    0, false,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'perfcache_manifest_1', 'hex'));

DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 1
       OR laplace._test_perfcache_metric(6) <> 1
       OR laplace._test_perfcache_lookup(ARRAY[2, 0, 1]) <>
          ARRAY[102, 100, 101]::bigint[]
       OR laplace._test_perfcache_metric(7) <> 0
       OR laplace._test_perfcache_active_epoch() <>
          decode(repeat('00', 16) || repeat('00', 32), 'hex') THEN
        RAISE EXCEPTION 'initial committed perfcache epoch was not activated';
    END IF;
END
$contract$;

BEGIN;
SELECT laplace._test_perfcache_admit(
    1, true,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'perfcache_manifest_2', 'hex'));
ROLLBACK;

DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 1
       OR laplace._test_perfcache_metric(5) <> 0 THEN
        RAISE EXCEPTION 'transaction abort changed the active perfcache epoch';
    END IF;
END
$contract$;

BEGIN;
SAVEPOINT perfcache_candidate;
SELECT laplace._test_perfcache_admit(
    1, true,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'perfcache_manifest_2', 'hex'));
ROLLBACK TO SAVEPOINT perfcache_candidate;
COMMIT;

BEGIN;
SAVEPOINT perfcache_candidate;
SELECT laplace._test_perfcache_admit(
    1, true,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'perfcache_manifest_2', 'hex'));
RELEASE SAVEPOINT perfcache_candidate;
ROLLBACK;

DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 1
       OR laplace._test_perfcache_metric(5) <> 0 THEN
        RAISE EXCEPTION 'savepoint or parent abort changed the active perfcache epoch';
    END IF;

    BEGIN
        PERFORM laplace._test_perfcache_admit(
            0, false,
            decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
            fixture.encoded_manifest)
        FROM native_perfcache_fixture AS fixture
        WHERE fixture.generation = 2;
        RAISE EXCEPTION 'stale expected perfcache epoch was accepted';
    EXCEPTION
        WHEN object_not_in_prerequisite_state THEN NULL;
    END;
END
$contract$;

DO $contract$
DECLARE
    corrupted bytea;
BEGIN
    SELECT set_byte(
               fixture.encoded_manifest,
               octet_length(fixture.encoded_manifest) - 1,
               get_byte(
                   fixture.encoded_manifest,
                   octet_length(fixture.encoded_manifest) - 1) # 1)
    INTO STRICT corrupted
    FROM native_perfcache_fixture AS fixture
    WHERE fixture.generation = 2;
    BEGIN
        PERFORM laplace._test_perfcache_admit(
            1, true,
            decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
            corrupted);
        RAISE EXCEPTION 'corrupt encoded manifest was admitted';
    EXCEPTION
        WHEN object_not_in_prerequisite_state THEN NULL;
    END;
    BEGIN
        PERFORM laplace._test_perfcache_admit(
            1, true,
            decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
            fixture.encoded_manifest)
        FROM native_perfcache_fixture AS fixture
        WHERE fixture.generation = 5;
        RAISE EXCEPTION 'artifact path outside the admitted root was accepted';
    EXCEPTION
        WHEN object_not_in_prerequisite_state THEN NULL;
    END;
    BEGIN
        PERFORM laplace._test_perfcache_admit(
            1, true,
            decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
            fixture.encoded_manifest)
        FROM native_perfcache_fixture AS fixture
        WHERE fixture.generation = 6;
        RAISE EXCEPTION 'manifest with a missing artifact was admitted';
    EXCEPTION
        WHEN object_not_in_prerequisite_state THEN NULL;
    END;
    IF laplace._test_perfcache_metric(1) <> 1
       OR laplace._test_perfcache_metric(5) <> 0
       OR (SELECT count(*) FROM laplace.perfcache_generation) <> 1
       OR (SELECT count(*) FROM laplace.perfcache_activation_event) <> 1 THEN
        RAISE EXCEPTION 'rejected manifests changed perfcache state';
    END IF;
END
$contract$;

SELECT laplace._test_perfcache_admit(
    1, true,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'perfcache_manifest_2', 'hex'));

DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 2
       OR laplace._test_perfcache_lookup(ARRAY[0, 2]) <>
          ARRAY[200, 202]::bigint[]
       OR laplace._test_perfcache_metric(7) <> 0
       OR laplace._test_perfcache_active_epoch() <>
          decode(repeat('22', 16) || repeat('b2', 32), 'hex')
       OR (SELECT count(*) FROM laplace.perfcache_generation) <> 2
       OR (SELECT count(*) FROM laplace.perfcache_activation_event) <> 2
       OR (SELECT sequence FROM laplace.perfcache_active_control WHERE singleton) <> 2 THEN
        RAISE EXCEPTION 'durable and shared perfcache epoch state diverged';
    END IF;

    BEGIN
        PERFORM laplace._test_perfcache_pin_then_error(
            decode(repeat('22', 16), 'hex'), decode(repeat('b2', 32), 'hex'));
    EXCEPTION
        WHEN internal_error THEN NULL;
    END;
    IF laplace._test_perfcache_metric(2) <> 0 THEN
        RAISE EXCEPTION 'resource-owner cleanup leaked a perfcache pin';
    END IF;
END
$contract$;

SELECT 'postgres.perfcache-epoch-contract passed' AS result;
