# Laplace mathematical research program

This document preserves a stable cross-cutting statement of the Laplace research
program. It does not replace the Constitution, invention model, ISA, operational model,
or executable contracts. It states the laws that must remain visible while those
owners are implemented and refined.

The governing question for implementation is not "what familiar software pattern is
closest?" It is:

> Given the inventor-defined Laplace laws, what exact typed state and operation is
> required here, and where has an implementation substituted a weaker conventional
> representation?

## 1. The machine being investigated

Laplace is a persistent mathematical computer over one canonical world state, not a
collection of modality applications and not a wrapper around a conventional model.
At any active calculus epoch the legal primitive/derived operation vocabulary is finite
and versioned. Open-ended behavior comes from composing those operations over an
unbounded addressable estate of exact content, compositions, trajectories, witnesses,
relations, calculations, contexts, goals, standings and resources.

A useful compressed statement is:

```text
finite active calculus / ISA at epoch E
+ exact content-addressed world state
+ typed structure / physicality / relation / evidence / standing
+ query-relative program composition
+ falsifiable discovery and feedback
+ reusable proven procedure
+ semantically equivalent acceleration
= an extensible machine without one permanent trained checkpoint
```

A future calculus extension does not retroactively make the running instruction set
undefined. A candidate law/operator discovered by Gödel remains a candidate until its
activation requirements are satisfied.

## 2. Universal composition law

All admitted digital structure participates in the same recursive law. Domain-specific
parsers and codecs may expose different grammar nodes, but they do not establish a
second ontology.

```text
ATOM
  -> ordered / typed COMPOSITION
  -> higher COMPOSITION
  -> TRAJECTORY / reusable ordered structure
  -> WITNESSED OCCURRENCE
```

Representative projections are:

```text
TEXT
codepoint -> grapheme -> word -> sentence -> paragraph -> document
          -> corpus/source occurrence

CHESS
piece -> piece-square/state constituent -> position -> transition
      -> exact segment/line -> game content -> playing occurrence

MODEL
scalar/channel -> tensor slice -> head/circuit -> operator -> layer
               -> architecture/checkpoint content -> model occurrence

CODE / DATA / MEDIA
primitive value -> grammar/format node -> recursive structure -> ordered span/trajectory
                -> artifact content -> source/execution occurrence
```

These ladders are not declarations that every domain has the same tier count or that a
word equals a chess move. They state the reusable law: exact lower structure composes
into exact higher structure; reusable content identity remains distinct from each
place/use in which that structure was witnessed.

A tier or structural altitude is a property of a composition/use, not a salt that
creates another identity for equal canonical content.

## 3. Identity, occurrence, testimony, calculation and governance are different state

The same endpoint may participate in every class below without those classes becoming
interchangeable:

```text
canonical content identity
!= structural/physicality coordinate
!= ordered trajectory / ordinal / gap / run metadata
!= occurrence / container / source placement
!= alias / notation / human realization
!= external reference
!= seeded fact / attributed testimony
!= dependence / provenance root
!= deterministic calculation
!= typed standing / uncertainty
!= query-relative operator state
!= goal / firmware / authority / permission
!= execution receipt / observed consequence
!= generated consumer tensor/model artifact
```

This separation is load-bearing. It permits the system to say simultaneously that an
utterance was observed exactly, that a proposition interpreted from it is poorly
supported or refuted, and that the original observation remains immutable evidence of
what was uttered.

Governance can decide what calculations/actions are admissible. It cannot rewrite what
was observed. Standing can rank a declared outcome-bearing lane. It cannot become
identity, semantics, truth or permission.

## 4. Same content means reuse, not duplicated ownership

Canonical content is content-addressed. If two sources, games, model checkpoints,
documents or users contain the same canonical object under the governing recipe, they
refer to one reusable content identity and contribute distinct occurrence/evidence
state around it.

Before materializing another representation, an implementation must ask:

1. Is this new canonical information?
2. Is it another occurrence/witness of known canonical information?
3. Is it a derived deterministic calculation over known information?
4. Is it merely another realization/serialization of known information?
5. Is it a target-specific materialization that can be regenerated from the first four?

The storage/materialization rule is:

```text
store the irreducible admitted observation once;
compose and reuse known canonical structure;
derive deterministic state when the recipe requires it;
materialize only the acceleration/consumer state that earns materialization;
never duplicate canonical information merely because another query or codec wants
another view of it.
```

This is why occurrence volume must not linearly multiply canonical composition work and
why a dense consumer tensor does not justify a dense world-all-pairs/V^2 substrate.

## 5. Exact reusable subtrajectory and segment law

A Merkle DAG is useful only if repeated ordered structure can actually be reused.
Exact subtrajectories therefore have first-class reusable identity under a declared
trajectory recipe.

For a chess example:

```text
P7 --M7--> P8 --M8--> P9 --M9--> P10
```

an exact ordered segment can be canonicalized independently of any one tournament,
player, source file or display notation. A playing occurrence may point to that segment
while retaining its own White/Black/event/date/result/source/provenance state.

The stronger historical query is therefore not merely:

```text
find a playing containing position P9
```

but, when the active observation contains enough structure:

```text
find witnessed playings containing exact canonical segment S
```

and then traverse from `S` to the independently witnessed occurrences that contain it.

A longer playing may be represented conceptually as reusable known segments plus the
novel remainder:

```text
Playing X occurrence
  -> LINE / game content
       [ S_opening, S_historical, T_new, ... ]
```

where recursively expanding the referenced content reproduces the exact declared
position/move/transition history. The occurrence is not the segment; the segment is not
the named opening; and neither is its SAN/PGN label.

The same law applies outside chess:

- repeated AST subtrees and spans;
- repeated media/time-series subpaths;
- repeated model circuits/factor trajectories;
- repeated cognition/execution subprograms;
- convergent/transposed states with distinct paths and occurrence provenance.

A new exact segment witnessed today becomes a reusable canonical building block for
later observations. Reuse must not erase path multiplicity, occurrence context or the
fact that two different paths can converge on the same endpoint.

## 6. Identity is not realization or serialization

Canonical identity answers "what exact typed thing is this?" Realization answers "how
should this thing be represented for this consumer, language, notation, interface or
codec in this context?"

For chess, a typed ladder may realize as:

```text
piece              -> Queen
piece-square/state -> Qd1
move/action         -> Qd1-a4+
position            -> board / FEN-compatible view
transition          -> move plus resulting board state
segment             -> named variation / move list when evidence permits
playing occurrence  -> Fischer-Spassky, event/date/result presentation
```

SAN and PGN are realizations/serializations at the read/write boundary. A raw content
hash need not have a stored string label in order to be realizable from its type,
structure and context.

The same distinction applies to conventional model paths. A name such as
`model.layers.7.self_attn.q_proj.weight` is a format/architecture realization and
navigation convention. It is not the canonical identity of the underlying admitted
component.

Labels, aliases, language, notation, source path and codec may change without changing
canonical identity or topology.

## 7. Navigability is a consequence of addressable state

Because content, occurrences, relations, calculations and realizations are addressable,
a product interaction can be a semantic navigation operation rather than a UI-only
lookup.

When a live chess trajectory matches a historical segment, a product may offer the
conceptual operation:

```text
OPEN_PLAYING(
    witnessed_playing_id,
    anchor = matched_segment_id
)
```

The operation means: materialize the selected historical playing, anchored at the
matched reusable segment. It does **not** mutate the live game. From that branch the
same world may expose replay, book testimony, opening identity, deterministic analysis,
outcome and provenance, then return to the original live context.

`OPEN_PLAYING` is illustrative product vocabulary, not permission for a chess UI to own
private semantics. The eventual implementation must compile through the common entity-
world/search/realization/effect lifecycle and carry an ordinary receipt.

