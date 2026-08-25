# Laplace invention model

This document defines how the invention's parts form one machine. It is derived from
the inventor's direct requirements and the invariant-level evidence audit. It does not
adopt the architecture, vocabulary, or completion boundary of another AI system.

## 1. One substrate, one execution machine

Laplace is a universal persistent substrate whose native engine performs identity,
composition, physicality, testimony, adjudication, traversal, inference, synthesis,
and materialization. PostgreSQL supplies durable relational state, transactions,
indexes, query planning, and server integration. SQL and C# both orchestrate the same
native instruction set.

Text, images, audio, video, code, games, models, and future modalities are not separate
intelligences. Edge codecs recover or emit a media representation. Inside the engine,
every modality uses the same content identities, composition forms, physicality types,
relation machinery, evidence records, trust dimensions, instructions, and receipts.

The parts form this closed execution loop:

```text
source or interaction
  -> recover exact content
  -> canonicalize under a declared recipe
  -> resolve content identity
  -> compose recursively
  -> witness content and relations
  -> adjudicate competing testimony
  -> address a goal and active discourse state
  -> construct and evaluate trajectories
  -> select supported result structure
  -> materialize the requested modality
  -> witness the result and its receipt
```

No stage creates a private meaning system. Every scalable stage operates on bounded
batches through the substrate ISA.

### 1.1 Three coupled invention moves

Laplace makes three distinct moves that must remain separate in design and proof.

First, it unifies technologies commonly treated as different systems. Unicode,
content addressing, recursive physicality, GIS, PostgreSQL, typed relations, spectral
mathematics, Glicko-2, indexed search, linear algebra, feedback, and target compilers
become cooperating consequences of one substrate and calculus. They are not a bundle
of products connected by adapters.

Second, it organizes exact and witnessed structure richly enough that absence becomes
constrained. A recurring motif can predict the physical, relational, temporal, and
counterfactual signature of a missing occupant without pretending that the occupant
has been observed.

Third, it treats the boundary of the current calculus as an input to discovery. A
persistent typed failure to explain an observation can generate and independently
test a candidate relation, law, operator, firmware operation, or cognition program.
The extension changes no active calculus until its evidence and activation contract
passes.

These moves make Laplace a calculating instrument rather than one permanent compressed
model. It preserves canonical reality and testimony so the measurement, coordinate
system, state space, operator, path cost, and realization appropriate to a particular
question can be generated and receipted. Relevance, importance, distance, and model
coordinates are properties of that calculation, not timeless scalar attributes of an
entity.

## 2. Content identity

Identity answers one question: what exact canonical content is this?

For a canonical atom, the identity preimage is its canonical content encoding. For a
composition, the identity preimage is the ordered sequence of constituent identities
under the content-composition domain. A one-constituent composition is the constituent
itself. An empty-content representation, an absent value, and a zero identifier are
three distinct states and must have separate encoded meanings.

The identity function excludes:

- source and source type;
- language and modality;
- semantic type and relation type;
- model and model architecture;
- trust, score, uncertainty, and consensus state;
- structural tier and ordinal;
- coordinate, radius, Hilbert key, and trajectory storage;
- ingestion route, batch size, process, machine, database row, and time.

Consequences are structural, not cosmetic:

1. The same content entering through different sources, models, modalities, APIs, and
   batches resolves to one identity before testimony is considered.
2. Every source can testify about the same entity without duplicating the entity.
3. Cross-model and cross-language correspondence can be exact when the underlying
   canonical content is exact.
4. Trust changes standing, never identity.
5. A geometry rebuild can change placement and locality without changing content IDs.
6. Structural tier records the lowest proven altitude of a representation; it never
   creates a second identity for the same content.
7. A content-changing transformation creates a new identity and a witnessed relation
   between input and output. It never rewrites the input's identity.

An entity is only this content identity. It does not contain a type or interpretation.
One entity can participate in several physical realizations and several contradictory
interpretations without receiving another entity ID.

The identity is the SIMD-optimized 128-bit BLAKE3 content hash. Its 128 bits correspond
to the four 32-bit physical dimensions and the 128-bit Hilbert address. This algorithm
and width are fixed; the clean implementation does not re-key Laplace. Byte order,
composition serialization, canonicalization recipe, and test vectors are generated as
one exact contract so every route emits the same 16 bytes.

## 3. Canonicalization is not testimony

Canonicalization establishes the byte or constituent representation used for content
identity. It does not decide what the content means or whether a proposition is true.

