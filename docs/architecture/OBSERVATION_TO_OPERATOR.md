# Observation-to-operator execution

## Purpose

Laplace does not require an ordinary observed document, prompt, book, game, code file, or other admitted composition to be converted into a separate semantic-fact graph before it can affect cognition.

Admission already creates usable machine state:

```text
exact content
-> canonical identity / Merkle composition
-> occurrence / source context
-> physicality trajectory
-> indexed structural observation surface
```

Even a baseline UAX29 text decomposition therefore gives the machine exact, queryable observations: where canonical content occurred, what contained it, what preceded or followed it, its ordinal/gap, what other content shared the container, and which larger compositions can be reached by tier ascent.

Configured seed sources separately provide explicit typed facts/testimony: definitions, taxonomies, semantic roles, mappings, equivalence, causal links, standards assertions, model testimony, curated examples, and other relation families. Those factual structures may carry higher source trust and earned standing than ordinary user/runtime observations.

The forward pass is the query-relative calculation that combines the selected structural observation and factual/testimony planes without flattening them.

This is the clean-machine counterpart of historical old-iteration laws in `SaltyPatron/Laplace#555`, the archived transformer slot map, and the trajectory-derived `usage_overlap`, `trajectory_continuations`, and `continuation_conditional_plane` implementation. Issue #132 owns the clean acceptance; #17/#60 own live query/search and #20/#129 own target compilation.

## State classes

The following are distinct even when they reference the same canonical endpoint:

```text
canonical content identity
occurrence / observation
physicality / trajectory calculation
seeded fact / testimony
calculated/derived result
evidence standing
query-relative operator state
exported target tensor/artifact
```

No class may impersonate another.

### Canonical content

Same canonical content has one identity. Source, user, document, language, time, relation, trust, tier role, and occurrence do not remint it.

If `fire` appears in a user prompt, Webster, Britannica, a novel, and a model vocabulary, those observations converge on the same canonical `fire` content while retaining separate occurrence/provenance paths.

### Occurrence and observation

An occurrence says that canonical content was observed at a particular structural position inside another admitted composition under a source/context/time boundary.

Observation is not a high-trust factual assertion. A user typing `the moon is cheese` is a real observation of that exact content and context; it does not make the proposition true.

Nevertheless the observation is useful to cognition because the exact sequence, containment, recurrence, and contextual neighborhood are real machine state.

### Physicality

Physicality is calculated structure, not testimony. It carries exact order, containment, ordinal, gap, multiplicity, recurrence, trajectory, geometry, and structural altitude under a declared recipe.

For text and other ordered modalities, structural operations such as containment, precedence, following, adjacency, recurrence, and local co-occurrence can be calculated at read time from physicality. They do not need duplicate semantic edges.

### Seeded facts and testimony

Selected semantic/standards/curated sources provide explicit typed claims and relations over canonical content. Examples include definitional, taxonomic, equivalence, partitive, causal, lexical, semantic-role, standard, mapping, game/book, and model-probe evidence.

These facts are not raw occurrence counts. They bind source, relation family, direction, context, provenance, dependence, valid time, and evidence/standing state.

A standards-derived or academically curated fact may legitimately begin with more trust than a UserPrompt observation under the selected evidence recipe. That affects weighting/standing where declared; it never rewrites canonical content or physicality.

Facts may themselves contain or point to observations, examples, definitions, spans, and other structure. The machine retains both the factual relation and the underlying observed content.

## Corpus is a direction through containment

For a resolved entity `x` at tier/altitude `N`, its corpus is not one separately seeded flat stream. It is the query-relative set of higher compositions containing occurrences of `x`.

Conceptually:

```text
Corpus_N(x) = { c | x occurs in c and altitude(c) > N }
```

The exact selected boundary may additionally constrain source, language, user, time, world, modality, document, session, game, or another context.

Because same content has one identity, every new admitted occurrence can immediately become reachable from the canonical entity without retraining a model.

For a word-level query, the engine can:

1. resolve the canonical word id;
2. use containment/trajectory indexes to find occurrences and enclosing sentences/documents;
3. recover the word's exact position inside each trajectory;
4. inspect predecessor/follower/sibling constituents and relative gaps;
5. fan out to other content in the same or enclosing compositions;
6. climb or descend tiers as required by the current program;
7. combine that observed structure with eligible seeded typed relations;
8. continue under the declared search/operator law.

This applies equally to user prompts, documents, books, games, code, media, or any future modality whose structure has been lowered into the universal composition/physicality model.

## Read-time structural relations

The following may be calculated from exact physicality rather than materialized as ordinary semantic testimony:

- CONTAINS / contained-by;
- PRECEDES / FOLLOWS for ordered occurrence structure;
- adjacency at a declared structural altitude;
- ordinal and relative gap;
- recurrence / repeated subpath;
- local co-occurrence / shared container;
- trajectory prefix and continuation;
- ancestor/descendant tier paths;
- exact transposition/convergence where two paths reach the same canonical state.

