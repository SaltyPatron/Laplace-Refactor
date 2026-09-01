# Substrate model compilation

## Status

This document captures the clean-product contract for compiling conventional model
artifacts from selected Laplace substrate state. It is intentionally separate from
model ingestion: imported checkpoints are witnesses; exported models are generated
consumer programs.

Primary implementation owners: #20, #61, #129.

## Native substrate versus consumer model

Laplace's native representation is not a transformer checkpoint. The substrate is the
content-addressed world state composed from canonical entities, physicality trajectories,
occurrences, testimony, calculated relations, consensus/standing epochs, and derived
operators.

A conventional model is a **compiled projection** of a selected, closed slice of that
state.

```text
canonical substrate + scope + recipe + target contract
    -> generated operator plan
    -> target-neutral model package
    -> GGUF / SafeTensors / other target codec
```

Changing the scope can produce another model without retraining or duplicating the
substrate.

## Scope is first-class

A target compilation request can constrain, as applicable:

- world/evidence epoch;
- source and dependence roots;
- valid and observation time;
- person/player/organization/domain/game/model scope;
- language and modality scope;
- canonical composition tiers / structural altitudes;
- physicality/trajectory families and ordinal/gap/recurrence laws;
- relation families, direction, arity, converse and composition laws;
- typed standing lanes including rating, deviation, volatility and epoch;
- target architecture, layer/head/expert schedule, precision, quantization and layout;
- preservation obligations and allowed losses.

This enables, for example, two exports over the same chess substrate:

```text
scope = Alekhine + Fischer
scope = Carlsen + Nakamura
```

The canonical boards, moves, positions, trajectories and shared evidence do not get
copied into separate native models. The compilation program selects different eligible
witnessed state and generates different target operators.

The same law applies to era-, source-, language-, domain-, firmware-, personality-,
organization-, or task-scoped targets.

## Same content still means same hash

Target compilation must never salt canonical identity with source, player, time, model,
layer, target architecture, or export recipe.

Repeated content and structure converge before model materialization. Different target
recipes may refer to the same canonical content while producing different consumer
slots because the selected relation/evidence/operator program differs.

This separation is what allows:

```text
one canonical entity
many occurrences
many witnessed contexts
many standing lanes
many target projections
```

without losing provenance or multiplying the content itself.

## Physicality trajectories are model inputs

Physicality is not only a storage optimization. It carries exact ordered structure that
can be compiled into consumer sequence/position/routing operators.

Eligible trajectory information includes:

- constituent order;
- ordinals and gaps;
- multiplicity and run length;
- recurrence and repeated visits;
- container hierarchy;
- point/curve/set/manifold geometry;
- temporal coordinates;
- packed exact payload channels;
- convergent/transposed path structure;
- prefix and subtrajectory reuse.

A target may emit conventional positional encodings for compatibility, but those are
projections of the selected trajectory laws rather than native authority.

## Transformer slots are consumer roles

For transformer-family targets, `Q`, `K`, `V`, `O`, FFN, gates, experts, embeddings,
normalization, positional encoding and output projection are target execution roles.
They are not the ontology of Laplace.

### Q/K

Q/K roles are generated from the compatibility/routing operators assigned by the
recipe. Candidate inputs may include structural incidence, containment, semantic role,
causal or temporal direction, geometry, trajectory compatibility, context, evidence,
and typed standing.

A single global similarity matrix is not a universal Q/K answer.

### V/O

V/O roles carry contribution, transport or update behavior and may use different typed
operator planes from Q/K. The target compiler must not assume `VO := QK` or recycle one
flattened adjacency for every tensor role.

### FFN / gates / routing

These roles are generated from declared transformation, completion, residual,
conditional, mixture, routing, or other substrate operators required by the target.

### Embeddings

Embeddings are compatibility surfaces for a consumer runtime. They never replace
canonical content identity or become the native semantic store.

## Multi-tier and multi-layer scheduling

The substrate exposes reusable structure at many altitudes. A target recipe may map
those altitudes and relation families differently across layers, heads and experts.

Illustrative only:

```text
lower layers  <- local constituent / ordinal / lexical / short-trajectory structure
middle layers <- phrase / sentence / AST / motif / semantic / causal operators
upper layers  <- discourse / document / world / evidence / goal-conditioned operators
heads         <- distinct relation or trajectory bands
experts       <- context / modality / domain / evidence scopes
```

There is no one-tier-per-layer requirement. The schedule is a deterministic compiler
choice whose inputs, losses, assignment and generated tensors are receipted.

## Reuse before dense materialization

The compiler must exploit the substrate's deduplication before filling target arrays:

- Merkle-DAG subtree reuse;
- same-content/same-hash convergence;
- run-length reuse;
- repeated-constituent reuse;
- trajectory prefix/subpath reuse;
- transposition/convergent-state reuse;
- typed operator memoization at a closed epoch;
- indexed candidate generation;
- sparse, banded or effective-rank operator materialization before dense consumer layout
  where the target permits it.

The target may require a dense tensor, but repeated native evidence does not justify
repeating the semantic calculation independently for every dense slot.

## `O(tier)` is a proof obligation

The architecture intends tier-bounded and reuse-sensitive work where the content and
physicality laws permit it. The implementation must measure rather than merely claim
that behavior.

A compilation receipt must report at least:

- selected structural altitude/tier ranges;
- unique canonical entities/compositions visited;
- trajectory vertices/subpaths visited and reused;
- relation/consensus cells visited;
- witness/occurrence volume represented;
- generated sparse/operator rank;
- target tensor slots materialized;
- dedup/reuse ratio;
- CPU, memory, I/O, database rows and elapsed time.

Repeated witnesses must not linearly multiply canonical composition work.

## Live learning versus frozen export

Laplace may continue admitting and folding new evidence while serving queries.

A compiled artifact binds a closed world/evidence epoch:

```text
artifact_A = compile(epoch=t, scope=A, recipe=R)
artifact_B = compile(epoch=t+1, scope=A, recipe=R)
```

`artifact_A` remains reproducible. New eligible evidence can change `artifact_B`
without mutating the earlier artifact and without retraining the substrate.

The compiler may perform scoped recompilation when its dependency receipt proves that
only a bounded operator region changed. Otherwise it must explain why whole-target
recompilation is required.

## Provenance to every generated role

Every generated tensor/slot/operator must trace back, as applicable, to:

- canonical content ids;
- physicality/trajectory ids and recipe;
- calculated structural relations;
- witnessed semantic relations and dependence roots;
- selected evidence/consensus/standing epoch;
- target operator key and layer/head/expert assignment;
- factorization/gauge/sign/order program;
- precision/quantization/layout transform;
- final serialized byte range.

A model that merely loads is not proven correct.

## Negative controls

Acceptance must deliberately reject:

- one flattened adjacency copied into every layer/head;
- Q/K and V/O forced to one plane;
- random or Gaussian filler presented as learned capacity;
- target shape used as semantic rank authority;
- source/time/occurrence salted into canonical identity;
- checkpoint tensor copy presented as substrate-generated knowledge;
- unreceipted conventional head schedules or positional defaults;
- repeated witnesses causing repeated canonical work;
- shape/load/correlation-only acceptance without behavioral or named-invariant proof.

## Implementation ownership

- #7 — canonical identity/composition/physicality trajectory persistence
- #16 — testimony, lineage, adjudication and immutable evidence epochs
- #17 — typed operators, indexing and finite query programs
- #20 — model ingestion/AImap/target compilation epic
- #61 — named target invariant/nonflattening acceptance
- #110 — typed standing
- #129 — substrate-scope/operator compiler implementation
