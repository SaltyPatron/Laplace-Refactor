# Chess proving ladder — cross-modal forward program with a pinned conventional ruler

Chess is a required whole-machine proving slice for Laplace because the domain supplies exact state,
legal transitions, measurable outcomes, rich historical trajectories, expert literature, lexical
semantics, deterministic calculations, external evaluators, and exact tablebase closures.

Primary clean issue ownership: #17, #60, #132, #136, #139, with evidence/standing from #16/#110,
source admission from #53, and whole-route cohesion from #70. Historical behavioral evidence and
counterexamples live in `SaltyPatron/Laplace` #574/#833/#834/#1419/#1424.

## What the proof must establish

The outcome is not a private chess engine beside Laplace. It is one common ISA/cognition program
that can select and compose:

- canonical board/game identity and physicality trajectories;
- exact legality/material/board calculations;
- classical proposal/search state;
- PGN/live-game observations and outcomes;
- player/opponent/rating/time-conditioned history;
- structural/motif/geometry calculations and observed residuals;
- grandmaster-book document observations and attributed testimony;
- WordNet/OMW/Wiktionary/dictionary lexical/sense state;
- opening/catalog/tablebase facts;
- external Stockfish calculations as a versioned witness;
- typed standing/uncertainty;
- query-relative goal/firmware/resource policy.

No one state class may impersonate another.

## Cross-modal chess examples

### `fork`

```text
lexical sources        candidate senses / definitions / taxonomy
grandmaster book       prose, explanation, diagram/variation grounding
board calculator       exact fork geometry
PGN/live games         observed occurrences, responses, players, outcomes
```

The program uses context to select the chess-eligible sense while retaining canonical content
identity and source-specific provenance.

### `gambit`

```text
lexical meaning
+ named opening / book explanation
+ exact material sacrifice / imbalance
+ observed game/player/era/time trajectories
```

Exact material remains negative when material is sacrificed. Other selected channels may support
the gambit under the active goal without rewriting the material calculation.

## Material-first law

Material is exact deterministic state and remains available without corpus evidence. It is the
start of the measured ladder, not a universal objective and not a signal to replace with frequency.

Historical observations such as a very large playing-strength gain from adding material are
valuable hypotheses/fixtures. The clean contract never hardcodes an Elo value for material, rook
files, pawn structure, or any later channel. The experiment measures them.

## Typed structural/calculation planes

A conforming recipe may calculate and expose separately:

- material/imbalance and phase;
- piece-square placement;
- bishop pair;
- rook open/semi-open files;
- pawn structure: doubled, isolated, connected, passed, backward/candidate passers, islands/chains
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
- transpositions and structural/geometry peers.

These are calculations/structures. Game/book/source observations about their effectiveness are
separate evidence lanes.

## One native program

The representative clean execution is:

```text
RESOLVE board + language/content identities
-> SENSE domain-ambiguous content
-> ORIENT goal/player/session/authority/resources
-> COMPILE guidance/search program
-> SELECT admissible observation/fact/calculation/standing providers
-> SCAN document/game physicality + evidence
-> CALCULATE board/material/structure/motif/tablebase state
-> PROPOSE legal/tactical candidate batch
-> FOLD/COMPARE only declared channels
-> SEARCH/UPDATE descendant states under finite resources
-> SELECT move / semantic act / typed partial or why-not
-> REALIZE/EFFECT
-> WITNESS result/consequence + receipt
```

Search-tree use must extend beyond root-only substrate steering. The physical plan uses native/
set-wise/batched/perfcache execution rather than one PostgreSQL/SPI query per node.

## Stockfish is the ruler/teacher, not Laplace

Stockfish represents a strong conventional chess/search architecture useful precisely because it is
independent of Laplace.

#139 owns the comparator contract. A comparator **generation** binds exact:

```text
engine release + binary digest + build target
NNUE/network/assets loaded
UCI option set
Threads / Hash / Syzygy boundary
hardware/topology/affinity/resource policy
opening/challenge suite
match depth/nodes/time/adjudication law
```

Changing any calculation-affecting comparator input creates a new generation.

### Calibration profile

A strength-limited Stockfish profile may locate a current Laplace variant coarsely. The UCI Elo
setting is a comparator control, not a universal human Elo assertion.

### Fixed reference profile

A reproducible resource profile is used for repeated regression/strength measurements across
Laplace versions.

### Host-max profile

