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

Resolving candidate language, grammar, usages, senses, referents, discourse bindings,
semantic act and completion obligations is part of the forward cognition program. A
rank-1 topic election, endpoint lookup, fixed read name, regex, highest-frequency sense,
nearest geometric point or highest-standing row cannot replace that work.

The system may retain several competing interpretations, project discriminating
queries, fold evidence, update the joint interpretation state and repeat. Ambiguity is
a valid persistent result when the evidence does not close.

### 3. S3 is structural; the relation/evidence web is semantic

Laplace's real S3/glome placement is structural physicality. It supplies exact
coordinates, centroids/radii, locality and geometric/curve candidate calculations.

The semantic web is the typed relation/evidence world: usages, senses, referents,
propositions, relation laws, testimony, dependence, standing, context, time, discourse,
goals and outcomes.

Structural proximity can propose a candidate. It cannot by itself establish meaning,
truth, relevance, sense or semantic completion.

### 4. `physicality.coord` and `physicality.trajectory` are not the same geometry

This distinction is explicit inventor-direct architecture:

```text
physicality.coord
    = the real four-component structural coordinate

physicality.trajectory
    = an ordered mantissa-packed manifest/address carrier
      containing constituent BLAKE3 identity plus structural metadata
```

Tier-0 `coord` values lie on the unit S3/glome. Higher compositions use the declared
four-dimensional arithmetic centroid of child `coord` values and may lie inside the
glome.

The `trajectory` carrier uses coordinate-shaped binary64 mantissa lanes to bit-pack the
canonical BLAKE3/SIMD identity and metadata for indexing, compact transport and exact
reconstruction. Conceptually `XYZ` provide the hash/address coordinate capacity and
`M` is the metadata-rich lane carrying ordinal, RLE/run and other typed metadata; the
exact generated ABI may use spare payload bits in other lanes as well.

The trajectory's coordinate-looking values are **not** the constituent's real S3
coordinate. A geometric curve must be realized by decoding the constituent IDs and
resolving each constituent's real `physicality.coord` in trajectory order.

Therefore Fréchet/Hausdorff/path geometry over the packed trajectory payload is a type
error even if a geometry library returns a finite number. The operation has measured
BLAKE3/metadata bit layout, not structural shape.

The dedicated architecture record is
`docs/architecture/PHYSICALITY_COORD_TRAJECTORY_REALIZATION.md`.

### 5. Borsuk-Ulam applies to the real S3 projection boundary, not the hash packer

For a continuous map `S3 -> R3` of the real unit `physicality.coord` manifold,
Borsuk-Ulam guarantees global non-injectivity somewhere. A continuous 3D display or
feature projection cannot replace the canonical four-component S3 state.

The packed trajectory hash/address coordinate is a different construction: a discrete,
exact bit encoding of BLAKE3 identity plus metadata. Borsuk-Ulam does not govern its
pack/unpack injectivity because it is not a continuous projection of S3.

This creates two distinct legitimate 3D notions that must never be conflated:

```text
real S3 coord -> continuous 3D view
    structural projection with an explicit loss contract

BLAKE3-128 id -> trajectory XYZ/M mantissa carrier
    exact discrete address/metadata encoding
```

### 6. Angular, Fréchet, Hausdorff, Karcher, Hilbert and packed hash locality remain distinct

No universal structural distance exists.

- angular/geodesic operates on the declared real S3 coordinate;
- Fréchet operates on a realized ordered coordinate curve;
- Hausdorff answers a different set/shape coverage question;
- Karcher-derived values are intrinsic manifold summaries and do not replace the
  canonical arithmetic composition centroid;
- Hilbert is an index/locality projection of real four-dimensional coordinates;
- packed BLAKE3 trajectory locality is address/payload structure, not real S3 locality;
- exact containment/order/ordinal/RLE relations come from decoded physicality and do
  not need approximation by any metric.

### 7. Daisy chains and frog hops are typed execution

Cognition may cross exact structural and semantic state classes while preserving the
meaning of every transition:

```text
exact composition
-> occurrence/container
-> structural role
-> usage
-> candidate sense/referent
-> typed semantic relation/evidence
-> another canonical entity
-> deterministic calculation
-> another physicality/document/game/source
-> derived proposition
```

This is the intended daisy-chain/frog-hop behavior. It is not one flattened graph walk.

### 8. Gödel discovers better ways to think, not only more facts

Successful and failed cognition executions are persistent typed trajectories. The
Gödel engine must be able to compare them, detect recurring useful or defective
procedures, synthesize candidate cognition programs/operators/firmware operations,
search counterexamples, evaluate held-out behavior and activate only proven versions.

Structural comparison can use exact AST/trajectory shape, real S3 angular calculations,
realized Fréchet/Hausdorff, Karcher-derived summaries, Hilbert locality, order/gaps and
recurrence. The packed trajectory remains the exact identity/ordering manifest used to
reconstruct what happened; it does not become the geometric shape metric itself.

Semantic comparison separately retains relation types, senses/referents, discourse,
obligations, evidence roots, contradiction, standing, world/time/context and outcomes.
A recurring structural motif may generate a procedural hypothesis but cannot establish
semantic equivalence.

### 9. Memory, skill, habit and muscle memory are different persistence levels

- **memory**: retained observations, trajectories, programs, outcomes and receipts;
- **skill**: a reusable versioned cognition program/operator with proven semantics;
- **habit**: firmware learns when a proven skill is worth proposing earlier;
- **muscle memory**: a repeatedly proven procedure acquires a semantically equivalent
  fused/indexed/perfcache/native fast path so primitive rediscovery is unnecessary.

Habit changes scheduling, not truth. Muscle memory changes physical execution, not
logical semantics. Every promoted operation retains ancestry, versioning, held-out
acceptance, counterexamples and replay through the primitive path.

### 10. The `How does X work?` family is a proof fixture, not a hardcoded grammar rule

The first execution of `How does lightning work?` may need to inspect the whole
utterance, usages, competing senses/referents and discourse before it can justify a
mechanism-explanation program. Repeated successful and failed examples may later allow
Gödel to discover a reusable mechanism program.

The learned result cannot be an English `starts_with("how does")` rule, a noun bonus,
a rank-1 topic election or any other pre-baked weighting shortcut. It must survive
paraphrase, language differences, ambiguous names and negative contexts where similar
surface forms require a different semantic act.

## Repository projection of these corrections

Current architecture/research records:

- `docs/architecture/STRUCTURAL_GEOMETRY_SEMANTIC_WEB_AND_GODEL_HABITS.md`
- `docs/architecture/PHYSICALITY_COORD_TRAJECTORY_REALIZATION.md`
- `docs/research/STRUCTURAL_TRAJECTORY_METRICS.md`
- `docs/research/BORSUK_ULAM_S3_PROJECTION.md`

Implementation/acceptance issues:

- #168 structural coord/packed-trajectory/metric/provider boundary;
- #169 Gödel procedural memory, habit and muscle-memory compilation;
- #170 whole-observation interpretation before focus.

These are corrections to derived product artifacts, not evidence that the implementation
is complete.
