# Target operator and tensor synthesis from typed substrate state

Status: inventor-direct architecture clarification, 2026-09-02. This document is additive to `NATIVE_COGNITION_MATHEMATICS.md`, `OBSERVATION_TO_OPERATOR.md`, `PROMPT_TRUNK_COGNITIVE_MICROCYCLE_AND_PROCEDURAL_MEMORY.md`, #16, #20, #110, #129, #132, #169 and #182. It records why earlier GGUF/model-generation work failed when distinct substrate planes were flattened, and specifies how target tensors/operators must instead be generated from the state Laplace already has.

## 1. The earlier GGUF failure class

The failure was not simply "too few layers" or "not enough heads". Adding target capacity while feeding every target slot from one flattened adjacency/similarity/embedding plane reproduces the same information at greater shape.

Laplace has many state classes that may reference the same canonical content but are not interchangeable:

```text
canonical content identity
physicality.coord / structural geometry
packed physicality.trajectory / ordinal / RLE / metadata
realized coordinate trajectories
occurrences and enclosing containers
contains / precedes / follows / recurrence / gap
seeded lexical/semantic/standards facts
source-specific testimony
relation type / direction / role / arity
source / source-type / model / context trust
root-dependence / contradiction / uncertainty
Glicko-2 standing and RD/volatility in typed lanes
deterministic calculations
active discourse / goal / obligations
query-relative operator state
```

If these are first collapsed to one global graph, scalar score, embedding, or matrix, no later increase in GGUF layers/heads can reconstruct the distinctions that were destroyed before tensor generation.

The governing rule is therefore:

> **Generate the target operators from the typed substrate planes; do not flatten the substrate and then try to recover semantics from tensor shape.**

GGUF, SafeTensors and similar formats are codecs for already-generated target state. They are not the algorithm that decides what the tensors mean.

## 2. "Exact dot product" means exact under a declared feature/operator recipe

Laplace already has enough explicit addressable state to construct sparse vectors and operators without first learning one dense embedding.

For a selected job `j`, let an entity/state `i` expose a sparse feature map

```text
x_i^(j) : typed_feature_key -> value
```

where keys may represent only the planes admitted by that job, for example:

- exact container/ordinal/gap/trajectory features;
- selected relation-family incidence;
- selected source/evidence roots;
- selected language/sense/context state;
- deterministic calculated features;
- typed standing/uncertainty values;
- structural geometry coordinates or basis coefficients.

If the sparse feature keys are canonically ordered, a dot product can be calculated by a merge/intersection over the two sorted lists:

```text
<x_i, x_k> = sum over keys present in both vectors of x_i[key] * x_k[key]
```

The arithmetic result is exact to the declared numeric representation and exact feature set. It does **not** become universal semantic truth merely because the arithmetic is exact. Its meaning is the meaning of the recipe that selected the features, units, evidence boundary and weights.

When the selected relation/evidence plane has a declared precision/metric `G_j`, use the typed weighted inner product

```text
<x_i, x_k>_G = x_i^T G_j x_k
```

rather than silently folding source trust, relation importance, frequency and geometry into an unexplained scalar.

This directly supports exact ranked lists, sparse compatibility operators, Gram matrices and target pair operators on CPU. A GPU may accelerate dense/batched execution but is not required by the mathematics.

## 3. Source provenance and trust are typed evidence, not one global hierarchy

Source provenance can legitimately initialize different priors, but source type cannot become permanent truth.

Examples relevant to the seeded lexical estate:

- Princeton WordNet is a lexicographer-built lexical database originating at Princeton and supplies explicit sense/synset/semantic-relation structure.
- Open Multilingual Wordnet is an aggregation/linking framework over many independently created wordnets; its individual component projects have different provenance and must retain their own source identities. Older OMW data also contains automatically extracted multilingual data, so `OMW` cannot be treated as one uniform trust scalar.
- Wiktionary is collaboratively edited and can be changed by community contributors. Its open-edit provenance is different from a Princeton lexicographer release, but a particular cited Wiktionary entry can still earn useful standing from later corroboration.
- a UserPrompt is exact high-authority evidence that the user produced that prompt in that context. It is generally weak evidence of an external-world proposition merely because the user typed it. For user intent, preference, instruction or self-report inside the user's authorized world scope, that same observation may be the directly relevant primary evidence.

Accordingly, do **not** encode a permanent total order such as:

```text
WordNet > OMW > Wiktionary > UserPrompt
```

as universal truth.

Instead preserve at least:

```text
source identity
source type
relation/fact family
assertion/observation kind
context/world/language/domain
root dependence
initial prior/default
current earned source standing
current relation/source-context standing
RD / volatility / evidence epoch
```