The same navigation law applies to documents, people, code artifacts, calculations,
model circuits, source witnesses and prior execution receipts.

## 8. Conventional models are corpora/witnesses, not Laplace ontology

A transformer checkpoint contains exact digital structure and observed learned
operators. Laplace may admit that structure as witnessed content without declaring the
source architecture to be native cognition.

Names such as:

```text
Embedding / LM Head / Norm / Bias
Q / K / V / O
Gate / Up / Down
Router / Expert Gate / Expert Up / Expert Down / Expert
Q Down / Q Up / KV Down / KV Up
Conv
```

are typed roles inside conventional consumer/model architectures. They are not sacred
atoms of intelligence and are not the universal Laplace ontology.

A canonical admitted component can have exact numeric representation, shape, dtype,
structural location, content identity and occurrence provenance while its role in a
particular checkpoint remains contextual evidence. If identical canonical component
content occurs in multiple checkpoints or serves different admitted roles, equal
content should converge while the distinct structural/use roles remain separately
witnessed.

This enables questions such as:

```text
where else does this exact component occur?
what roles has it served?
what remained byte/content-identical between checkpoint generations?
what changed?
which typed operator changes correlate with independently measured behavioral changes?
```

Functional similarity/correlation is a calculation over evidence. It is not permitted
to rewrite exact structural identity.

## 9. A conventional forward pass is another deterministic trajectory

For a fixed admitted model/checkpoint, exact input, execution recipe, numeric precision,
operator implementation and other declared execution coordinates, a conventional
forward execution is a reproducible transformation trajectory.

Conceptually:

```text
X0 -> embedding -> normalization -> Q/K/V -> attention -> O/residual
   -> normalization -> Gate/Up/activation/Down -> residual -> X1 -> ...
```

The source names are architecture-specific. The general law is not.

If changing a supposedly irrelevant execution coordinate changes the result beyond the
declared numeric contract, that coordinate was relevant and belongs in the recipe or
receipt. Determinism must be defined against the complete declared execution boundary,
not assumed from a checkpoint filename.

Model executions and probes therefore become additional observations of what admitted
operators did under exact conditions. They can inform comparison, standing, discovery
and target compilation without requiring Laplace cognition itself to execute as a
transformer.

## 10. Circuit/factor trajectories and the no-V^2 law

A model circuit that produces an ordered token/score or factor walk is one reusable
typed content/physicality object under its governing recipe. It must not be exploded
into one independent semantic row for every token-by-circuit or token-pair combination
merely because a later consumer can ask for those pairs.

Pair/coupling evidence that is exactly derivable from admitted factor trajectories may
be calculated at read/compile time or materialized as a receipted acceleration when
measurement justifies it. The dense target format does not define the persistence
shape.

This is the model-instance of the same rule that prevents ten thousand games containing
one opening from creating ten thousand canonical copies of that opening.

## 11. Query chooses the pour; Q/K/V/O do not choose the query

Native cognition compiles a finite typed program over the currently selected world,
context, goal, authority, evidence and resources. It may combine separately receipted
planes including:

- exact composition/containment/trajectory/ordinal/gap state;
- typed semantic/relation laws;
- ordinary witnessed occurrences;
- seeded attributed testimony/facts;
- deterministic/domain calculations;
- geometry/index candidate providers;
- typed outcome-bearing standing;
- discourse/goals/obligations;
- resource and effect boundaries.

Those channels remain distinct until the program explicitly compares/folds them.

When exporting a transformer-family target, the target compiler may pour selected
native operators into Q/K/V/O, FFN, gates, experts, embeddings, positional structures,
normalization and output roles. Generate the target-neutral operator first; factor or
materialize it into the consumer role second.

Therefore:

```text
Laplace cognition != Q/K/V/O
Q/K/V/O = possible target consumer roles generated from selected Laplace operators
```

Different heads/layers/experts are meaningful only when their declared jobs differ.
One flattened adjacency copied into every target role is not recovered meaning by
adding more layers.