A full-strength conventional ceiling challenge may deliberately use all suitable host resources.
The host/configuration receipt is part of the result; it cannot be presented as hardware-independent.

### Census/teacher profile

Stockfish may calculate position evaluations, PVs, move deltas/quality labels, tactical candidates,
or bounded search results as a versioned provider.

In the inventor's use of `training`, these calculations can become admitted evidence that Laplace
later compares with independent games, books, exact tablebases, player trajectories, and outcomes.
This is not a requirement for gradient descent and the evaluator is not truth by fiat.

A Stockfish `bestmove` cannot satisfy the Laplace final selection obligation merely by existing.

## Frozen-ruler experiment law

One experiment pins:

```text
comparator generation
Laplace world/evidence epoch
Laplace firmware/recipe
opening/challenge suite
Laplace and comparator resource policy
adjudication/tie/randomization law
```

Every Laplace rung is compared against that same ruler. Match/evaluation results do not enter the
same pinned world while the experiment runs; later admission creates a new evidence epoch.

If Stockfish is tuned more aggressively, a new comparator generation is published and the reference
ladder is rerun. Old measurements remain attached to their original ruler.

## Cumulative ladder

A useful initial ladder is:

```text
A0  legal/tactical proposal + material
A1  + classical placement / phase
A2  + bishop pair / rook files / pawn structure
A3  + remaining deterministic structures / motifs / geometry
A4  + learned placement residuals
A5  + learned structural residuals
A6  + global game/trajectory observation
A7  + player/opponent/rating/time conditioning
A8  + opening/shape/tablebase/catalog providers
A9  + grandmaster-book/expert evidence where applicable
A10 + lexical/sense/domain cross-modal providers where applicable
A11 + complete selected #136 Chess Forward Pass
```

The exact sequence is an experiment recipe, not permanent architecture.

Every rung should support, where feasible:

- **adjacent ablation**: `Ai` versus `Ai-1` under matched resources;
- **external ruler**: `Ai` versus the same comparator generation;
- **full-minus-one**: complete program versus complete program with one channel removed.

This distinguishes isolated contribution, cumulative progress, and interactions.

## Match protocol

The acceptance protocol should include:

- exact color-swapped paired opening suite;
- same opening distribution for all variants;
- identical relevant resource budgets across compared Laplace variants;
- sufficient games for the stated statistical conclusion;
- W-D-L, Elo with uncertainty/margin and/or SPRT where appropriate;
- CPU, nodes, memory, I/O, elapsed, provider/crossing metrics;
- failures, crashes, time losses, adjudications, unavailable providers;
- raw game/transcript/config/result artifacts;
- no mid-run score promoted as final evidence.

A playing-strength gain is meaningful only with its cost and configuration boundary.

## Uncertainty-driven compute

Typed uncertainty may guide physical effort:

- exact terminal/tablebase closure can stop speculative search;
- strong low-uncertainty agreement can reduce confirmation work;
- novel/high-RD/contradictory states can receive more work when resources permit;
- exhaustion returns typed partial/upper-bound/why-not state.

Standing never overrides exact legality or terminal constraints.

## Move receipt

A selected move can expose separate contributions:

```text
exact material/tactical state
deterministic structural calculations
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
the calculation recipe/version.

## Long-term hypothesis and falsifiability

The experiment is allowed to show either result.

If the complete Laplace program eventually beats a pinned full-strength Stockfish generation in a
statistically defensible match, that is a measured outcome and may be stated. The design does not
assume it in advance.

If it does not, the ladder identifies where expected gains fail and which typed planes are useful,
neutral, harmful, or too expensive.

The point is not to reproduce Stockfish internally. Laplace is testing whether a persistent typed
cross-modal world can become a stronger decision system while a strong conventional platform stays
available as a ruler, tactical witness, and regression opponent.

## Acceptance / deliberate defects

#136 and #139 own executable acceptance. Required failures include:

- hidden Stockfish bestmove fallback;
- comparator configuration drift between rungs;
- evaluator output promoted to tablebase/world truth;
- match games mutating their own frozen experiment epoch;
- material/rook/pawn Elo constants hardcoded instead of measured;
- one permanent scalar flattening every chess channel;
- substrate used only at the root;
- book/lexical state used only by an explanation UI;
- per-node SQL/SPI search;
- superiority claimed without the pinned full-strength result.

The positive proof keeps Stockfish independent and makes every additional Laplace plane measurable.