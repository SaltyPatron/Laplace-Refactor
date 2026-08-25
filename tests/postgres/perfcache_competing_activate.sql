SELECT laplace._test_perfcache_admit(
    1, true,
    decode(repeat('00', 16), 'hex'), decode(repeat('00', 32), 'hex'),
    decode(:'candidate_manifest', 'hex'));
