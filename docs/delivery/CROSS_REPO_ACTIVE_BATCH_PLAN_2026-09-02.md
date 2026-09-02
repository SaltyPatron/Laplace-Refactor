# Cross-repository active batch plan — 2026-09-02

This document records the current delivery boundary between the running old product (`SaltyPatron/Laplace`) and the clean implementation (`SaltyPatron/Laplace-Refactor`). It is a dated execution/status projection, not higher authority than the invention/product/architecture contracts.

## Repository boundary

`SaltyPatron/Laplace` is repaired directly in that repository. A clean-refactor implementation does not repair the running old product.

Old-product failures are retained here as counterexamples, acceptance cases, and contract evidence only. They do not define the clean ABI/schema or authorize copying legacy implementation. Conversely, old-product incident work must not be postponed because this refactor is intended to replace it.

The two repositories can advance in parallel where their branch/runtime state is independent.

## Current clean integration state

PR `#128` remains the single integration finish line to `main` for the accumulated branch estate.

On the audited pre-documentation head `8d24ecc38e20d3a7092483d65507bcf17976283f`, workflow run `33586684409`:

- selected and configured the custom-stack product proof;
- completed the PostgreSQL/native build (`1043/1043`);
- entered the custom-stack core lane with 37 selected core tests and three selected physical tests;
- passed 30 core tests before stopping at `postgres.mutation-composition-presence-blind-detected`;
- the deliberate mutant emitted the intended defect, `blind composition-presence mutant published without observing canonical state`, but `tests/expect_postgres_mutation_failure.cmake` classified it as an unrelated reason;
- because the core lane stops on failure, the three selected physical tests did not execute.

Therefore `#102` does **not** have a failed real-CILI measurement on this head. It remains unmeasured because the shared mutation-expectation gate stopped the run first. The next integration action is to repair that expectation/classification contract without weakening the deliberate defect, rerun the exact new head, then execute and retain the physical CILI receipts.

The documentation commit containing this file advances the PR head; acceptance must apply to that exact new head, not to the audited predecessor.

## Current clean capability owners

### `#102` — real CILI cardinality/resource proof

Instrumentation and durable receipt capture exist, but acceptance requires the real locked CILI whole-route execution on the exact accepted head. Required output includes canonical entity/physicality deltas, explicit/logical occurrence counts, references/mappings/evidence/testimony, crossings/presence rounds/plans, peak PostgreSQL RSS, data/WAL/workspace growth, elapsed time, resource ceilings, and zero replay growth for canonical state.

The historical ~24M request / ~20.7 GiB plan is a broken-baseline counterexample, not a target and not a current measurement.

### `#110` — typed standing / earned matchup activation

The native standing calculation, generated ISA/C# parity and durable PostgreSQL event/state/period history are partial implementation. Remaining work is product activation and earned adjudication:

- manufacture/authority-select stock standing recipes;
- lower admitted testimony/outcomes into exact typed matchup events;
- source-type prior -> earned source-specific override;
- independent return-leg corroboration/refutation;
- dependence reduction before matchup cardinality;
- immutable late/corrected replay and as-of history;
- standing/contradiction/referential epochs;
- installed/readback/restart replay and loaded-native-artifact identity proof;
- exact `rating + RD + volatility + lane + epoch` consumption by query/search.

The newest old-product counterexamples strengthen this acceptance: row witness count/latest time can agree with evidence while the derived rating payload is still corrupt. Clean standing integrity must therefore be established by the exact event/period receipt and accepted successor calculation, not by row freshness.

### `#132` — observation/fact/calculation provider bridge

Ordinary admitted content is already an observation through canonical identity + occurrence + physicality trajectory. It must be usable immediately for containment, ordinal/gap, recurrence, adjacency, shared-container context, trajectory prefixes/subpaths and continuation without waiting for a source-specific semantic extractor.

Seeded typed facts/testimony and deterministic/domain calculations remain separate provider classes. Standing remains another separate typed channel. The query program may combine them only explicitly.

### `#60` — typed filtered indexed provider/search execution

The native typed A*/declared-best-first kernel is implemented, but closure requires real providers and physical execution:

- physicality/occurrence providers;
- semantic/testimony providers;
- exact `#110` standing lookup;
- safe filter pushdown before frontier materialization;
- set-wise PostgreSQL/index/perfcache candidate generation;
- public ISA/SQL/C# route parity;
- physical-plan/frontier/crossing/CPU/memory/I/O receipts;
- deliberate defects for RBAR/recursive/dynamic/fallback/giant-adjacency/raw-hop/global-standing substitutions.

### `#17` — complete cognition route

The native guidance/operator/solver/search kernels are partial implementation. Closure requires one real installed public route that compiles a request into finite typed guidance/search/operator state, executes provider selection -> bounded typed frontier -> search/operator/fold -> guidance update repeatedly until semantic completion, selects a semantic act or exact typed WHY_NOT, realizes/effects it, and witnesses the complete receipt.

