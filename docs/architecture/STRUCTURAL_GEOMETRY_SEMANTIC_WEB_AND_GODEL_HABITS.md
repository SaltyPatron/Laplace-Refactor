# Structural geometry, packed trajectory manifests, semantic web, interpretation, and Gödel habits

Status: inventor-direct architecture clarification, 2026-09-02. This document records
required product behavior; it is not evidence that the implementation exists.

## 1. The correction

Laplace must not begin a request by electing one token, noun, topic, or referent and
then biasing the rest of execution toward it. Doing so hardcodes the conclusion that
cognition is supposed to calculate.

For an utterance such as:

```text
How does lightning work?
```

`lightning` is not entitled to be the first focus. At entry, Laplace has an exact
observed composition and persistent context. It must recover and calculate the
structural evidence of the whole observation, inspect usages and enclosing contexts,
retain competing senses and referents, infer candidate interpretations jointly, and
only then bind operands and semantic obligations when the evidence justifies them.

The same rule applies to every modality. An endpoint, visually central node, noun,
high-degree entity, nearest geometric candidate, highest Glicko standing, most frequent
constituent, or caller-selected topic cannot impersonate interpretation.

## 2. `coord`, `trajectory`, and realized curve are three different structural objects

The critical storage/type distinction is:

```text
physicality.coord
    = the real four-component structural placement

physicality.trajectory
    = an ordered exact mantissa-packed manifest/address carrier
      containing canonical constituent identity plus metadata

realized coordinate curve
    = the ordered curve formed by decoding trajectory constituent IDs
      and resolving each constituent's real physicality.coord
```

They may use geometry-compatible storage, but they do not share geometric meaning.

### `physicality.coord`

This is the real structural coordinate. Tier-0 atoms are placed on the pinned unit
`S3`/glome. Higher-tier compositions use the declared four-dimensional arithmetic
centroid of their actual child coordinates and retain radius, so composite centroids
may lie inside the glome rather than on the unit S3 boundary.

Angular/geodesic calculations and Hilbert locality operate on the declared real
coordinate, not on the packed trajectory payload.

### `physicality.trajectory`

This is the ordered, exactly invertible packed manifest. For composition physicalities,
its coordinate-shaped binary64 lanes carry the constituent's canonical BLAKE3-128
identity plus ordinal, run/RLE, flags and other typed metadata. Conceptually the
`X/Y/Z` mantissa capacity is a hash/address coordinate used for compact transport and
indexing, while `M` is the metadata-rich lane. The exact historical/current packing
also uses spare `Z` payload bits for some flags; generated ABI is authoritative for the
precise bit allocation.

A trajectory vertex is therefore useful as an address/index coordinate, but it is not
the constituent's S3 coordinate. Running spatial math directly on packed `XYZM` values
measures payload bits, not structural shape.

### Realized curve

When a calculation needs actual geometric trajectory shape, the engine decodes the
trajectory identities and metadata, resolves each constituent's real `coord`, preserves
order and multiplicity/RLE, and constructs the realized coordinate curve. Fréchet,
Hausdorff and other path/shape calculations consume that realized curve when their
recipe requires real structural geometry.

The dedicated architecture record is
`docs/architecture/PHYSICALITY_COORD_TRAJECTORY_REALIZATION.md`.

## 3. S3 is structural; the web is semantic

Laplace's real S3/glome coordinate geometry belongs to **physicality and structure**.
The packed trajectory manifest also belongs to structure, but is a typed payload rather
than live geometry.

The semantic web is the typed world of witnessed/calculated relations, usages,
occurrences, referents, senses, propositions, evidence, dependence, standing, context,
time, discourse, programs, and receipts. Semantic connection is established through
those typed laws and observations, not by declaring geometrically nearby content or
hash-address-local content to mean the same thing.

Therefore:

```text
real coord / centroid / radius / Hilbert
packed identity trajectory / ordinal / RLE / flags
realized coordinate curves / structural metrics
    = structural state and structural candidate calculations

semantic relation/evidence web
    = meanings, claims, usages, causes, roles, references, standing, context
```

They cooperate in cognition, but neither may replace the other.

## 4. Structural metric families remain distinct