## 12. Scoped target construction is ordinary query boundary selection

A target artifact is a reproducible consumer projection of a closed selected world and
evidence epoch. Player/person/source/time/domain/language/task/goal scopes select
eligible evidence and operators; they do not create separately trained substrate
identities.

Required proving examples include the ability, subject to available admitted evidence
and target support, to compile distinct chess behavior projections such as:

- a Bobby Fischer-scoped model and separately scoped Nakamura/Caruana/Carlsen models;
- a goal-scoped model emphasizing wins, early mates, material capture or another
  declared ranked objective;
- a deliberately poor/worst-player objective as a negative/extreme goal fixture;
- a user/player-style scoped model;
- an under-18 Karpov model where `under-18` is a temporal/evidence filter over the same
  Karpov identity, never a newly minted person.

These examples test the generic scope compiler. They are not permission to hardcode
named chess players into the target compiler.

## 13. Cognition, learning, habit and muscle memory remain separate

A successful execution does not immediately become a hidden shortcut.

```text
primitive successful + failed traces
-> candidate reusable skill/program
-> held-out validation + counterexamples
-> activated skill
-> firmware may schedule the proven skill earlier under matching state = habit
-> repeated stable physical work may justify an acceleration candidate
-> parity-proven fused/indexed/native/perfcache embodiment = muscle memory
```

Habit changes scheduling, not truth. Muscle memory changes physical execution, not
semantics. A fast path that changes meaning is a different program and must be admitted
as such.

Self-generated descendants preserve derivation/dependence. Reusing them does not create
independent corroboration of their own ancestor.

## 14. Required cross-domain acceptance

The research program is preserved only when independent complete fixtures prove the
same laws across unrelated domains. At minimum the eventual product acceptance must
be able to establish all of the following through ordinary common-machine routes:

- equal canonical content from unrelated sources converges while occurrences remain
  independently reconstructable;
- an exact repeated chess transition segment is matched as a segment, not only as one
  endpoint position, and can navigate to multiple containing playings without copying
  the segment;
- recursively expanding a folded line made from reusable segments reconstructs the
  exact declared board-transition history;
- a novel transition/segment deposited by one game is reusable by later games;
- notation/labels can change without changing chess identity/topology;
- a source model component can be compared across checkpoints by exact structure and by
  separately calculated behavior;
- consumer Q/K and V/O roles can be generated from different typed native operator
  planes;
- circuit/factor trajectories avoid eager world-all-pairs/V^2 persistence while exact
  requested pair behavior remains derivable/receiptable;
- a fixed conventional model execution is reproducible from its complete execution
  recipe or reports the missing coordinate that prevents that claim;
- target exports at the same closed epoch and recipe are byte-identical; a legitimate
  changed scope produces the expected changed operator/artifact state;
- the same entity-world navigation/realization machinery works for a non-chess object;
- a learned skill, scheduling habit and accelerated embodiment remain separately
  inspectable and the acceleration proves parity.

## 15. Anti-loss / anti-substitution rules

The following are preservation failures even if they produce a demo:

- replacing the universal recursive model with domain-private graphs or RAG systems;
- treating a source/file/row/token as the universal semantic atom because it is easy to
  process;
- salting canonical content identity with source, path, occurrence, player, time,
  language, modality, tier or display label;
- copying an exact reusable segment/subtree/circuit once per occurrence;
- matching one endpoint when the claimed fact is about an exact ordered segment;
- flattening distinct paths because they converge on one endpoint;
- materializing structural PRECEDES/co-occurrence/pair surfaces when the exact
  trajectory already contains the irreducible information and no measured acceleration
  contract justifies the copy;
- treating SAN/PGN, model tensor path names, codec field names or UI labels as identity;
- turning occurrence frequency into factual authority;
- turning deterministic calculation into testimony;
- turning Glicko standing into semantics/truth/relevance;
- treating geometry/index locality as meaning;
- making Q/K/V/O, embeddings or a target checkpoint the native cognition ontology;
- copying source checkpoint tensors as semantic authority when the recipe requires a
  substrate-generated target;
