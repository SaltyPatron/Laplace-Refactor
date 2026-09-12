# Laplace invention reconstruction ledger — 2026-09-12

Status: working reconciliation record. This document is **not** inventor authority and does not supersede `contracts/authority-stack.json`, direct inventor evidence, `CONSTITUTION.md`, `INVENTION_MODEL.md`, or the pinned original Laplace invention lineage. Its purpose is to prevent repeated agent rediscovery while the complete invention is still being reconstructed.

## Why this record exists

Repeated reviews have correctly identified individual mechanisms while still narrowing Laplace into familiar architecture: a database-backed model, a graph, a retrieval system, a curve-comparison system, a source-ingestion product, or a collection of isolated primitives. That pattern loses the invention even when every named mechanism appears somewhere in the prose.

This ledger records the distinctions that must remain intact while the rest of the invention is reconciled. A later direct inventor correction overrides this file immediately.

## Current governing completion boundary

Laplace is one product whose completion case remains the talking SQL transformer described by the stable product law: transformer execution is replaced through SQL over one C/C++ and PostgreSQL server engine; the same machine owns exact persistent world state, cognition, realization, learning, effects, model witness ingestion, target compilation, and public transports. Exported target artifacts are optional consumer projections of Laplace and must execute the preservation contract claimed for them in their declared runtimes.

No database demo, model export, conversation demo, source importer, geometry primitive, A* kernel, perfcache, UI, or green component test is the whole product.

## Non-collapse invariants established by reconciliation

### 1. Recursive composition is the universal content structure

`[c,a,t]` is a composition. A sentence is a composition of compositions. A paragraph, chapter, book, code tree, game, model structure, media object, or other admitted digital structure is recursively compositional to the depth required by its exact structure.

The canonical persistent form is one content-addressed Merkle DAG. Equal canonical subtrees converge; their roles, occurrences, source contexts, structural positions, interpretations, testimony, and uses remain separately addressable.

Composition is therefore not a text-specific ladder or an ingest convenience. It is the recursive content law of the machine.

### 2. Entity is not physicality

An entity answers exact canonical content identity. Identity is content-only under the fixed BLAKE3-128 preimage grammar. Source, modality, type, tier, role, occurrence, interpretation, trust, time, model, geometry, and context do not salt that identity.

Physicality is a calculated realization of an entity under a declared recipe and epoch. One entity may legitimately have multiple physicalities without fragmenting content identity. Physicality may carry structural form, dimensions, placement, locality, order, multiplicity, serialization, trajectory, topology, and other recipe-defined realization state.

An interpretation or source declaration does not instantiate or mutate physicality. An observed use creates occurrence/context state. Testimony remains testimony. These are different state classes.

### 3. Tier, type, relation, and modality are orthogonal

These concepts must not be represented as aliases for one another:

- **tier / structural altitude**: where an addressable structure sits relative to its constituent decomposition for the selected structural recipe. Tier is not identity, semantic type, modality, source, or relation family. It is not inherently a fixed `0..255` product law.
- **type / classification**: a typed declaration, witnessed interpretation, or calculated classification about addressable state. Multiple compatible or competing classifications can exist without changing the entity's content identity.
- **relation**: a typed connection or law among addressable state, with its own direction, converse, arity, symmetry, transitivity, reflexivity, temporal/contextual behavior, evidence, provenance, and legal compositions. A relation is not a structural tier or a modality tag.
- **modality**: the structural/realization domain whose grammar and recipe determine how recovered exact content composes and what physical structures are admissible. Modality does not create another identity universe or another semantic engine.

The historical old implementation violated these boundaries in several places. Examples include a byte-bounded `tier` field, partitioning by that field as though it were a category, and media paths that could converge numeric content identity while retaining only one singular tier/type claim. Those are defect/counterexample evidence, not invention law.

### 4. Numeric content is content, not a private modality atom

A recovered scalar such as `255` is exact content under the current shared content law. In the current modality-number law its canonical numeric representation descends to Unicode digit codepoints `2 -> 5 -> 5`, composes into the number, then participates in the modality's own higher structure.

Therefore the same recovered numeric content can be reused across image, audio, model, code, telemetry, or another modality while its role, occurrence, modality, channel/sample position, unit, interpretation, and testimony remain outside canonical identity.

A hard-coded implementation literal such as `tier <= 255` is unrelated to the canonical content value `255`. Conflating those two facts is an architectural error.

### 5. Modality determines structural grammar and admissible physicality