Each canonicalization recipe declares:

- accepted input forms;
- exact decode and validation rules;
- normalization operations;
- loss classification;
- output encoding;
- version and immutable test vectors;
- inverse data when exact reconstruction requires it.

Original source bytes, recovered canonical content, and any transformed content remain
separately addressable. Their relations preserve how one became another.

Exact composition and semantic equivalence are different. A precomposed Unicode
character and a base character followed by a combining mark retain different ordered
content identities. NFC, NFD, case transformation, transliteration, decoding,
transcoding, synonymy, and visual equivalence are typed relations produced under a
pinned standard or recipe. An equivalence relation can make the entities discoverable
together; it never aliases one entity ID to the other.

## 4. Unicode and DUCET establish the universal atom frame

Unicode supplies a complete, script-independent codepoint space and the property data
needed for deterministic segmentation and normalization. UCD properties drive
grapheme, word, sentence, Indic-conjunct, canonical-combining, decomposition, and
composition behavior. ASCII or English punctuation rules cannot substitute for UCD.

DUCET supplies a deterministic, language-neutral collation ordering over the whole
Unicode space. The implementation must process the complete collation-element sequence,
including expansions, contractions, explicit weights, implicit weights, canonical
equivalence, and algorithmic Hangul behavior. A first-weight approximation is not
DUCET.

The resulting order is a permutation of all 1,114,112 codepoint positions, including
assigned, unassigned, reserved, surrogate, and noncharacter positions:

```text
codepoint -> complete DUCET key -> total-order rank
```

Codepoint identity derives from canonical UTF-8 content, not from rank. Rank is the
input to physical placement. A new Unicode or DUCET release therefore creates a new
geometry recipe and perfcache generation while content IDs remain unchanged for
unchanged canonical content.

Every modality begins from this shared atom system. Recovered numeric scalars are
encoded by one canonical Unicode numeric representation and compose above codepoints.
Image channels, audio samples, model values, chess state fields, instruction operands,
and other bounded values do not mint private byte, integer, pixel, sample, or game
alphabets.

This shared floor is what makes exact cross-modal convergence possible. `[2,5,5]` is
one content identity whether it is a channel intensity, an IP segment, byte maximum,
index zero's value, an audio sample, a tensor value, or instruction data. Role never
enters that content hash. Role, position, source, modality, witness, and surrounding
composition establish what that same content is doing in a particular observation.

## 5. S3 placement and Hilbert locality

Physicality is a calculated realization of an entity. It records structural form,
constituent shape and cardinality, dimensionality, geometry, radius, trajectory, runs,
multiplicity, ordered structure, and structural altitude. One content entity can have
several immutable, versioned physicalities under different legitimate realization
recipes or geometry epochs.

The physicality trajectory is the bit-perfect Merkle-DAG container from trunk to
leaf. Under its pinned recipe it directly calculates container and constituent
identity, role, ordinal, multiplicity, run boundaries, tier path, ancestry,
containment, precedence, following, shape, dimensions, centroid, radius, locality, and
exact recurrence. These facts require no source to assert them and do not pass through
source trust or consensus. A receipt binds them to the content, trajectory, recipe,
and geometry epoch. Text, models, images, audio, video, code, and games all yield this
exact structural surface before testimony supplies interpretation or semantics.

A source declaration does not instantiate a physicality. A declared MIME or type is an
attestation about an entity or physicality. A structural classifier's type result is a
derived attestation whose lineage identifies the physical features and rules that
produced it. These claims can agree or contradict one another and can be adjudicated;
neither changes the entity identity or the calculated physicality.

Observed use belongs to occurrence and context. Seeing the same word in another
sentence, the same content in another container, or the same bytes executed by another
process adds occurrences and attestations; it does not mutate physicality. Witnesses
and attestations can refer to entities, physicalities, relations, occurrences, other
attestations, and derived claims.

The DUCET rank indexes a deterministic super-Fibonacci distribution on the unit
3-sphere in four-dimensional coordinates. Each Unicode atom receives one fixed point
for a declared geometry recipe:

```text
DUCET rank -> super-Fibonacci S3 point -> four-dimensional coordinate
```

For a composition, the coordinate is the arithmetic centroid of its immediate
constituent coordinates. The centroid is commutative. Two different ordered
compositions with the same constituent multiset therefore share the same point.
Their content IDs and trajectories remain different.

Composite points can lie inside the unit sphere. Their radius is meaningful and must
not be erased by normalizing every composite back to the sphere. Direction and radius
are distinct query dimensions.