Cross-domain acceptance must exercise the same lifecycle rather than private domain-specific cognition engines.

## Old-product counterexamples that remain direct old-repo work

The old repository has its own direct closure obligations:

- `SaltyPatron/Laplace#1423` — current DB lifecycle blocker: extension upgrade cannot rename a same-signature `generation.walk_continuations` input parameter;
- `SaltyPatron/Laplace#1395` — live rating runaway / complete evidence-derived player standing reconstruction and live validation;
- `SaltyPatron/Laplace#1397` — canonical event-time/rating-period standing replay, independent of provider/file/chunk/worker order;
- `SaltyPatron/Laplace#1292` — journal-visible fold completion and automatic idempotent scoped recovery from durable evidence.

Relevant old-main repairs/counterexamples include fixed-point carrier totalization, fixed-point Illinois termination, full evidence-derived chess rating reconstruction, a count/time-current-but-corrupt derived-standing fixture, and deployment artifact identity failures. These inform clean acceptance but are not imported as implementation.

## Prioritized batches

### P0-R1 — merge the clean integration finish line

Owners: PR `#128` + `#102`.

Implementation/acceptance chunk:

1. repair the mutation expectation/classification so the intended composition-presence mutant is recognized;
2. rerun all required exact-head checks;
3. execute the three selected physical tests, including the real CILI whole-route proof;
4. retain the content-addressed terminal measurement receipts;
5. merge only the exact accepted head;
6. verify the same authority/requirements/tests on fresh `main`.

Do not create a sibling finish-line PR to bypass this gate.

### P0-O1 — old deployment + live rating repair (parallel repository)

Owners in `SaltyPatron/Laplace`: `#1423`, then `#1395`.

Repair the extension upgrade, deploy/restart exact old-main generation, execute the complete evidence-derived live rating reconstruction, inspect extrema/historical anchors/failure cells, and rerun substrate-lift. A refactor success cannot close this batch.

### P1-R2 — activate clean standing

Owner: `#110`, from fresh `main` after PR `#128`.

Finish stock recipe activation, testimony lowering, earned source overrides, independent return legs, dependence reduction, immutable late/corrected replay/epochs, installed artifact identity/readback/restart proof, and exact typed standing consumption.

### P1-O2 — old event/fold correctness (parallel repository)

Owners in `SaltyPatron/Laplace`: `#1397` + `#1292`.

Treat these as one coordinated writer/fold batch after the live incident is stabilized: canonical event-time rating periods, batch/worker-order invariance, explicit durable fold-completion tokens, automatic scoped refold, operational visibility, and deterministic late-event replay.

### P2-R3 — provider/index bridge

Owners: `#132` + `#60`.

Deliver as one provider/index batch:

- physicality/occurrence frontier providers;
- seeded typed-relation/fact providers;
- deterministic/domain-calculation providers;
- safe source/context/time/world/authority/evidence/relation/direction/trajectory/dependence filter pushdown;
- set-wise PostgreSQL/index/perfcache candidate generation;
- exact typed standing lookup from `#110`;
- bounded frontier/path receipts and representative physical-plan measurements.

No provider class, index, perfcache, raw hop count, KNN/ANN result, or global standing score may impersonate the query program.

### P3-R4 — complete cognition loop

Owner: `#17`.

Compile real request/discourse state, run the common provider/search/operator/fold/update loop to semantic completion, emit exact WHY_NOT where incomplete, expose native/PostgreSQL/SQL/C#/public route parity, integrate realization/effect, and prove representative cross-domain end-to-end behavior and physical execution receipts.

## Why these chunks

- `#128/#102` is a self-contained integration/physical-acceptance gate; merging it first removes the branch-estate bottleneck.
- `#110` must be stable before query/search consumes standing as a real provider.
- `#132/#60` are two sides of the same provider/index boundary: what typed planes exist and how they are generated/filter-pushed/routed physically. Splitting them into competing PRs would create duplicate interfaces.
- `#17` depends on those real providers and standing to close the whole loop; finishing it before them would encourage fixture-only or private fallback paths.
- old `#1397/#1292` both affect writer/fold publication semantics and are best sequenced as one coordinated old-repo batch after the urgent live rating repair.

## Batch discipline

- One semantic owner and one finish line per batch.
- Resume refactor work from fresh `main` after PR `#128`; do not rebuild a branch forest.
- Independent old/refactor batches can execute in parallel.
- Exact-head CI/physical acceptance gates merge.
- Instrumentation alone does not close a measured issue.
- A source-tree fix does not prove an installed/running artifact is current.
- A logically correct answer reached through a forbidden physical path remains a defect.
- Close an issue only when its positive acceptance and deliberate counterexamples agree with the installed behavior required by that issue.
