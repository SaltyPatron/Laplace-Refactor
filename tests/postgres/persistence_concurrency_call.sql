CREATE FUNCTION pg_temp.persistence_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('01', 32), 'hex'), decode(repeat('02', 32), 'hex'),
            decode(repeat('03', 32), 'hex'), decode(repeat('04', 32), 'hex'),
            decode(repeat('05', 32), 'hex'), decode(repeat('06', 32), 'hex'),
            decode(repeat('07', 32), 'hex'), decode(repeat('08', 32), 'hex'),
            decode(repeat('09', 32), 'hex'), decode(repeat('0a', 32), 'hex')
        ],
        decode(repeat('a0', 32), 'hex'),
        16777216::bigint, 4, 1, 1023::bigint,
        1::smallint, 2::smallint, 0
    )::laplace.execution_context
$context$;

CREATE TEMP TABLE concurrency_input (
    source_fingerprint bytea,
    recipe_fingerprint bytea,
    frames bytea[]
);
INSERT INTO concurrency_input VALUES (
    decode(:'persistence_source', 'hex'),
    decode(:'persistence_recipe', 'hex'),
    ARRAY[
        decode(:'persistence_concurrent_frame_0', 'hex'),
        decode(:'persistence_concurrent_frame_1', 'hex'),
        decode(:'persistence_concurrent_frame_2', 'hex'),
        decode(:'persistence_concurrent_frame_3', 'hex'),
        decode(:'persistence_concurrent_frame_4', 'hex'),
        decode(:'persistence_concurrent_frame_5', 'hex'),
        decode(:'persistence_concurrent_frame_6', 'hex')
    ]
);

DO $concurrency$
DECLARE
    result laplace.canonical_deposit_result;
BEGIN
    SELECT laplace.canonical_deposit_batch(
        pg_temp.persistence_context(), source_fingerprint,
        recipe_fingerprint, frames)
    INTO STRICT result
    FROM concurrency_input;
    IF result.status <> 0 OR result.plan_count <> 11 THEN
        RAISE EXCEPTION 'concurrent persistence call did not complete its plan contract';
    END IF;
END
$concurrency$;
