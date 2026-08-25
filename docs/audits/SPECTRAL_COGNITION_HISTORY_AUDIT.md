# Historical spectral and cognition-path audit

## Scope and evidence status

This is a read-only clean-room audit of the current historical repository and its Git
history. It determines what was implemented, which behavior was demonstrated, which
invention distinctions were erased, and which defects must be detected by the new
acceptance suite. It does not authorize reuse of historical source, names, tests,
constants, layouts, or pipeline structure.

The inspected current files and Git blobs are:

| Historical file | Lines | Blob |
|---|---:|---|
| `engine/dynamics/src/eigenmaps.cpp` | 253 | `e9d8d4bf2ce60ae28bb1bda64fd4ae20c159f665` |
| `engine/dynamics/src/gram_schmidt.cpp` | 48 | `6a631940ddbc6ebb78b3737ce1b379051bfa5772` |
| `engine/dynamics/src/procrustes.cpp` | 87 | `66594153212e9e2f0aae6db93542c9959a994786` |
| `app/Laplace.Cli/FoundryExport.cs` | 1,768 | `25ad5b27071a172d8f6e7ef3416fe5e4491deffc` |
| `app/Laplace.Cli/FoundryCommands.cs` | 2,469 | `b4e13d2b483aecd78d014500f8b3e92bb0ae4082` |
| `app/Laplace.Core/Core/FoundryDefaults.cs` | 83 | `4cca7b5a977ab067274a1a5140a5f6d9ae467657` |
| `app/Laplace.Cli.Tests/FoundryPlaneAlgebraTests.cs` | 150 | `073f076478dd4d6018b68e6b1536496ceb7b8e13` |

The seven files total 4,858 current lines. Git records 13 commits touching the
Eigenmaps source, five touching Gram-Schmidt, four touching Procrustes, 42 touching
`FoundryExport`, 36 touching `FoundryCommands`, and seven touching the defaults file.
Those counts measure change activity, not authorship, intent, or defect count.

## Recovered execution path

The current basis path is observably:

1. Read several relation, trajectory, adjacency, or geometric metric planes.
2. Peak-normalize planes independently (`FoundryExport.cs:740-747`; call sites include
   `FoundryCommands.cs:606-609`, `623-632`, and `1144-1174`).
3. Remove every nonpositive value before basis construction
   (`FoundryExport.cs:750-766`; `FoundryCommands.cs:686-699`, `1199-1202`).
4. Concatenate the remaining COO triples without retaining a plane/type operand
   (`FoundryExport.cs:769-782`).
5. Sum duplicate endpoint pairs, symmetrize the matrix, construct a normalized scalar
   Laplacian, and run a symmetric eigensolver (`eigenmaps.cpp:57-150`, `155-194`).
6. Transpose the output and apply ordinary Euclidean QR
   (`FoundryExport.cs:1108-1121`; `gram_schmidt.cpp:11-47`).
7. Fit a rectangular spectral-to-four-dimensional projection with scale and
   translation, then overwrite the first four spectral columns with that projection
   (`procrustes.cpp:17-76`; `FoundryExport.cs:1153-1203`).
8. Fill remaining target dimensions with seeded Gaussian values
   (`FoundryExport.cs:1215-1223`).

This is a conventional scalar-affinity spectral pipeline. It is not a generated
typed-incidence cognition operator over exact physical structure plus selected
epistemic state.

## Findings

### 1. The native boundary erases relation type before mathematics begins

The sparse entry point accepts only COO row, column, and scalar weight arrays plus
matrix dimensions (`engine/dynamics/include/laplace/dynamics/eigenmaps.h:16-22`). It
cannot express:

- exact physicality recipe or trajectory identity;
- relation family, law, direction, arity, roles, source and target fields, or units;
- occurrence scope, time, world, or context;
- witness, source, dependence roots, consensus epoch, or uncertainty;
- operator program, mass measure, null-space contract, or calculation receipt.

No caller can recover those distinctions after they cross this boundary.

### 2. Independent planes become an edge-count-weighted scalar union

`Normalize` divides each plane by its maximum absolute entry. `Union` then only
concatenates triples. The native triplet reducer sums duplicates
(`eigenmaps.cpp:186-188`). Therefore type does not survive, and the effective
cross-plane influence depends materially on how many triples each plane contributes.

The historical test suite explicitly records this behavior: duplicate pairs must
remain because native summation is described as the only block-weighting mechanism
(`FoundryPlaneAlgebraTests.cs:41-57`), while peak normalization leaves block influence
governed by edge count (`:66-76`). Those tests characterize the scalar pipeline; they
do not prove Laplace relation semantics.

Two inputs with identical endpoints and scalar totals but different laws—such as
calculated containment/precedence versus attested synonymy/translation—therefore
produce the same basis input. This is direct type erasure.

### 3. Direction is destroyed

The sparse path replaces the input with `(W + W^T) / 2`
(`eigenmaps.cpp:190-194`). Containment, precedence, causality, role binding, and other
directed relations cannot retain their direction through this basis construction.
The point-based path likewise makes the k-nearest-neighbor graph undirected
(`eigenmaps.cpp:240-250`).