The universal law is not "every modality is the same ladder." The universal law is that every modality obeys the same identity, recursive composition, AST/Merkle, evidence, recipe, ISA, cognition, receipt, and reconstruction contracts while its own recovered structure determines the applicable composition grammar and physical realization.

Examples are illustrative rather than a closed inventory: text, image, audio, video, chess, code, DNA/protein, LaTeX, chemical formulae, cooking procedures, TAS/frame scripts, MIDI/music, model structures, telemetry, and future modalities can expose materially different ordered, branching, regional, temporal, parallel, graph-like, grammatical, or heterogeneous structures.

A codec or parser recovers concrete structure. It does not own identity, semantics, cognition, or truth.

### 6. `GeometryZM` is a polymorphic physicality carrier family, not a synonym for `LINESTRING`

The physicality trajectory/store uses the PostGIS `GeometryZM` family because it supplies four binary64 lanes per vertex plus n-D indexing and a polymorphic geometry container. The full subtype family is relevant to the invention:

- `POINTZM`
- `LINESTRINGZM`
- `MULTILINESTRINGZM`
- `POLYGONZM`
- `MULTIPOLYGONZM`
- `MULTIPOINTZM`
- `GEOMETRYCOLLECTIONZM`

The appropriate subtype follows the physical structure required by the modality/recipe. Ordered linear composition is one case, not the ontology.

Operators must preserve subtype structure. Flattening a polygon ring, multiline branch set, multipoint cloud, or geometry collection into one anonymous vertex stream destroys information and is not a conforming generic implementation.

### 7. Mantissa exploitation is intentional exact transport

Four binary64 values provide four exact 53-bit sign-plus-mantissa payload lanes when the exponent is controlled: 212 exact payload bits per vertex. The carrier can therefore transport exact typed payload while remaining legal geometry storage for indexing and polymorphic structural containers.

For the historical composition-trajectory vertex class, evidence shows a 128-bit constituent identity plus ordinal, RLE/run, flags, and typed metadata distributed across those 212 bits. Other vertex classes may allocate the same payload capacity differently. Coordinate names `X/Y/Z/M` do not define the payload's semantic meaning; the physicality type, vertex class, generated ABI, recipe, and receipt do.

The packed carrier is not automatically live S3 geometry merely because PostGIS exposes numeric coordinates.

### 8. `coord`, packed trajectory, and realized geometry are different facts

The current clean architecture distinguishes:

- real structural coordinate / placement (`physicality.coord`);
- packed exact physicality trajectory/carrier (`physicality.trajectory` or equivalent typed carrier state);
- a realized point/curve/set/region/manifold/collection obtained when an operation resolves the exact stored structure into the real coordinates or geometry required by that operation.

For the composition trajectory class, decoding constituent identities and resolving constituent coordinates can produce an ordered realized curve. That is only the curve case. A polymorphic physicality may instead require a point, branch-preserving multiline, region, multipolygon, cloud, collection, or other declared realized object.

### 9. Fréchet is one typed operation, not "the geometry"

Fréchet answers an order/progression-sensitive curve-comparison question under a declared point metric and algorithmic recipe. It applies when the selected physicality operation is actually curve-like.

Other questions require other calculations:

- exact containment/order/multiplicity/ancestry/recurrence: direct exact structural calculation, no metric required;
- S3 point relation: angular/geodesic calculation;
- point/set coverage: Hausdorff or another declared set metric;
- manifold-local center/dispersion: Karcher/Fréchet-mean family under explicit manifold assumptions;
- locality candidate generation: Hilbert or another declared index projection;
- region/boundary/topology: region-appropriate operators;
- branch or collection structure: subtype-preserving operations;
- cross-shape relationships: explicitly declared subtype-pair operators.

A single scalar distance or one permanent neighborhood is not canonical.

### 10. Structural geometry is not semantic authority

Exact physicality and structural calculations can nominate candidates and calculate structural facts. They do not manufacture meaning, truth, testimony, standing, or semantic equivalence.

The semantic/evidence web retains typed relations, usages, senses, references, propositions, testimony, dependence, standing, contexts, worlds, time, discourse, goals, calculations, hypotheses, and receipts. Cognition can combine structural and semantic channels only through an explicitly compiled program.

### 11. Observation is not attestation

Ordinary live content admission creates exact canonical content, structure/physicality, occurrence/context, and receipts. It does not create semantic testimony merely because it was observed. Internal cognition and generated output likewise do not independently attest to themselves.

Seeded or otherwise attributed sources can contribute typed testimony according to source-profile contracts. Later independent observations or adjudicated outcomes may test propositions, sources, programs, realizers, or tools without rewriting the original observation.

