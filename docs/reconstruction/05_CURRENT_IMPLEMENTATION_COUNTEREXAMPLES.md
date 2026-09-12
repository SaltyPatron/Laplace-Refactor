# Current implementation counterexamples

Purpose: prevent current code/screens from being used as invention authority. This file records where concrete implementation behavior is useful precisely because it demonstrates divergence from higher-authority product law.

## 1. Technical-workspace browser presented as product shell

Current browser work in the Refactor has exposed top-level technical workspaces such as:

```text
Graph
Sources
Cognition
SQL
Events
Admin
```

Observed/current code behavior includes collection/source selectors, raw IDs, preflight/job JSON, SQL editing and receipt/JSON detail panels.

**Classification:** IMPLEMENTATION EVIDENCE / PRODUCT COUNTEREXAMPLE when treated as the ordinary product information architecture.

Why it is not product authority:

- users are asked to navigate implementation subsystems rather than information worlds;
- source/provider machinery is exposed as a primary workflow;
- context does not naturally persist as a person/player/topic/team/etc. pivots across evidence, ranking, relations and acquisition;
- generic JSON/record inspection substitutes for domain-native realization;
- the UI mirrors backend boundaries instead of the user's subject/task.

Operator/admin surfaces remain legitimate; the counterexample is making them the primary product mental model.

## 2. Fragmented old Explore tools

Old Laplace exposed separate routes/tools including Browse, Highway, Mesh, Warehouse, Matchup, Constellation, Walk and Audit.

These demonstrate useful capabilities and visualizations, but their fragmentation is not final UX law.

Direct/current reconstruction rule:

> topic, entity, type/noun, relation, source, ranking/standing, consensus/evidence, time/event, structural tier and applicable visualizations must be able to appear as contextual facets/pivots of the same information world where relevant.

A separate tool page may still exist for expert use. The user should not need to understand which internal subsystem owns the question before exploring the thing they care about.

## 3. Source-first chess acquisition

Historical chess UI required the user to know/select providers such as Chess.com/Lichess/FIDE and move between provider-specific ingestion/search workflows.

**Classification:** HISTORICAL IMPLEMENTATION COUNTEREXAMPLE.

Correct ordinary workflow:

```text
search person/player/entity
-> resolve candidates
-> show known identities/evidence
-> discover eligible missing provider/source evidence
-> authorize/preflight acquisition if needed
-> acquire with source provenance
-> reconcile referential identity under evidence law
-> return to the same entity world
```

Source-estate-first operation remains valid for operators/admins performing bulk seeding, recipe/release management, credential management, scheduling and failure recovery.

## 4. Provider-name convenience becoming identity evidence

Historical chess behavior has included provider-discovery paths that risked turning provider real-name text into `CORRESPONDS_TO`-style referential evidence.

**Classification:** COUNTEREXAMPLE.

Convenience discovery may produce a candidate. It cannot establish referential equivalence without the declared evidence/authority contract.

```text
similar name / provider search result
    != canonical person identity
    != witnessed correspondence
```

## 5. Master/detail narrowing of the sports-site metaphor

A historical UI decision documented “a league site, not a lab” and illustrated:

```text
league -> division -> team -> position -> player -> roster -> schedule
```

That was useful evidence against graph-lab UX, but it was later over-read as the complete product architecture.

**Classification:** HISTORICAL PRODUCT EVIDENCE, superseded in its narrow master/detail interpretation by direct correction.

Current reconstruction law: the sports/reference-site analogy means a dense, domain-native, multi-entry information world. The hierarchy above is one projection; peer pivots include topics, types/nouns, relations, sources, rankings, consensus/evidence, events/time and structural altitude.

## 6. “Start at a word” narrowing

Historical Browse/Topic/language screens prove some useful text/language exploration, but Laplace exploration is not “type a word and browse its neighbors.”

**Classification:** IMPLEMENTATION/PARTIAL PRODUCT EVIDENCE.

A user may start at:

- a topic;
- person/player/team/organization;
- type/class/noun;
- relation;
- source;
- ranking/leaderboard;
- event/game/time slice;
- structural tier;
- domain/world;
- canonical identity or realized content.

Text is one modality/domain surface, not the product ontology.

## 7. Standing presentation collapse

Historical/current product surfaces have mislabeled or collapsed distinct quantities.

Required distinction:

```text
rating      = arena-scoped standing state
RD          = uncertainty/deviation
eff_mu      = rating - 2*RD (conservative bound)
witnesses   = eligible observation cardinality under selected law
refutation  = separately declared verdict/confidence test
Elo         = explicitly defined match/performance measurement only
```

Generic witness count must not be labeled `games` unless the selected domain/query proves witnesses are game occurrences.

**Classification:** UI/measurement COUNTEREXAMPLE.

## 8. Current screenshots as defect evidence

Installed screenshots showing empty/lab-like exploration, raw identifiers, disconnected tools, no useful chat result, or implementation-centric controls prove deployed behavior at that moment. They do **not** prove those screens define Laplace's intended product.

Screenshots are therefore retained as:

- conformance evidence;
- usability evidence;
- regression evidence;
- product-counterexample evidence.

They are never elevated above direct/current product law.

## 9. Mechanism-level counterexamples that must stay separate from UX

The dossier must not let UI damage obscure deeper machine substitutions. Separate counterexample families include:

- rank-1 topic/focus selection before whole-observation cognition;
- observation/internal generation treated as semantic attestation;
- geometry/proximity promoted to semantics;
- packed trajectory payload measured as if it were live S3 geometry;
- fixed-hop/KNN/global adjacency substituted for typed query-relative cognition;
- one scalar standing/importance score substituted for typed evidence/arena state;
- `VO := QK` or one flattened matrix substituted for target-role-specific compilation;
- accelerator miss treated as semantic absence;
- perfcache state treated as canonical truth;
- successful component/API/model load treated as whole-product acceptance.

These require mechanism dossiers, not UI fixes.