### 4. Contradiction was first inverted, then removed from the basis

Commit `91efb66a` documents that the prior implementation applied absolute value to
every COO weight. A refutation of `-0.7` became positive affinity `+0.7`; when a
positive and negative occurrence shared endpoints, duplicate summation reinforced
them rather than preserving disagreement.

The current implementation drops all nonpositive values (`eigenmaps.cpp:169-178` and
`FoundryExport.cs:754-766`). This prevents the inversion but still removes
contradictory structure from the spectral basis. Keeping signed operator planes in a
later model-export calculation does not restore the evidence law erased from the
basis. The new design instead represents contradiction as competing typed constraints
with provenance and PSD precision for self-adjoint energy, or uses a separately
declared signed/directed construction.

### 5. Disconnected null spaces are diagnosed but accepted under the wrong claim

The solver requests one extra eigenpair and always discards exactly one lowest mode
(`eigenmaps.cpp:94-117`, `142-149`). The source itself explains that a disconnected
graph has multiple near-zero modes, that retained leading columns become component
indicators, and that the implementation only prints a diagnostic while returning
success (`:119-140`).

A generated AImap cannot publish merely because the eigensolver returned success. Its
null-space dimension and requested mode semantics must be declared and certified.

### 6. The post-solve QR changes the mathematical contract without recertification

The normalized-Laplacian solve emits eigenvectors of its declared operator. The code
maps them through degree scaling (`eigenmaps.cpp:142-148`), transposes them, and applies
generic Euclidean QR (`FoundryExport.cs:1108-1121`). It does not afterward prove the
original eigenpair equations, eigenvalue association, generalized mass orthogonality,
or induced operator behavior.

QR can orthonormalize a column span while mixing distinct eigenmodes. A span test is
not an eigenpair certificate. Any numerical post-process must be part of the declared
solver or must re-pass the complete operator contract.

### 7. The reported Procrustes step is a rectangular projection and overwrites modes

The native transform maps a `source_dim` space into four dimensions, applies scale and
translation, and stores a rectangular matrix (`procrustes.cpp:8-15`, `17-60`). This is
not a rigid same-dimensional orthogonal alignment of two AImaps. The caller then
rescales and writes the four projected values into columns zero through three of the
spectral basis while retaining the other columns unchanged
(`FoundryExport.cs:1173-1200`).

Commit `91efb66a` explicitly records that this write destroys the orthonormality
established earlier and that no test then determined its impact. The hybrid result is
not recertified as an eigenspace, a distance-preserving alignment, or a valid coupled
physical/spectral coordinate system.

### 8. Requested target width was imitated rather than derived

The current path caps spectral rank through `BasisRank = 256`
(`FoundryDefaults.cs:19-28`; `FoundryExport.cs:1062-1075`) and fills the remaining
target columns with seeded Gaussian values (`FoundryExport.cs:1215-1223`). The source
comments identify much of that region as unallocated noise rather than calculated
representation (`:1225-1262`).

The earlier history is more direct:

- `e2f65e1e` on 2026-05-29 11:21 UTC requested the full target width, then applied
  Eigenmaps, Gram-Schmidt, a spectral-to-S3 transform, and Gram construction.
- `25f9e4c1`, committed 17 minutes later, capped the solve at 64 modes because 2,049
  requested eigenpairs consumed excessive workspace and did not converge reliably;
  removed the broken buffer-layout Gram-Schmidt call; and cycled the 64 basis columns
  across a 2,048-wide target.
- By `0b4674f2` on 2026-06-16, the current family of post-solve QR,
  spectral-to-four-dimensional overwrite, and target-capacity filling was present.

Repeating columns or adding untyped random columns can produce the requested tensor
shape. It cannot establish the requested rank, representation, operator behavior, or
semantic provenance.

### 9. Material constants replaced measured program contracts

The current defaults hard-code degree 48, metric neighborhood 16, metric probe 64,
corpus cap 200,000, basis rank 256, dense-SVD boundary 6,000, oversample 16, one power
iteration, metric gain 4, coordinate scale 20, and capacity fraction 0.05
(`FoundryDefaults.cs:19-29`, `48-52`). The source does not bind those values to input
shape, error bounds, machine capacity, relation law, evidence standing, held-out
behavior, or a query receipt.

The clean ISA must derive resource shape from the program and machine, and it must
measure any cross-channel scaling against declared units and acceptance behavior.

### 10. Coverage arrived after the high-change pipeline and pins concepts, not closure

The first C# plane-algebra suite was added by `dd7e0c5e` on 2026-08-01. Its own header
states that roughly 1,800 lines of `FoundryExport` and 2,200 lines of
`FoundryCommands` previously had no direct coverage and that another named test used
a stub producing four GGUF signature bytes (`FoundryPlaneAlgebraTests.cs:6-13`).

The added tests prove local list and normalization behavior. They do not prove:

- relation-law preservation through the native boundary;
- exact physicality structural facts independent of testimony;
- direction, arity, field, unit, epoch, or provenance preservation;
- eigenpair and null-space validity after QR and projection;
- target-rank validity after cycling or noise insertion;
- generated model behavior on withheld evidence;
- end-to-end conversation or cross-modal realization.

## Historical change sequence

| Commit | Time | Observed change | Audit implication |
|---|---|---|---|
| `95877bf6` | 2026-05-23 08:21 UTC | Introduced Gram-Schmidt and Laplacian Eigenmaps. | Numerical primitives preceded an invention-level typed operator contract. |
| `275ba224` | 2026-05-23 08:15 UTC | Introduced S3 placement and rectangular spectral-to-4D transform. | Distinct coordinate systems were coupled before alignment semantics were specified. |
| `e8d76772` | 2026-05-27 19:44 UTC | Added sparse graph entry and spectral model-codec path. | COO scalar input became the semantic choke point. |
| `e2f65e1e` | 2026-05-29 11:21 UTC | Wired full-target Eigenmaps, QR-like discipline, 4D projection, and Gram matrices into synthesis. | Shape and pipeline completion were treated as behavioral evidence. |
| `25f9e4c1` | 2026-05-29 11:38 UTC | Capped rank at 64, removed a buffer-scrambling orthogonalization call, and cycled columns to fill target width. | A resource failure produced semantic substitution rather than an explicit unsupported result. |
| `36403c3f` | 2026-06-01 20:48 UTC | Changed the calculation to a normalized Belkin–Niyogi form. | Numerical definition changed after downstream use had begun. |
| `0b4674f2` | 2026-06-16 15:45 UTC | Current basis/post-processing family appears in `FoundryExport`. | Later work accumulated around an unproven basis contract. |
| `91efb66a` | 2026-07-31 22:15 UTC | Removed absolute-value refutation inversion, made QR failure fatal, and added disconnected-graph diagnostics. | Critical semantic defects survived until late in the pipeline and one remains warning-only. |
| `dd7e0c5e` | 2026-08-01 00:13 UTC | Added first C# plane-algebra tests. | Tests followed rather than drove the implementation and preserved known scalar-union limitations. |

## Deliberate-defect acceptance obligations

| New evidence ID | Deliberately broken implementation | Required failure |
|---|---|---|
| `LP-TEST-RELATION-PLANE-TYPE-ERASURE` | Concatenate distinct relation laws into identical COO endpoint/weight triples. | Typed domains, transports, direction, defects, and induced behavior become indistinguishable. |
| `LP-TEST-QUERY-METRIC-FAMILY-DIVERGENCE` | Replace angular, Fréchet, Hausdorff, Karcher-derived, Hilbert, and structural channels with one affinity. | A crafted fixture with disagreeing metric orderings must fail. |
| `LP-TEST-QUERY-STRUCTURAL-NEIGHBOR-NO-TESTIMONY` | Route exact trajectory relations through source trust or consensus. | Changing testimony incorrectly changes exact containment, order, tier, or ancestry. |
| `LP-TEST-EVIDENCE-METRIC-PSD` | Encode contradiction as negative precision in a symmetric spectral solve. | The operator violates the declared energy and solver contract. |
| `LP-TEST-AIMAP-NUMERICAL-CERTIFICATE` | Accept solver return code without independent eigenpair, null-space, and orthogonality checks. | A disconnected or perturbed fixture publishes invalid modes. |
| `LP-TEST-AIMAP-POSTPROCESS-VALIDITY` | Apply Euclidean QR after a generalized solve without recertification. | Eigenpair or mass-orthogonality checks fail. |
| `LP-TEST-AIMAP-PROJECTION-NONALIGNMENT` | Overwrite spectral columns with a rectangular K-to-4 projection. | Eigenspace, distance, or dependent-operator behavior fails. |
| `LP-TEST-AIMAP-RANK-NONCYCLING` | Cycle a lower-rank basis or insert untyped random columns to claim target width. | Rank, covariance, and induced-operator tests fail. |
| `LP-TEST-QUERY-CONTEXTUAL-IMPORTANCE` | Use one permanent part-of-speech importance table. | Queries where function words, negation, ordinals, or exact surface form are decisive fail. |
| `LP-TEST-QUERY-RANK-NONAUTHORITY` | Persist a query-specific scalar as universal similarity. | Recalculation under another goal or epoch corrupts layer separation. |

## Disposition

The historical sources are retained as defect evidence and mathematical experiment
history. The recoverable value is:

- proof that sparse spectral primitives and native numerical dependencies were
  exercised;
- concrete examples of semantic type erasure and invalid numerical post-processing;
- a change sequence showing which resource and convergence problems occurred;
- precise negative controls for the clean implementation.

No historical spectral source, pipeline, constant, test, name, or artifact is accepted
as clean implementation input. The new implementation begins from exact
physicality-derived structure, typed relation algebra, immutable epistemic epochs,
program-scoped neighborhood contracts, generated operator mathematics, independent
numeric certificates, and complete behavioral acceptance.
