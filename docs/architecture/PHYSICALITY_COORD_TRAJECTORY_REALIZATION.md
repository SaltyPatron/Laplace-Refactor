# Physicality coordinate, packed trajectory, and realized curve

Status: inventor-direct architecture clarification, 2026-09-02. This document corrects any wording that treats `physicality.coord`, `physicality.trajectory`, and a realized coordinate curve as interchangeable geometry.

## 1. Three different structural objects

Laplace intentionally uses geometry-shaped storage for more than one structural purpose. The storage type does not define the semantic type.

```text
physicality.coord
    = the real four-component structural coordinate

physicality.trajectory
    = an ordered exact mantissa-packed manifest/address carrier
      whose vertices carry canonical identity plus structural metadata

realized curve
    = the ordered curve obtained by decoding trajectory constituent identities
      and resolving each constituent's real physicality.coord
```

These three objects must never be substituted for one another.

## 2. `physicality.coord` is the real structural coordinate

`physicality.coord` is the canonical calculated four-component placement for that physicality recipe.

- Tier 0 atoms lie on the pinned unit `S3` / glome placement generated from the Unicode/DUCET geometry recipe.
- Higher compositions use the declared four-dimensional arithmetic centroid of their actual child `coord` values and retain radius; they may therefore lie inside the glome rather than on the unit `S3` boundary.
- Angular/geodesic calculations, four-dimensional locality calculations, the Hilbert projection, radius, and other operations that declare the real structural coordinate consume `physicality.coord`.
- Equality of a composite centroid does not imply equal identity or equal order; exact identity remains BLAKE3 content identity and exact order remains in composition/trajectory structure.

Borsuk-Ulam applies to continuous lower-dimensional maps of the actual `S3` structural domain, not to every value stored in a geometry column and not to the packed trajectory carrier.

## 3. `physicality.trajectory` is a packed manifest, not a spatial path

`physicality.trajectory` is stored in a `GeometryZM`-compatible carrier because the binary64 slots provide an indexable fixed-width transport with exploitable database geometry/index machinery. Its coordinates are payload coordinates, not the live `S3` coordinates of the constituent.

For the composition trajectory class, every vertex encodes the constituent's canonical BLAKE3-128 identity and metadata required to reconstruct its structural occurrence. The design can be thought of as a hash/address coordinate in the `XYZ` mantissa lanes plus a metadata-rich `M` lane. The exact carrier is lossless for its declared payload.

The historical/current carrier evidence uses four binary64 values with the exponent pinned and sign+mantissa carrying 53 payload bits each, for 212 exact payload bits per vertex. The 128-bit identity is distributed across the `X/Y/Z` payload capacity; ordinal, run length/RLE, flags and other typed metadata occupy the remaining capacity, with `M` the metadata-rich lane. Historical packing also uses spare `Z` payload bits for part of the flags word. The clean contract must therefore describe the exact generated bit layout rather than assuming that host coordinate names imply spatial meaning.

Conceptually:

```text
trajectory vertex
  X/Y/Z payload lanes  -> BLAKE3-128 constituent address plus any declared spare metadata bits
  M metadata lane      -> ordinal, run/RLE and other typed metadata under the vertex recipe
```

The fact that sequence position already supplies an ordinal does not make the packed ordinal meaningless: the encoded value is part of the exact portable/invertible carrier and may support validation, slicing, indexing, replay, run representation, or future recipe semantics. Its redundancy must be judged by the declared ABI and measured use, not deleted implicitly.

A packed `trajectory` vertex is therefore an extremely useful **address/index coordinate**, but it is not a point on Laplace's structural `S3` geometry.

## 4. The realized curve is the geometric trajectory

When a calculation needs the geometric shape of a composition, the engine performs the typed realization:

```text
packed trajectory
  -> decode constituent canonical IDs + ordinal/run/metadata
  -> resolve each constituent physicality.coord under the pinned geometry epoch
  -> preserve declared order and multiplicity/RLE
  -> produce the realized coordinate curve
```

