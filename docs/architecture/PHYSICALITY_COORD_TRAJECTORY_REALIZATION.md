# Physicality coordinate, GeometryZM trajectory carrier, and realized geometry

Status: inventor-direct architecture clarification, corrected during 2026-09-12 invention reconciliation. This document corrects wording that treated `physicality.coord`, `physicality.trajectory`, a realized curve, or one geometry metric as interchangeable with the complete physicality model. Higher inventor-direct evidence remains authoritative.

## 1. The storage class does not define the ontology

Laplace intentionally uses geometry-shaped storage for multiple structural purposes. The storage type, subtype, coordinate field names, and a convenient metric do not define what the represented content *is*.

The governing distinctions are:

```text
physicality.coord
    = real four-component structural placement for the declared coordinate recipe

physicality.trajectory
    = exact typed GeometryZM-family carrier for the physical realization
      whose subtype and payload recipe are determined by the represented structure

realized geometry
    = the real point / curve / branch set / region / cloud / collection / manifold view
      produced when an operation resolves the exact carrier into the coordinate class
      and topology that operation actually requires
```

For the **ordered composition trajectory class**, realized geometry is commonly an ordered coordinate curve obtained by decoding constituent identities and resolving each constituent's real `physicality.coord`. That case must not be generalized into a claim that every physicality is a `LINESTRING` or that every physicality operation is a curve comparison.

These objects and operation classes must never be silently substituted for one another.

## 2. `physicality.coord` is real structural placement

`physicality.coord` is the calculated four-component placement selected by the active physicality/geometry recipe.

- Tier-0 Unicode atoms use the pinned `S3` / glome placement generated from the Unicode/DUCET geometry recipe.
- Higher compositions use the declared four-dimensional arithmetic centroid of their actual child coordinates with multiplicity and retain radius; they may therefore lie inside the glome rather than on the unit `S3` boundary.
- Angular/geodesic calculations, four-dimensional locality calculations, the Hilbert projection, radius, and other operations that explicitly consume the real structural coordinate operate on `physicality.coord`.
- Equality of centroid, radius, projection, or locality does not imply equal identity, equal constituents, equal order, equal topology, or equal meaning.

Borsuk-Ulam applies to continuous lower-dimensional maps of the actual `S3` structural domain. It does not automatically apply to every value stored in a geometry-compatible carrier and does not govern the discrete packed trajectory payload.

## 3. `physicality.trajectory` uses the `GeometryZM` family

`physicality.trajectory` uses a `GeometryZM`-family carrier because PostGIS provides a polymorphic geometry container, four binary64 lanes per vertex, and n-dimensional index machinery that Laplace can exploit without making ordinary GIS semantics authoritative.

The relevant physicality topology is not fixed to one subtype. Historical inventor product law and direct reconciliation require preserving the full family where the modality/structure calls for it:

```text
POINTZM
LINESTRINGZM
MULTILINESTRINGZM
POLYGONZM
MULTIPOLYGONZM
MULTIPOINTZM
GEOMETRYCOLLECTIONZM
```

The concrete shape follows the selected modality/structural recipe. Examples of the distinction, without making the examples a closed ontology:

- one addressable point-like physicality may use `POINTZM`;
- an ordered linear composition may use `LINESTRINGZM`;
- parallel or branching sequence structure may require `MULTILINESTRINGZM`;
- a closed region may require `POLYGONZM` or `MULTIPOLYGONZM`;
- an unordered cloud may require `MULTIPOINTZM`;
- heterogeneous compound physicality may require `GEOMETRYCOLLECTIONZM`.

A subtype is part of the physical form, not the semantic type of the content. Modality, tier, type/classification, relation family, source, and physicality subtype remain different coordinates of the machine.

A generic physicality operator must preserve subtype structure. Flattening polygon rings, multiline branches, multipoint sets, or collection components into one anonymous vertex stream is a lossy substitution unless the declared operation explicitly requests and receipts such a projection.

## 4. Mantissa exploitation is exact typed transport

The carrier's binary64 slots can be used as exact payload lanes under a pinned exponent recipe. With sign plus mantissa available, each binary64 contributes 53 exact payload bits; four lanes therefore provide 212 exact payload bits per vertex.

The coordinate-looking host names `X/Y/Z/M` do not assign semantic meaning to those bits. Meaning comes from the physicality type, geometry subtype, vertex class, generated ABI, recipe, and receipt.

For the **historical/current ordered composition-trajectory vertex class**, evidence shows a 128-bit canonical constituent identity plus ordinal, run/RLE, flags, and other typed metadata distributed across the 212 available bits. Conceptually:

```text
composition trajectory vertex
  payload capacity -> canonical constituent address + structural occurrence metadata
  exact decoder    -> identity, ordinal, run/RLE, flags, declared metadata
```