Each coordinate component is quantized under a pinned numeric contract. A four-
dimensional Hilbert transform encodes the four quantized axes into a 128-bit,
byte-sortable locality key. The Hilbert key supports exact indexing of cells, locality
windows, neighborhood enumeration, density, gaps, and candidate retrieval.

The separation is absolute:

- content ID proves canonical content identity;
- coordinate describes structural placement;
- radius describes centroid concentration;
- Hilbert key serializes placement for locality indexes;
- trajectory preserves ordered constituent identity;
- relations and testimony establish meaning and standing.

A coordinate or Hilbert collision never merges identities. Proximity never becomes a
semantic assertion by itself.

Centroid and radius together remain many-to-one. Distinct constituent multisets can
share both values. Physicality organizes and describes realization; exact constituent
identity, multiplicity, trajectory, and testimony remain available whenever a query
requires structural authority.

### 5.1 Neighborhood is calculated for the query

`Neighbor` is not one relation and is never one permanent score. Laplace can calculate
different neighborhood families over the same identities:

- direct Merkle-DAG structure: container, constituent, ancestor, descendant, sibling,
  ordinal, preceding, following, tier, run, and repeated occurrence;
- S3 and four-dimensional point geometry: angular, radial, coordinate, cell, Hilbert,
  and density relationships;
- realized trajectory and shape geometry: Fréchet, Hausdorff, Karcher-derived, and
  other declared point/curve/set/manifold calculations;
- epistemic neighborhood: relation type, Glicko-2 standing, source and source-type
  trust, root independence, contradiction, model lineage, context, and epoch;
- semantic and operator neighborhood generated from eligible typed relations for a
  declared cognition or model-compilation program.

A query can combine these channels and calculate one ordering for its goal. Relation
importance is contextual: tier, containment, source, role, and question can make a
noun, a function word, an ordinal, a pixel, a tensor axis, or another constituent
decisive in one query and irrelevant in another. The calculation retains every input,
normalization, comparison, and tie rule in its receipt.

The resulting scalar, path cost, partial order, or selected set is a query result. It
does not replace the contributing structural, geometric, epistemic, or semantic
channels. Karcher-derived calculations do not replace the arithmetic centroid rule for
composite physicality. Geometric proximity proposes candidates; it never asserts
semantic equivalence by itself.

### 5.2 Indexed A-star search replaces universal relevance lookup

Laplace does not ask one permanent vector space to decide what is relevant. A query
compiles the goal, completion predicate, permitted typed transitions, accumulated
cost, remaining-cost estimate, evidence boundary, context constraints, and tie rules.
Native, PostgreSQL, PostGIS, physicality, trajectory, relation, occurrence, and
perfcache indexes generate bounded frontier batches. A-star or another explicitly
declared best-first program selects which typed state to expand next.

One frontier can walk container ancestry, another ordinal sequence, another exact
identity, another a geometric neighborhood, another eligible semantic testimony, and
another source or consensus standing. Each transition retains its type and receipt.
The selected path therefore answers the compiled goal rather than merely returning
the globally nearest stored rows.

A metric top-k instruction remains useful inside a program when the requested
operation is actually metric-nearest retrieval. It is one candidate generator, not
the definition of cognition. An exact A-star result additionally proves its heuristic
and reopen contracts; a bounded or non-admissible search reports that different
completion semantics instead of claiming optimality.

## 6. Trajectories and realized curves

A stored composition trajectory is a lossless ordered constituent manifest. Each
composition vertex carries the constituent's complete 128-bit BLAKE3 identity,
position-derived ordinal, run length, and typed metadata in four packed binary64
mantissas. It carries identities rather than copied constituent coordinates. Actual
constituent coordinates remain in their live physicality rows.

The four-slot form is also a typed physicality payload carrier. A declared vertex class
may allocate the same payload bits to a relation-walk record or exact factor values.
The physicality type, vertex class, source recipe, and result contract determine the
meaning. Reuse of a 128-bit host storage type does not turn its bits into content
identity, and reuse of a geometry storage type does not turn packed payload into spatial
coordinates. Every reader validates the declared class before decoding.

A realized curve resolves those identities to their active geometry epoch and then
constructs the ordered coordinate sequence. Fréchet distance operates on realized
curves. It must never operate on packed identity mantissas as though they were spatial
coordinates.

This gives composition two exact channels:

- the centroid and Hilbert key organize the constituent multiset;
- the trajectory and realized curve retain constituent order.

