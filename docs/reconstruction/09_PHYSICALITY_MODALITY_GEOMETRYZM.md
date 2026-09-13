# Physicality, modality and GeometryZM

Status: reconstruction dossier. This file is constrained by inventor-direct corrections and must not inherit line-only assumptions from implementation examples.

## 1. Governing correction

**DIRECT CURRENT LAW**

Physicality trajectory uses the **GeometryZM datatype/carrier family**. It is not universally a `LINESTRINGZM` and not a GIS-semantic ontology.

Depending on the physical structure required by modality/form/tier/recipe, the lawful carrier may use GeometryZM subtypes including:

```text
POINTZM
MULTIPOINTZM
LINESTRINGZM
MULTILINESTRINGZM
POLYGONZM
MULTIPOLYGONZM
GEOMETRYCOLLECTIONZM
...
```

The exact subtype is a structural realization choice. It does not decide what modality the content is.

## 2. Direction of derivation

The intended direction is:

```text
modality / formal structure
        +
structural tier / role
        +
recipe / realization law
        ↓
what physical structure actually exists
        ↓
appropriate GeometryZM representation/carrier
```

Rejected direction:

```text
GeometryZM subtype
        ↓
defines modality or semantics
```

Historical `Hartonomous-001` schema evidence explicitly stated the principle in implementation form: modality lives on the entity/type side rather than being defined by `physicality_type`, while the physicality store admitted a broad GeometryZM subtype family so future modalities could share the same substrate machinery.

## 3. GeometryZM is not “GIS meaning”

**HISTORICAL INVENTOR EVIDENCE + CURRENT CORRECTION**

Earlier physicality/mantissa work describes GeometryZM as a generic four-lane carrier rather than a geographic ontology. The four binary64 lanes provide controlled exact mantissa capacity for typed payload/metadata encodings where the applicable recipe defines what lanes mean.

Therefore:

- X/Y/Z/M are not globally privileged semantic axes;
- geographic interpretation is one possible domain use, not the universal meaning;
- numeric closeness of packed payload values does not automatically mean structural closeness;
- PostGIS geometry operations are lawful only when the input type/recipe says the numeric values represent the required live geometry.

## 4. `physicality.coord` is live structural coordinate state

**DIRECT CURRENT LAW**

`physicality.coord` is the real four-component structural coordinate used by structural geometry.

Current direct correction establishes:

- Tier-0 atom coordinates lie on the unit S3/glome under the declared placement recipe;
- higher compositions use the declared exact four-dimensional arithmetic centroid of immediate child coordinates, retaining radius and not normalizing back to S3;
- Hilbert/locality and angular/geodesic calculations consume the appropriate live coordinate state under their typed contracts.

The coordinate is a structural physical representation. It is not semantic truth, sense, relevance or evidence.

## 5. Packed physicality trajectory/address carrier is different state

**DIRECT CURRENT LAW**

The packed trajectory/address representation uses coordinate-shaped binary64 lanes as an **exact payload carrier** for constituent identity and structural metadata.

Conceptually it can carry information such as:

- constituent BLAKE3 identity/address bits;
- ordinal/order;
- run/RLE information;
- tier/structural metadata;
- recipe/type-specific metadata within the declared ABI.

The coordinate-looking doubles in this packed payload are not the constituent's live S3 coordinate.

Therefore this is a type error:

```text
packed trajectory payload
    -> Fréchet/Hausdorff as if payload were live geometry
```

A geometry library returning a finite number does not make the operation meaningful.

## 6. Realized structural curve/shape

When an operation needs actual structural geometry over a trajectory/composition, it must:

```text
packed manifest/address state
    -> decode constituent identities + ordinal/run/type metadata
    -> resolve each constituent's live `physicality.coord`
    -> construct the modality/recipe-declared realized geometry
    -> apply the typed structural metric
```

For an ordered curve-like structure, that realized geometry may support Fréchet.

For a set/region/other shape, a different metric/operator may be appropriate.

The direct correction specifically prevents the reconstruction from treating “realized geometry” as universally a single LINESTRING just because one architecture note discusses ordered trajectories.