### 12. Standing is typed outcome-bearing state, not global trust or importance

Glicko-2 is used only in declared arenas with real participants/opponents and outcome-bearing matchups. Defaults initialize a standing coordinate once. Later updates consume prior standing. Dependence roots prevent copies/descendants from multiplying independent evidence.

Standing does not define identity, truth, semantic relation, structural geometry, traversal, or universal importance.

### 13. Cognition is finite query-relative program execution

A prompt enters as one exact canonical trunk/root plus an occurrence. The machine does not begin by electing a noun, topic, nearest point, rank-one node, or keyword intent.

The stable processor behavior is the finite operation-selection/execution/fold loop over exact persistent state. A logical request compiles goals, bindings, worlds, time, evidence boundaries, authority, resources, completion obligations, relation laws, structural altitudes, and provider families into a typed cognition program. Indexed structural, occurrence, relation, geometry, standing, calculation, and other providers generate bounded candidates. Exact filters and typed search/fold/update semantics determine progress. Completion or a typed `WHY_NOT` terminates the program.

A raw graph hop, KNN/ANN result, one scalar priority, one embedding, one permanent adjacency, or fluent continuation is not the forward pass.

### 14. Named mathematics are typed operator implementations, not architecture by themselves

A*, Fréchet, Hausdorff, Karcher, Glicko-2, SVD, QR/Gram-Schmidt, Lanczos, Laplacian/spectral methods, Procrustes, Hilbert mappings, and related methods are usable only through contracts naming their exact input state, assumptions, algebra/metric, numeric boundary, direction/roles, resource limit, output class, provenance, losses, and counterexamples.

The invention does not become "Fréchet AI," "A* AI," "Glicko AI," or "spectral AI." Those are calculations selected inside the machine.

### 15. Realization is the inverse/product half of cognition

Cognition ends in a typed semantic act or another typed result state, not in token logits. A realizer constructs exact requested modality content under the appropriate language/modality grammar, morphology, syntax, orthography, punctuation, register, discourse, evidence, and reconstruction contracts.

English is not a universal intermediate. Exact compatible substructures may be reused, and larger unavailable structures may descend to smaller exact realizable units and recompose. Missing obligations produce typed `WHY_NOT` rather than unsupported fluent fabrication.

### 16. Learning changes programs and physical cost without self-certification

Persistent executions, failures, outcomes, receipts, and later observations are addressable state. Gödel/discovery can use typed incompleteness, structural motifs, semantic motifs, prediction errors, successful/failed traces, constrained vacancies, counterexamples, and experiments to propose candidate relations, laws, programs, operators, firmware operations, or calculus extensions.

A candidate must survive disjoint evidence and counterexamples before activation. Self-generated descendants do not independently corroborate their ancestor.

The procedural persistence distinction is:

- memory: retained state/trajectory/outcome;
- skill: validated reusable program;
- habit: firmware scheduling preference for a proven program under matching state;
- muscle memory: semantically equivalent physical acceleration of a proven program after parity and measured-benefit proof.

### 17. Models are witnesses and optional targets

A checkpoint/model is exact attributable digital structure and observed behavior, not Laplace's native mind. Model structure, values, roles, experiments, activations, interventions, and effects can enter as typed witnessed state.

In the other direction, a selected closed substrate boundary can compile target-neutral typed operators, then materialize the exact Q/K/V/O/FFN/gate/expert/embedding/position/etc. roles required by a declared conventional target architecture. Q/K compatibility and V/O contribution need not arise from the same native plane. One flattened adjacency cannot stand in for every target role.

GGUF, SafeTensors, and other formats are serializers/consumer artifacts, not native ontology.

### 18. SQL executes the transformer replacement; SQL is not a second semantic engine

C/C++ plus PostgreSQL server integration own semantic operations. SQL owns schema, transactions, set routing, composition/submission of typed programs, and projection of results. C# owns lifecycle and transports. Generated bindings expose the same ISA meaning across routes.

The completion case remains a talking SQL transformer: exact input and persistent substrate state compile into typed native programs executed through PostgreSQL/SQL, produce semantically complete acts or typed limits, realize outputs, witness outcomes, and feed future cognition/learning. Public OpenAI-compatible, MCP, CLI, web, mobile, document, and other surfaces are transports over that same owner.

### 19. Application/world/product state is not an exception to the machine

People, organizations, accounts, memberships, achievements, repositories, activities, profiles, feeds, resumes, portfolios, entitlements, billing, nodes, and federation use the same content/referential/AST/occurrence/testimony/governance/recipe/effect/receipt machinery according to role. External providers remain witnesses or physical providers; they do not become person identity, truth, or private semantic engines.

