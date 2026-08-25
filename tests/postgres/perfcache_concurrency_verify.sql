DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 3
       OR laplace._test_perfcache_metric(2) <> 0
       OR laplace._test_perfcache_metric(3) <> 0
       OR laplace._test_perfcache_metric(4) <> 0
       OR laplace._test_perfcache_metric(5) <> 0 THEN
        RAISE EXCEPTION 'retired perfcache generation did not drain exactly';
    END IF;
END
$contract$;

SELECT laplace._test_perfcache_replay_committing(
    activation_event.activation_transaction_id)
FROM laplace.perfcache_activation_event AS activation_event
WHERE activation_event.sequence = 3;

DO $contract$
BEGIN
    IF laplace._test_perfcache_metric(1) <> 3
       OR laplace._test_perfcache_metric(5) <> 0 THEN
        RAISE EXCEPTION 'committed-XID visibility-gap helper did not converge';
    END IF;
END
$contract$;

SELECT 'postgres.perfcache-concurrent-handoff passed' AS result;