Containment, adjacency, sequence, recurrence, and order-sensitive similarity are
therefore directly indexable without flattening a composition into a label.

For structural queries, an indexed constituent-ID projection narrows the candidate
containers and the full trajectory decoder establishes exact positions, repeated
occurrences, and run spans. Two constituents co-occur when the decoded scoped
composition contains both; precedence and following come from their decoded positions;
ancestry comes from recursive container-to-child trajectories. These results need no
persisted pairwise relation and no testimony.

## 6.1 Repetition, sparse deposition, and the Merkle DAG

Repeated content resolves to the same identity before storage. A sky-blue pixel seen
100,000 times is not 100,000 pixel entities. One pixel content node is referenced by
its parents. If the same pixels form the same patch, the patch is also one node. A run
of identical patches is an ordered trajectory over that one patch identity with the
run length recording how many consecutive occurrences it represents.

The content hash is calculated over the logical expanded constituent sequence, using
a streaming implementation that need not materialize repeated IDs. Plain and
run-encoded trajectories therefore produce the same parent identity. Runs exceeding a
single packed count are represented by consecutive run vertices without losing the
logical ordinal or total count.

The Merkle DAG stores each distinct content and composition once. Sparse deposition
omits entity and physicality writes already proven present, while the parent trajectory,
witness, attestation, source scope, and occurrence counts retain what was observed.
Deduplication never erases occurrence or evidence.

This applies equally to pixels, patches, audio samples, repeated words, repeated game
states, tensor values, and million-digit numeric content. Width is bounded by resources
and encoded in batches; it is not bounded by a token window.

## 7. Structural altitude and typed composition

Tier is the lowest structural altitude at which content has been proven. It is not a
semantic type and is not part of content identity. A lower-tier entity can stand at a
higher altitude without receiving a new ID. A higher-tier composition cannot be
silently coerced downward.

Composition shape is typed independently from tier. Ordered sequences, sets, named
records, matrices, tensors, spatial fields, temporal streams, and executable programs
have different validation and indexing behavior while sharing the same identity and
physicality machinery.

A set obtains an identity by sorting and deduplicating member IDs under the set
composition domain. An ordered sequence retains source order. A named record binds
field identities to value identities. These shapes cannot be interchanged merely
because all can be stored as arrays.

## 8. Testimony, trust, and adjudication

Identity says what content is. Testimony says what a witness asserts about it.

Each testimony record retains the complete typed proposition and its evidence state:

- subject, relation, object, and context;
- witness and source identity;
- source type, model identity, and analyzer identity;
- positive, negative, draw, uncertain, absent, and unknown distinctions;
- observation count and outcome data;
- source trust, source-type trust, relation trust, model trust, and context trust;
- valid time, observation time, sequence, and supersession data;
- recorded or calculated provenance;
- calculation program, inputs, parameters, engine identity, and receipt when derived.

Adjudication computes the current standing of a typed proposition while preserving
every testimony that contributed. Glicko-2 provides a dynamic rating state where its
mathematics fits the evidence contract; it is not the traversal procedure and it does
not replace relation semantics.

Consensus, personal observation, shared truth, and local belief are separately scoped.
An individual's statement can change that individual's observed world without
changing universal truth.

### 8.1 Epistemic kind and derivation lineage

Laplace represents these states separately:

```text
witnessed
copied
derived
independently corroborated
currently believed
```

A recorded observation has an attributable witness. A copied assertion retains the
assertion it copied and does not become an independent root. A derived claim retains a
provenance DAG containing its exact inputs, instruction program, engine, parameters,
receipt, and transitive root observations. Independent corroboration requires a new
observation whose dependence analysis does not reduce it to an existing root. Current
belief is an adjudicated view over this evidence; it is neither a witness nor a rewrite
of testimony.

Inference may consume another inference, but its effective independent support is the
union of distinct eligible root observations, not the number of derived descendants.
A cycle in the derivation graph contributes no new independent support. Producing one
thousand derived claims from one witness cannot create the standing of one thousand
independent witnesses.

Learning is monotonic at the lower layers: it adds occurrences, attestations,
adjudicated epochs, and derived claims. It does not rewrite what the entity content was
or turn a source claim into physical structure. A newly calculated realization is a
new versioned physicality with its own recipe and receipt, not an in-place semantic
mutation.

### 8.2 Epistemic dependence and consensus epochs

