# Research notes: structural trajectory metrics on Laplace S3

Status: research/implementation input, 2026-09-02. This note supports
`docs/architecture/STRUCTURAL_GEOMETRY_SEMANTIC_WEB_AND_GODEL_HABITS.md`,
`docs/architecture/PHYSICALITY_COORD_TRAJECTORY_REALIZATION.md`, and issue #168. It
does not define semantic similarity and does not supersede product authority.

## Research question

Laplace has multiple coordinate-shaped structural representations that must not be
confused:

```text
physicality.coord
    real four-component structural coordinate

physicality.trajectory
    packed BLAKE3 identity / ordinal / run-RLE / flags / metadata manifest

realized coordinate curve
    child physicality.coord values ordered by the decoded trajectory
```

Which mathematical calculations are useful for comparing the **real structure** while
retaining the exact packed manifest and without turning geometry into a semantic
embedding?

The answer is plural: exact structure, angular/geodesic, Fréchet, Hausdorff,
Karcher-derived, Hilbert/locality and other channels answer different questions and
must remain typed.

## Coordinate-class rule

Before any metric executes, its recipe declares the coordinate class it consumes.

- `physicality.coord` is the real structural coordinate. Tier-0 atoms lie on unit S3;
  higher compositions use the declared arithmetic centroid and can lie inside the
  glome.
- `physicality.trajectory` is not a curve of real positions. Its geometry-compatible
  binary64 lanes are an exact payload/address carrier. The 128-bit constituent
  identity occupies the `X/Y/Z` mantissa capacity, while ordinal/run/flags and other
  metadata occupy the remaining capacity with `M` the metadata-rich lane. Historical
  packing also uses spare `Z` payload bits for flags.
- A realized curve decodes the trajectory and resolves each constituent ID to the
  constituent's real `physicality.coord`, preserving declared order and multiplicity.

A spatial function run directly on packed trajectory `XYZM` can return finite,
repeatable numbers and still be semantically meaningless. Numeric validity is not a
coordinate-type proof.

## S3 point distance

For two unit S3 points under the pinned Laplace geometry recipe, a natural intrinsic
point distance is the great-circle/geodesic angle:

```text
d_S3(p,q) = acos(clamp(dot(p,q), -1, 1))
```

The implementation declares numeric precision, clamping, zero/antipodal behavior, and
units in the recipe.

Laplace S3 coordinates are structural content locations, not automatically rotation
quaternions. The common quaternion equivalence `q ~ -q` must not be introduced unless a
separate physicality type explicitly asks for it.

Borsuk-Ulam constrains continuous lower-dimensional views of this real S3 domain. It
does not constrain the discrete BLAKE3 identity packing in `physicality.trajectory`.

## Fréchet distance

Fréchet distance compares curves while respecting progress along each curve. Alt and
Godau describe continuous polygonal-curve comparison; Eiter and Mannila provide a
discrete variant over sampled sequences.

For a composition physicality the geometric operation is:

```text
packed trajectory
  -> exact decode of constituent IDs + ordinal/run metadata
  -> resolve each ID to real child physicality.coord
  -> preserve order/RLE
  -> realized coordinate curve
  -> Fréchet calculation
```

Laplace use cases include:

- comparing realized constituent trajectories of compositions;
- comparing AST/grammar structural paths after realization;
- comparing game/action/execution trajectories;
- comparing successful and failed cognition traces as a structural discovery channel;
- detecting repeated structural procedures whose exact constituent identities differ.

A Laplace Fréchet recipe states:

- the coordinate class and underlying point metric;
- continuous or discrete algorithm;
- monotone/non-monotone behavior;
- open/closed/subcurve behavior;
- vertex realization/sampling/refinement law;
- numerical precision/tolerance;
- maximum curve cardinality/resource budget;
- exact failure/partial disposition.

Computing Fréchet directly over packed trajectory payload values measures the
BLAKE3/metadata encoding layout, not the shape of the composition.

Reference: Helmut Alt and Michael Godau, *Computing the Fréchet Distance Between Two
Polygonal Curves*, International Journal of Computational Geometry & Applications 5
(1995), 75-91, DOI `10.1142/S0218195995000064`.

Reference: Thomas Eiter and Heikki Mannila, *Computing Discrete Fréchet Distance*,
Technical Report CD-TR 94/64 (1994).

## Hausdorff distance

Hausdorff distance answers a set-coverage question: how far must each point of one set
reach to find the other set? It does not encode the same monotone correspondence/order
constraint as Fréchet.

A useful controlled fixture is:

```text
same realized sampled point set, different traversal order
  -> Hausdorff can remain unchanged
  -> Fréchet can change
```

A substitution mutant must fail.

## Karcher / Fréchet means on a manifold

Karcher means are intrinsic minimizers of an aggregate squared geodesic-distance
objective under declared manifold conditions.

Potential Laplace structural uses include:

- local prototype points for a real-coordinate set;
- dispersion and cluster diagnostics;
- manifold-local candidate generation;
- summarizing repeated **realized** structural execution motifs for Gödel hypothesis
  generation.

They are not the canonical Laplace composite centroid. Canonical composition
physicality uses the declared arithmetic centroid of actual child `coord` values and
retains radius. A Karcher-derived center is an additional calculated view.

On positively curved S3, global uniqueness is not automatic. The recipe needs a
convexity/admissibility region or explicit multiple-minima policy, initialization,
stopping tolerance, iteration/resource ceiling and failure disposition.

Reference: Hermann Karcher, *Riemannian Center of Mass and Mollifier Smoothing*,
Communications on Pure and Applied Mathematics 30 (1977), 509-541, DOI
`10.1002/cpa.3160300502`.

## Hilbert locality

The four-dimensional Hilbert key is an indexing/locality projection over the declared
**real structural coordinates**. It is not semantic distance and it is not the packed
trajectory BLAKE3 hash/address coordinate.

A query can use Hilbert ranges to narrow candidate work, then exact-check the candidate
with the structural operation it actually requires.

## Packed hash/address coordinate

The `physicality.trajectory` carrier deserves its own explicit treatment because it
can look like ordinary geometry to PostgreSQL/PostGIS.

The binary64 mantissa slots provide a compact exact coordinate-shaped representation
for BLAKE3-128 constituent addressing plus metadata. This is useful for:

- constituent GIN/index extraction after typed decode;
- first-child/address probes;
- portable exact trajectory reconstruction;
- run/RLE and ordinal validation;
- fixed-width batch transport;
- future typed payload classes that reuse the carrier ABI.

It is **not** useful as an S3 point metric merely because it has X/Y/Z/M numeric fields.
If a future recipe intentionally defines a metric over hash-address space, that would
be a separately named discrete address metric and could not be called S3, Fréchet,
Hausdorff or semantic similarity by implication.

## Exact physicality structure remains first-class

Metric geometry is unnecessary for structural facts already calculated exactly from
physicality/trajectory:

- identity and constituent identity;
- containment and ancestry;
- ordinal and gap;
- multiplicity/run;
- precedes/follows;
- exact recurrence;
- structural altitude/tier;
- exact trajectory reconstruction.

These facts should not be approximated by distance calculations.

## Structural metric outputs are not semantics

The semantic web remains separate typed state. Two structurally similar realized
curves can mean different things, and semantically related expressions can have very
different exact structures across languages, modalities, or grammars.

Required negative controls:

1. **packed versus realized** — packed-payload Fréchet differs from realized-curve
   Fréchet and only the declared realized calculation may claim physical shape;
2. **structurally close, semantically incompatible** — close real S3/Fréchet shape but
   conflicting relation/evidence/context state;
3. **structurally distant, semantically related** — different language/grammar or
   modality realizes an equivalent semantic act through witnessed relations;
4. **same set, different order** — Hausdorff near/equal while Fréchet differs;
5. **same arithmetic centroid, different Karcher view** and the converse;
6. **Hilbert-local but angularly/geodesically non-nearest** candidate ordering;
7. **antipodal S3 locations** remain distinct structural positions;
8. **hash-address locality versus S3 locality** — similar packed ID lanes do not imply
   nearby real structural coordinates or semantics.

## Gödel-engine consequence

Structural metrics can generate hypotheses for repeated procedures. A family of
successful cognition traces may have comparable realized curve shape or exact AST
motifs even when concrete content differs. Gödel may use that to propose a reusable
cognition program.

The packed identity trajectory remains useful for exact addressing and reconstructing
the trace, while the realized curve supplies geometric shape. Promotion still requires
semantic/outcome evidence:

```text
exact packed manifest
  -> reconstruct what happened

realized structural similarity
  -> candidate motif

semantic/outcome evidence
  -> determine whether the motif is a reusable cognition program
```

No stage may be silently substituted for another.

## Implementation research questions

- Which Fréchet variants are required first for realized physicality curves: discrete,
  continuous polygonal, subcurve, closed-curve?
- Which trajectory vertex classes can be decoded and resolved to real S3/centroid
  coordinates without database crossings when perfcache coverage is complete?
- Can safe Hilbert candidate bands reduce realized Fréchet/Hausdorff candidate pairs
  without changing recall under a declared completeness bound?
- Which Karcher solver and convergence certificate are appropriate for local S3 point
  sets under the deterministic numeric contract?
- Which exact trajectory fields remain deliberately redundant for validation/indexing,
  including packed ordinal versus vertex sequence position?
- Which metric calculations merit dedicated vector-first ISA operations versus generic
  typed providers?
- Which structural summaries help #169 procedural-memory discovery without creating a
  stored universal embedding?

Each answer lands as a versioned recipe/provider/acceptance contract rather than a
hidden mathematical convention.
