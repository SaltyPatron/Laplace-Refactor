# Research notes: structural trajectory metrics on Laplace S3

Status: research/implementation input, 2026-09-02. This note supports
`docs/architecture/STRUCTURAL_GEOMETRY_SEMANTIC_WEB_AND_GODEL_HABITS.md` and issue
#168. It does not define semantic similarity and does not supersede product authority.

## Research question

Laplace physicality places exact content on a versioned S3/glome structural geometry
and realizes compositions as ordered constituent trajectories. Which mathematical
calculations are useful for comparing that **structure** without turning geometry into
a semantic embedding?

The required answer is plural: angular/geodesic, Fréchet, Hausdorff, Karcher-derived,
Hilbert/locality, exact order/gap/containment and other channels answer different
questions and must remain typed.

## S3 point distance

For two unit S3 points under the pinned Laplace geometry recipe, a natural intrinsic
point distance is the great-circle/geodesic angle:

```text
d_S3(p,q) = acos(clamp(dot(p,q), -1, 1))
```

The implementation must define numeric precision, clamping, zero/antipodal behavior,
and units in the recipe.

Important Laplace-specific caveat: S3 coordinates here are structural content
locations. They are not automatically rotation quaternions, so the common quaternion
orientation equivalence `q ~ -q` must not be introduced. Using `abs(dot(p,q))` would
fold antipodal structural locations together and change Laplace geometry unless an
explicit separate physicality type asks for that behavior.

## Fréchet distance

Fréchet distance compares curves while respecting progress along each curve. Alt and
Godau describe it as a measure of resemblance between curves compatible with their
parametrizations; Eiter and Mannila provide a discrete variant over sampled sequences.

Laplace use cases include:

- comparing realized constituent trajectories of compositions;
- comparing AST/grammar structural paths after realization;
- comparing game/action/execution trajectories;
- comparing successful and failed cognition traces as a structural discovery channel;
- detecting repeated structural procedures whose exact constituent identities differ.

A Laplace Fréchet recipe must state:

- the underlying point metric, normally the declared S3 geodesic metric for S3 curves;
- continuous or discrete algorithm;
- monotone/non-monotone behavior;
- open/closed/subcurve behavior;
- vertex realization/sampling/refinement law;
- numerical precision/tolerance;
- maximum curve cardinality/resource budget;
- exact failure/partial disposition.

Packed physicality trajectory vertices can carry constituent IDs and metadata. Those
bytes are not coordinates. A geometry-dependent Fréchet calculation first resolves the
IDs through the pinned geometry generation to a realized coordinate curve.

Reference: Helmut Alt and Michael Godau, *Computing the Fréchet Distance Between Two
Polygonal Curves*, International Journal of Computational Geometry & Applications 5
(1995), 75-91, DOI `10.1142/S0218195995000064`.

Reference: Thomas Eiter and Heikki Mannila, *Computing Discrete Fréchet Distance*,
Technical Report CD-TR 94/64 (1994).

## Hausdorff distance

Hausdorff distance answers a set-coverage question: how far must each point of one set
reach to find the other set? It does not encode the same monotone correspondence/order
constraint as Fréchet.

That distinction is useful for Laplace:

```text
same sampled point set, different traversal order
  -> Hausdorff can remain unchanged
  -> Fréchet can change
```

A deliberate acceptance fixture should construct exactly that disagreement. An
implementation that substitutes Hausdorff for Fréchet because both return a distance
must fail.

## Karcher / Fréchet means on a manifold

Karcher developed a Riemannian center-of-mass construction. In modern terminology,
Fréchet/Karcher means are intrinsic minimizers of an aggregate squared geodesic-distance
objective under the declared manifold conditions.

Potential Laplace structural uses include:

- local prototype points for a set of related structural trajectories/vertices;
- dispersion and cluster diagnostics;
- manifold-local candidate generation;
- summarizing repeated structural execution motifs for Gödel hypothesis generation.

