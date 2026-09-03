# Inventor-direct synthesis reconciliation — 2026-09-02

## Status of this record

This is a **reconciliation audit**, not a new source of inventor authority and not
implementation evidence. Its purpose is to prevent requirements already present in
direct inventor material, governing product law, current issue acceptance, and
historical behavioral evidence from being separated into apparently unrelated feature
ideas.

Authority remains governed by `contracts/authority-stack.json`.

In particular, a human message can quote prior agent prose. That proves the human
submitted the quoted material for reconciliation; it does not, by itself, make every
quoted sentence inventor-authored. Quoted agent material is therefore treated here as a
lead and retained only where it is corroborated by direct requirements, stable product
law, executable owners, or concrete historical evidence.

## Audit trigger

The 2026-09-02 review was triggered by the explicit concern that `Laplace` and
`Laplace-Refactor` may fail to preserve the invention/vision as an integrated machine,
even though many individual mechanisms appear in documents, issues and old code.

The audit found that concern materially justified in the preservation layer:

1. Refactor issue #184 named this file and
   `docs/product/LAPLACE_MATHEMATICAL_RESEARCH_PROGRAM.md` as its primary records, but
   neither file existed on authoritative `main`.
2. The clean chess documents preserve LINE/PLAYING/event separation, shared canonical
   positions/moves/transitions and generic entity-world traversal, but did not state the
   stronger exact **reusable subtrajectory/segment fold-and-expand law** as stable
   product prose.
3. Several concrete player/time/goal-scoped model-export acceptance examples survived
   only in an old-repository issue comment rather than in stable cross-cutting product
   law.
4. Identity-versus-realization is present in domain-specific places, but the connection
   between chess notation, model tensor paths, UI labels and other serialization names
   needed to be stated as one general preservation law.
5. Historical model ingestion already contains the "one circuit = one entity = one
   physicality trajectory" correction, but that storage lesson can be lost if it is
   remembered only as a model ETL optimization instead of an instance of the common
   Merkle-DAG/materialization law.

This repair therefore creates the #184-referenced stable research-program document and
makes the missing joins explicit without inventing a second architecture.

## Corroborated synthesis

### A. One recursive content machine, not parallel modality systems

Corroborated governing material already says:

- every digital structure is a typed universal AST whose canonical persistent form is
  a content-addressed Merkle DAG;
- text, games, code, model state and other modalities use the same identity,
  composition, relation, evidence, execution and realization laws;
- language, modality, source and model are witnessed dimensions rather than engine
  partitions;
- same canonical content has one identity while occurrences/provenance remain distinct.

The synthesis to preserve is therefore not three separate analogies but one law:

```text
ATOM -> COMPOSITION -> higher COMPOSITION -> TRAJECTORY -> WITNESSED OCCURRENCE
```

Text, chess, checkpoints, code and future media instantiate this law at different typed
structural altitudes.

### B. Exact segment reuse is a first-order Merkle-DAG consequence

Existing clean chess law already distinguishes reusable LINE content from PLAYING
occurrence and records exact ordered trajectories. Historical old-product code also
queries canonical positions inside witnessed playings.

The missing statement was the stronger consequence: if an exact ordered transition
subpath repeats, the subpath itself is reusable canonical structure. It can be folded
into a larger line, referenced by multiple playing occurrences, expanded losslessly,
and used as an anchor for navigation/query.

This is materially different from a position-only history lookup:

```text
weak query:   historical games containing P9
strong query: historical playings containing exact S = P7-M7-P8-M8-P9-M9-P10
```

Both are valid queries. The second may not be silently implemented as the first when
the product claim is about the exact line/segment.

A future game reusing a known opening segment and a known historical segment should not
need to duplicate those exact canonical substructures merely to preserve the new game
occurrence. Its occurrence/path/provenance remains independent.

### C. Navigation is world traversal, not UI trivia

A historical observation such as "this exact line occurred in a witnessed Fischer-
Spassky playing" can naturally expose a navigation/effect operation anchored at that
segment. The product may present an `OPEN_PLAYING`-like action, but the semantic owner
must be the generic entity-world/search/realization/effect lifecycle rather than a
private chess UI callback.

The live game and historical playing are separate contexts. Navigating to the historical
trajectory must not mutate the live game.

This same law underlies recentering a graph on a person, opening a source document at an
occurrence, inspecting a model circuit in another checkpoint, or reopening a prior
execution receipt.

### D. Realization names do not own identity

The clean chess world already requires display labels not to affect node identity. The
same distinction applies across domains:

```text
canonical chess move/transition != SAN string
canonical game/line             != PGN serialization
canonical model component       != HuggingFace/GGUF tensor path
canonical entity                != English display label
```

Realization selects a language/notation/codec/view using available type and context.
The inability of a UI to find a stored string label is not proof that a typed object is
"unrealized" if its structural realization recipe is available.

### E. Conventional model anatomy is witnessed architecture, not native cognition

Current clean model owners already state that Q/K/V/O, FFN, gates, experts, embeddings,
position, normalization and output heads are target/consumer roles. Historical model
code inventories conventional tensor roles and preserves per-circuit factor/score
trajectories.

The synthesis is:

- checkpoint structure is admitted evidence/corpus state;
- exact content identity remains separate from the role/path under which one checkpoint
  uses it;
- induced/functional behavior is separately calculated/witnessed;
- Laplace may compare recurring components, operator changes and measured behavioral
  changes without becoming Q/K/V/O internally;
- target compilation pours selected native operators into consumer roles only after the
  target-neutral operator exists.

Thus the observation "a Q projection exists" is evidence about a conventional
architecture. It is not a declaration that Q is an atom of Laplace cognition.