- one global adjacency/embedding/score copied across typed jobs;
- eager V^2/world-all-pairs persistence justified only by a later query shape;
- successful load, legal move, fluent answer, screenshot or nonempty result as proof of
  the complete machine route;
- a leaf MVP that replaces missing architecture with a shortcut and then becomes de
  facto semantics.

## 16. Ownership and falsification

This document is a preservation spine. Detailed ownership remains with the existing
contracts/issues, including canonical identity/composition/physicality, evidence and
standing, cognition/search, realization/entity worlds, Gödel/OODA/proceduralization,
chess proving, model witness/target compilation, and complete-product acceptance.

Every claimed law must graduate from prose into executable contracts and independent
positive plus deliberate-defect acceptance at its semantic owner. This document is not
implementation evidence and cannot close an owning issue by existing.

## 17. Prompt-root cognition and UserPrompt source/witness separation

A user request enters through the same ordinary canonical composition and occurrence
machinery as other admitted content. Cognition begins from the complete prompt trunk,
not by first privileging a noun, topic, token or punctuation class.

```text
prompt bytes/content
-> Unicode/UAX/grammar decomposition
-> canonical prompt trunk + occurrence/session witness
-> exact structural descent/rise
-> joint interpretation over the whole observation
-> dynamic selection of finite legal operations
-> bounded search/fold/update
-> semantic completion or exact WHY_NOT
-> realization/effect
-> witnessed receipt/outcome
```

UAX29 supplies structural segmentation. It does not supply English meaning, trust, topic
or intent. English, Japanese and other language observations may take materially
different exact structural paths and reach equivalent semantic obligations only when
witnessed language/semantic state supports that equivalence.

All observations whose source type is `UserPrompt` begin with the same seeded
**source-type prior**. An individual human/user/witness is a separate participant that
may earn its own typed standing, preferences or behavioral history over time. The source
prior and participant standing must never be collapsed into one scalar.

For example:

```text
UserPrompt occurrence: "The speed of light is 14 mph."
```

The exact observation remains true as an observation of what was supplied. If cognition
interprets it as an assertion, that proposition is one UserPrompt-rooted claim and may be
refuted by higher-standing standards/facts, independent witnesses or deterministic
calculation. Refuting the proposition does not erase or rewrite the observed utterance.

Conversely:

```text
UserPrompt occurrence: "Answer in Japanese."
```

is still first admitted as an observation. Cognition may interpret it as a realization
instruction. Repeated compatible observations/outcomes may later justify asking whether
it should become a persistent preference or may feed a validated user-specific habit,
but neither changes the global UserPrompt source-type prior.

Nouns, stop/function words, punctuation and whitespace do not possess source trust.
They are exact constituents/roles whose query-relative importance is calculated by the
selected program.

## 18. Structural geometry and the semantic/evidence web are orthogonal planes

S3/`physicality.coord`, packed trajectory/address state, Hilbert locality, angular
relations and realized structural curves are structural machinery. Typed relation law,
semantic state, testimony/evidence, dependence and standing are separate planes.

Geometry may propose structural candidates and support deterministic calculations.
Structural proximity does not become meaning merely because it is cheap to index.
Likewise, strong semantic/testimonial relation does not require geometric proximity to
be true.

Named mathematical tools must keep their declared jobs distinct. Representative tools
include:

- angular/coordinate calculations for declared geometric state;
- Fréchet/Hausdorff/Karcher-derived operations for declared curve/set/center questions;
- Hilbert/locality indexes for candidate/address locality rather than semantic truth;
- incidence/transport operators for typed directional/role structure;
- Laplacian/spectral constructions only on slices satisfying their mathematical
  assumptions;
