# Native cognition execution contract

## Status and authority

This document is the single operational description of Laplace's native cognition
execution path. It consolidates stable invention laws already expressed by the
Constitution, Invention Model, Operational Model, Native Cognition Mathematics,
Engineering Standards, requirements, and Issues #17, #60, and #132. It does not make
an implementation complete merely by existing.

The phrase **forward pass** is used only as a comparison with conventional
transformers. Laplace's native operation is not autoregressive token-by-token dense
attention. It is a finite, query-relative execution program over exact persistent
substrate state.

## 1. The execution spine

A native cognition program follows this logical spine:

```text
EXACT INPUT / OBSERVATION
    -> RESOLVE canonical identities + selected structural altitude
    -> ORIENT from goal, discourse/session state, authority and obligations
    -> COMPILE a finite typed guidance/search program
    -> SELECT only admissible transition-provider families
    -> SCAN indexed physicality/occurrence candidates
    -> SCAN indexed eligible typed facts/testimony
    -> SCAN optional geometry/locality/standing candidates
    -> PUSH safe hard filters into candidate generation
    -> FORM a bounded typed frontier
    -> ROUTE with typed filtered indexed A* or the declared best-first law
    -> FOLD only program-declared structural/semantic/evidence/standing channels
    -> UPDATE guidance, bindings, trajectory, deficits and completion obligations
    -> REPEAT only while unresolved obligations and finite resources permit
    -> SELECT a complete semantic act or typed why-not result
    -> REALIZE / EFFECT through the declared modality or operation recipe
    -> WITNESS the exact result, path, evidence boundary and execution receipt
```

This sequence is one logical program. PostgreSQL, PostGIS, perfcaches, native memory,
and other providers may produce different physical plans without changing its meaning.

A raw graph hop, fixed fanout, one-hop lookup, selected topic, nearest-neighbor result,
scalar score, fluent continuation, or nonempty row set is not a forward pass.

## 2. Exact identity is resolved before contextual meaning is selected

Laplace does not need to rediscover the identity of already admitted content on every
request. Canonical content resolves exactly. Its constituents, containing
compositions, physicality trajectories, occurrences, admitted interpretations,
translations, senses, relations, testimony, provenance, dependence roots, and standing
remain separately addressable state.

Therefore the runtime question is not generally:

> What does this opaque token probably mean?

It is:

> Which admitted aspects of this exact content are admissible and useful for the
> current goal, context, structural altitude, evidence boundary, authority and
> completion obligations?

Canonical identity is exact. Meaning is not flattened into identity: competing senses,
translations, testimony, contradictions, contexts and historical epochs remain typed
and independently inspectable.

## 3. Observation and fact planes remain separate

Ordinary admitted content is already usable observation state. Physicality and
occurrence indexes can calculate containers, ancestors, constituents, ordinals, gaps,
precedence/following, recurrence, shared-container incidence, trajectory prefixes,
continuations, and source/session/game paths without first materializing semantic edge
rows for those structural facts.

Seeded or later witnessed facts contribute separately typed definitional, taxonomic,
translation, equivalence, causal, semantic-role, standards, game, model, or other
relations. Their provenance, dependence, contradiction and standing remain evidence
state rather than being converted into occurrence counts.

A query may use either plane independently. It may combine them only through the
compiled operator law. Neither plane is a universal adjacency graph.

## 4. Typed filtered indexed search

The canonical search umbrella is **typed filtered indexed search**. When the declared
heuristic and state laws satisfy the proof obligations below, its optimal variant is
**typed filtered indexed A\***.

For a compiled query `q`, the search program contains at least:

```text
start state(s)
enabled typed transition families
hard source/context/time/world/authority/evidence filters
path-dependent typed state coordinates
transition cost law g
remaining-cost estimate h
state identity/dominance law
reopen law
deterministic tie law
requested path multiplicity
terminal/completion predicate
finite CPU/memory/I/O/database/search boundary
```

The planner does not first load a large generic neighborhood and filter it afterward.
For every transition family it chooses the narrowest semantically safe candidate
provider and pushes admissible hard predicates into that provider before expansion.
Examples include:

- constituent, containment, ordinal, run and trajectory indexes;
- relation endpoint, type, role and direction indexes;
- source, context, valid-time, observation-time, world, visibility, authority and
  evidence-epoch selectors;