No single `distance`, `neighbor`, or scalar affinity is canonical.

### Exact structural relations

Merkle-DAG identity, composition, containment, ancestry, role, tier/altitude,
constituent identity, ordinal, multiplicity, gap, recurrence, precedes/follows, and
source/enclosing occurrence are exact structural calculations under the selected
physicality recipe.

### S3 angular/geodesic distance

For unit Tier-0 points `p,q` on the glome, the declared angular/geodesic calculation may
use an `acos(clamp(dot(p,q),-1,1))` form where that is the pinned recipe.

Laplace S3 points are content-structure locations, not orientation quaternions. An
implementation must not silently apply quaternion antipodal equivalence (`q == -q`) or
`abs(dot)` unless a separately declared physicality type explicitly requires it.

Angular proximity is structural proximity only. It is never semantic equivalence,
truth, relevance, or evidence.

### Borsuk-Ulam projection boundary

For a continuous map of the actual unit structural manifold

```text
f : S3 -> R3
```

Borsuk-Ulam guarantees that some antipodal pair shares a projected value. A continuous
three-dimensional view of the real S3 coordinate therefore cannot be globally
injective.

This theorem does **not** apply to the trajectory's BLAKE3 mantissa packing as though
that were a continuous `S3 -> R3` projection. The hash/address coordinate is a discrete
exact payload encoding whose contract is pack/unpack correctness. The two different
3D notions must remain typed separately.

### Radius

Composite radius and other declared radial quantities remain distinct from centroid,
angle, curve shape, identity, and semantic standing.

### Hilbert locality

The 128-bit Hilbert projection is an indexed locality/addressing aid over the declared
real four-dimensional structural coordinates. It is not semantic distance and is not
the trajectory's packed BLAKE3 hash/address coordinate.

### Fréchet trajectory distance

Fréchet is a structural curve/trajectory-shape comparison preserving progress/order.
For Laplace it runs on a realized coordinate curve when the recipe asks for physical
shape. Packed identity vertices must first be decoded and resolved to live coordinates.

The base point metric, continuous versus discrete variant, monotonicity, open/closed
curve treatment, sampling/refinement law, numerical bound, and resource limit are part
of the recipe and receipt.

### Hausdorff distance

Hausdorff compares point/set coverage and does not preserve trajectory order in the way
Fréchet does. A pair may therefore be close under Hausdorff and far under Fréchet, or
vice versa; that divergence is useful information and must not be normalized away.

### Karcher-derived manifold calculations

A Karcher/Fréchet mean is an intrinsic Riemannian center calculated from geodesic
structure. It can be useful for local manifold summaries, prototypes, dispersion,
cluster diagnostics, and candidate generation.

It is **not** Laplace's canonical composite physicality centroid. The canonical
composite centroid remains the declared arithmetic centroid of actual child
physicalities. Karcher-derived values are additional calculated views.

Because S3 has positive curvature, existence/uniqueness and convergence assumptions
matter. The calculation must declare its admissible region, initialization, tolerance,
iteration/resource bound, multiple-solution/tie behavior, and failure disposition.

### Other declared structural metrics

Additional point, curve, set, manifold, topological, spectral, gap, recurrence, shape,
or trajectory calculations can be added as typed operations. Their storage types or
numeric compatibility never allow one metric to impersonate another.

## 5. Interpretation precedes focus

The first cognition problem is not `find the topic`. It is to explain the observed
request well enough to compile a justified program.

For text, the initial state can include all exact and calculated structure available
from the observation:

```text
codepoints
-> graphemes / other selected segmentation units
-> canonical word/form candidates
-> phrase/clause/sentence/utterance compositions
-> ordinals, gaps, punctuation and whitespace
-> enclosing discourse/session trajectories
-> observed usage containers and continuations
-> candidate senses/referents/relations
```

All constituents remain eligible until the current program proves that some are
irrelevant to an obligation. There is no global noun-over-function-word,
content-word-over-punctuation, or topic-token priority.

Interpretation is a joint constraint problem. Candidate meanings of `work` constrain
candidate meanings of `lightning`; the construction around `how does ... work`
constrains both; current discourse, source, language, prior observations, semantic
relations, and counterexamples constrain the whole state.