Evidence independence is a graph, not a source-name count. Quotations, mirrors,
syndication, shared primary sources, duplicated datasets, model ancestry, fine-tunes,
distillation, common training corpora, and generated descendants create typed
dependence edges. Adjudication calculates effective evidence from the dependence graph
and records the exact dependence recipe in its receipt.

Consensus is an immutable, time-addressable calculated epoch over immutable testimony.
It records its input boundary, dependence graph, relation laws, rating program,
parameters, engine, and completion state. A later epoch can change current standing
without overwriting an earlier view. Historical queries pin an epoch or valid time;
current queries select the latest completely published eligible epoch.

### 8.3 Deposit and derived state

Canonical content, structure, occurrence, testimony, and their receipts are deposited
in append-oriented batches. Adjudicated standing, reproducible statistics, search
structures, and workload indexes are derived state with explicit source boundaries and
publication epochs. Expensive derived work does not serialize every deposited row
through one mutable proposition hotspot.

A derived epoch is calculated in bounded native and PostgreSQL batches, validated
against its declared input boundary, and published coherently only when complete. A
deposit-complete receipt and a complete-ingest receipt are different states. The latter
cannot be issued until every derived state required by that ingest contract is visible
and reconciled.

Persistent state is classified as canonical structure, retained testimony, derived
reproducible state, retained inference result, active working state, or acceleration
artifact. A statistic or pairwise relation is materialized only when a declared query,
inference, durability, or performance contract justifies its storage. Rebuildable state
must reproduce canonical results exactly from its source boundary and recipe.

## 9. Relation algebra

Every relation type declares how it participates in inference. The declaration covers
direction, converse, symmetry, transitivity, reflexivity, arity, temporal behavior,
context behavior, confidence propagation, legal composition with other relations, and
counterexample handling.

`A is-a B` and `B is-a C` can support `A is-a C`. `A likes B` and `B likes C` cannot
support `A likes C`. Walking an edge backward requires a declared converse; it never
happens because storage permits reverse lookup.

Observed patterns can propose new relation behavior. Such a proposal remains a typed
hypothesis until counterexample search, replay, and evidence thresholds establish its
standing.

## 10. Thought, goals, and discourse state

Thought is dynamic, goal-conditioned trajectory construction through the persistent
relational world model.

The active goal declares objective, constraints, success conditions, acceptable
uncertainty, resources, effect permissions, and termination conditions. Working state
retains active entities, propositions, references, unresolved questions, hypotheses,
counterfactual worlds, current plans, and salient prior turns without confusing them
with universal truth.

The executive ranks candidate trajectories using goal relevance, relation standing,
context, expected information gain, novelty, cost, and firmware policy. It updates
active state when evidence arrives, shifts representation when a trajectory fails to
advance the goal, and inhibits attractive results that violate evidence or constraints.

The executive operates on a canonical cognition guidance state rather than a topic or
entity identifier. That state binds the active goal and operands to unresolved
completion obligations, discourse and world bindings, time and evidence scopes,
authority, available resources, counterfactual assumptions, and a typed knowledge-
deficit vector. Deficit components can identify a missing actor, event, time, place,
cause, mechanism, referent, calculation, evidence condition, or other relation role.
They remain typed even when a particular search execution derives a scalar priority.

A guidance operation declares typed operands, preconditions, predicted effects on the
completion obligations, information value, execution cost, effect authority, and its
required receipts. Curiosity is not the mere existence of a deficit: it is a deficit
made eligible for an information-seeking operation by the active goal, expected
reduction, cost, evidence rules, and authority. This lets the executive ask, calculate,
search, test, or leave a deficit explicitly unresolved without promoting every gap into
an investigation.

The active guidance loop is therefore calculated from richer state:

```text
goal and typed operands
current bindings and completion obligations
typed knowledge deficits
eligible operations and predicted effects
query-specific cost and remaining-work ordering
execution receipt and updated guidance state
```

The operation/operand/program-state pattern is a cognition ISA contract, not a fixed
opcode table. Completion occurs only when every required obligation is satisfied or
has a permitted typed unresolved disposition; reaching an entity, returning a row, or
minimizing a scalar path cost is insufficient.

Internal epistemic states are explicit: observed, attested, inferred, hypothesized,
predicted, contradicted, refuted, and unknown. A hypothesis records the motif or rule
that produced it, evidence on both sides, context, time, and resolution state.

Counterfactual execution occurs in an isolated world scope. It can remove, replace, or
assume relations without contaminating canonical state.

### 10.1 Native cognition is generated calculation