- dependence-root selectors;
- typed standing-lane selectors;
- GiST, Hilbert, point/curve/set/manifold and other geometry candidate indexes;
- immutable perfcache modules whose declared access law matches the requested plane.

Candidate generation is acceleration, not truth. Exact native validation remains the
authoritative predicate wherever an index or perfcache can produce false positives.
An accelerator miss cannot establish semantic absence unless its completeness contract
for the exact boundary proves that conclusion.

For certified A*:

\[
f_q(s)=g_q(s)+h_q(s)
\]

must use nonnegative declared transition costs for the claimed optimum, an admissible
heuristic, consistency or explicit reopen semantics, complete typed state identity,
deterministic tie handling, the requested path multiplicity, a finite declared
boundary, and a completion certificate. If any condition is missing, the engine names
the actual bounded/best-first law and returns reachability, upper-bound, partial,
unknown, exhausted, or another exact typed disposition rather than claiming an
optimum.

## 5. Frontier generation is set-wise and bounded

The frontier is a typed working set, not a cursor over database rows.

For a logical iteration with frontier `F_t`, candidate generation is conceptually:

\[
C_t = \bigcup_r \operatorname{IndexedProvider}_{q,r}(F_t)
\]

\[
A_t = \operatorname{ExactFilter}_q(C_t)
\]

\[
F_{t+1}=\operatorname{SearchLaw}_q(A_t)
\]

where `r` ranges only over transition families enabled by the compiled program.

The implementation may partition batches for finite resources, but scalar execution is
a one-element batch of the same semantic operation. Partitioning, PostgreSQL routing,
SPI boundaries, or provider choice cannot change the logical result.

Corpus growth therefore does not authorize proportional whole-corpus work. The
intended economics come from exact identity reuse, Merkle-DAG/subtree reuse,
trajectory-prefix and run reuse, transposition/convergent-state reuse, per-epoch result
reuse, selective indexes/perfcaches, hard-filter pushdown, and a bounded active
frontier. Any complexity claim remains a measured contract, never an architectural
slogan.

## 6. Fold and completion are part of the same pass

Search does not end at retrieval. Surviving candidates retain separate channels until
the current program explicitly combines them. Possible channels include exact
structure, semantic compatibility, relation transport, trajectory/geometry, source and
context, dependence, contradiction, epistemic standing and uncertainty, novelty,
resource cost, and remaining obligations.

A permanent global weight cannot impersonate the query-relative comparison law.
Glicko-2 standing is one eligible typed input in a declared lane; it is not meaning,
truth, traversal, or universal importance.

Each iteration updates the guidance state. The next query is projected from the new
bindings, trajectory, deficits and unresolved obligations. Cognition stops when the
semantic act is complete, a permitted unresolved result is reached, or a declared
finite limit is exhausted.

## 7. Exact rise, typed edit, and descent

The composition system must preserve the distinction between an identity-preserving
observation change and a content-changing transformation.

An edit operation first resolves the addressable target and the state class being
changed. Its receipt must classify the consequence, for example:

```text
witness / provenance / interpretation edit
    -> canonical content unchanged; new witnessed state or evidence epoch
physicality-role / occurrence edit
    -> canonical content unchanged; new occurrence/physicality state as declared
relation / standing edit
    -> canonical endpoints unchanged; new testimony/calculation/standing epoch
canonical constituent or value edit
    -> new canonical content identity and every affected ancestor composition
intentional AST transformation
    -> new exact composition plus a receipted relation to the original
```

When a recipe is declared invertible, descending through the resulting composition
must reproduce the exact declared output bytes/content. No embedding inversion,
probabilistic regeneration, or approximate text reconstruction may substitute for an
exact inverse contract.

This is the machine form of "bubble up, change the typed state, bubble back down": the
change is exact because the system knows which state class changed and which identities
must or must not change.

## 8. Native engine, PostgreSQL and SQL ownership

The semantic search/cognition algorithm belongs to the native C/C++ and PostgreSQL
server engine. PostgreSQL supplies durable state, transactions, constraints,
partitions, statistics, indexes and plan-visible set routing. SQL composes and invokes
typed operations; it does not become another cognition engine.

Primary cognition/search paths therefore require prepared/parameterized, reusable,
set-oriented plans at representative cardinality. Native vector/batch crossings are
bounded and measured.

