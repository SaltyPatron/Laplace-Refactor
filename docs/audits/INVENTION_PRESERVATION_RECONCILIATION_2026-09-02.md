# Invention preservation reconciliation — 2026-09-02

## Status of this record

This is a **reconciliation audit**, not a new source of inventor authority and not implementation evidence. Its purpose is to prevent requirements already present in direct inventor material, governing product law, current issue acceptance, and historical behavioral evidence from being separated into apparently unrelated feature ideas.

Authority remains governed by `contracts/authority-stack.json`.

A human message can quote prior agent prose. That proves the human submitted the quoted material for reconciliation; it does not, by itself, make every quoted sentence inventor-authored. Quoted agent material is therefore a lead and is retained only where corroborated by direct requirements, stable product law, executable owners, or concrete historical evidence.

## Audit trigger

The 2026-09-02 review was triggered by the explicit concern that `Laplace` and `Laplace-Refactor` may fail to preserve the invention/vision as an integrated machine even though many individual mechanisms appear in documents, issues and old code.

The audit found that concern materially justified in the preservation layer:

1. The mathematical research program and inventor-direct synthesis must exist on the authoritative integration/main path and be loaded before agent work selection.
2. The clean chess documents preserved LINE/PLAYING/event separation, shared canonical positions/moves/transitions and generic entity-world traversal, but needed the stronger exact **reusable subtrajectory/segment fold-and-expand law** as stable product prose.
3. Concrete player/time/goal-scoped model-export acceptance examples must not survive only in historical issue comments.
4. Identity-versus-realization must be one general law spanning chess notation, model tensor paths, UI labels and other serialization names.
5. The historical model lesson that one circuit is one reusable structure/trajectory must remain a common Merkle-DAG/materialization law rather than an ETL optimization anecdote.

## Corroborated synthesis

### A. One recursive content machine, not parallel modality systems

Every digital structure is a typed universal AST whose canonical persistent form is a content-addressed Merkle DAG. Text, games, code, model state and other modalities use the same identity, composition, relation, evidence, execution and realization laws. Language, modality, source and model are witnessed dimensions rather than engine partitions. Same canonical content has one identity while occurrences/provenance remain distinct.

```text
ATOM -> COMPOSITION -> higher COMPOSITION -> TRAJECTORY -> WITNESSED OCCURRENCE
```

Text, chess, checkpoints, code and future media instantiate this law at different typed structural altitudes.

### B. Exact segment reuse is a first-order Merkle-DAG consequence

If an exact ordered transition subpath repeats, the subpath itself is reusable canonical structure. It can be folded into a larger line, referenced by multiple playing occurrences, expanded losslessly, and used as an anchor for navigation/query.

```text
weak query:   historical games containing P9
strong query: historical playings containing exact S = P7-M7-P8-M8-P9-M9-P10
```

Both are valid queries. The second may not be silently implemented as the first when the product claim is about the exact line/segment.

A future game reusing a known opening segment and a known historical segment should not duplicate those exact canonical substructures merely to preserve the new game occurrence. Its occurrence/path/provenance remains independent.

### C. Navigation is world traversal, not UI trivia

A historical observation such as “this exact line occurred in a witnessed playing” can expose navigation/effect behavior anchored at that segment. The product may present an `OPEN_PLAYING`-like action, but the semantic owner is the generic entity-world/search/realization/effect lifecycle rather than a private chess UI callback.

The live game and historical playing are separate contexts. Navigating to the historical trajectory must not mutate the live game. The same law applies to recentering a graph on a person, opening a document at an occurrence, inspecting a model circuit in another checkpoint, or reopening a prior execution receipt.

### D. Realization names do not own identity

```text
canonical chess move/transition != SAN string
canonical game/line             != PGN serialization
canonical model component       != HuggingFace/GGUF tensor path
canonical entity                != English display label
```

Realization selects language/notation/codec/view using available type and context. Failure to find a stored string label is not proof that a typed object is unrealizable when its structural realization recipe exists.