Laplace does not deposit a universal graph and then pretend that graph is cognition.
For the current goal, context, time, world, and epistemic epoch, a logical cognition
program selects exact layered substrate inputs and generates typed operator
applications from the relation algebra and layer-correct constraint metrics.

For relation occurrence \(r:i\rightarrow j\), a generated compatibility term can take
the form \((\delta x)_r=x_j-T_rx_i\), where \(T_r\) is the relation's typed transport
law. For exact physical structure, \(G_r\) enforces the physicality recipe rather than
source confidence. For attested relations, the epistemic component of \(G_r\)
determines how independently supported components of that defect count. The induced
family \(\delta^*G\delta\) is a mathematical candidate
for field propagation and spectral calculations, not a command to flatten different
relations into one scalar adjacency matrix.

The resulting intelligence contract separates:

```text
exact layered substrate
generated layer-metrized typed operator
typed incompatibility and innovation calculus
deterministic counterfactual act selection
typed modality realization
```

A graph, matrix, AImap, embedding, head, reranker, or target tensor is a generated,
receipted view. Canonical state and the calculation program remain sufficient to
reproduce or independently check it.

## 11. Frayed edges and the Gödel engine

Laplace detects recurring relational motifs, derives expectations from them, and
identifies a missing or contradictory role in an otherwise supported structure. The
gap produces a hypothesis with traceable evidence; it does not create an asserted
fact.

The decisive signal is not an empty query result. It is a constrained vacancy in a
repeated typed structure: surrounding physicality, containment, precedence, roles,
geometry, testimony, time, counterfactual consequences, or independently supported
relations predict that something with a particular signature belongs there, but no
sufficiently supported occupant is present. The predicted occupant can be an entity,
physicality, relation, relation family, motif role, causal explanation, law, operator,
firmware operation, cognition strategy, or mathematical construction.

The vacancy and a candidate occupant remain separate objects. A candidate can carry a
predicted physical and relational signature and improve held-out explanation without
becoming observed, witnessed, or currently believed. Reality must still supply an
occurrence, testimony, experiment outcome, or independent corroboration.

Frayed structure is a typed defect object rather than a linear-solver residual. It can
describe an existing relation-law incompatibility, unsatisfied boundary demand,
unexplained observation innovation, independently supported contradiction, missing
motif role, or counterfactual outcome difference. Each retains its own units,
provenance, context, and evidence epoch.

A missing-relation candidate is generated and fitted without access to its evaluation
boundary. Its value is improvement on pinned withheld observations or boundary demands
minus a declared complexity cost. Adding a nonnegative constraint term or improving
training energy alone cannot establish the candidate. A useful candidate remains a
derived hypothesis until new testimony and counterexample search change its standing.

The Gödel engine evaluates candidate higher-order structure by explanatory
compression, unseen-case prediction, counterexample survival, cross-context reach,
reasoning-cost reduction, and observed outcome improvement. Accidental correlations
do not earn reusable rule status merely because they recur.

The same mechanism operates over language morphology, causal paths, software changes,
security experiments, scientific hypotheses, conversation, and reasoning procedures.
A successful reasoning procedure can be compiled into deterministic firmware only
after replay, defect injection, comparison, and explicit activation.

Laplace treats incompleteness as a discovery signal rather than claiming to eliminate
it. Three feedback rates remain distinct:

```text
fast cognition: query -> calculate/search -> semantic act
learning: observation -> testimony -> adjudicated epoch
Gödel discovery: persistent typed fray -> candidate extension -> counterexample and experiment -> versioned calculus extension
```

The experimental cycle is Laplace-native OODA. Observe deposits exact content,
physicality, occurrence, testimony, and outcome receipts. Orient generates the
problem-specific operator, indexed search, semantic field, and typed defects. Decide
selects the most informative valid hypothesis, test, query, counterfactual, or act.
Act realizes or executes it. The observed consequence then returns through the same
universal substrate rather than being silently converted into a parameter adjustment.

Laplace can represent and inspect its own programs, source, histories, receipts,
outputs, failures, firmware, and artifacts. A self-generated diagnosis or extension
retains that ancestry and cannot serve as independent corroboration of itself.
Acceptance of an engine-changing extension requires separately derived checks and
observed outcomes under the extension's declared experiment contract.

## 12. Firmware

Firmware is a deterministic cognitive policy over a shared world model. It controls
which trajectories receive computation, how evidence is valued, how aggressively gaps
are explored, how contradictions are sought, when uncertainty is sufficient, and how
results are expressed.