A valid implementation may retain competing interpretation states such as:

```text
H1: mechanism request about atmospheric lightning
H2: mechanism request about a product/network/entity named Lightning
H3: labor/performance reading involving an entity named Lightning
H4: unresolved/ambiguous
```

Those are hypotheses, not answers. The engine projects queries that discriminate among
them, folds the resulting evidence, and updates the interpretation state. Only after
sufficient convergence may an operand become privileged.

## 6. Daisy chains and frog hops are typed execution, not raw graph hops

Laplace cognition may move among different structural and semantic state classes while
preserving exact identity and receipts:

```text
entity
-> occurrence/container
-> structural role
-> sense/referent candidate
-> witnessed semantic relation
-> another canonical entity
-> calculated physicality/trajectory
-> another relation/provider family
-> derived proposition
```

A useful route may daisy-chain compatible operations and frog-hop across observations,
lexical sources, documents, games, calculations, geometry, relation families, and
evidence providers. This is not permission to flatten them into one adjacency graph.
Every transition retains its type, direction, context, evidence, physicality recipe,
and calculation receipt.

## 7. Gödel learns procedures, not only facts

The Gödel engine is required to discover persistent improvements to **how Laplace
calculates**, not merely additions to what Laplace knows.

Repeated successful and failed cognition trajectories are persistent observations.
Gödel can compare them structurally and semantically while retaining the separation
between those channels.

Structural comparisons can include exact trajectory/AST motifs, ordinals and gaps,
real S3 angle, realized-curve Fréchet, Hausdorff, Karcher-derived local summaries,
Hilbert locality, recurrence, containment, and other declared physicality calculations.
The packed BLAKE3 trajectory coordinate can participate in exact address/index work but
must not masquerade as a geometric shape metric.

Semantic comparisons can include typed relation motifs, obligations satisfied or left
open, evidence roots, contradictions, referential bindings, world/time/context,
standing, semantic acts, outcomes, and counterexamples.

A recurring structural shape is not automatically a recurring semantic procedure.
Gödel must show that the proposed abstraction improves held-out prediction,
completion, cost, or outcomes under the declared semantic program.

## 8. Persistence ladder: memory, skill, habit, muscle memory

The required progression is approximately:

```text
raw observation / execution receipt
        ↓
repeated successful or failed trajectory family
        ↓
structural + semantic motif hypothesis
        ↓
candidate reusable cognition program / operator / relation law
        ↓
fit on allowed evidence
        ↓
held-out evaluation + counterexample search
        ↓
versioned activation
        ↓
reusable recipe / ISA instruction / firmware operation
        ↓
optional measured index, perfcache, compiled native fast path
```

These levels are distinct:

- **memory**: retained observations, trajectories, programs, outcomes, and receipts;
- **skill**: a reusable versioned program that performs a proven operation;
- **habit**: firmware learns when a proven operation is a useful candidate to schedule;
- **muscle memory**: a repeatedly proven procedure is compiled or accelerated so the
  same logical computation no longer has to be rediscovered from primitive steps on
  every execution.

Habit is not a hardcoded regex or permanent bias. Muscle memory is not opacity.
Every promoted operation retains ancestry to the experiments that justified it, can be
replayed under its original calculus, can be compared with the primitive route, and can
be retired or superseded by later evidence.

## 9. Example: learning `How does X work?`

The first executions may need to calculate the interpretation from primitive
observations:

```text
observe exact utterance
-> inspect structural usages of all constituents
-> retain competing interpretations
-> query semantic/document/discourse evidence
-> fold evidence
-> bind a mechanism-request program when justified
-> resolve prerequisites, transitions and effects
-> complete a causal/process semantic act
-> realize requested language
-> witness outcome and receipt
```

After enough independent successful and failed examples, Gödel may hypothesize a
reusable program family whose obligations often resemble:

```text
resolve the requested phenomenon/system under context
resolve prerequisites / initiating conditions
resolve internal transitions / mechanism
resolve effects / terminal consequences
check causal/process support and contradictions
produce a semantically complete explanation act
```

