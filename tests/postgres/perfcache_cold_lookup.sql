DO $contract$
DECLARE
    first_values bigint[];
    second_values bigint[];
    loads_after_first bigint;
    catalog_selects_before bigint;
    manifest_selects_before bigint;
BEGIN
    IF laplace._test_perfcache_metric(7) <> 0 THEN
        RAISE EXCEPTION 'fresh backend had a preexisting native manifest load';
    END IF;
    catalog_selects_before := laplace._test_perfcache_metric(8);
    manifest_selects_before := laplace._test_perfcache_metric(9);
    first_values := laplace._test_perfcache_lookup(ARRAY[2, 0, 1]);
    loads_after_first := laplace._test_perfcache_metric(7);
    second_values := laplace._test_perfcache_lookup(ARRAY[1, 2]);
    IF first_values <> ARRAY[202, 200, 201]::bigint[]
       OR second_values <> ARRAY[201, 202]::bigint[]
       OR loads_after_first <> 1
       OR laplace._test_perfcache_metric(7) <> loads_after_first
       OR laplace._test_perfcache_metric(8) <> catalog_selects_before
       OR laplace._test_perfcache_metric(9) <> manifest_selects_before + 1 THEN
        RAISE EXCEPTION 'cold materialization or SQL-free hot lookup contract failed';
    END IF;
END
$contract$;

SELECT 'postgres.perfcache-cold-and-hot-lookup passed' AS result;
