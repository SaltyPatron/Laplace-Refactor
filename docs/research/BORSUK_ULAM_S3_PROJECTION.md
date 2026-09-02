# Borsuk-Ulam consequences for Laplace S3 structural projections

Status: research/implementation input, 2026-09-02. This note supplements
`STRUCTURAL_TRAJECTORY_METRICS.md`, `PHYSICALITY_COORD_TRAJECTORY_REALIZATION.md`, the
glome geometry acceptance, and issue #168. It constrains the real `S3` coordinate
projection boundary; it does not define semantic equivalence and it does not describe
the trajectory's discrete BLAKE3 payload packing.

## The theorem applies to the real `physicality.coord` S3 domain

For every continuous map

```text
f : S^n -> R^n
```

Borsuk-Ulam guarantees at least one antipodal pair `x` and `-x` such that

```text
f(x) = f(-x).
```

Laplace Tier-0 atom `physicality.coord` values are real four-component structural
points on the pinned unit `S3` / glome. Therefore any declared continuous view

```text
f : S3 -> R3
```

is necessarily non-injective somewhere on the full manifold. At least one antipodal
pair is collapsed by that continuous three-component view. Continuous maps into lower
Euclidean dimensions inherit the same non-injectivity consequence.

This is a mathematical reason that a continuous 3D/2D view cannot replace the real
four-component `physicality.coord` on the complete S3 structural domain.

Higher-tier composition centroids need an additional qualification: the canonical
four-component arithmetic centroid of child `coord` values may lie inside the glome
rather than on unit S3. Borsuk-Ulam is therefore invoked for the actual S3-valued
boundary/domain of the selected physicality recipe, not indiscriminately for every
four-component centroid stored by Laplace.

## `physicality.trajectory` is a different object

`physicality.trajectory` is not the lower-dimensional projection governed by the
statement above. It is an ordered, exact, mantissa-packed payload carrier.

For composition physicalities its vertices encode a canonical BLAKE3-128 constituent
identity plus ordinal/run/RLE/flags and other typed metadata. The coordinate-shaped
`X/Y/Z/M` storage is exploited as an address/index carrier. Conceptually the `XYZ`
mantissa capacity forms a hash/address coordinate, with `M` the metadata-rich lane;
the historical/current exact layout also uses spare `Z` payload bits for metadata.

That mapping is discrete and its correctness contract is exact pack/unpack. It is not a
continuous map from the real S3 coordinate of the constituent into R3. Borsuk-Ulam does
not imply a collision in this hash payload representation.

The two operations must not be conflated:

```text
real physicality.coord on S3 -> continuous R3 display/feature projection
    Borsuk-Ulam constrains global injectivity

BLAKE3-128 constituent id -> trajectory mantissa XYZ/M payload
    discrete exact address/metadata encoding
```

A trajectory vertex can look like a 3D/4D coordinate to a geometry library while
remaining semantically non-spatial. Applying a geometric metric directly to those
payload numbers measures packed bits, not Laplace structural shape.

## This does not make antipodes canonically identical

Borsuk-Ulam says that a lower-dimensional continuous map must identify some antipodal
pair. It does **not** say that canonical S3 points `x` and `-x` are equal content,
equal physicality, or semantically equivalent.

Accordingly, the prohibition on silently applying quaternion orientation semantics
(`q ~ -q` or `abs(dot)`) remains valid. Laplace S3 is a structural content geometry,
not automatically the projective rotation space `SO(3)`.

## Hopf projection is a separate many-to-one structural view

The standard Hopf map

```text
h : S3 -> S2
```

has `S1` fibers. The S2 base therefore intentionally loses distinctions present on S3,
including antipodal pairs sharing the same base location.

Consequences:

- Hopf base coordinates cannot replace `physicality.coord`;
- equal Hopf base location does not imply equal S3 point, constituent, trajectory, or
  semantic state;
- fiber phase/state is required when a calculation needs to distinguish points in the
  same fiber;
- a base-only visualization carries an explicit many-to-one loss receipt.

## Finite Unicode placement versus the continuous theorem

Borsuk-Ulam is about the complete continuous sphere. Laplace Tier-0 Unicode placement
is a finite sampled population on S3. A particular continuous projection may happen to
be injective on that finite sample while remaining globally non-injective.

Therefore acceptance must not falsely claim:

```text
Borsuk-Ulam => the current 1,114,112 sampled atom points necessarily contain a
projected antipodal collision.
```

The correct architectural claim is:

```text
no continuous R3 representation has a global injectivity contract over the complete
canonical S3 domain.
```

A finite no-collision observation is a property of the selected sample/projection
epoch, not a proof that the projection can replace S3.

## Relation to angular, Fréchet, Hausdorff, Karcher and Hilbert calculations

Borsuk-Ulam is a representation/projection constraint, not another distance metric.

- **Angular/geodesic** calculations consume real `physicality.coord` when the recipe
  asks for S3 geometry.
- **Fréchet** consumes a realized ordered curve: decode the packed trajectory's
  constituent IDs, resolve each child's real `coord`, then compare those coordinates.
  Running Fréchet directly on packed trajectory `XYZM` is a typed defect.
- **Hausdorff** has the same realized-coordinate requirement when used for structural
  set/shape geometry, while remaining distinct from order-sensitive Fréchet.
- **Karcher/Fréchet means** are intrinsic manifold calculations over the declared real
  coordinate set and remain additional views rather than the canonical arithmetic
  composition centroid.
- **Hilbert locality** is an index projection over the declared real four-dimensional
  coordinate and is not the trajectory's BLAKE3 hash/address packing.

Every receipt must therefore identify which coordinate class was consumed:

```text
real coord
packed trajectory payload
realized curve
continuous display projection
Hopf base/fiber view
Hilbert index projection
```

Storage compatibility cannot erase this type distinction.

## Semantic-web consequence

All of these remain structural state. Projection collision, hash-address locality,
centroid proximity, or curve similarity is not semantic convergence.

```text
physicality / packed structural manifest / realized geometry = structure
semantic relation/evidence web = meaning, relation, evidence, context, standing
```

The semantic web may use structural candidates, but it decides semantic admissibility
under the active program.

## Implementation and acceptance consequences

1. Continuous S3 -> R3/R2 views declare full-manifold non-injectivity.
2. The discrete trajectory BLAKE3 payload is explicitly excluded from the Borsuk-Ulam
   projection claim.
3. `physicality.coord` and `physicality.trajectory` never share a geometric meaning
   merely because both use geometry-compatible storage.
4. Fréchet/Hausdorff/geometric curve work first realizes trajectory IDs to child
   `physicality.coord` values.
5. A mutant that applies Borsuk-Ulam to the hash packer fails the type contract.
6. A mutant that runs spatial math on packed trajectory payloads fails even if it
   returns finite numeric results.
7. A mutant that treats a collision-free finite S3 sample as proof of global
   projection injectivity fails.
8. Equal projected values never establish canonical identity or semantic equivalence.

## Research anchors

- Karol Borsuk, *Drei Sätze über die n-dimensionale euklidische Sphäre*, Fundamenta
  Mathematicae 20 (1933), 177-190.
- The standard Hopf fibration `S3 -> S2` supplies the relevant many-to-one fiber view.

The theorem is used to constrain continuous views of Laplace's **real S3 coordinate**.
It does not convert packed content addressing or topology into semantic truth.