The following are forbidden as primary semantic/search mechanisms:

- cursor-driven cognition;
- RBAR or caller-driven per-row traversal;
- one SQL/SPI query per frontier state when a set-wise frontier operation is possible;
- recursive SQL/CTEs used as the native graph/search engine;
- dynamic SQL strings generated per candidate, frontier row, relation, or fallback;
- scalar-query fallback after a batch/index path fails, unless the fallback is an
  explicitly declared equivalent finite physical plan with its own receipt and
  performance acceptance;
- unbounded whole-corpus scans or giant adjacency materialization before filtering;
- unbounded in-memory edge loading;
- row triggers or per-row cache maintenance on scalable paths;
- an index/perfcache miss treated as authoritative absence without a completeness
  proof;
- hidden semantic repair, deferred-write completion, cache-build, vacuum-like repair,
  or maintenance drains that an otherwise valid interactive query must wait on before
  its admitted state is usable.

`drain` is not globally forbidden terminology. Epoch-pinned readers may legitimately
finish against a retired immutable perfcache generation after an atomic generation
switch. That lifecycle drain is distinct from semantic maintenance inserted into the
cognition path.

Administrative DDL/package generation may use dynamic construction where required by
its own explicit boundary. Such use cannot leak into the semantic hot path or become a
private execution language.

## 9. Required execution receipt and performance evidence

Every representative cognition/search execution must expose enough metrics to prove
what work actually happened. At minimum, where applicable, the receipt records:

```text
compiled program / query identity
world, evidence, source, time, context and authority boundary
selected transition-provider families
index / partition / perfcache generation used by each provider
frontier states in / out
candidate rows generated
candidate rows rejected by hard filters
exact candidate states accepted
rows examined
index scans and relevant heap/table fetches
server/SPI/native crossings
g and h evaluations
dominance prunes
hard-filter / authority / dependence / contradiction prunes
reopen events
requested and returned path count
peak frontier width
peak working memory
CPU time
I/O time and bytes / buffers where available
elapsed time
result, upper-bound/optimality/completion disposition
open obligations and continuation condition for incomplete work
```

PostgreSQL evidence includes representative `EXPLAIN (ANALYZE, BUFFERS, WAL,
SETTINGS, TIMING)` and `pg_stat_statements`/equivalent observations where the route
uses PostgreSQL. A claim of index use must identify the selected plan; a claim of
speedup must compare the same logical program and boundary with the relevant
acceleration removed or substituted where practical.

## 10. Required deliberate defects

Acceptance must turn red when any of these mutants is introduced:

- raw-hop or fixed-fanout equivalence;
- endpoint-only state dominance;
- `g`-only priority while claiming A*;
- known path reported as shortest without a completion proof;
- universal adjacency or one global embedding/similarity plane;
- per-frontier-state SQL/SPI expansion;
- recursive SQL search engine;
- dynamic per-candidate SQL cognition;
- scalar/RBAR fallback on a batch path;
- dropped source/context/time/evidence/authority filter;
- dependent copies counted as independent evidence;
- accelerator miss treated as absence;
- silent maintenance/repair drain required before query use;
- unbounded adjacency load or full-corpus scan where the declared indexed provider is
  available;
- result parity without the required plan/crossing/frontier/performance receipt.

A mutation test that merely checks that rows still appear is insufficient. The mutant
must fail the exact semantic or physical-plan property it violates.

## 11. Implementation ownership and current proof boundary

- `#7` owns canonical identity/composition/physicality and indexed structural
  candidates.
- `#14` owns typed perfcache acceleration and generation handoff.
- `#16` owns testimony, lineage, adjudication and evidence epochs.
- `#17` owns guidance, typed operators, search and answerability.
- `#60` owns typed connection/search state, indexed frontier generation, A*/best-first
  semantics and certificates.
- `#110` owns typed Glicko-2 standing consumed as an eligible search channel.
- `#132` owns the bridge from ordinary observation/physicality plus seeded facts into
  the common live/target operator program.
- `#18` owns semantic acts and realization.
- `#129` owns compilation of selected substrate operator programs into conventional
  target-model roles.

A primitive search kernel, graph viewer, one-hop query, historical implementation, or
green contract test does not prove this complete execution path. Product proof requires
the installed public route to execute the same logical program with the required
semantic, plan, performance and deliberate-defect evidence.
