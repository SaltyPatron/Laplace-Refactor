# Branch-estate closure

`contracts/branch-estate-closure.json` is the executable law. `state/branch-estate-ledger.json` is the current observed reconciliation ledger. This document explains them and does not create additional product authority.

## Why this exists

A branch is a temporary concurrency boundary. It is not a durable implementation store, a product milestone, or evidence that work is delivered.

The 2026-09-02 audit found 124 branches in `SaltyPatron/Laplace-Refactor`, including five open PR heads and at least thirteen live no-open-PR tips with unique commits after obvious preservation, stale, and already-represented lineages were removed. Several branch tips contain implementation absent from both current `main` and the large #128 integration branch.

That state means repository structure itself can hide unfinished work. Branch closure is therefore product-integrity acceptance, not cosmetic Git cleanup.

## Governing rule

Every unique non-`main` branch has exactly one disposition:

- `ACTIVE_PR`: reviewed current route to `main`;
- `MERGED`: exact behavior exists on authoritative `main` and the merge/replacement commit is named;
- `REUSE`: valid implementation is being replayed/rederived through a current owner;
- `ADAPT`: required behavior survives but the stale implementation violates current architecture and must be rederived;
- `SUPERSEDED`: an exact replacement owner and behavior-level proof are named;
- `RETIRE`: the old behavior is invalid/obsolete and requirement non-loss is proven;
- `ARCHIVE`: historical evidence only; it cannot hold the only implementation of required behavior;
- `UNACCOUNTED_UNIQUE_BRANCH`: explicit P0 failure state.

A branch name, old PR description, commit count, or a statement that something was "probably merged elsewhere" is not a disposition.

## Reconciliation is behavioral

Raw Git commit arithmetic is insufficient because the existing estate contains stacked, forked, rebased, reconstructed, and partially superseded lineages. Two branches can each be hundreds of commits ahead of `main` while containing mostly the same historical work.

For each live tip the reconciliation process therefore records the merge base and unique commits, then compares final behavior and current authority. Still-valid behavior maps to a requirement, semantic owner, acceptance gate, and exact route to `main` before an old branch may be retired.

## Branch-creation freeze

While #183 has unresolved unique tips:

- do not open speculative feature branches;
- do not create sibling implementations to bypass a red owning PR;
- do not create another integration branch to collect previous integration branches;
- a new branch is allowed only as the explicit repair/reconciliation vehicle of an existing owner and it must enter the ledger immediately.

Fixing the existing five open PRs is part of this closure campaign. The freeze does not prohibit work required to make those owners green and merge them.

## Terminal condition

The complete product cannot release until all terminal counters are zero:

```text
open PRs with failed required checks          0
unaccounted unique branch tips               0
valid orphan implementation                  0
required behavior with no current owner      0
active branch without explicit disposition   0
branch-only acceptance/evidence               0
```

The goal is not merely fewer branches. The goal is one authoritative `main` containing every accepted implementation, with any surviving non-main ref either a short-lived registered PR branch or explicit historical evidence whose valid behavior is represented elsewhere.

## Current baseline

The current mutable baseline is `state/branch-estate-ledger.json`. It intentionally records unresolved branches as `UNACCOUNTED_UNIQUE_BRANCH`; that makes the unfinished state visible instead of hiding it behind a branch name.

Known branch-only findings already requiring reconciliation include the cognition-forward-pass side fork, native tabular-decomposition work, and the live product probe. The remaining families require ancestry and behavior-level reconciliation before they can be called merged, superseded, adapted, reused, or retired.
