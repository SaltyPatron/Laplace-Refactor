# Chess proving ladder — cross-modal forward program with a pinned conventional ruler

Chess is a required whole-machine proving slice because it supplies exact state, legal transitions,
measurable outcomes, historical trajectories, expert literature, lexical semantics, deterministic
calculations, external evaluators, and exact tablebase closures.

Primary clean ownership: #17, #60, #132, #136, #139, with evidence/standing from #16/#110, source
admission from #53, and whole-route cohesion from #70. Historical behavioral evidence and negative
controls live in `SaltyPatron/Laplace` #487/#491/#574/#833/#834/#1419/#1424.

## The three-part proving stack

```text
Stockfish
  one strong classical calculated chess metric/provider
  external calibration/reference/full-strength opponent

Laplace
  canonical persistent chess world
  cross-modal Chess Forward Pass under test

cutechess-cli
  neutral match/tournament conductor
  paired openings/colors/clocks/process lifecycle
  PGN/transcript/result recorder
```

Stockfish is not a separate epistemic class or final-move authority. Cute Chess is not a decorative
demo wrapper. The former supplies a powerful conventional metric/opponent; the latter keeps the
experiment mechanically stable while Laplace variants change.

## Canonical chess law

Chess follows the same content law as every other modality:

> **same canonical content = same BLAKE3 identity**

The old issue `SaltyPatron/Laplace#491` records the required identity ruling: canonical
`PositionContent`/Hash128 is substrate identity; Zobrist is a TT/search accelerator only.

The primitive domain is bounded — 64 squares, fixed piece kinds/colors, bounded move forms — while
complete positions and trajectories are combinatorially huge.

A game is an ordered physicality trajectory over shared canonical states/actions. The same position
or move reached by several PGNs, a book, self-play, or analyzer source remains one canonical content
identity while every occurrence keeps its own event/source/player/time/provenance state.

## Material-first law

Material is exact deterministic state and remains available without corpus evidence. It is a
baseline to supplement, not a universal objective and not a signal to replace with popularity.

Historical observations such as a very large material Elo gain are hypotheses/fixtures to reproduce
or falsify. The clean contract never hardcodes an Elo value for material, rook files, pawn structure,
Stockfish analysis, or any later plane.

## Typed chess calculations

A conforming recipe may expose separately:

- material/imbalance and phase;
- piece-square placement;
- bishop pair;
- rook open/semi-open files;
- pawn structures: doubled, isolated, connected, passed, backward/candidate passers, islands/chains
  where defined;
- king safety/pawn shield;
- mobility, constrained pieces, space under an explicit definition;
- threats, hanging pieces, pins, forks, skewers, discovered attacks and mate motifs;
- outposts/weak squares;
- piece coordination, batteries, connected rooks, rook-on-seventh structures;
- minor/major placement and exchanges;
- last-move/trajectory context;
- opening/LINE state;
- exact tablebase state/distance;
- transpositions and structural/geometry peers;
- Stockfish classical analysis under a declared generation/recipe.

These are calculations/structures. Game/book/player/source observations about their effectiveness are
separate evidence lanes.

## Stockfish is another classical metric plane

Stockfish's distinction is the breadth/strength of its calculation, not ontology.

A reproducible analysis coordinate binds approximately:

```text
canonical position/state id
candidate move id when move-scoped
Stockfish generation:
  release/build/binary digest
  NNUE/network identity
  calculation-affecting UCI options
analysis recipe:
  fixed depth and/or nodes
  searchmoves / MultiPV policy
  tablebase boundary/options when selected
  adapter/calculation version
-> calculated result content
```

Eligible output may include cp/mate score, WDL estimate, per-candidate move delta/quality,
depth/seldepth/nodes, PV/MultiPV, and declared tactical/search labels.

These remain distinct from exact Laplace legality/material calculations, exact tablebase facts,
observed PGN outcomes, grandmaster-book testimony, player history, and lexical/semantic facts.

### Deterministic convergence and deduplication

For a deterministic census profile:

- same canonical input + same generation/recipe should converge to the same calculated content;
- repeated execution does not create independent semantic support;
- run/provenance occurrence may remain separately visible;
- crash/resume cannot double-count completed calculations;
- a changed result under an allegedly deterministic closed recipe is a typed reproducibility
  discrepancy, not a new position identity and not something to average away.

Use fixed binary/network/options, fixed depth/nodes, and single-thread execution where required to
eliminate parallel scheduling variation. Full-strength multithread/time-based match search is a
separate profile and need not promise bit-identical internal traces.

The same canonical position/move observed in many PGNs/books/games can therefore share one Stockfish
calculation while every occurrence/source remains independently attributable.

## Cross-modal chess examples

### `fork`

```text
lexical sources        candidate senses/definitions/taxonomy
grandmaster book       prose/explanation/diagram grounding
board calculator       exact fork geometry
Stockfish              classical tactical/evaluation metric
PGN/live games         observed occurrences/responses/players/outcomes
```

### `gambit`

```text
lexical meaning
+ named opening/book explanation
+ exact material sacrifice/imbalance
+ optional Stockfish classical analysis
+ observed game/player/era/time trajectories
```

The material deficit remains exact even when other selected channels support the gambit under the
active goal.

## One native program

```text
RESOLVE board + language/content identities
-> SENSE domain-ambiguous content
-> ORIENT goal/player/session/authority/resources
-> COMPILE guidance/search program
-> SELECT admissible observation/fact/calculation/standing providers
-> SCAN document/game physicality + evidence
-> CALCULATE board/material/structure/motif/tablebase/Stockfish state as selected
-> PROPOSE legal/tactical candidate batch
-> FOLD/COMPARE only declared channels
-> SEARCH/UPDATE descendant states under finite resources
-> SELECT move / semantic act / typed partial or why-not
-> REALIZE/EFFECT
-> WITNESS result/consequence + receipt
```

A Stockfish `bestmove` cannot satisfy the final Laplace selection obligation merely because it
exists. Search-tree use must extend beyond root-only substrate steering and the physical plan cannot
perform one PostgreSQL/SPI query per node.

## Stockfish profiles

### Deterministic analysis/census

Creates reproducible classical metric state over selected canonical positions/moves.

### Calibration opponent

Strength-limited profile for coarse localization of a Laplace variant. UCI Elo is a comparator
control, not a universal human rating claim.

### Fixed reference opponent

Pinned resource/configuration profile used as the unchanged external ruler across the whole ladder.

### Host-max opponent

Full-strength conventional ceiling challenge, potentially multithreaded with large hash/tablebases/
host tuning. Every calculation-affecting setting and hardware/resource boundary is receipted.

Changing any field creates a new generation.

## Cute Chess orchestration contract

A match generation binds exact Cute Chess version/recipe plus:

- exact engine executable/configuration identities;
- paired/color-swapped opening suite and order;
- gauntlet/round-robin/self-play/variant scheduling;
- clocks/time-control/depth/nodes interface as selected;
- process lifecycle and crash/time-loss handling;
- adjudication/result semantics;
- PGN/transcript/result artifact settings;
- deterministic queue/order when required.

The harness can measure:

```text
Ai vs Ai-1                    incremental channel contribution
full vs full-minus-X          channel interaction/necessity
Ai vs Stockfish(reference)    progress on one frozen ruler
Laplace X vs Laplace Y        firmware/provider comparison
Laplace full vs SF host-max   conventional ceiling challenge
self-play/regression          behavior/stability
```

Produced PGNs are admitted only into a later evidence epoch. A frozen experiment cannot mutate its
own world while measuring it.

## Benchmark analysis-boundary honesty

Because Stockfish analysis can be admitted as a classical metric, every benchmark declares whether
its exact positions/moves were already analyzed by the selected generation.

Two valid experiment types:

- **Stockfish-informed** — the Stockfish calculated plane is eligible like any other selected metric;
- **held-out / Stockfish-blind** — benchmark positions are outside the analysis census or the
  Stockfish plane is disabled at inference.

The defect is hiding which experiment was run.