A high-quality source can be excellent for one relation family and poor or silent for another. OMW's aggregated wordnets can carry the standing of their original projects rather than inheriting one undifferentiated OMW score.

`content trust` must likewise not be salted into canonical content identity. If a product wants a content- or proposition-level standing, it is a query/evidence-derived view over testimony and roots, not a permanent property of the BLAKE3 entity.

## 4. Laplace-generated state may witness itself but may not manufacture independent authority

Laplace must be able to observe and reason over its own execution, receipts, generated text, code, model artifacts and predictions. That is required for Gödel discovery, debugging, self-correction and procedural learning.

But self-generation cannot create a new independent evidentiary root for the proposition that generated it.

```text
external/primary observation A
    -> Laplace derivation B
    -> Laplace explanation C
    -> Laplace summary D

independent roots supporting the lineage remain {A}, not {A,B,C,D}
```

Laplace can make an attributable **derived claim** and can witness facts about its own internal execution (for example, which provider ran, what rows were read, what program produced an artifact). Whether Laplace-generated prose should be admitted as ordinary external-world `attestation` is a separate policy decision and must not bypass dependence accounting.

The enforceable safety boundary is therefore not "Laplace may never reason better than a human." Raw reasoning capability is not capped by a source-trust scalar. The enforceable epistemic rule is:

> Laplace cannot bootstrap independent authority by repeatedly citing, transforming, or agreeing with its own descendants.

External observations, deterministic formal checks, or independently rooted evidence can provide later return-leg outcomes that change the standing of a Laplace program/source lane.

## 5. S3 is a structural anchor; generated N-dimensional spaces are calculated views

`physicality.coord` provides the real four-component structural geometry under the selected physicality recipe. Tier-0 atoms lie on the S3/glome; higher compositions are real four-dimensional centroids and may lie inside the glome.

That four-dimensional state can serve as an anchor, gauge/reference plane, initialization, structural feature family, or alignment reference for generated spaces. It is not necessary to pretend that every target embedding is literally the S3 coordinate.

A selected target/job may generate an `n`-dimensional coordinate basis from the relevant typed operator. For a self-adjoint positive-semidefinite slice, a representative path is:

```text
selected typed incidence δ_r
+ evidence/precision metric G_r
    -> L_r = δ_r* G_r δ_r
    -> sparse eigensolve / Lanczos
    -> selected non-null spectral basis U_k
```

This is Laplacian-eigenmap/spectral-basis territory when the declared operator satisfies those assumptions. Directional, causal, signed, irreversible or otherwise non-self-adjoint relation families must retain an appropriate different operator/factorization instead of being forced through a symmetric Laplacian.

The other named tools have distinct jobs:

- **Lanczos**: obtain selected eigenpairs/singular subspaces efficiently from large sparse operators without dense world materialization;
- **Gram-Schmidt / QR-class orthonormalization**: enforce a declared orthonormal basis when combining/augmenting compatible coordinate channels;
- **SVD**: factor a selected rectangular or asymmetric compatibility/contribution operator and expose rank/loss explicitly;
- **Procrustes alignment**: align compatible coordinate spaces across scopes/epochs/models where a declared correspondence set exists, resolving gauge/rotation/reflection choices under that contract;
- **Laplacian Eigenmaps / spectral embedding**: generate coordinates from selected neighborhood/operator structure; the graph/operator and its evidence weighting must be typed and scoped rather than universal.

These calculations do not all perform the same "morph." Their combination is recipe-owned. A target compiler may start from or include S3 structural coordinates, append spectral/semantic coordinates, orthonormalize a chosen basis, align it to an earlier generation on shared anchors, and factor it to the target dimensionality, but every transformation and loss must be receipted.

## 6. Generate target pair operators first; factor them only when the target requires tensors

The clean mental model for transformer-family output is not:

```text
make embedding -> copy embedding into lots of heads -> hope behavior appears
```

It is:

```text
select typed substrate job
-> generate the operator the target head/layer must perform
-> factor/materialize that operator into the target's required parameterization
```

For head/layer job `h`, generate a target-neutral operator such as:

```text
M_h(i,j) = declared compatibility/transport/contribution law
           over selected structural + semantic + evidence state
```

Examples can include separate operators for:

- containment/ordinal/trajectory compatibility;
- lexical sense/taxonomy;
- semantic role;
- causality/direction;
- translation/cross-language correspondence;
- discourse/reference;
- source/provenance/evidence routing;
- observed continuation;
- deterministic domain calculations;
- selected geometry/trajectory similarity.

If a target architecture requires `Q_h K_h^T`, factor the **specific** compatibility operator `M_h` into Q/K factors. For a finite matrix and declared dimension `d`, truncated SVD or another deterministic factorization can produce the best approximation under the chosen norm; if `rank(M_h) <= d`, an exact factorization may be possible under the selected numeric contract. If target rank is insufficient, the compiler records the residual/loss or rejects a claimed preservation guarantee.

