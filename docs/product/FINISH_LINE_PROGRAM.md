# Laplace finish-line program

Tracking: #23, #180. Complete-product acceptance: #22.

This document explains `contracts/finish-line-program.json`. The JSON contract is the
machine-readable program authority; this document does not create a second finish line.

## What the finish line is

Laplace is not finished because a branch looks coherent, a PR is green, a component
works, an old iteration once demonstrated a behavior, a model loads, a query returns a
plausible answer, or a benchmark emits a large scalar.

The product finish line is the complete installed-machine outcome:

```text
exact product/invention law
  -> executable requirement + contract
  -> one semantic implementation owner
  -> positive acceptance
  -> deliberate defect proves the acceptance is sensitive
  -> provider/route parity where applicable
  -> accepted package/install/activation identity where applicable
  -> durable/public readback where applicable
  -> representative physical-plan/performance receipt where applicable
  -> owning milestone exit predicate
  -> #22 complete installed-product acceptance
  -> release
```

A pull request is therefore an implementation vehicle, never the product finish line.
Issue #23 owns the whole-product outcome. #22 owns terminal acceptance. #180 and the
finish-line contract own work selection, closure, anti-substitution and rework control.

## One program, three different graphs

Do not collapse these into one list:

1. `contracts/operation-model.json` owns the machine's operational dependency graph.
2. GitHub phase milestones own coarse capability exits and the work queue.
3. `contracts/finish-line-program.json` joins those to terminal evidence, bounded
   sprints, anti-substitution controls and completion rules.

The operation model dynamically owns stage enumeration. The finish-line contract does
not copy a stage list that can drift; every operation-model stage must instead map to a
known phase/milestone.

## GitHub milestones

The actual GitHub milestone numbers are intentionally recorded because the numeric ID
is not the phase number in every case.

| Phase | GitHub milestone | Title |
| ---: | ---: | --- |
| 0 | 1 | Phase 0 — Canonical cutover |
| 1 | 3 | Phase 1 — Industrial build and dependency foundation |
| 2 | 2 | Phase 2 — Universal execution framework and ISA |
| 3 | 4 | Phase 3 — Universal substrate state |
| 4 | 5 | Phase 4 — Batch world-state admission |
| 5 | 6 | Phase 5 — Universal query and conversation |
| 6 | 7 | Phase 6 — Model independence |
| 7 | 8 | Phase 7 — Complete product surfaces |
| 8 | 9 | Phase 8 — Full acceptance and release |

The older operation-model Phase 0 projection currently says `Product authority and
clean-room foundation` while the GitHub milestone is `Canonical cutover`. The contract
records both coordinates rather than pretending they are identical. This divergence is
visible reconciliation debt; later edits should converge the projection instead of
silently rewriting historical GitHub identity.

Milestones close on their exit predicates, not issue percentages. A required P0 owner,
installed-route proof, deliberate defect or downstream handoff remaining open keeps the
milestone open even if every convenient leaf issue is closed.

## Current bounded sprint

The active sprint is **substrate execution correctness**. It exists because later world
admission and performance work cannot be trusted if the machine still chooses the
wrong execution grain or performs semantic work twice.

```text
#4 topology/resource grant authority
  -> #10 common execution framework/provider authority
      -> #177 dependency-frontier composition execution
      -> #179 calculate each semantic request once
          -> #171 physical-plan semantic invariance across world admission
```

PR #178 is one #177 implementation slice: deterministic dependency-frontier planning.
It is not #177 completion. Remaining #177 acceptance includes provider-bound frontier
execution, bounded concurrent memory, deterministic merge, scalar/provider parity,
single-semantic-DAG scaling and receipts.

A sprint is not a calendar bucket. It is a bounded dependency-aware outcome queue.
Only unblocked work or work producing an explicit prerequisite is active. A red owning
PR is repaired rather than bypassed with another implementation branch unless it is
explicitly retired. Merging one item must expose the next unblocked action.

## Anti-substitution gate

Conventional techniques are allowed as physical providers where they satisfy the
Laplace contract. They are not allowed to become undeclared semantic replacements.
The global negative-control matrix therefore rejects at least these substitutions:

- dense/all-pairs state or arithmetic for sparse/indexed `N -> K` execution;
- file, source record or transport chunk as the worker atom when a semantic DAG exposes
  independent ready work;
