SELECT laplace._test_perfcache_hold(
    decode(repeat('22', 16), 'hex'),
    decode(repeat('b2', 32), 'hex'),
    3000);
