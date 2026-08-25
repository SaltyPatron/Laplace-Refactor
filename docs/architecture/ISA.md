# Substrate instruction set architecture

## Purpose

The substrate ISA is the only semantic execution contract in Laplace. It replaces
private feature implementations with typed programs executed by the native engine.
SQL composes and submits programs. C# supplies lifecycle, scope, and transport.

## Program properties

Every program declares:

- ISA major and minor version;
- instruction sequence;
- typed input and result registers;
- source, type, trust, context, and authorization scope;
- identity, Unicode, geometry, relation, firmware, and perfcache activation epochs;
- deterministic seed material when an instruction requires it;
- resource bounds derived from input shape and machine capacity;
- transaction and visibility requirements;
- receipt detail level;
- exact error behavior.

A program is validated completely before execution changes persistent state.

## Value system

The ISA is vector-first. Required value classes include:

- canonical byte spans, Unicode codepoint-position vectors, and encoded media blocks;
- content identities and identity vectors;
- typed scalar, fixed-point, coordinate, and ordinal vectors;
- ordered trajectories, logical run spans, sparse deposition sets, and unordered
  collections;
- S3 atom points, four-dimensional centroids, radii, Hilbert keys, packed trajectory
  vertices, and realized coordinate curves;
- versioned physicality realizations and occurrence/context references;
- relation, source, type, trust, and context identities;
- testimony vectors, epistemic kinds, derivation DAGs, dependence graphs, and root
  observation sets;
- consensus epoch descriptors and derived-state publication boundaries;
- temporal intervals, world scopes, referential identities, and epistemic states;
- goals, discourse frames, hypotheses, counterfactual states, and plans;
- relation-law, motif, cognition-program, firmware, and valuation values;
- typed neighborhood components, metric-family descriptors, query-ranking programs,
  partial orders, Pareto sets, path costs, and neighborhood receipts;
- typed search states, terminal predicates, transition-family registries, frontier
  batches, accumulated and remaining-cost components, duplicate/reopen policies, and
  search-completion certificates;
- search-priority laws, state-resource and dominance vectors, requested path-count and
  diversity contracts, typed transition receipts, and frontier-expansion plan receipts;
- persistent-state references;
- candidate and evidence sets;
- instruction subprograms;
- model structures, target-runtime values, and effect envelopes;
- materialization blocks;
- execution receipts and typed errors.

Null, unknown, unsupported, refuted, and empty are different states.

## Instruction families

The version-one registry covers these complete semantic families. Concrete encodings,
operand schemas, effects, and errors live in the generated machine contract:

1. **Unicode atom frame** — validate codepoint positions, compute complete DUCET keys,
   resolve rank, and address the active S3 atom plane.
2. **Content** — canonicalize, identify with BLAKE3-128, compare, and retrieve exact
   content.
3. **Structure** — decompose, compose, address constituents, consume run spans, apply
   sparse deposition, and construct trajectories and Merkle DAG nodes.
4. **Physicality** — place atoms, accumulate arithmetic centroids, preserve radius,
   encode Hilbert locality, pack trajectory metadata, realize curves, reconstruct the
   trunk-to-leaf Merkle DAG, calculate its exact structural relations, and calculate
   typed point, curve, set, and manifold distances.
5. **Perfcache** — validate module contracts, bind a coherent epoch, perform batch
   lookups, compare canonical results, and report exact loaded artifacts.
6. **Testimony** — witness positive, negative, uncertain, absent, and unknown evidence;
   distinguish witnessed, copied, derived, independently corroborated, and currently
   believed state; apply independently typed trust and preserve source scope.
7. **Adjudication** — compare testimony, execute matchups and rating updates, and
   calculate immutable standing epochs without deleting testimony; preserve derivation
   lineage, dependence, root-observation identity, and as-of query semantics.
8. **Relation algebra** — validate direction, converse, symmetry, transitivity,
   reflexivity, arity, time, context, confidence propagation, and legal compositions.
9. **Addressing** — resolve content and referential identities, construct query/key
   relations, and retrieve value sets through exact indexes.
10. **Traversal and correlation** — traverse relation and physical structure in
    bounded bulk steps; calculate typed structural, angular, radial, Hilbert,
    Fréchet, Hausdorff, Karcher-derived, semantic, and epistemic neighborhood
    components; execute declared contextual ranking programs; and compare evidence
    across languages, modalities, sources, models, times, and worlds. Every scalar or
    selected neighborhood retains its component, normalization, comparison, tie,
    Glicko-2, trust, context, and epoch receipt.
11. **Goal and executive** — establish goals; compile indexed A-star or declared
    best-first state searches; generate bounded frontier expansions; calculate typed
    accumulated and remaining costs; apply canonical duplicate, reopen, pruning, and
    tie rules; execute requested path multiplicity; retain depth and other completion
    resources in dominance; preserve typed transition receipts; name priority semantics
    exactly; rank work; allocate resources; update active state; shift strategy; inhibit
    invalid candidates; and determine exact completion.
12. **Working and discourse state** — bind turns, speakers, references, propositions,
    unresolved questions, salient trajectories, corrections, and artifacts.
13. **Hypothesis and counterfactual** — create typed hypotheses, isolate assumed worlds,
    predict observations, search contradictory evidence, and resolve status.
