SELECT laplace._test_perfcache_admit(
    2, true,
    decode(repeat('22', 16), 'hex'), decode(repeat('b2', 32), 'hex'),
    decode(:'perfcache_manifest_3', 'hex'));

DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 3
       OR laplace._test_perfcache_metric(3) <> 1
       OR laplace._test_perfcache_metric(4) <> 1
       OR laplace._test_perfcache_active_epoch() <>
          decode(repeat('33', 16) || repeat('c3', 32), 'hex') THEN
        RAISE EXCEPTION 'concurrent reader did not retain the retired generation';
    END IF;
END
$contract$;