Firmware does not contain a separate universe and does not change truth. Two firmware
artifacts can inspect the same substrate state and choose different trajectories for
the same goal. Every decision rule, parameter, tie break, input epoch, and resulting
trace is content-addressed and replayable.

Coding firmware implements a complete engineering procedure: inspect requirements and
contracts, research exact APIs, analyze callers and data shapes, implement through the
canonical engine, build, run implementation-level tests, inject defects, inspect
plans and performance, package the exact artifact, install it, verify the loaded
artifact, and record the result.

## 13. Effects and execution envelopes

Rich internal exploration is distinct from external effects. An effect proposal is
canonicalized with its arguments, targets, identity, substrate epoch, firmware,
permissions, policy inputs, detector results, expiration, and approvals. The executor
verifies the exact approved envelope hash immediately before execution.

The ISA validates types, bounds, declared effects, resource estimates, and result
capacity before the operation mutates durable or external state. The receipt binds the
executed program, input identities, engine and dependency identities, output
identities, durable boundary, timing, and effect result.

## 14. Perfcaches

A perfcache is a deterministic, immutable, memory-mapped acceleration plane derived
from canonical PostgreSQL state or declared upstream standards. It is not independent
truth and never owns the sole copy of testimony.

There are multiple perfcaches because key spaces, semantics, density, lookup shape,
source inputs, rebuild triggers, and activation lifetimes differ. Combining unrelated
records into one untyped blob would destroy these contracts.

The initial typed planes include:

| Plane | Key | Value role | Declared source dependency |
|---|---|---|---|
| Unicode atom | codepoint and reverse content ID | content ID, full DUCET rank, S3 point, Hilbert key, UCD properties and normalization tables | pinned UCD, DUCET, geometry recipe |
| relation highway | relation ID, bit, and band | relation routing metadata and band masks | canonical relation manifest |
| canonical number | bounded numeric value | precomposed shared numeric identity and geometry | Unicode atom plane and numeric recipe |
| typed content composition | content ID | hot precomposed geometry and structural facts | declared typed composition inputs |
| state transition | typed `(state, operation)` composition | resulting state identity | declared transition corpus and state semantics |
| model factor | typed model/source/factor address | versioned factor and projection records | model testimony and factor recipe |

All planes use one native mapping, validation, checksum, publication, registry, and
diagnostics foundation. Each plane supplies its own typed key codec, record validator,
lookup implementation, source recipe, and semantic parity verifier.

Each artifact declares:

- magic, format version, byte order, header and record sizes;
- exact section offsets, lengths, alignments, and integer bounds;
- cache kind, key kind, value kind, scope, and dependency IDs;
- complete source fingerprint and generation recipe fingerprint;
- deterministic ordering and duplicate semantics;
- whole-artifact checksum and declared section checksums;
- producer identity, toolchain identity where numeric bytes depend on it, and time;
- exact artifact length and reserved-byte requirements.

Generation writes a unique temporary artifact, verifies it through the production
loader and semantic verifier, flushes file and directory state, then publishes by an
atomic same-filesystem rename. Readers map immutable complete artifacts.

Activation is registry-driven. The registry validates every declared dependency,
source fingerprint, recipe, checksum, semantic verifier, and loaded file identity. A
required plane that is missing, stale, corrupt, or incompatible produces a typed
readiness failure. It cannot silently change execution semantics.

Planes load, verify, activate, replace, unload, and report readiness independently.
An activation epoch pins a coherent set for each executing program. A new plane can be
published while existing readers finish on the prior immutable mapping. Diagnostics
report the path, inode or platform file identity, full artifact hash, header source
fingerprint, generation recipe, record count, activation epoch, dependent planes, and
processes using it.

Every perfcache has an exact parity test against the canonical native and PostgreSQL
operation for every record or a mathematically complete stratified proof for an
unbounded space. Corruption, stale-source, wrong-sort, duplicate-key, wrong-value,
endianness, truncation, extension, dependency-mismatch, and concurrent-publication
defects are injected and must be detected.

## 15. ISA and batch execution

Identity, composition, testimony, adjudication, addressing, traversal, hypothesis,
selection, materialization, persistence, perfcache management, and receipts are typed
ISA instruction families. Their primary operands are vectors, sets, and typed views.

One-element execution uses the same native kernels as many-element execution. SQL
submits and composes programs in set operations. C# moves source blocks, sessions,
service requests, and lifecycle commands. Neither implements private identity,
geometry, evidence, traversal, or selection behavior.