## 7. Structural metrics remain different operations

**DIRECT CURRENT LAW**

There is no one universal structural distance.

- angular/geodesic distance: operates on the declared live S3 coordinate state;
- Fréchet: operates on compatible realized ordered curves;
- Hausdorff: answers a different set/shape coverage question;
- Karcher-derived summaries: intrinsic manifold calculations and not a replacement for canonical arithmetic composition centroid;
- Hilbert: locality/index projection over real 4D coordinate state;
- exact containment/order/ordinal/run: decoded structural relations, not approximate metrics;
- packed BLAKE3/address locality: payload/address structure, not live S3 geometry.

A cognition/program may use several of these providers without collapsing them into one global “semantic distance.”

## 8. Structural geometry is not semantics

**DIRECT CURRENT LAW**

S3/GeometryZM/trajectory calculations can support:

- exact physical structure;
- locality;
- candidate generation;
- structural similarity;
- recurrence/motif discovery;
- bounded search acceleration;
- procedural-hypothesis generation.

They cannot alone establish:

- word sense;
- referential identity;
- relation truth;
- source authority;
- evidence independence;
- semantic relevance;
- completion;
- cognition policy.

A structurally similar pair may be semantically different. A semantically equivalent pair may have very different exact/physical structure.

This distinction is explicitly required by current cognition/Gödel law.

## 9. Modality examples must remain examples, not universal mappings

The reconstruction may discuss concrete fixtures such as:

- text;
- image;
- audio;
- video;
- chess;
- DNA/genomic sequences;
- LaTeX/math;
- chemical formulas/structures;
- cooking recipes;
- TAS/game scripts;
- MIDI/music.

But it must not invent a static table such as:

```text
image -> POLYGON
sound -> LINESTRING
video -> MULTILINESTRING
```

unless an authoritative recipe/spec explicitly defines that particular realization.

The rule is **modality/form/recipe determines the physical structure**; GeometryZM is sufficiently polymorphic to carry the structure without creating a separate physicality ontology per modality.

## 10. Identity remains outside physicality

Canonical content identity does not include the physical coordinate, GeometryZM subtype, modality, source, tier, context or occurrence.

This matters because the same exact content can:

- occur in multiple sources;
- participate at multiple structural roles/tiers;
- receive different lawful physicality realizations/recipes;
- be observed in different modalities/contexts where the content identity law says it is still equal.

Physicality is a calculated realization under recipe/epoch, not the thing that mints content identity.

## 11. Historical evolution requiring a supersession matrix

Older Hartonomous material sometimes includes stronger claims such as:

- “geometry is meaning”;
- fixed semantic interpretations of coordinate axes;
- point/linestring duality as if universal;
- spatial query functioning directly as semantic inference;
- LMDS/Hilbert/other spatial machinery treated as the dominant representation.

Those records remain historically important, but current direct law separates structural geometry from the typed semantic/evidence web.

A later dossier must date each historical claim and mark whether it was:

- preserved in narrower structural form;
- generalized;
- replaced;
- directly superseded.

## 12. Counterexamples

Reject:

- “GeometryZM trajectory = LINESTRINGZM” as universal law;
- modality inferred from physicality subtype;
- treating packed trajectory payload as live S3 geometry;
- applying Fréchet/Hausdorff without first satisfying the operation's input-type/realization contract;
- geometry/proximity promoted directly to semantic truth/relevance;
- one universal distance replacing exact structural relations and typed semantic/evidence providers;
- reminting canonical identity because physicality/tier/modality differs.

## 13. Research still required

1. exact dated history of `physicality_type` and broad GeometryZM constraints in Hartonomous repositories;
2. first appearance and revisions of the 212-bit mantissa/trajectory ABI;
3. modality-specific physicality recipes for representative text/image/audio/video/chess/code/formal-structure fixtures;
4. historical coordinate-axis interpretations and explicit supersession dates;
5. exact current generated ABI for packed trajectory payload versus live coord;
6. complete metric/provider matrix showing input type, output type, completeness/loss and semantic authority boundary.