The learned operation must not be `if text starts with "how does" then EXPLAIN`.
It must be a versioned program induced from witnessed structure and outcomes that can
also survive paraphrase, language change, different structural altitudes, and negative
controls where the same surface fragments mean something else.

## 10. Promotion and activation law

A candidate procedure may become persistent only when its receipt names at least:

- generating trajectory family and root observations;
- structural metric recipes used in discovery;
- semantic/evidence provider families used in discovery;
- fitting, evaluation and counterexample boundaries;
- dependence roots and self-ancestry exclusions;
- program/operation signature and typed operands;
- preconditions and completion obligations;
- expected effect on guidance state;
- complexity and resource cost;
- held-out completion/prediction/outcome improvement;
- semantic parity with the primitive route where the new form is an optimization;
- deliberate defects and counterexamples;
- activation authority, calculus version, firmware version and retirement law.

Self-generated descendants cannot independently certify the program that generated
them. A fast path that changes semantics is a new candidate semantic program, not an
optimization.

## 11. Required implementation consequences

1. Conversation/query compilation retains the whole unresolved interpretation state
   before any focus/referent becomes privileged.
2. `physicality.coord`, packed `physicality.trajectory`, and realized coordinate curves
   have separate operation types and receipts.
3. Physicality/occurrence providers expose exact packed trajectory decoding and real
   structural metric families without confusing the two.
4. Semantic providers expose typed relation/evidence web state separately from
   structural geometry.
5. Search/guidance state retains structural and semantic channels without flattening
   either into a universal scalar.
6. Execution receipts persist successful and failed trajectories for Gödel comparison
   and replay.
7. Gödel may propose cognition programs, operators, relation laws, firmware operations,
   and acceleration artifacts from those trajectories.
8. Activated discovered programs register through the same recipe/ISA/framework
   lifecycle as human-supplied programs.
9. Perfcaches/indexes/native fast paths implement muscle memory only after semantic
   parity and measured benefit are proven.
10. The public product exposes why a learned operation was selected and allows
    drill-through to primitive evidence/trajectory ancestry.

## 12. Deliberate defects

Acceptance must reject at least:

- electing a rank-1 topic before joint interpretation;
- noun/content-word/global-frequency weighting as an interpretation shortcut;
- hardcoded English regex dispatch presented as learned cognition;
- interpreting packed trajectory XYZM as live S3 geometry;
- Fréchet calculated directly over packed identity bytes while claiming shape distance;
- S3 angular proximity treated as semantic similarity or truth;
- Hilbert locality or BLAKE3 hash-address locality treated as semantic distance;
- applying Borsuk-Ulam to the discrete trajectory packer rather than the real S3 view;
- Hausdorff substituted for Fréchet while claiming order-sensitive parity;
- Karcher mean substituted for the canonical arithmetic composition centroid;
- one universal scalar combining structural and semantic channels;
- structural motif recurrence promoted to semantic law without held-out outcomes;
- self-generated descendants counted as independent support;
- learned habit becoming mandatory dispatch despite contradictory current context;
- compiled muscle-memory path changing semantic result while claiming optimization;
- activation without versioned calculus/firmware/program ancestry and replay.

## 13. Research anchors

The external mathematics is used as vocabulary and algorithmic basis for declared
Laplace structural operations, not as semantic authority.

- Karol Borsuk, *Drei Sätze über die n-dimensionale euklidische Sphäre*, Fundamenta
  Mathematicae 20 (1933), 177-190.
- Helmut Alt and Michael Godau, *Computing the Fréchet Distance Between Two Polygonal
  Curves*, International Journal of Computational Geometry & Applications 5 (1995),
  75-91, DOI `10.1142/S0218195995000064`.
- Thomas Eiter and Heikki Mannila, *Computing Discrete Fréchet Distance*, Technical
  Report CD-TR 94/64 (1994).
- Hermann Karcher, *Riemannian Center of Mass and Mollifier Smoothing*, Communications
  on Pure and Applied Mathematics 30 (1977), 509-541, DOI
  `10.1002/cpa.3160300502`.

These references justify keeping the metric/projection families mathematically
distinct. Laplace's typed storage and semantic contracts decide which one is legal for
a given operation.
