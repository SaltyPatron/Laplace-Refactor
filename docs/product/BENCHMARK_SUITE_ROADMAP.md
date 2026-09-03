# Benchmark suite roadmap

Tracking: #146, #165, #166

The clean product benchmark system is a versioned registry of profiles/suites that execute through the same resource-plan/receipt authority used by normal Laplace operations. The legacy repository now has the first executable registry/runner shape; the refactor must reproduce it natively rather than importing old implementation details.

## Required profile families

| Profile family | Required proof |
|---|---|
| core composition | exact built artifact + Unicode/Merkle/physicality work |
| physical-core/SMT scaling | measured affinity curve, not extrapolation |
| bit-perfect realization | exact input/output digest/bytes |
| persistent roundtrip | deposit/read/reconstruct + DB/I/O/WAL/storage receipts |
| query plan/actual | exact/estimated/bounded preflight + actual postflight work |
| sparse addressability | `N`, `K`, index/perfcache/provider, actual selected-work complexity |
| optional accelerator | same program/result; CPU vs accelerator physical plan, transfers/VRAM/K |
| cold/warm/reuse | first observation vs canonical/index/perfcache reuse |
| storage economics | canonical data/index/perfcache/TOAST/dead/free/write-maintenance bytes/work |
| corpus admission | complete corpus identity and all unit/byte/output dispositions |
| cognition accepted work | quality-passing complete-route work/resource receipts |
| model export | closed scope/epoch -> deterministic target artifact + resources |
| competitor equivalence | derived accepted-work price comparison from exact underlying receipts |

## Sparse complexity

A profile may label a physical plan `O(log N)+O(K)` only when it records the exact provider/index law behind the logarithmic address term and the selected algorithm over `K`. If the selected phase sorts, pairs, solves, or iterates superlinearly, the receipt names that actual shape.

## Accelerator boundary

The benchmark must keep these facts distinct:

```text
accelerator installed
accelerator eligible
accelerator selected by physical plan
accelerator bytes/work actually admitted
```

The substrate/world need not reside on the accelerator. A sparse provider can receive only the already-selected mathematical workset. Provider substitution passes only with the same semantic/result contract.

## Benchmark -> billing -> Gödel

Every benchmark profile emits the same typed work/resource dimensions that production execution emits. Benchmark receipts therefore calibrate estimates and prices and provide optimization evidence; they are not a parallel observability system.