### F. Forward execution is another typed trajectory

For a fixed conventional model/checkpoint, input, numeric precision, operator
implementation and complete execution recipe, the forward calculation is a deterministic
transformation trajectory under the declared tolerance/bit contract.

If changing an omitted execution coordinate changes the result, that coordinate was not
irrelevant; the recipe/receipt boundary was incomplete. This is the same general
record-versus-calculate law used by deterministic chess/tablebase/evaluator operations
and other domain calculations.

### G. Materialization follows information, not query combinatorics

Historical model ingestion records the important correction that exploding every
circuit/token pairing into independent attestations is a representation defect. The
circuit's full token/score walk belongs in one lossless physicality trajectory; requested
pair/coupling state can be derived or selectively materialized.

This is the model version of the same general rule:

```text
do not store the same opening once per game;
do not store the same subtree once per document occurrence;
do not store the same circuit content once per checkpoint occurrence;
do not materialize V^2 pair state because a later query can name pairs.
```

Occurrence multiplicity is evidence/provenance. It is not permission to duplicate
canonical structure or redo canonical composition work linearly.

### H. Scoped exports are query/evidence boundaries over one world

Current #20/#61/#129 law already supports source/person/player/time/domain/model scopes
and treats target models as compiled views over closed epochs.

Concrete old-product acceptance material additionally names useful extreme fixtures:

- Fischer-scoped versus Nakamura/Caruana/Carlsen-scoped chess exports;
- goal/objective-scoped exports;
- intentionally poor/worst-player behavior;
- user/player-style emulation;
- under-18 Karpov.

The important law is not the celebrity names. `under-18 Karpov` must be a temporal
filter over the same Karpov identity and eligible occurrences/evidence, not a second
person or a separately trained substrate. The examples belong in stable acceptance as
probes of the generic scope compiler.

## Current repository mapping

| Preserved law | Primary clean owner(s) | Current status at this audit |
| --- | --- | --- |
| canonical identity / composition / physicality | #7 | governing law exists; implementation remains staged/partial by current program state |
| occurrence / testimony / dependence | #16 | explicit owner |
| typed standing | #110 | explicit owner; must remain separate from meaning/truth |
| finite cognition / typed operators | #17, #60 | partial native kernels; complete provider/public route remains open |
| observation + facts + calculations in one forward program | #132 | explicit owner |
| discourse / semantic acts / realization | #18 | explicit owner |
| generic entity worlds / navigation | #68 | explicit owner |
| chess complete cross-modal proving slice | #136 | explicit owner |
| chess world integration | `docs/product/CHESS_GRAPH_INTEGRATION.md` | exists; this repair adds explicit exact-segment law |
| model witnesses / target compilation | #20, #61, #129 | strong clean requirements; implementation remains open |
| Gödel / OODA / reusable learning | #19, #169, #182 | explicit owners |
| mathematical preservation spine | #184 | issue exists; referenced stable records were missing before this repair |
| complete product | #23 / #22 | explicit terminal owners |

## Historical evidence used only as evidence/counterexample

The old `SaltyPatron/Laplace` repository is not clean implementation authority, but it
contains useful behavioral evidence that explains why these laws must remain visible:

- `ChessMoveCommentary.HistoricalPositionLineAsync` demonstrates canonical historical
  position lookup against witnessed playings; it is evidence for the weaker primitive
  from which exact-segment acceptance must be distinguished.
- `ModelTokenEdgeETL` explicitly records that per-token/circuit row explosion is a
  representation defect and carries one circuit's full walk in one physicality.
- the old invention catalog already records circuit testimony trajectories, exact
  occurrence, chess trajectory hierarchy, source-scoped synthesis, recipe-driven export
  and dynamic-frontier inference.
- old issue #928 contains source-/goal-/person-/time-scoped export examples in issue
  discussion; those examples are retained as acceptance probes rather than old ABI
  authority.

No old code/schema is imported into the clean implementation by this audit.

## Specific preservation defects to keep closed

Future audits should fail if any of these regressions occur:

1. `#184` references governing product/audit files that do not exist on authoritative
   `main`.
2. exact trajectory reuse is documented only as a chess optimization rather than a
   universal recursive composition law.
3. historical line matching silently falls back to endpoint/position equality while
   claiming exact segment equality.
4. source/player/time labels are salted into canonical segment/content identity.
5. SAN/PGN/tensor path/display labels become identity authority.
6. target-model role names are promoted into native Laplace cognition ontology.
7. repeated occurrences cause repeated canonical composition/model-circuit work.
8. derived pair surfaces are eagerly materialized at V^2/world-all-pairs scale without a
   measured, rebuildable acceleration contract.
9. an issue comment is the only surviving record of a product-defining acceptance law.
10. a UI-private action becomes the only implementation of semantic navigation.
11. a familiar narrow MVP substitutes for missing generic semantics and is then treated
    as the architecture.

## Repair performed from this audit

This audit is paired with the following preservation changes:

- create `docs/product/LAPLACE_MATHEMATICAL_RESEARCH_PROGRAM.md` as the stable #184
  preservation spine;
- create this reconciliation record;
- strengthen `docs/product/CHESS_GRAPH_INTEGRATION.md` with explicit exact segment
  identity/folding, fold-expand reconstruction, matched-segment navigation and typed
  chess realization acceptance;
- bind the stable research-program document into `contracts/authority-stack.json` ahead
  of the agent working projection;
- project the reuse/realization/non-explosion rules into `AGENTS.md` so future agents are
  told to test for drift before writing another copy/materialization.

These changes preserve architecture. They do not claim runtime implementation or close
#184, #136, #129, #23 or #22.