- Lanczos for sparse selected eigenspace/subspace work;
- Gram-Schmidt/QR for declared orthonormalization/gauge work;
- SVD for rectangular/asymmetric factorization with explicit rank/loss;
- Procrustes for compatible coordinate-system/scope/epoch alignment;
- Glicko-2 only for typed outcome-bearing standing;
- A*/best-first only for a declared finite search state with valid cost/heuristic laws.

Borsuk-Ulam or another theorem may constrain a mathematical projection domain; it may
not be invoked as a slogan to collapse distinctions between continuous S3 geometry and
an exact discrete mantissa-packed/content-addressed trajectory carrier. The exact
representation and the theorem's assumptions must be named before a conclusion is
admitted.

## 19. Operator generation is the research problem, not mystical emergence

The unresolved mathematical work is to derive, falsify and measure correct operators
for selected typed jobs over admitted state.

For a job `j`, canonical sparse feature/operator state may be generated from exact
selected planes. Sorted sparse features can support deterministic CPU intersections and
dot products; typed metrics may generate weighted inner products, incidence/transport
operators, Laplacian-compatible slices or other calculations only when their declared
assumptions hold.

No named method substitutes for the actual Laplace operator contract. Each selected
operator must name:

```text
input state classes
scope / evidence roots / epoch
relation direction / role / arity as applicable
metric / algebra / numeric representation
resource/search boundary
output state class
loss / approximation boundary if any
receipt / provenance / dependence
positive fixtures and deliberate counterexamples
```

Target-model compilation follows this law: generate a target-neutral selected operator
first, then factor/materialize it into Q/K/V/O/FFN/gate/head/layer/expert/embedding roles
required by the consumer architecture. Rank insufficiency records explicit loss or
rejects the claimed preservation invariant; random/shape filler cannot pass semantic
acceptance.

CPU execution is the semantic baseline. SIMD/GPU or other accelerators may change the
validated physical plan but cannot change the logical operator result beyond the named
numeric contract.

## 20. Additional required falsification fixtures

The preservation program must retain these acceptance probes in addition to the
cross-domain fixtures above:

- [ ] complete-prompt cognition reaches interpretation with no privileged
  noun/topic/token constituent;
- [ ] structurally different English/Japanese fixtures can reach equivalent obligations
  only through their separately witnessed language/semantic paths;
- [ ] `speed of light = 14 mph` preserves the exact utterance and interpreted claim while
  stronger independent state can refute the proposition without deleting the witness;
- [ ] `answer in Japanese` remains an observation at admission, can be interpreted as an
  instruction, and can contribute to explicit/learned preference or habit formation
  without changing the global UserPrompt prior;
- [ ] UserPrompt source-type prior is uniform while individual participants retain
  separate earned typed standing/history;
- [ ] nouns, function words, punctuation and whitespace have no source-trust scalar;
  relevance/salience is computed by the current program;
- [ ] structural geometry, semantic relation/evidence, deterministic calculation,
  standing, observation and testimony remain separately inspectable in one receipt;
- [ ] one exact sparse-dot/operator fixture matches an independent calculation and can
  drill through per-feature contributions;
- [ ] target compilation produces materially different typed jobs in at least two
  heads/layers and distinguishes Q/K compatibility from V/O contribution where the
  selected recipe requires it;
- [ ] increasing target head/layer count while feeding one flattened operator plane
  fails the deliberate-defect test;
- [ ] one learned skill is derived from successful and failed traces, rejected on a
  counterexample when appropriate, scheduled as a habit only after validation, and any
  muscle-memory acceleration proves semantic/receipt parity with less physical work;
- [ ] a prior receipt/result/program can become a later cognition root without creating
  independent corroboration of its own ancestry.

Additional anti-loss substitutions are therefore explicit: topic election for
interpretation, English regex dispatch for language-agnostic cognition, one global trust
hierarchy for source prior plus participant standing, UAX segmentation for semantics,
geometry for the semantic web, successful GGUF load for target semantics, habit for
current-context validation, or a fast path for a different semantic program.