- one custom ETL/decomposer engine per source instead of provider + recipe + universal
  AST/common machine;
- read, parser-feed, probe, worker or deposit boundaries entering canonical identity;
- one global adjacency, embedding, nearest-neighbor metric or flat score replacing
  typed structural/semantic/evidence/physicality/operator state;
- BPE/fixed token vocabulary replacing the Unicode-native symbolic floor;
- gradient/checkpoint training becoming native admission/learning authority;
- GPU/world/model residency becoming a requirement instead of an optional sparse
  physical provider;
- cursor/RBAR/recursive/dynamic-per-candidate SQL becoming the cognition engine;
- UI, API, C# or a source adapter implementing private semantics;
- source/path/tier/occurrence/time/worker/batch metadata entering canonical content
  identity;
- a planner executing expensive semantic work, discarding it, and then executing it
  again under the name `real execution`;
- successful load, expected shape, fluent/nonempty output, historical behavior or a
  local benchmark standing in for complete acceptance.

A prose prohibition is not enough for milestone closure. Critical substitutions need a
deliberate defect or mutation in the owning acceptance graph.

## Rework escalation

Repeated leaf repair is itself a defect signal.

When the same defect class appears in two independent leaves, the next primary repair
must move to the common owner:

```text
second independent occurrence
  -> identify generic semantic/physical owner
  -> update/create generic contract + implementation owner
  -> add cross-family fixture or mutation
  -> make leaf implementations consume the generic correction
  -> document only genuinely source/domain-specific remainder
```

Current examples include source-specific decomposer reinvention, batch boundaries
changing identity, duplicated lifecycle/receipt machinery, file-grain scheduling and
semantic request calculation repeated in planning and execution.

This is intended to stop the pattern where every new corpus, modality, UI or benchmark
gets another special-case repair over the same architectural hole.

## Direct corrections outrank pattern matching

Issue #24 closed one historical evidence boundary. It does not freeze later inventor
corrections out of the product.

On continuation or resumed work:

1. load the direct-evidence/product authority stack before current implementation shape;
2. classify every material direct correction before deriving implementation prose;
3. bind it to an existing requirement/contract/issue/test or create/update one;
4. update or explicitly supersede lower-authority artifacts it invalidates;
5. keep unresolved direct corrections visible as obligations.

Assistant hypotheses, familiar library architecture, current source layout, old
implementation patterns and model-training priors cannot manufacture invention
authority. If they conflict with higher product law, they lose.

## Benchmark truth

Performance evidence names both semantic work and physical plan.

The old-iteration run `33608791817` currently means:

- about 465,000 four-character BPE-equivalent units/s is a measured old-iteration
  **single-thread core floor**, not a refactor result;
- its first file-grain multi-worker curve is a finite-corpus scheduler lower bound,
  not a whole-machine ceiling;
- replicated independent streams can measure aggregate host saturation, but do not
  prove one semantic DAG is internally parallel;
- refactor performance is owed independently under #166 using matched semantic work,
  exact provider/resource receipts and acceptance fingerprints.

No scalar multiplication, duplicated workload, hidden second semantic calculation,
quality change or unlabelled source-boundary change is allowed to manufacture a
speedup claim.

## Definition of progress

Progress is an accepted reduction in the remaining finish-line graph. It is not the
number of issues, PRs, commits, documents or tests created.

A work item advances when its executable exit condition becomes true and its evidence
unblocks a required downstream owner. A new abstraction that does not reduce such an
obligation is not automatically progress.

## Machine enforcement

`tests/finish_line_program_tests.py` validates the contract against the operation model,
authority stack and agent projection. It also mutates attractive failure modes so the
gate proves it notices them, including:

- removing a mandatory whole-product owner;
- dropping a required conventional-substitution control;
- making an active sprint item lack an executable exit or next action;
- changing a GitHub milestone identity;
- promoting a PR to product-finish-line authority;
- removing mutation sensitivity from the delivered evidence chain.

The test is registered in CTest and the test-evidence registry. #180 remains open until
live GitHub issue/PR reconciliation, stale #128-era wording, milestone projection drift,
and #22 consumption of this contract are also closed. This contract constrains future
work; it is not documentation pretending the product is delivered.