A semantic relation with the same rendered verb can still exist as testimony in another lane. For example a model or feedback source may explicitly attest a typed PRECEDES relation. The existence of that semantic/testimony relation does not justify materializing every text word adjacency as an attestation.

## Seeded facts augment observations

Seeded sources answer different questions from the observation plane.

Examples:

```text
physicality observation:
    "volt" occurred in these sentences at these ordinals beside these constituents

seeded/curated fact:
    volt IS_A potential unit

physicality observation:
    "ohm" and "resistance" repeatedly occur in these electrical-document contexts

seeded/curated fact:
    ohm IS_A resistance unit
```

Both are useful, but they are not the same evidence.

A forward program may use physicality to discover candidate contexts and a high-trust typed fact to route or rank them. It may also use observed structure when no curated relation exists. A factual edge can connect entities that never co-occurred; an observation can reveal recurring usage that no curated source named explicitly.

The correct combination is query-relative and receipted.

## Query-relative forward program

A representative program is:

```text
RESOLVE
  canonical operands, selected tier/altitude, source/world/time scope

ORIENT
  goal + discourse/session trajectory + current observation state

SCAN-STRUCTURE
  indexed containers, occurrence positions, ordinals, gaps, successors,
  recurrence, trajectory continuations, cross-tier containment

SCAN-FACTS
  eligible typed attestations/relations/links under source/context/dependence scope

SCAN-GEOMETRY
  optional S3/Hilbert/curve candidates when the query declares that plane

COMPOSE
  retain structural observation, factual/testimony, standing, geometry,
  contradiction and uncertainty as separate channels

ROUTE / SEARCH
  bounded fanout, typed hop, A-star, or another declared search law

PROPOSE / STEER / SELECT
  compare only channels declared by the current program

REALIZE
  render selected canonical structure

WITNESS
  append the emitted result/action/consequence and update the active trajectory
```

Each emitted constituent updates the active frontier, residual/obligation state, ordinal context, and session trajectory before the next step. A one-time static retrieval followed by blind token draining is not the conforming loop.

## Indexing and finite search

The observation plane is useful precisely because it can be filtered/indexed rather than expanded into an all-pairs graph.

Candidate providers may include:

- constituent/containment indexes;
- ordinal/run/trajectory indexes;
- source/context/time/world filters;
- geometry/GiST/Hilbert or curve candidate indexes;
- typed semantic relation endpoint/type/direction indexes;
- dependence-root indexes;
- typed standing lanes;
- perfcaches that accelerate a proven access law.

The query program defines admissible transitions, costs, heuristic, terminal condition, multiplicity, evidence boundary, and resource limit. The index does not define meaning or completion.

## Transformer and target-model projection

A conventional transformer is one possible consumer of the native operator state. Target slots are generated roles, not Laplace ontology.

The governing rule is:

> **Pour follows query.**

If a native relation/trajectory/operator can be queried over a closed substrate boundary, a target compiler can factor/materialize that read into whatever target slot implements the corresponding behavior. If the native operator is not defined, filling a tensor by shape or conventional default does not create the missing semantics.

A target recipe may generate Q/K/V/O, QK/KV/VO-like pair operators, FFN/gates/experts, position/sequence representations, embeddings, output heads, or a non-transformer target from selected native planes.

One historically demonstrated correspondence is:

| Target role | Native source |
| --- | --- |
| query formation / Q | active election/frontier and completion obligations |
| key/address / K | typed relation arena keys plus trajectory ordinal/address keys |
| value/contribution / V | eligible typed consensus/standing plus observed continuation/use state |
| output / O | steer/merge/share-normalized fold into the active stream |
| positional | physicality trajectory: order, ordinal, gap, recurrence, time/geometry |
| gate/routing | declared relation/highway/firmware/operator selection |
| heads/layers/experts | recipe-selected tiers, relation families, hops, contexts, geometry and trajectory bands |

That table is evidence, not a demand for one universal fixed schedule. #129 permits target-specific operator plans and independent Q/K versus V/O planes.

### UAX29-only content already contributes operator signal

Without any source-specific semantic extractor, observed physicality can generate or constrain:

- positional/ordinal operators;
- continuation conditionals;
- co-occurrence/shared-container incidence;
- local contextual compatibility;
- recurrence and path-frequency operators;
- cross-tier containment operators;
- source/user/time/session/game scoped usage planes;
- sequence and trajectory contribution operators.

Seeded facts then add typed meaning/definition/taxonomy/causal/equivalence/etc. planes and their source trust/standing.

A richer dictionary/encyclopedia/parser recipe may add additional explicit factual testimony, but **ordinary observed content is already a forward-pass input before that enrichment exists**.

## Existing historical implementation evidence

The old iteration contains several direct demonstrations of this law:

- `SaltyPatron/Laplace#555`: corpus is containment; `containers_of` exposes exact usage without a global GenCorpus.
- `consensus.usage_overlap`: shared usage is calculated from geometry/trajectory neighborhoods and explicitly replaces redundant PRECEDES attestations.
- `generation.trajectory_continuations`: finds every exact-context occurrence and its successor directly from stored trajectories.
- `generation.continuation_conditional_plane`: factors corpus/discourse continuation evidence derived from trajectories and explicitly rejects a PRECEDES-consensus text leg.
- archived `transformer-slot-map-annotated.md`: positional = physicality trajectory; K/V/Up from indexed consensus arenas; heads are typed by comparison plane; pour follows query.
- `SaltyPatron/Laplace#1018`: `containers_of(word_id('Sherlock'),2)` already returned 119 sentences and 10 documents while conversational consumers ignored that available observation state.

These are behavioral evidence/counterexamples for the clean implementation; they do not make the old ABI/schema clean authority.

## Trust and standing

Source trust and relation standing affect declared evidence calculations, not structural truth.

An ordinary runtime observation may be low trust as a proposition while remaining exact as an occurrence. A curated or standards-derived fact may be high trust as testimony while still remaining testimony rather than formal truth.

Typed Glicko standing may rank competing factual/operator candidates only in the exact declared lane. It must not become a universal entity weight or erase rating deviation/volatility/evidence epoch.

Formal calculation and exact physicality do not become votes. They may independently establish an outcome and then provide return-leg evidence about sources/operators that made claims.

## Same-content reuse

Repeated observations do not require repeated canonical work.

For repeated content, the implementation should reuse:

- canonical Merkle subtrees;
- run-length structure;
- trajectory prefixes/subpaths;
- exact transposition/convergent states;
- per-epoch operator results where valid;
- indexed endpoint and occurrence lookups.

Occurrence volume can change empirical usage/standing while canonical identity remains constant. A model compiler should report canonical work separately from occurrence/witness volume.

## Acceptance

### Observation-only proof

Ingest several UAX29-only sentences/documents with no source-specific semantic relation extractor.

The installed product must prove that a selected word immediately exposes:

- all selected occurrences/containers;
- exact ordinals and relative gaps;
- predecessors/followers at the declared tier;
- shared-container/co-occurrence candidates;
- exact-context continuations;
- source/document/session provenance.

Adding one new prompt/document must change the relevant observation-derived candidate/continuation state without retraining a model or reminting existing content.

### Fact-only proof

Admit a seeded typed factual relation between two canonical entities that do not occur together in the observation fixture. The relation remains queryable with exact source/provenance/standing even when observation providers are disabled.

### Combined proof

Run one query with both provider classes enabled. Its receipt identifies structural observation contributions and seeded factual/testimony contributions separately through selection. Disabling either class changes only the corresponding channel.

Higher source trust changes the eligible factual/testimony contribution where the recipe declares it; it does not change physicality identity or occurrence counts.

### Multi-tier proof

The same canonical source boundary queried at different selected tiers/container depths/gap windows produces different operator/frontier state with exact receipts. Cross-tier containment is explicit rather than collapsed into one flat vocabulary.

### Target-compilation proof

Compile a small target from the same closed boundary:

- at least two heads/layers consume different declared trajectory/relation planes;
- an observation-only trajectory plane affects a target operator without requiring proposition extraction;
- a seeded fact plane affects a different operator or head;
- every generated target slot traces to native operator inputs;
- the identical closed epoch + recipe is deterministic;
- a later admitted observation may create a later generation without mutating the earlier artifact.

### Cross-route proof

Native, PostgreSQL, managed, Matchup/Connection, Conversation, and target compilation must agree on the logical meaning of the selected observation/fact operators. Product surfaces may render differently; they may not implement private semantics.

## Deliberate defects

Acceptance rejects:

- requiring semantic proposition extraction before UAX29 content can influence cognition;
- treating raw occurrence as a high-trust factual assertion;
- treating seeded facts as mere unweighted occurrence counts;
- materializing word PRECEDES/FOLLOWS/co-occurrence attestations instead of reading physicality;
- a global GenCorpus/flat token stream;
- n-gram tables standing in for exact occurrence trajectories;
- one universal adjacency/embedding/similarity plane;
- KNN/ANN as semantic authority;
- source/time/context metadata salted into canonical content identity;
- fixed conventional Q/K/V/O filler independent of selected substrate state;
- one operator plane copied into every head/layer;
- endpoint-only path dedup that erases trajectory/context state;
- model export with semantics different from live query;
- source-specific semantic extraction presented as a prerequisite for ordinary observation use.

## Optional semantic enrichment

Source-specific academic extraction remains valuable where selected: Webster senses, Britannica article structure, Roget classes, equations, citations, semantic roles, and other explicit structures can contribute additional typed testimony/calculation.

That enrichment extends the factual/testimony plane. It does **not** define the minimum condition for an observed document to participate in the forward pass.

## Ownership

Primary acceptance owner: #132.

Core implementation owners:

- #7 — canonical identity/composition/physicality persistence;
- #14 — typed perfcache/index acceleration;
- #16 — testimony, lineage and evidence epochs;
- #17 — query-relative typed operators/guidance;
- #60 — typed connection/A-star execution;
- #110 — typed standing;
- #20 / #129 — target operator compilation;
- #18 — realization/conversation consumer;
- #19 — learning/discovery/OODA consumer;
- #22 — complete-product acceptance.