### E. Conventional model anatomy is witnessed architecture, not native cognition

Q/K/V/O, FFN, gates, experts, embeddings, position, normalization and output heads are target/consumer roles. Checkpoint structure is admitted evidence/corpus state; exact content identity remains separate from the role/path under which one checkpoint uses it; induced/functional behavior is separately calculated/witnessed; recurring components/operators may be compared without Laplace becoming Q/K/V/O internally; target compilation pours selected native operators into consumer roles only after the target-neutral operator exists.

### F. Forward execution is another typed trajectory

For a fixed conventional model/checkpoint, input, numeric precision, operator implementation and complete execution recipe, the forward calculation is a deterministic transformation trajectory under the declared tolerance/bit contract. If changing an omitted execution coordinate changes the result, the recipe/receipt boundary was incomplete.

### G. Materialization follows information, not query combinatorics

Do not explode every circuit/token pairing into independent stored attestations when the circuit’s full token/score walk is one lossless physicality trajectory. Requested pair/coupling state can be derived or selectively materialized.

```text
do not store the same opening once per game;
do not store the same subtree once per document occurrence;
do not store the same circuit content once per checkpoint occurrence;
do not materialize V^2 pair state because a later query can name pairs.
```

Occurrence multiplicity is evidence/provenance. It is not permission to duplicate canonical structure or redo canonical composition work linearly.

### H. Scoped exports are query/evidence boundaries over one world

Source/person/player/time/domain/model/goal scopes are filters over one persistent substrate, not separately trained worlds. Representative extreme fixtures include Fischer-scoped versus Nakamura/Caruana/Carlsen-scoped chess exports, goal/objective-scoped exports, intentionally poor/worst-player behavior, user/player-style emulation, and under-18 Karpov.

The celebrity names are acceptance probes for the generic scope compiler. `under-18 Karpov` is a temporal filter over the same Karpov identity and eligible occurrences/evidence, not a second person.

## Current repository mapping

| Preserved law | Primary clean owner(s) |
| --- | --- |
| canonical identity / composition / physicality | #7 |
| occurrence / testimony / dependence | #16 |
| typed standing | #110 |
| finite cognition / typed operators | #17, #60 |
| observation + facts + calculations in one forward program | #132 |
| discourse / semantic acts / realization | #18 |
| generic entity worlds / navigation | #68 |
| chess complete cross-modal proving slice | #136 |
| model witnesses / target compilation | #20, #61, #129 |
| Gödel / OODA / reusable learning | #19, #169, #182 |
| mathematical preservation spine | #184 |
| complete product | #23 / #22 |

## Historical evidence boundary

The old `SaltyPatron/Laplace` repository is behavioral evidence/counterexample only, not clean implementation authority. Historical position lookup, model circuit trajectory corrections, invention catalog records and scoped export examples can explain required behavior without importing old ABI/schema authority.

## Preservation defects that must remain closed

Future audits should fail if any of these regressions occur:

1. Governing product/audit files referenced by the program do not exist on authoritative main/integration.
2. Exact trajectory reuse is documented only as a chess optimization rather than a universal recursive composition law.
3. Historical line matching silently falls back to endpoint/position equality while claiming exact segment equality.
4. Source/player/time labels are salted into canonical segment/content identity.
5. SAN/PGN/tensor path/display labels become identity authority.
6. Target-model role names are promoted into native Laplace cognition ontology.
7. Repeated occurrences cause repeated canonical composition/model-circuit work.
8. Derived pair surfaces are eagerly materialized at V^2/world-all-pairs scale without a measured, rebuildable acceleration contract.
9. An issue comment is the only surviving record of a product-defining acceptance law.
10. A UI-private action becomes the only implementation of semantic navigation.
11. A familiar narrow MVP substitutes for missing generic semantics and is then treated as the architecture.

This record preserves architecture and reconciliation. It does not claim runtime implementation or close #184, #136, #129, #23 or #22.
