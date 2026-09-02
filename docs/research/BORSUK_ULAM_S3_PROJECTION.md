# Borsuk-Ulam consequences for Laplace S3 structural projections

Status: research/implementation input, 2026-09-02. This note supplements
`STRUCTURAL_TRAJECTORY_METRICS.md`, the glome geometry acceptance, and issue #168.
It constrains structural projection behavior; it does not define semantic equivalence.

## The theorem applies directly to the S3 projection boundary

For every continuous map

```text
f : S^n -> R^n
```

Borsuk-Ulam guarantees at least one antipodal pair `x` and `-x` such that

```text
f(x) = f(-x).
```

Therefore for Laplace's canonical structural glome:

```text
f : S3 -> R3
```

any continuous three-dimensional projection or continuous three-component feature map
is necessarily non-injective somewhere on the full S3 domain. At least one antipodal
pair is collapsed by the projection. The same non-injectivity consequence applies to
continuous maps into lower Euclidean dimensions as well.

This is a mathematical reason, not merely a UI convention, that a 3D/2D projection
cannot replace canonical four-component S3 physicality.

## This does not make antipodes canonically identical

Borsuk-Ulam says that a lower-dimensional continuous map must identify some antipodal
pair. It does **not** say that canonical S3 points `x` and `-x` are the same Laplace
physicality, the same content, or semantically equivalent.

The distinction is:

```text
canonical S3 structural state:
    x != -x unless the exact physicality/identity contract separately says otherwise

continuous lower-dimensional projection:
    there exists x for which f(x) = f(-x)
```

Accordingly, the existing prohibition on silently applying quaternion orientation
semantics (`q ~ -q` or `abs(dot)`) remains valid. Laplace S3 is a structural content
geometry, not automatically the projective rotation space `SO(3)`.

Borsuk-Ulam actually strengthens the projection-nonauthority rule: collisions are not
an implementation accident that a clever continuous 3D projection can universally
avoid.

## Hopf projection is an even stronger many-to-one case

Laplace also uses Hopf structure as a calculated view. The standard Hopf map

```text
h : S3 -> S2
```

satisfies

```text
h(x) = h(-x)
```

for every antipodal pair, not merely for one guaranteed pair. More generally, each
Hopf base point has an `S1` fiber, so the base projection intentionally collapses an
entire circle of S3 points.

Consequences:

- Hopf base coordinates cannot be canonical physicality identity;
- equal Hopf base location does not imply equal S3 point, constituent, trajectory, or
  semantic state;
- fiber phase is required when the calculation needs to distinguish points within a
  Hopf fiber;
- a visualization using only the S2 base is intentionally lossy and must receipt that
  loss.

## Finite Unicode placement versus the continuous theorem

Borsuk-Ulam is a theorem about the full continuous sphere. Laplace's Tier-0 Unicode
placement is a finite population of sampled S3 points. A particular continuous
projection can be injective on that finite sampled subset even though it cannot be
injective on all of S3.

Therefore acceptance must not falsely claim:

```text
Borsuk-Ulam => the current 1,114,112 sampled Unicode points necessarily contain a
projected antipodal collision.
```

Instead it establishes the stronger architectural boundary:

```text
no continuous lower-dimensional representation has a global injectivity contract over
canonical S3.
```

Any finite-sample no-collision observation is a property of that sample/projection
epoch, not a theorem that the projection is lossless.

## Relation to angular, Fréchet, Hausdorff, Karcher and Hilbert calculations

Borsuk-Ulam is not another interchangeable distance metric. It constrains projection
and representation.

- **Angular/geodesic** distance operates on canonical S3 points and can distinguish
  antipodes at distance `pi` under the declared unit-sphere metric.
- **Fréchet** compares realized ordered coordinate trajectories; doing it after a
  lossy lower-dimensional projection can collapse distinctions that exist in S3.
- **Hausdorff** has the same projection-loss concern for sets/shapes while answering a
  different set-coverage question.
- **Karcher/Fréchet means** should be calculated on the declared manifold when the
  program requires intrinsic geometry; calculating after an unreceipted projection
  changes the optimization problem.
- **Hilbert locality** is a discrete/index projection over declared four-dimensional
  coordinates and remains a candidate/index operation rather than a theorem-preserving
  replacement for S3.

A metric recipe must therefore state whether it consumes canonical S3 coordinates or a
particular projected view. Projected calculations carry the projection identity and
loss contract in their receipt.

## Semantic-web consequence

Projection collision is structural representation loss, not semantic convergence.
Two canonical structural states may map to the same 3D/S2 display point and still have
completely different relation/evidence webs. Conversely, semantically related entities
may be distant in S3.

This is another reason for the inventor-direct separation:

```text
S3 / physicality = structure
semantic relation/evidence web = semantics
```

A projection may help humans or candidate generation see the structure. The semantic
web resolves meaning, evidence, relation type, context, standing, discourse and goal.

## Implementation and acceptance consequences

1. Every continuous S3 -> R3/R2 display or feature projection is declared lossy at the
   full-manifold contract level.
2. No projection output can replace canonical four-component physicality or content
   identity.
3. Projected equality never establishes canonical S3 equality or semantic equivalence.
4. Exact structural operations requiring S3 use canonical coordinates or prove that
   the selected projection preserves the needed invariant for the bounded input.
5. Fréchet/Hausdorff/Karcher calculations state whether they operate intrinsically on
   S3 or on a declared projection and receipt any loss.
6. Hopf S2 base-only views explicitly retain the fact that fibers are many-to-one.
7. A deliberate mutant that treats a collision-free finite sample as proof of global
   projection injectivity must fail.
8. A deliberate mutant that treats equal projected points as equal semantic entities
   must fail.

## Research anchors

- Karol Borsuk, *Drei Sätze über die n-dimensionale euklidische Sphäre*, Fundamenta
  Mathematicae 20 (1933), 177-190. The Borsuk-Ulam theorem is the relevant projection
  obstruction.
- The standard Hopf fibration `S3 -> S2` is a many-to-one structural projection with
  circle fibers and identifies antipodal points in the base projection.

The theorem is used here to constrain what Laplace is allowed to infer from a
lower-dimensional representation. It does not convert topology into semantic truth.
