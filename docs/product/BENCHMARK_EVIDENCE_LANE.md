# Benchmark evidence suite

Tracking: #140, #142, #144, #146, #157, #165, #166
Legacy runnable evidence: `SaltyPatron/Laplace` benchmark suite and dispatch workflow.

## Product requirement

Laplace performance/economic claims must be reproducible from explicitly dispatched benchmark runs bound to exact source/package/topology/resource identities. Benchmarks are evidence and estimator/Gödel inputs; they are not automatically triggered by push, merge, or pull request merely because source changed.

A benchmark lane must be independently dispatchable against an exact revision or installed product identity and produce immutable artifacts/receipts that can later be compared without relying on prose recollection.

The benchmark system is a **versioned suite**, not a collection of unrelated workflow snippets. The clean product requires:

```text
benchmark profile registry
        ↓
versioned suite selection
        ↓
execution-boundary/provider binding
        ↓
common benchmark runner
        ↓
typed execution/resource receipts
        ↓
immutable evidence artifact
        ├─ estimator calibration
        ├─ billing/pricing
        ├─ capacity planning
        ├─ Gödel optimization
        └─ competitor-equivalent reports
```

The workflow dispatches a named suite. The registry defines what that suite means. Harnesses/providers perform the measured operation. The receipt is authoritative evidence.

## Separation from delivery

Benchmark execution must remain separate from ordinary CI/CD unless a particular small performance invariant is intentionally promoted to an acceptance gate. Long-running throughput, energy, corpus, query, model/export, or whole-system measurements must not make every source push consume the measured host.

Manual benchmark workflows must:

- have no `push` or `pull_request` trigger;
- share the measured-host/resource concurrency authority so delivery/seed/benchmark jobs cannot distort one another;
- never silently deploy, migrate, reseed, or mutate product state merely to collect a source/core benchmark;
- explicitly declare when a benchmark requires an installed/live product and bind the installed package/activation epoch separately from source revision;
- upload the complete raw evidence used to compute headline metrics.

## Exact execution identity

Source identity and execution identity are distinct until proven equal.

Every profile binds the exact executable/library/perfcache/generated-code/package generation that performed the work. A source checkout beside a stale installed `.so`, extension, perfcache, GPU kernel, or model exporter is not proof that the selected source was measured.

At minimum retain content digests for every load-bearing built/installed artifact and the loader/provider path that admitted it.

The old repository has already encountered this exact defect class: `/opt/laplace/lib` can win dynamic-library resolution over `build/engine/core`. The benchmark suite must fail closed rather than produce a correctly formatted receipt for the wrong binary.

## Minimum provenance

Each receipt/artifact should identify or preserve, as applicable:

- repository and exact commit;
- built/installed package digest and activation epoch;
- exact binary/library/perfcache/provider digests and load paths;
- benchmark registry/profile/suite contract versions;
- exact inputs/corpus digest;
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

A PSU nameplate/rated wattage is a hardware-capacity fact, not measured wall draw. CPU package/domain energy (for example Intel RAPL) must remain distinct from whole-system wall energy. Whole-system energy requires a compatible measured provider such as a wall meter, UPS/PDU telemetry, BMC sensor, or another calibrated source.

## Benchmark families

The complete product exposes separate benchmark families rather than one misleading tokens/second scalar:

1. core Unicode/composition/identity throughput;
2. canonical content ingest and bit-perfect realization/roundtrip;
3. persistent deposit/read/reuse throughput and storage amplification;
4. canonical ID/relation/composition/physicality traversal;
5. sparse indexed candidate selection and selected-work complexity;
6. complete query/cognition/generation accepted-work throughput;
7. CPU-only versus optional sparse accelerator-provider parity/economics;
8. model decomposition/export/build;
9. corpus-scale ingestion such as UD Treebanks;
10. cold/warm/cache/perfcache and first-observation/reuse cases;
11. concurrency/scaling curves over physical cores, SMT, NUMA and multiple nodes;
12. storage data/index/perfcache footprint and write-maintenance cost;
13. energy and hardware-dollar normalized useful work;
14. competitor-equivalent accepted-work cost under versioned external pricing.

## Sparse-addressability complexity contract

The intended physical-plan transformation for addressable sparse work is:

```text
naive/dense all-pairs candidate surface:   O(N^2)
indexed address / candidate location:       O(log N)
selected useful work:                       O(K)

representative intended shape:              O(log N) + O(K)
```

For an admitted direct/perfcache provider, the address component may be effectively `O(1)` for that operation.

This notation is not a universal claim that every Laplace program runs in `O(log N)+O(K)`. A benchmark or product receipt making that claim must name:

- exactly what population `N` counts;
- exactly what selected set `K` counts;
- the index/perfcache/provider and lookup law supplying the address term;
- filters applied before and after candidate materialization;
- candidates generated/rejected/accepted;
- exact/estimated/bounded/actual cardinality status;
- rows/IDs/edges/physicalities touched;
- database/native/provider crossings;
- CPU, memory, I/O and storage work;
- fallback behavior if the accelerator is unavailable, stale, or incomplete;
- semantic/result parity against the unaccelerated logical operation.

If the selected operation itself performs `O(K log K)`, `O(K^2)`, an iterative numerical solve, or another shape, the receipt states that real selected-work complexity instead of flattening everything to `O(K)`.

The purpose is to prove that work scales with **addressable relevant state**, rather than materializing an `N^2` world merely because a conventional dense formulation would.

## Optional accelerator / GPU law

GPU is an optional physical execution provider, not semantic authority and not the storage location of the Laplace world.

A conforming accelerator comparison holds the logical program, selected world/evidence epoch, result contract, input, and semantic output constant while changing only the physical provider/resource plan.

For each CPU-only/GPU-assisted pair retain at least:

```text
N world/candidate population
K selected workset
selected operator/calculation identities
CPU-only wall/resource receipt
GPU-assisted wall/resource receipt
host -> device bytes
device -> host bytes
kernel launches / active interval where available
peak/additional VRAM
GPU/provider identity
speedup
semantic/result parity
```

A large speedup from offloading dozens/hundreds/thousands of selected operations is valid even when the substrate itself is hundreds of gigabytes. It means the sparse planner identified the math worth accelerating; it does **not** mean the entire world was loaded into VRAM or brute-forced by the GPU.

The benchmark suite must therefore distinguish:

```text
GPU installed
GPU eligible
GPU selected by physical plan
GPU work/bytes actually admitted
```

These are four different facts.

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

The legacy suite now contains a dedicated scaling harness that fills one hardware thread per physical core before adding SMT siblings, synchronizes worker start after exact artifact/perfcache load, and checks that every scaling point constructs the same logical node population. The refactor must preserve that proof shape through native resource-plan receipts rather than relying on host-specific shell conventions.

## Legacy evidence to preserve

The old repository contains useful historical measurements that should be re-run and preserved while the refactor implementation matures, including:

- a committed single-thread core composition benchmark over real mixed text measuring codepoints/s, 4-character BPE-equivalent tokens/s and tier-tree nodes/s;
- a new explicit physical-core/SMT scaling suite that measures rather than extrapolates whole-machine aggregate throughput;
- bit-perfect Moby Dick engine roundtrip measurements;
- a distinct database-backed Moby Dick record/reconstruct measurement;
- extensive `EXPLAIN` / `EXPLAIN (ANALYZE, BUFFERS)` plan-vs-actual measurements;
- corpus ingest throughput and before/after optimization receipts;
- provider/precision GPU-vs-CPU measurements that are useful historical evidence but are not substitutes for end-to-end sparse-offload receipts.

These measurements are historical evidence, not automatic claims about the final refactor. The refactor must reproduce equivalent workload contracts under its own typed resource/execution receipts.

## Query / billing / Gödel integration

The benchmark suite is not a marketing-only surface. Its receipts feed:

- preflight estimator calibration;
- pricing/product epochs;
- membership allowance and throttle design;
- capacity planning;
- plan-vs-actual error analysis;
- Gödel optimization/refactoring candidates;
- index/perfcache admission/removal;
- accelerator-provider admission/removal;
- competitor-equivalent economics.

The same resource dimensions used by execution/billing must be used by benchmark receipts. A benchmark may additionally calculate conventional token-equivalent comparison metrics, but they cannot replace the native work/resource vector.

A benchmark finding such as `N -> K` narrowing, a repeated bad scan, an index with poor value/storage ratio, or a small sparse math kernel with large accelerator benefit becomes reusable optimization evidence for Gödel. It does not mutate semantic law merely because it is profitable.
