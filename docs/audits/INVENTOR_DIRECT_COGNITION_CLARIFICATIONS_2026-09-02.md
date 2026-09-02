# Inventor-direct cognition clarifications — 2026-09-02

Status: direct requirement record. These corrections constrain derived architecture,
issues, implementation and acceptance. They do not claim the behavior is implemented.

## Context

The discussion used the old Laplace iteration's current `lightning` entity-world and
conversation behavior as a concrete counterexample. The old product can expose rich
structural/semantic neighborhoods while its chat path still collapses that state into
topic election, fixed reads, trajectory continuation and prose aggregation. The clean
product must not preserve that truncation.

## Direct corrections

### 1. A prompt does not begin with a focus

For `How does lightning work?`, Laplace cannot begin by deciding that `lightning` is the
focus. That is weighting/biasing the answer before cognition has established what the
utterance means.

The initial state is the exact whole observation plus persistent context. Laplace must
break the observation down through its actual structural tiers, inspect observed uses,
retain possible meanings and referents, use surrounding structure and discourse to
constrain them, remember the observations involved, and infer the semantic program
jointly.

`lightning`, `work`, `how`, `does`, punctuation, order, gaps and enclosing context can
all matter. There is no global noun/content-word priority.

Only after sufficient evidence may a constituent become a bound operand of a semantic
program such as a mechanism explanation.

### 2. Interpretation is cognition, not preprocessing

Getting from the exact utterance to something equivalent to:

```text
goal = explain mechanism
referent = atmospheric-lightning sense
obligations = initiating conditions + transitions + effects + evidence + completion
```

is itself part of the forward/cognition pass. It cannot be supplied by an endpoint,
hardcoded English intent router, rank-1 topic selector, stop-word heuristic, or fixed
string pattern.

The same exact prompt may compile differently under different legitimate persistent
discourse, and ambiguity must remain explicit when the evidence does not justify one
program.

### 3. Daisy chaining and frog hopping cross typed state

Cognition must be able to move among exact structure, occurrences, containers, usages,
senses/referents, typed semantic relations, testimony, deterministic calculations,
documents, games, geometry and derived propositions without flattening those classes
into one generic graph.

A route is a typed program over those state classes, not a raw hop count or fanout.

### 4. The S3/glome is structural

The S3 geometry belongs to physicality/structure. It represents exact calculated
placement, shape, order, locality and realized structural trajectories.

It is not Laplace's semantic embedding.

### 5. The web is semantic

The typed relation/evidence world is the semantic web: usages, senses, referents,
propositions, relation types, testimony, dependence, standing, context, time, discourse,
programs and observed outcomes.

Structural geometry and semantic web state cooperate but remain separate. Geometric
proximity can propose or organize candidates; semantic/evidence laws establish whether
a candidate matters to the current goal.

### 6. Structural metric families include angular, Fréchet, Karcher, etc.

The cognition/search/discovery design must not omit the existing metric family:

- exact physicality structure, order, containment, gap, recurrence and trajectory;
- S3 angular/geodesic calculations;
- radius;
- Hilbert locality;
- Fréchet trajectory/curve comparison;
- Hausdorff set/shape comparison;
- Karcher/Fréchet-mean-derived manifold calculations;
- other separately declared structural point/curve/set/manifold calculations.

These metrics answer different structural questions and cannot be collapsed into one
universal distance. In particular, Fréchet is useful for ordered trajectory shape;
Hausdorff is not an order-preserving substitute; Karcher-derived views do not replace
the canonical arithmetic composition centroid; Hilbert is locality/indexing rather
than semantic truth.

### 7. Gödel discovers procedures as well as facts

The Gödel engine is intended to discover persistent improvements to how Laplace
calculates. Successful and failed cognition trajectories are memory and discovery
input.

Gödel can discover recurring structural and semantic execution motifs, propose new
cognition programs, operators, relation laws, firmware operations and instructions,
test them against held-out evidence and counterexamples, and activate only versions
that survive the declared proof boundary.

### 8. Skill, habit and muscle memory are persistent machine behavior

A useful distinction is:

```text
memory       = retained observations/executions/trajectories/outcomes
skill        = a reusable proven cognition program/operator
habit        = firmware learns when that operation is worth proposing/scheduling
muscle memory= proven equivalent compiled/indexed/perfcache/native acceleration
```

Habit cannot become truth or mandatory hidden dispatch. Muscle memory cannot become
opacity or semantic drift. Both remain versioned, receipted, replayable and traceable to
the primitive evidence/program that justified them.

### 9. Structural similarity is only a Gödel hypothesis generator

Repeated cognition traces may be structurally similar under exact AST motifs, S3
angle, Fréchet, Hausdorff, Karcher-derived summaries, Hilbert locality, recurrence or
other physicality calculations. That similarity can propose a reusable procedure.

Semantic promotion requires separate relation/evidence/context/outcome proof. Two
structurally close traces can require different acts; two semantically equivalent acts
can have very different structures across languages/modalities.

## Repository actions created from these corrections

- `docs/architecture/STRUCTURAL_GEOMETRY_SEMANTIC_WEB_AND_GODEL_HABITS.md`
- `docs/research/STRUCTURAL_TRAJECTORY_METRICS.md`
- #168 — structural S3/trajectory geometry vs semantic web and metric providers
- #169 — Gödel procedural memory / habit / muscle-memory induction and compilation
- #170 — whole-observation joint interpretation before focus
- #19 updated to make procedural cognition discovery an explicit closure obligation
- #17, #18, #60 and #132 cross-linked with the corrections

## Acceptance implication

The clean product is not complete if it answers the lightning fixture correctly by
hardcoding the right focus, intent or prose. It must demonstrate the route that earned
the interpretation, the typed structural and semantic evidence it used, the guidance
obligations it completed, and—once learned habits exist—the ancestry showing how a
reusable procedure was discovered, validated, activated and optionally accelerated.