14. **Motif and Godel operations** — discover recurring typed structures; calculate
    constrained vacancies and their predicted signatures; classify missing entities,
    physicalities, relations, families, motifs, laws, operators, firmware operations,
    and cognition programs; generate candidate occupants; partition fitting and
    evaluation evidence; search counterexamples; execute experiment programs; score
    explanatory and predictive utility; and publish versioned accepted extensions.
15. **Generated operator mathematics** — compile logical cognition programs, generate
    typed incidence and transport applications, calculate evidence metrics, apply
    matrix-free operator families, solve constrained fields, certify numerical results,
    and generate receipt-bound spectral AImaps.
16. **Typed defects and innovation** — calculate solver, relation, boundary, motif,
    contradiction, innovation, and counterfactual defects without interchanging their
    semantics; evaluate missing-relation hypotheses against pinned withheld evidence.
17. **Firmware and valuation** — load deterministic cognition policy, rank candidate
    trajectories and outcomes, execute tie rules, and record decision traces.
18. **Selection and composition** — choose supported result structures without softmax
    continuation and retain every selected constituent, relation, ordinal, and reason.
19. **Conversation and pragmatics** — resolve intent and reference, compile logical
    cognition programs, execute persistent
    discourse programs, and separate literal proposition from expected response.
20. **Modality realization** — calculate and generate exact requested content from a
    typed semantic act through a modality realizer with evidence closure.
21. **Model operations** — ingest model structure and values as testimony, compare
    induced operators under basis, scale, head, and neuron symmetries, fit substrate
    relation operators, construct target-runtime artifacts, and trace every target
    value.
22. **External effects** — construct canonical effect envelopes, validate authority,
    bind reviewed hashes, execute exact approved effects, record outcomes, and return
    the observed consequence through the universal occurrence/testimony path.
23. **Persistence and receipt** — deposit canonical state, occurrences, and testimony;
    construct and coherently publish versioned derived state; record program, inputs,
    source boundary, engine, dependencies, parameters, outputs, effects, and exact
    completion boundary.

The numbering, encoding, and exact operands are contract decisions, not file-order
accidents.

## Transformer replacement mapping

Laplace implements the functions conventionally associated with query, key, value,
attention, routing, residual state, projection, and output through substrate
instructions:

- query and key become typed address and correlation operations over persistent state;
- values are witnessed content, relations, physicality, testimony, and calculated
  results;
- attention becomes a generated layer-metrized typed compatibility and routing
  operator whose relation laws remain explicit;
- residual state is explicit persistent program state and receipts;
- projection is a declared typed operation over universal values;
- output selection is supported by evidence rather than normalized token probability;
- materialization targets any modality or external execution format.

This mapping does not inherit a transformer's context window, token-only value model,
softmax, layer shape, head shape, source-model architecture, or output modality.

## Bulk execution

Every scalable instruction operates on vector and set views with explicit lengths,
strides, null maps, ownership, and lifetime. Implementations use bounded arenas and
pre-sized result contracts. Database access is planned and parameterized by batches.

Required invariants:

- one-element and many-element calls produce identical semantic results;
- input order and declared result order are deterministic;
- duplicate handling is explicit;
- partial execution cannot be reported as complete;
- cancellation leaves a typed durable boundary and exact receipt;
- retries and repeated ingestion preserve idempotency contracts;
- expanded and run-span inputs produce the same content ID, centroid, logical order,
  readback, and testimony;
- sparse deposition cannot suppress occurrence evidence or parent structure;
- memory and database-call counts are bounded by declared input shape.

## SQL execution surface

The public surface exposes typed program validation, execution, explanation, and
receipt inspection. SQL declarations bind directly to native entry points or compose
those entry points relationally. There is no handwritten SQL implementation of an ISA
instruction.

Execution explanation reports instructions, operand shapes, indexes, native kernels,
database plans, resource estimates, and receipt requirements before execution.

## Native dispatcher

The engine registry binds each opcode and ISA version to exactly one implementation.
Registration includes operand types, result types, purity, persistence effects,
ordering, determinism, resource estimator, executor, and receipt encoder.

The dispatcher rejects:

- unknown opcodes or versions;
- type mismatches;
- undeclared persistence effects;
- invalid scopes;
- insufficient result capacity;
- arithmetic or bounds violations;
- an instruction sequence that cannot produce the declared result.

Perfcache modules register acceleration for an existing opcode; they never register a
second semantic implementation. The dispatcher can execute the canonical kernel and
the accelerated kernel over the same batch and compare exact results under test and
diagnostic modes.

## Conversation

Conversation is a stored ISA program, not an endpoint-specific behavior. It must:

- resolve every input modality into universal content and structure;
- address prior turns, corrections, entities, evidence, and artifacts directly;
- retain active propositions, unresolved questions, ellipsis, rejected interpretations,
  cross-modal referents, and prior program receipts rather than topic recency alone;
- traverse and correlate the substrate rather than return a nearby lookup;
- compile the request into a typed logical cognition program and generate its physical
  operator and execution plan without a permanent flattened semantic graph;
- preserve every requested goal, role, relation, negation, temporal constraint,
  evidence boundary, and completion predicate before topic or candidate ranking;
- select and compose only supported results;
- materialize the requested modality;
- witness the turn and retain a complete receipt;
- produce the same semantics through SQL, HTTP, CLI, and other product transports.

## Versioning

ISA major versions change encoded or semantic compatibility. Minor versions add
instructions or value forms without changing existing meanings. Stored programs and
receipts retain the version that executed them. Upgrade tooling verifies every stored
program before activating a new engine version.
