CREATE EXTENSION laplace;

CREATE FUNCTION pg_temp.execution_context()
RETURNS laplace.execution_context
LANGUAGE SQL IMMUTABLE PARALLEL SAFE
AS $context$
    SELECT ROW(
        ARRAY[
            decode(repeat('01',32),'hex'), decode(repeat('02',32),'hex'),
            decode(repeat('03',32),'hex'), decode(repeat('04',32),'hex'),
            decode(repeat('05',32),'hex'), decode(repeat('06',32),'hex'),
            decode(repeat('07',32),'hex'), decode(repeat('08',32),'hex'),
            decode(repeat('09',32),'hex'), decode(repeat('0a',32),'hex')
        ],
        decode(repeat('a0',32),'hex'), 1048576::bigint, 4, 1,
        1023::bigint, @LAPLACE_FRAMEWORK_MAJOR@::smallint,
        @LAPLACE_FRAMEWORK_MINOR@::smallint,
        @LAPLACE_FRAMEWORK_CONTEXT_READ_ONLY@::integer
    )::laplace.execution_context
$context$;

CREATE FUNCTION pg_temp.digest(byte_value integer)
RETURNS bytea
LANGUAGE SQL IMMUTABLE STRICT PARALLEL SAFE
AS $digest$
    SELECT decode(repeat(lpad(to_hex(byte_value), 2, '0'), 32), 'hex')
$digest$;

CREATE FUNCTION pg_temp.recipe(
    recipe_id bytea,
    parent_recipe_id bytea,
    seed integer,
    scope_kind integer)
RETURNS laplace.stock_recipe_manifest
LANGUAGE SQL IMMUTABLE STRICT PARALLEL SAFE
AS $recipe$
    SELECT ROW(
        recipe_id, parent_recipe_id, pg_temp.digest(17),
        pg_temp.digest(seed + 1), pg_temp.digest(seed + 2),
        pg_temp.digest(seed + 3), pg_temp.digest(seed + 4),
        pg_temp.digest(seed + 5), pg_temp.digest(224),
        pg_temp.digest(seed + 6), pg_temp.digest(seed + 7),
        pg_temp.digest(seed + 8), 1::numeric, scope_kind, 1, 1, 0
    )::laplace.stock_recipe_manifest
$recipe$;

CREATE FUNCTION pg_temp.plane(plane_id bytea, recipe_id bytea)
RETURNS laplace.stock_perfcache_plane_manifest
LANGUAGE SQL IMMUTABLE STRICT PARALLEL SAFE
AS $plane$
    SELECT ROW(
        plane_id, recipe_id,
        pg_temp.digest(145), pg_temp.digest(146), pg_temp.digest(147),
        pg_temp.digest(148), pg_temp.digest(149), pg_temp.digest(150),
        pg_temp.digest(151), 1, 0
    )::laplace.stock_perfcache_plane_manifest
$plane$;

CREATE TEMP TABLE stock_expected (
    root_recipe bytea NOT NULL,
    child_recipe bytea NOT NULL,
    plane bytea NOT NULL,
    catalog bytea NOT NULL,
    recipe_set bytea NOT NULL,
    perfcache_set bytea NOT NULL,
    isa_receipt bytea NOT NULL
);

INSERT INTO stock_expected VALUES (
    decode(:'stock_root_recipe', 'hex'),
    decode(:'stock_child_recipe', 'hex'),
    decode(:'stock_plane', 'hex'),
    decode(:'stock_catalog', 'hex'),
    decode(:'stock_recipe_set', 'hex'),
    decode(:'stock_perfcache_set', 'hex'),
    decode(:'stock_isa_receipt', 'hex')
);

DO $test$
DECLARE
    zero_id bytea := pg_temp.digest(0);
    expected record;
    root_id bytea;
    child_id bytea;
    plane_id bytea;
    items laplace.stock_catalog_item[];
    reversed_items laplace.stock_catalog_item[];
    result laplace.stock_catalog_result;
    replay laplace.stock_catalog_result;
    rejected boolean := false;
BEGIN
    SELECT * INTO STRICT expected FROM stock_expected;
    root_id := expected.root_recipe;
    child_id := expected.child_recipe;
    plane_id := expected.plane;
    items := ARRAY[
        ROW(pg_temp.recipe(root_id, zero_id, 16, 1), NULL, 1, 0)::laplace.stock_catalog_item,
        ROW(pg_temp.recipe(child_id, root_id, 32, 4), NULL, 1, 0)::laplace.stock_catalog_item,
        ROW(NULL, pg_temp.plane(plane_id, child_id), 2, 0)::laplace.stock_catalog_item
    ];
    reversed_items := ARRAY[items[3], items[2], items[1]];
    result := laplace.stock_recipe_compile_catalog_batch(
        pg_temp.execution_context(), items);
    IF result.catalog_id <> expected.catalog OR
       result.recipe_set_fingerprint <> expected.recipe_set OR
       result.perfcache_set_fingerprint <> expected.perfcache_set OR
       result.isa_receipt_id <> expected.isa_receipt OR
       result.recipe_count <> 2 OR result.source_count <> 1 OR
       result.perfcache_plane_count <> 1 OR result.maximum_scope_kind <> 4 OR
       result.version <> 1 THEN
        RAISE EXCEPTION 'stock catalog result differs from direct native execution';
    END IF;
    replay := laplace.stock_recipe_compile_catalog_batch(
        pg_temp.execution_context(), reversed_items);
    IF replay IS DISTINCT FROM result THEN
        RAISE EXCEPTION 'stock catalog replay is caller-order dependent';
    END IF;
    IF (SELECT count(*) FROM laplace.stock_recipe) <> 2 OR
       (SELECT count(*) FROM laplace.stock_perfcache_plane) <> 1 OR
       (SELECT count(*) FROM laplace.stock_catalog_receipt) <> 1 OR
       (SELECT count(*) FROM laplace.stock_catalog_receipt_member) <> 3 OR
       (SELECT count(*) FROM laplace.execution_receipt
        WHERE receipt_id = result.isa_receipt_id) <> 1 THEN
        RAISE EXCEPTION 'stock catalog durable closure is incomplete';
    END IF;
    BEGIN
        UPDATE laplace.stock_recipe
        SET grammar_provider_id = pg_temp.digest(255)
        WHERE recipe_id = child_id;
        PERFORM laplace.stock_recipe_compile_catalog_batch(
            pg_temp.execution_context(), items);
    EXCEPTION WHEN data_corrupted THEN
        rejected := true;
    END;
    IF NOT rejected THEN
        RAISE EXCEPTION 'stock catalog replay accepted conflicting durable recipe state';
    END IF;
END
$test$;
