# PR 93/94 local and remote reconciliation — 2026-08-31

## Classification

This audit records observed development lineage. It is not product authority,
implementation acceptance, or permission to copy a superseded implementation into
the clean refactor.

The inventor confirmed that the dirty PR 93 and PR 94 worktrees contain work from
the interrupted local coding session, while GitHub/UI-agent work continued on remote
branches. Therefore pull-request supersession is not a deletion decision. The local
trees were first preserved byte-for-byte, then compared against the later remote
work file by file.

## Recoverable inputs

| Boundary | Interrupted base | Preserved complete tree | Published preservation ref |
| --- | --- | --- | --- |
| PR 93 local worktree | `e8de3e44b59eed0b87deb302c224db194a9e4f55` | `822bb9b35697ff65104bb1b269872b2d49967cbc` | `origin/preserve/pr93-local-wip-20260831` |
| PR 94 local worktree | `fe80778b8c93b71102db0a5691160a96976dd072` | `fe23b5ee9b374b0d637dbbbdf525040ddc06ecca` | `origin/preserve/pr94-local-wip-20260831` |

The original dirty worktrees remain untouched. `state/local-work-preservation-2026-08-31.json`
contains the index-parent commits, tree identities, status/delta digests, recovery
commands, and remote namespace inventory.

## PR 93: recursive decomposition orchestration

PR 93 was closed as superseded by PR 94. The preserved local tree contains real
work, but the central recursive decomposition behavior is also the historical
counterexample exposed by the later canonical-witness boundary repair:

- provider, span, media, range, depth, and parent metadata were lowered into
  ordinary composition requests;
- `RecursiveAdmissionPreservesExactProfileAndAddsDecomposition` required the
  recursive request count and profile span count to exceed the legacy plan;
- the resulting metadata content could change the canonical entity/physicality
  population instead of remaining occurrence-scoped structural testimony.

That implementation must not be replayed as current product behavior. The useful
requirement recovered from it is the need to retain exact recursive syntax-provider
structure. Current authority places that structure in source occurrence,
physicality-trajectory witness, provider receipt, and attributed interpretation
state without admitting witness metadata as canonical source content.

Disposition: **preserved historical implementation and deliberate-defect fixture;
semantically superseded by the witness-only boundary pursued in PR 107**.

## PR 94: canonical composition and explicit occurrence intent

The preserved PR 94 delta separates canonical calculation from occurrence emission:

- composition requests default to no occurrence;
- an explicit request flag emits occurrence testimony;
- source ordinal, source fingerprint, and occurrence context cannot change entity or
  physicality identity;
- occurrence storage/memory are accounted from explicitly requested records;
- native and PostgreSQL tests exercise zero-occurrence canonical calculation and
  explicit source occurrence emission.

PR 107 retains and expands this semantic boundary. Its composition contract names
`request.flags.emit_occurrence`; its engine and PostgreSQL work distinguish emitted
`occurrence_count` from `logical_occurrence_count`; and its tests add source
structural-witness, resource-accounting, atom-reuse, recursive-merge, and
witness-binding coverage.

Disposition: **semantic work retained and evolved in PR 107; preserve PR 94 as exact
lineage until PR 107 is repaired, reconciled with current main, and accepted**.

## PR 94 workflow delta

The local workflow changes made hosted CI call custom-stack CI, made custom-stack a
reusable workflow, and removed pull-request execution from the destructive product
activation workflow. Current main now has the more explicit `product-path` classifier
and fail-closed gate from PR 105, with hosted/custom-stack/PostgreSQL/package evidence
selected from exact changed paths. Product activation remains dispatch-only.

Disposition: **the safety intent is retained by the current product-path governance;
the old YAML hunks are not replayed mechanically**. They remain recoverable in the
PR 94 snapshot and may be consulted as behavioral lineage if the current product
gate exposes a regression.

## No-loss conclusion

- Every complete interrupted tree is reachable from a published remote ref.
- The dirty worktrees were not reset, cleaned, rebased, or overwritten.
- PR 93's central code path is retained as evidence of the rejected identity/witness
  conflation, not silently discarded.
- PR 94's occurrence-intent boundary is identifiable in PR 107 rather than being
  mistaken for obsolete work.
- The PR 94 workflow intent is represented by the later product-path gate.
- No preserved hunk is promoted to accepted implementation solely because it existed
  locally or appeared in an earlier pull request.

## Remaining implementation boundary

PR 107 is not accepted by this audit. It diverged from current main, removes newer
product-path files when viewed from the current base, and still needs a dedicated
current-main integration, full native/PostgreSQL proof, and deliberate-defect
validation. This audit only proves where the interrupted work stopped and where its
requirements and implementation lineage continued.