## Frozen-ruler law

One experiment pins:

```text
Stockfish opponent generation
Stockfish analysis generation + inclusion/holdout law
Cute Chess generation/recipe
Laplace world/evidence epoch
Laplace firmware/recipe
opening/challenge suite
hardware/resource policy
match/adjudication/tie law
```

Every rung uses the same boundary. Improving the comparator/orchestrator creates a new generation
and requires a new reference run rather than rewriting old results.

## Cumulative ladder

A representative recipe is:

```text
A0  deterministic legality/tactical proposal + material
A1  + classical placement/phase
A2  + rook-file/pawn-structure/bishop-pair and other exact structures
A3  + motifs/geometry/remaining deterministic calculations
A4  + optional Stockfish classical-analysis plane under declared scope
A5  + learned placement residuals
A6  + learned structural residuals
A7  + global game/trajectory observations
A8  + player/opponent/rating/time conditioning
A9  + opening/shape/tablebase/catalog providers
A10 + grandmaster-book/expert evidence where relevant
A11 + lexical/sense/domain cross-modal providers where relevant
A12 + complete selected #136 Chess Forward Pass
```

The exact sequence is an experiment recipe, not ontology. Each component/rung should support
adjacent, full-minus-one, and fixed-external-ruler measurement where mathematically useful.

## Measurement protocol

- exact color-swapped paired opening suite;
- identical opening distribution/order across variants;
- identical relevant Laplace resource budgets;
- sufficient games for the stated statistical conclusion;
- W-D-L, Elo with uncertainty/margin and/or SPRT where appropriate;
- CPU, nodes, memory, I/O, elapsed, provider/crossing metrics;
- failures, crashes, time losses, adjudications, unavailable providers;
- raw PGN/config/transcript/result artifacts;
- no mid-run score promoted as final evidence.

## Uncertainty-driven compute

Typed uncertainty may guide physical effort: exact terminal closure can stop speculative search;
low-uncertainty agreement can reduce confirmation; novel/high-RD/contradictory state can receive more
work; exhaustion returns typed partial/upper-bound/why-not state. Standing never overrides exact
legality or terminal constraints.

## Move receipt

```text
exact material/tactical state
deterministic structural calculations
Stockfish classical analysis when selected
classical proposal
observed game trajectory/outcome state
player/context-conditioned state
book/expert testimony
lexical/sense state
opening/tablebase/motif/geometry state
standing/uncertainty
physical search/prune/deepen work
final selection/completion reason
```

Observed/expert contributions trace to exact evidence/dependence/provenance. Calculations trace to
the exact calculation recipe/version.

## Long-term hypothesis and falsifiability

The experiment is allowed to show either result. If the complete Laplace program eventually beats a
pinned full-strength Stockfish generation in a statistically defensible match, that is a measured
outcome; if not, the ladder identifies which expected gains fail, interact badly, or cost too much.

The point is not to reproduce Stockfish internally. Stockfish remains a strong classical metric,
analysis provider, regression opponent, and independent ruler while Laplace tests whether a
persistent typed cross-modal world can become a stronger decision platform.

## Acceptance / deliberate defects

#136 and #139 own executable acceptance. Required failures include:

- analyzer/source-specific copies of equal canonical positions;
- deterministic Stockfish reruns multiplying independent support;
- same-recipe output disagreement hidden by averaging;
- multithread/time-based full-strength search mislabeled deterministic census;
- hidden Stockfish bestmove fallback;
- comparator or Cute Chess drift between rungs;
- Stockfish output promoted to tablebase/world truth;
- held-out claims over previously analyzed positions without disclosure;
- match games mutating their own frozen epoch;
- hardcoded material/rook/pawn/Stockfish Elo constants;
- one scalar flattening every chess plane;
- root-only substrate use;
- book/lexical state used only by explanation UI;
- per-node SQL/SPI search;
- superiority claimed without pinned full-strength evidence.

The positive proof keeps canonical identity shared, classical metrics typed, Cute Chess neutral, and
every additional Laplace plane measurable.