Do not reuse Q/K automatically for V/O. A contribution/transport operator may be different:

```text
C_h != M_h
```

and target V/O factors must be generated from the operator they are supposed to implement. A mutant `VO := QK` is precisely the flattening failure.

FFN/gates/experts similarly come from declared transformation/completion/routing programs, not random filler inserted because the target metadata requires another tensor.

## 7. Heads and layers are generated jobs over the substrate estate

The large number of possible heads/layers comes from the combinatorics of the admitted world, not from a hardcoded small matrix list.

A target recipe can allocate independent jobs by combinations such as:

```text
tier / altitude
relation family
relation direction / role / arity
trajectory scale / gap band
language / grammar / sense
source / provenance / dependence class
evidence epoch / standing lane
world / time / discourse
geometry / structural metric family
deterministic calculation family
goal / firmware / target task
```

This yields a potentially enormous set of meaningful candidate operators while the active target contains only the subset its architecture/capacity/recipe selects.

The target layer schedule is therefore generated from the data and selected behavior contract. More layers or heads are useful only if they receive distinct typed jobs. Repeating one flattened plane into more slots is not additional cognition.

## 8. CPU-first construction is a correctness property; acceleration is optional

Nothing in the core construction requires a GPU as semantic authority:

- sorted sparse dot products and intersections are CPU operations;
- incidence/operator generation is sparse/set-wise;
- Lanczos is designed for large sparse eigenproblems;
- QR/Gram-Schmidt, SVD and Procrustes all have CPU implementations;
- PostgreSQL/PostGIS indexes can produce bounded candidate sets;
- generated dense tensors can be streamed/materialized in bounded blocks.

A GPU, SIMD width, accelerator, or distributed provider can reduce elapsed time. The logical operator, selected evidence boundary, target tensor bytes within the declared numeric/quantization contract, and provenance receipt must not depend on the accelerator being semantic authority.

## 9. Required target-generation receipt

Every generated target tensor/operator must be traceable to:

```text
target architecture + slot/head/layer/expert id
logical operator/program id
canonical entity/physicality/trajectory scope
relation/calculation families
occurrence/source/world/time/language/context scope
evidence and dependence roots
standing recipe + epoch + RD/volatility where used
source/source-type priors actually consumed
structural S3/basis inputs where used
spectral/factorization/alignment recipe
rank/dimension/null-space/gauge choices
numeric precision / quantization
residual / declared information loss
CPU/GPU/provider implementation identity
materialization cost and deterministic seed/tie law where applicable
```

A tensor that has the correct shape but no such ancestry is filler, not generated Laplace cognition.

## 10. Acceptance

Acceptance must prove at least:

- a sparse sorted-list fixture computes independently checked exact dot products without dense embeddings;
- source/evidence weighting changes the typed weighted operator without mutating canonical identity or exact occurrence structure;
- Princeton WordNet, two distinct OMW component projects, Wiktionary and UserPrompt observations remain distinct source/root classes rather than one source-type scalar;
- a copied/derived/self-generated chain contributes no extra independent root support;
- at least two target heads/layers are generated from materially different typed operator planes;
- Q/K and V/O can be generated from different operators;
- an S3 structural anchor and a separately generated spectral/semantic basis remain distinguishable and traceable;
- Lanczos/orthonormalization/SVD/Procrustes each appear only where their declared mathematical contract applies;
- target rank insufficiency produces an explicit residual/loss or failure rather than random/shape filler;
- increasing layer/head count while feeding one flattened operator fails the non-flattening behavior fixture;
- CPU-only generation produces the declared target-neutral operators/artifact; optional acceleration preserves semantics;
- GGUF serialization consumes generated target tensors and cannot invent missing tensor semantics;
- held-out behavior distinguishes a typed multi-plane construction from the historical flattened GGUF negative control.

## 11. Ownership

- #16: testimony, root dependence, source/relation/context trust and return legs;
- #110: typed Glicko-2 standing lifecycle and numerical integrity;
- #132: observation/fact/calculation planes feeding live operators;
- #20: model-independence and target-compilation epic;
- #129: concrete substrate-scope -> target operator/tensor compiler;
- #169: procedural discovery may eventually discover improved operator-generation/physical-plan recipes;
- #182: prompt-root cognition selects the live operators that target compilation may later materialize;
- old `SaltyPatron/Laplace#928`: historical source-scoped GGUF/model-export evidence and counterexamples.

This document does not replace any of those owners. It makes explicit the missing bridge: **the tensors, weights, embeddings, heads and layers are calculated products of the typed substrate state, not arbitrary parameters that must be trained or filled after the substrate has already been flattened.**
