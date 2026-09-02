# Benchmark evidence lane

Tracking: #140, #142, #144, #146, #157, #165
Legacy runnable evidence: `SaltyPatron/Laplace` manual benchmark workflow/documentation.

## Product requirement

Laplace performance/economic claims must be reproducible from explicitly dispatched benchmark runs bound to exact source/package/topology/resource identities. Benchmarks are evidence and estimator/Gödel inputs; they are not automatically triggered by push, merge, or pull request merely because source changed.

A benchmark lane must be independently dispatchable against an exact revision or installed product identity and produce immutable artifacts/receipts that can later be compared without relying on prose recollection.

## Separation from delivery

Benchmark execution must remain separate from ordinary CI/CD unless a particular small performance invariant is intentionally promoted to an acceptance gate. Long-running throughput, energy, corpus, query, model/export, or whole-system measurements must not make every source push consume the measured host.

Manual benchmark workflows must:

- have no `push` or `pull_request` trigger;
- share the measured-host/resource concurrency authority so delivery/seed/benchmark jobs cannot distort one another;
- never silently deploy, migrate, reseed, or mutate product state merely to collect a source/core benchmark;
- explicitly declare when a benchmark requires an installed/live product and bind the installed package/activation epoch separately from source revision;
- upload the complete raw evidence used to compute headline metrics.

## Minimum provenance

Each receipt/artifact should identify or preserve, as applicable:

- repository and exact commit;
- built/installed package digest and activation epoch;
- benchmark contract/version and exact inputs/corpus digest;
- host/runner identity;
- CPU model, sockets, cores, hardware threads, cache/NUMA/ISA and affinity;
- memory capacity and relevant memory-domain/grant information;
- storage/provider topology when persistence is measured;
- accelerator inventory and whether an accelerator was admitted/used by the physical plan;
- operating system/kernel/compiler/library identities;
- current workload/isolation state;
- wall time and CPU/core time;
- memory peak/byte-time where available;
- storage/network/database quantities where applicable;
- energy counters with the measurement boundary and source explicitly named;
- output cardinalities and semantic/result fingerprints;
- benchmark stdout/stderr and machine-readable receipt;
- hashes of preserved evidence artifacts.

A PSU nameplate/rated wattage is not measured power draw. CPU package/domain energy (for example Intel RAPL) must remain distinct from whole-system wall energy. Whole-system energy requires a compatible measured provider such as a wall meter, UPS/PDU telemetry, BMC sensor, or another calibrated source.

## Benchmark families

The complete product should expose separate benchmark families rather than one misleading tokens/second scalar:

1. core Unicode/composition/identity throughput;
2. canonical content ingest and bit-perfect realization/roundtrip;
3. persistent deposit/read/reuse throughput and storage amplification;
4. canonical ID/relation/composition/physicality traversal;
5. complete query/cognition/generation accepted-work throughput;
6. model decomposition/export/build;
7. corpus-scale ingestion such as UD Treebanks;
8. cold/warm/cache/perfcache and first-observation/reuse cases;
9. concurrency/scaling curves over physical cores, SMT, NUMA and multiple nodes;
10. storage data/index/perfcache footprint and write-maintenance cost;
11. energy and hardware-dollar normalized useful work;
12. competitor-equivalent accepted-work cost under versioned external pricing.

## Scaling law

Do not multiply a one-thread measurement by a core/thread count and publish it as measured machine throughput. Record actual scaling points, preferably at least:

```text
1 worker
2 workers
3 workers
4 workers
physical-core count
selected SMT points
hardware-thread count
```

Each point must retain actual affinity/topology/resource grant and concurrent-workload shape. Per-thread, aggregate, speedup and parallel-efficiency metrics remain distinct.

## Legacy evidence to preserve

The old repository contains useful historical measurements that should be re-run and preserved while the refactor implementation matures, including:

- a committed single-thread core composition benchmark over real mixed text measuring codepoints/s, 4-character BPE-equivalent tokens/s and tier-tree nodes/s;
- bit-perfect Moby Dick engine roundtrip measurements;
- a distinct database-backed Moby Dick record/reconstruct measurement;
- extensive `EXPLAIN` / `EXPLAIN (ANALYZE, BUFFERS)` plan-vs-actual measurements;
- corpus ingest throughput and before/after optimization receipts.

These measurements are historical evidence, not automatic claims about the final refactor. The refactor must reproduce equivalent workload contracts under its own typed resource/execution receipts.

## Billing and Gödel integration

The benchmark lane is not a marketing-only surface. Its receipts feed:

- preflight estimator calibration;
- pricing/product epochs;
- membership allowance and throttle design;
- capacity planning;
- plan-vs-actual error analysis;
- Gödel optimization/refactoring candidates;
- index/perfcache admission/removal;
- competitor-equivalent economics.

The same resource dimensions used by execution/billing must be used by benchmark receipts. A benchmark may additionally calculate conventional token-equivalent comparison metrics, but they cannot replace the native work/resource vector.