The complete ingest boundary starts at the first source byte and ends after exact
durable identities, structures, testimony, indexes, progress, and receipts are visible.
The i7-6850K acceptance gates remain at least 500,000 input records per second and no
more than 30 seconds per GB for qualifying real corpora.

Each corpus contract defines an input record exactly. Every performance receipt also
reports input bytes, logical occurrences, distinct content entities, physicalities,
compositions, testimony, derived propositions, durable rows, database calls, WAL,
storage writes, cold or warm state, and every stage's elapsed time. The record-rate gate
remains mandatory, while these normalized counters make unlike inputs comparable and
prevent a cheap record definition from hiding work.

## 16. Model ingestion and construction

A model is a witness. Its tokens, tensors, layers, heads, experts, routes, values,
architecture roles, behavior, and source provenance enter universal content,
composition, testimony, and calculated-result forms.

Exact content identity supplies correspondence where canonical content matches.
Structural and behavioral calculations propose further correlations without erasing
source-specific state. Different model architectures coexist without being forced into
one source architecture.

Substrate statistics are executable model-construction inputs. A declared relation
kernel calculates pairwise targets from typed observations such as precedence,
containment, ordinal distance, co-occurrence, dependency, semantic role, frame,
translation, causality, context, source diversity, testimony, and trust. Laplace fits a
bilinear operator against those targets and factorizes it into the exact query and key
projection shapes of a declared neural target. Different relation families can produce
separate head-specific operators. Desired witnessed contextual transformations provide
the target for value and output operators.

Target closure includes every tensor and numeric convention required by the declared
architecture: input and output embeddings, query, key, value, output, normalization,
feed-forward, gate, router, positional, scale, bias, and architecture-specific state.
No tensor is accepted merely because a target file can be serialized. The declared,
selected, calculated, factorized, emitted, quantized, loaded, and executed sets must
reconcile exactly.

Ingested neural systems provide another evidence family. Their tensor topology,
operators, activations, behavior, and quantization become witnessed observations that
can be correlated with explicit substrate relations across heads, layers, model
families, languages, and modalities. A source model does not become a teacher or the
semantic owner. Agreement, disagreement, and unexplained behavior remain attributable
evidence that Laplace can adjudicate with corpus, lexical, syntactic, semantic, visual,
audio, code, game, and other testimony.

Cross-model comparison is operator-based rather than raw-weight-based. For query and
key factors, the induced bilinear form `M = WQ * WK^T`, its observed pairwise logits,
and its behavior under declared inputs are the comparable evidence. If an invertible
change of basis produces new factors with the same `M`, Laplace records different
factor content but one equivalent induced operator under that test. Head permutation,
neuron permutation, compensating scale, basis rotation, and other architecture
symmetries are handled through their induced functions and explicit equivalence
receipts. Numeric resemblance between raw tensors cannot establish semantic agreement.

The native C and C++ engine and PostgreSQL server integration calculate, fit,
factorize, evaluate, quantize, and emit these operators in vector batches on CPU. SQL
submits and composes those programs through the ISA; it does not privately implement
the mathematics. A GPU is not required for the product contract.

Export selects substrate state under a declared target program and constructs every
target value with a receipt. The produced artifact must load and converse correctly in
its declared runtime. File shape, size, or nonempty output is not acceptance.

GGUF is one exact target-runtime container. Its tensor names, dimensions, layouts,
architecture metadata, numeric meaning, and quantization must match the declared
runtime contract. If that target architecture uses operations absent from native
Laplace conversation, those operations belong to the compiled target and do not alter
Laplace's native execution rules.

## 17. Proof standard

Laplace is proven by executing its real implementation through public contracts.
Critical tests must fail when a deliberate defect breaks the exact mechanism under
test. Identity tests corrupt identity logic. Perfcache tests corrupt real artifacts.
SQL tests execute installed native entry points. Conversation tests require new
relational trajectories rather than a stored answer. Export tests run the artifact in
the target runtime.

Laplace's own parser, query, inference, and diagnostic surfaces are instruments. They
produce attributable evidence and receipts but cannot be the sole certifier of the
behavior they implement. Standards vectors, independent calculations, external target
runtimes, deliberately corrupted implementations, package identity, and direct state
reconciliation provide independent checks appropriate to each claim.

No component claim substitutes for the complete product result. The invention works
only when identity, geometry, evidence, cognition, execution, persistence,
materialization, installation, and performance work together.