Historical packing used most `X/Y/Z` capacity for the BLAKE3-128 constituent address and also used spare `Z` capacity for flags; `M` was metadata-rich. The generated ABI, not a mnemonic such as "XYZ = hash, M = metadata," owns the exact bit allocation.

Other vertex classes may allocate the same 212-bit capacity differently. A factor, tensor, game, media, relation, execution, or future physicality payload is not required to pretend its bits are a composition child identity merely because it uses the same transport class.

## 5. Ordered composition trajectories are exact structural manifests

For an ordered recursive composition, the trajectory carries enough exact information to reconstruct the declared structural occurrence rather than merely approximate its shape.

A conceptual decode is:

```text
packed composition trajectory
  -> exact constituent identities
  -> ordinal / role / multiplicity / run metadata
  -> exact ordered structural occurrence
  -> recursive constituent resolution
  -> trunk-to-leaf Merkle structure
```

This directly supports exact structural calculations such as constituent identity, containment, ancestry, ordinal, gap, multiplicity, runs, recurrence, and precedes/follows under the selected structural recipe.

Those facts do not need Fréchet, Hausdorff, an embedding, a semantic relation row, or source testimony merely to exist. They are calculated consequences of exact content and physicality.

The packed ordinal can remain useful even when storage sequence appears to supply position: it is part of a portable/invertible carrier and can support validation, slicing, replay, run representation, index extraction, and future declared recipes. Redundancy is removed only through an explicit ABI and acceptance change, not by inference from host container order.

## 6. Realization is polymorphic too

When a calculation needs real geometry rather than exact packed addressing, it performs the realization required by that operation.

For an ordered composition curve:

```text
packed trajectory
  -> decode constituent IDs + ordinal/run/metadata
  -> resolve each constituent physicality.coord under the pinned geometry epoch
  -> preserve order and multiplicity/RLE
  -> realized coordinate curve
```

For other physicality shapes the realization preserves the declared topology instead of coercing it to a curve:

```text
packed / stored physicality
  -> typed decode
  -> resolve the real coordinate or component geometry required by the recipe
  -> preserve branches / rings / regions / sets / heterogeneous components
  -> realized point / multiline / polygon / multipolygon / multipoint / collection / other declared object
```

The exact clean ABI for each class belongs to generated contracts and acceptance. The architectural requirement is that realization preserves every distinction required by the consuming operation or explicitly declares the loss.

## 7. Fréchet is one typed curve operation

Fréchet is appropriate only when the program asks an order/progression-sensitive question about curve-like realized geometry under a declared point metric and algorithmic recipe.

A conforming Fréchet recipe names at least:

- input coordinate class;
- geometry/topology class accepted;
- underlying point metric;
- continuous or discrete algorithm;
- monotone/non-monotone law as applicable;
- open, closed, or subcurve treatment;
- sampling/refinement behavior;
- numeric precision/tolerance;
- resource/cardinality boundary;
- partial/failure disposition and receipt.

Running Fréchet directly over packed payload `XYZM` measures the numerical layout of the address/metadata encoding rather than the real structural shape. That is a typed semantic defect even if the library returns a finite deterministic number.

Fréchet is not a generic synonym for physicality similarity.

## 8. Other physicality questions use different calculations

No single distance, neighbor, or similarity is canonical.

### Exact structural facts

Identity, constituent identity, containment, ancestry, ordinal, gap, multiplicity/run, recurrence, precedes/follows, exact reconstruction, and structural altitude are calculated exactly from canonical structure and the applicable physicality/occurrence recipe. Metric approximation is unnecessary.

### S3 point geometry

For unit `S3` atom points, an intrinsic angular/geodesic calculation may use the pinned `acos(clamp(dot(p,q),-1,1))` law. Laplace's S3 points are content-structure locations and must not silently inherit quaternion orientation equivalence such as `q ~ -q`.

### Hausdorff and set geometry

Hausdorff answers a set-coverage question and does not impose Fréchet's monotone traversal correspondence. Equal or near-equal Hausdorff with materially different Fréchet is valid typed divergence, not an inconsistency to normalize away.

### Karcher / Fréchet means

An intrinsic mean is a separately calculated manifold view. It is not the canonical arithmetic composition centroid. On positively curved `S3`, admissible region, initialization, convergence, uniqueness/ties, iteration bounds, and failure behavior must be explicit.

### Hilbert locality

Hilbert is a locality/index projection over the declared real structural coordinate space. It can narrow candidate work but does not define semantic distance or replace exact validation.

### Regions, branches, collections, and cross-shape operations

Polygon/region operations, branch-aware multiline operations, point-cloud/set operations, collection-aware operations, and explicitly declared cross-shape relationships are separate typed families. The substrate operator must dispatch by the declared physicality and geometry classes and preserve topology required by the question.

Additional point, curve, set, manifold, topological, spectral, temporal, recurrence, shape, or domain-specific calculations may be added through the ISA/recipe lifecycle. Numeric compatibility never authorizes one operation to impersonate another.