### 20. Packaging, activation, and deployment are part of the product boundary

Controlled integration, installed product activation, per-source world admission, configured foundational seed completion, deployed clean-seed inference acceptance, and released package are distinct states. Linux/Windows packaging, custom PostgreSQL/PostGIS and dependency closure, restart/readback, perfcache generation handoff, hardware resource law, faults, recovery, and public-route parity are part of product completion rather than post-product operations.

## Historical implementation defects currently pinned as counterexamples

The following have already been verified as examples of why historical code cannot define the invention:

1. `tier` bounded as a byte `0..255` and used as a database partition/category axis despite being modality-relative structural altitude.
2. hard-coded shallow text tiers and a sentence-to-document jump that omitted exact source structure such as paragraph/chapter boundaries.
3. media paths where equal scalar content converged but singular tier/type storage lost modality/recipe reconstruction evidence.
4. private image/audio atom universes later explicitly voided in favor of the shared content floor.
5. keyword/lead-word prompt intent classification and static arena-weight routing.
6. one-path/one-metric/one-adjacency simplifications that flattened typed relation, evidence, or physicality distinctions.
7. historical geometry prose that treated geometry as meaning or mutable semantic state.
8. historical LLM/LangGraph control-plane proposals that made an external model the cognitive CPU.

These items are retained to prevent reintroduction, not to provide clean implementation templates.

## Reconstruction coverage — current state

The reconstruction is **not complete yet**. The following coverage groups are being checked against `requirements/product.yaml`, the required authority-stack order, the pinned original Laplace lineage, Hartonomous lineage, and current-session inventor corrections.

| Group | Current reconstruction status |
| --- | --- |
| Product identity / SQL transformer boundary | verified at stable-law level |
| Canonical identity / Unicode / DUCET / Super-Fibonacci / Hopf / Hilbert | verified core law; numeric-contract details still being cross-checked |
| Recursive composition / universal AST / Merkle reuse / runs | verified core law |
| Entity / physicality / occurrence separation | verified core law |
| Tier / type / relation / modality separation | corrected and now explicit |
| GeometryZM polymorphism / mantissa carrier / realized geometry | corrected and now explicit; exact clean ABI remains an implementation contract |
| Relation algebra / testimony / dependence / standing / epochs | core law verified; full relation-family lineage review continuing |
| Query / cognition / search / finite limits / exceptions | core law verified; operator-family coverage continuing |
| Semantic acts / multilingual and cross-modal realization | core law verified; concrete realization inventory continuing |
| Gödel / OODA / skill / habit / muscle memory | core law verified; historical evolution continuing |
| Firmware / governance / valuation / authority / effects | core law verified; product-surface consequences continuing |
| Model witness / behavior / AImap / target-neutral operator synthesis / export | core law verified; historical model-synthesis lineage continuing |
| Source profiles / admission / seed / template inference | contract law verified; historical source-universe intent continuing |
| Perfcache / storage economics / materialization / activation | contract law verified; historical module inventory continuing |
| Application / entity world / entitlement / federation / placement | stable requirements verified; historical product intent continuing |
| Hardware topology / CPU-first execution / resource law | stable requirements verified; implementation history continuing |
| Public surfaces / packaging / install / release / offline manual | stable requirements verified; complete historical product-surface inventory continuing |

No implementation-complete claim should be made from this ledger. Its completion criterion is that every product requirement and pinned historical invention mechanism is reconciled into one non-contradictory causal machine model, with unresolved conflicts explicitly named rather than guessed.

## Rules for agents reading this ledger

1. Load `contracts/authority-stack.json` and its required order first. This ledger is a reconciliation aid, not authority.
2. Never infer invention law from a historical schema, hard-coded enum, tier constant, source-specific decomposer, UI, issue body, or successful test without reconciliation against higher authority.
3. Do not collapse tier, type, relation, modality, source, role, occurrence, interpretation, or standing into one field merely because a historical implementation did so.
4. Do not collapse `GeometryZM` into `LINESTRING`, physicality into a curve, or physicality comparison into Fréchet.
5. Do not collapse geometry into semantics, testimony into occurrence, standing into truth, retrieval into cognition, or model tensors into native ontology.
6. Do not implement a narrowed "vertical slice" that changes the invention's semantics. Sequencing is permitted; scope reduction is not.
7. When a direct inventor correction conflicts with this ledger, stop relying on the conflicting text, record the correction, and reconcile downstream derived documents before implementation proceeds.