They are not the canonical Laplace composite centroid. Laplace's canonical composition
physicality uses the declared arithmetic centroid of actual child points and retains
radius. A Karcher-derived center is an additional calculated view.

On positively curved S3, global uniqueness is not automatic for arbitrary point sets.
The recipe therefore needs an admissible/convexity region or another exact uniqueness
policy, initialization, stopping tolerance, iteration/resource ceiling, multiple-minima
handling and failure disposition.

Reference: Hermann Karcher, *Riemannian Center of Mass and Mollifier Smoothing*,
Communications on Pure and Applied Mathematics 30 (1977), 509-541, DOI
`10.1002/cpa.3160300502`.

## Hilbert locality

The four-dimensional Hilbert key is an indexing/locality projection over the declared
structural coordinates. Its purpose is candidate discovery and ordered locality, not
semantic distance and not a substitute for exact S3 geometry.

A query can use Hilbert ranges to narrow candidate work, then exact-check the candidate
with the structural operation it actually requires. A Hilbert miss only proves absence
when the exact index boundary has a completeness contract for that operation.

## Exact physicality structure remains first-class

Metric geometry is not required for structural facts already calculated exactly from
physicality:

- identity and constituent identity;
- containment and ancestry;
- ordinal and gap;
- multiplicity/run;
- precedes/follows;
- exact recurrence;
- structural altitude/tier;
- exact trajectory reconstruction.

These exact facts should not be approximated by a distance calculation merely because
a distance operator is available.

## Structural metric outputs are not semantics

The semantic web remains separate typed state. Two structurally similar trajectories
can mean different things, and two semantically equivalent/related expressions can
have very different exact structural trajectories across languages, modalities, or
grammars.

Required negative controls:

1. **structurally close, semantically incompatible** — close S3/Fréchet shape but
   conflicting relation/evidence/context state;
2. **structurally distant, semantically related** — different language/grammar or
   modality realizes an equivalent semantic act through witnessed relations;
3. **same set, different order** — Hausdorff near/equal while Fréchet differs;
4. **same arithmetic centroid, different Karcher view** and the converse;
5. **Hilbert-local but angularly/geodesically non-nearest** candidate ordering;
6. **antipodal S3 locations** remain distinct structural positions.

## Gödel-engine consequence

Structural metrics are particularly useful as **hypothesis generators** for repeated
procedures. A family of successful cognition traces may have comparable structural
shape under Fréchet or exact AST motifs even when the concrete content differs.
Gödel may use that to propose a reusable cognition program.

Promotion still requires semantic/outcome evidence:

```text
structural similarity
  -> candidate motif
  != semantic equivalence
  != instruction activation
```

The candidate must demonstrate held-out completion/prediction/outcome improvement,
counterexample survival, dependence-aware evidence, declared complexity/resource cost,
and explicit activation. This is the boundary between structural S3 and semantic web.

## Implementation research questions

- Which Fréchet variants are required first for exact physicality trajectories: discrete
  sampled, continuous polygonal, subcurve, closed-curve?
- Which trajectory vertex classes can be realized to S3 coordinates without database
  crossings when the active perfcache generation covers them?
- Can safe Hilbert candidate bands reduce Fréchet/Hausdorff candidate pairs before
  exact calculation without changing recall under a declared completeness bound?
- Which Karcher solver and convergence certificate are appropriate for local S3 point
  sets under the product's deterministic numeric contract?
- Which metric calculations merit dedicated vector-first ISA operations versus generic
  typed metric-provider calls?
- Which structural trajectory summaries are useful to #169 Gödel procedural-memory
  discovery without creating another stored universal embedding?
- What representative cross-language, chess/game, document/AST, and cognition-trace
  fixtures produce deliberately divergent metric rankings?

Each answer must land as a versioned recipe/provider/acceptance contract rather than a
hidden mathematical convention.