Fréchet, Hausdorff, curve length, trajectory-shape and other geometric path calculations operate on that realized curve when their recipe requires real structural coordinates.

Running Fréchet or another spatial metric directly over the packed `XYZM` payload measures the numerical layout of BLAKE3/metadata bits. It can return a perfectly valid floating-point number while answering the wrong question. That is a typed semantic defect, not merely an approximation.

## 5. There are therefore multiple legitimate '3D' views

The phrase `3D coordinate` is ambiguous in Laplace and must be qualified.

### Packed hash/address coordinate

The trajectory carrier exposes three coordinate-shaped payload lanes whose main job is to encode the canonical 128-bit BLAKE3 identity for addressability/indexing. This is a discrete exact content-address representation, not a continuous projection of `S3`.

### Continuous/display projection

A UI or calculated view may map the real four-component `coord` into three dimensions for display or another declared calculation. That is a separate projection with an explicit loss contract.

### Hopf base view

The Hopf `S3 -> S2` base is another calculated structural view with explicit fiber loss and fiber phase/state when those distinctions matter.

None of these replaces the real `coord` or canonical content identity.

## 6. Borsuk-Ulam boundary

For a continuous map of the real unit structural manifold

```text
f : S3 -> R3
```

Borsuk-Ulam guarantees at least one antipodal pair with equal projected value. This establishes a global non-injectivity boundary for continuous three-dimensional projections of the actual `S3` coordinate space.

It does **not** apply to the trajectory's BLAKE3 mantissa packing as though that packing were a continuous `S3 -> R3` map. The hash/address coordinate is a discrete exact encoding of canonical identity. Its correctness question is bit-exact pack/unpack and index semantics, not manifold injectivity.

Therefore two very different statements must remain separate:

```text
real S3 coord -> continuous R3 view
    globally non-injective by Borsuk-Ulam

BLAKE3-128 id -> packed trajectory XYZ/M carrier
    discrete typed payload encoding; exact round-trip is the contract
```

## 7. Structural geometry is still separate from the semantic web

All of the objects above remain structural machinery.

```text
real coord / centroid / radius / Hilbert
packed identity trajectory / ordinal / RLE / flags
realized coordinate curve / angular / Fréchet / Hausdorff / Karcher-derived views
    = structural state and structural candidate calculations

relations / senses / referents / usages / propositions / testimony / dependence /
standing / world / time / discourse / goals / semantic acts
    = semantic and epistemic web state
```

A cognition program may use both. Neither structure nor hash locality establishes meaning by itself.

## 8. Acceptance consequences

Acceptance must reject:

- reading `physicality.trajectory.X/Y/Z/M` as live `S3` coordinates;
- computing Fréchet/Hausdorff directly over packed identity vertices and claiming shape distance;
- substituting a trajectory hash/address coordinate for `physicality.coord`;
- substituting the real centroid coordinate for exact ordered trajectory identity;
- dropping packed ordinal/RLE/metadata merely because array position appears redundant;
- treating Borsuk-Ulam as a property of the discrete hash packer;
- treating a 3D display projection as the packed hash coordinate or vice versa;
- treating any structural coordinate, hash address, locality result, or curve metric as semantic equivalence.

Positive tests must prove exact trajectory pack/unpack, exact constituent reconstruction, real-coordinate realization, metric divergence between packed-payload math and realized-curve math, and cross-route agreement on which coordinate class each operation consumes.

## Evidence continuity

The old iteration's archived substrate invariant already recorded the same critical distinction: `coord` is real four-dimensional placement; `trajectory` is an ordered mantissa-packed identity/ordinal/run/flags manifest; shape metrics require a realized curve of child coordinates. `docs/audits/TRAJECTORY_PAYLOAD_AND_INDEX_AUDIT.md` carries that recovered behavior forward as clean-room evidence. This document promotes the inventor-direct distinction into the current architecture record without importing the old implementation as authority.
