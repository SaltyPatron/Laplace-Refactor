BEGIN;

SELECT laplace._test_perfcache_admit(
    3, true,
    decode(repeat('33', 16), 'hex'), decode(repeat('c3', 32), 'hex'),
    decode(:'perfcache_manifest_4', 'hex'));

PREPARE TRANSACTION 'laplace-perfcache-must-not-prepare';