## 9. There are multiple legitimate coordinate-shaped views

The phrase `3D coordinate` or `geometry` is ambiguous unless qualified.

### Packed address/payload view

A trajectory or other physicality vertex may expose coordinate-shaped binary64 lanes used as exact typed payload. This is discrete transport/index state, not automatically a continuous structural coordinate.

### Continuous/display projection

A UI or calculated view may map the real four-component coordinate into three dimensions for display or another declared calculation. That is a separate projection with explicit loss.

### Hopf base view

The Hopf `S3 -> S2` base is another calculated structural view with explicit fiber loss and retained fiber information where required.

None of these replaces canonical content identity, the real coordinate, the exact packed carrier, or one another.

## 10. Borsuk-Ulam boundary

For a continuous map of the real unit structural manifold

```text
f : S3 -> R3
```

Borsuk-Ulam guarantees at least one antipodal pair with equal projected value. This establishes a global non-injectivity boundary for continuous three-dimensional projections of the actual `S3` coordinate space.

It does **not** apply to the trajectory's BLAKE3/mantissa packing as though the discrete exact payload were a continuous `S3 -> R3` map.

Therefore these remain separate:

```text
real S3 coord -> continuous R3 view
    globally non-injective by Borsuk-Ulam

canonical identity / typed metadata -> packed GeometryZM lanes
    discrete typed payload encoding; exact round-trip is the contract
```

## 11. Structural physicality is not semantic authority

All of the objects above remain structural/calculated machinery.

```text
real coord / centroid / radius / Hilbert
GeometryZM subtype / topology
packed identity or other typed payload / ordinal / RLE / flags
realized point / curve / set / region / collection
angular / Fréchet / Hausdorff / Karcher / other declared structural calculations
    = structural state and structural candidate calculations

relations / senses / referents / usages / propositions / testimony / dependence /
standing / world / time / discourse / goals / semantic acts / outcomes
    = semantic and epistemic web state
```

A cognition program may lawfully use several of these channels together. No structural coordinate, topology, hash locality, metric, or index result establishes semantic equivalence or truth by itself.

## 12. Tier, type, modality, relation, and physicality shape remain separate

Physicality bugs have repeatedly originated in collapsing surrounding coordinates. The following distinction is mandatory:

- **tier / altitude** describes structural level relative to constituent decomposition under a selected recipe;
- **type / classification** is witnessed or calculated interpretive state and can be multiple or competing;
- **modality** determines the applicable structural grammar/realization domain while remaining outside content identity;
- **relation** is a typed connection/law with its own direction, arity, evidence, context, and composition rules;
- **physicality subtype/shape** is the concrete structural realization selected by the modality/physicality recipe.

A historical implementation field that combined several of these is defect evidence, not permission to combine them in the clean machine.

## 13. Acceptance consequences

Acceptance must reject at least:

- reading packed `physicality.trajectory` lanes as live `S3` coordinates;
- assuming every trajectory is a `LINESTRING`;
- flattening `MULTILINESTRING`, polygon rings, multipoints, multipolygons, or geometry collections to one vertex stream while claiming lossless parity;
- computing Fréchet/Hausdorff directly over packed address/metadata vertices and claiming real shape distance;
- using Fréchet as the generic physicality or cognition similarity operation;
- substituting a packed address coordinate for `physicality.coord`;
- substituting a centroid for exact ordered/branched/regional physicality structure;
- dropping packed ordinal/RLE/metadata because host container order appears redundant;
- treating Borsuk-Ulam as a property of the discrete payload packer;
- treating a display projection as packed payload or vice versa;
- treating any structural coordinate, subtype, locality result, or metric as semantic equivalence;
- collapsing tier, type, relation, modality, or physicality shape into one category field.

Positive tests must prove, as applicable:

- exact typed pack/unpack and payload-class decoding;
- exact constituent reconstruction for composition trajectories;
- preservation of subtype topology;
- real-coordinate or real-geometry realization appropriate to the operation;
- metric-family divergence fixtures;
- exact structural facts without metric substitution;
- cross-route agreement on coordinate class, topology class, recipe, epoch, and operation semantics.

## Evidence continuity

The old Hartonomous/Laplace lineage contains both valuable invention evidence and obsolete implementation choices. The preserved invention law includes the universal four-lane GeometryZM carrier, full subtype family, subtype-aware operator dispatch, recursive composition, mantissa exploitation, modality-derived physical structure, and the distinction between packed structural manifests and real geometry. Historical schema, fixed tier/type fields, or one implementation's preferred subtype remain non-authoritative unless reconciled through the current authority stack.

`docs/audits/TRAJECTORY_PAYLOAD_AND_INDEX_AUDIT.md`, `docs/audits/INVENTION_RECONSTRUCTION_2026-09-12.md`, the pinned original Laplace invention lineage, and direct inventor corrections provide the reconciliation trail. A future direct correction changes this derived architecture record explicitly rather than being averaged with older prose.
