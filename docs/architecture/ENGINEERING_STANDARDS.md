# Laplace engineering standards

These standards keep implementation choices subordinate to the complete invention.
They apply to native code, PostgreSQL server integration, SQL, C#, generators, source
recipes, tests, packages, and delivery automation. Passing formatting or compiling is
not sufficient; the implementation must preserve the product laws.

For query, search, conversation, realization planning, model-operator generation, or a
related database hot path, `docs/architecture/COGNITION_EXECUTION.md` and
`contracts/cognition-execution.json` are mandatory execution authority in addition to
the general standards below.

## 1. Begin with the whole operation

Before implementing a component, identify the complete product behavior, its typed
AST inputs and outputs, recipe, ISA instructions, immutable context, semantic owner,
physical providers, persistent and derived effects, receipts, and acceptance defects.
The current branch, one source format, one issue, or one convenient demonstration
cannot define the abstraction.

Every change answers:

- why the behavior exists in the transformer-replacement machine;
- which exact distinctions it consumes, preserves, calculates, witnesses, or realizes;
- how it participates in the persistent Merkle-DAG world model;
- how SQL composes it and the native engine executes it;
- how it behaves in vector, batch, PostgreSQL, replay, cancellation, and failure paths;
- which deliberate defect proves the test detects a wrong mechanism.

Component success is not product success. A lookup that returns rows, a renderer that
reconstructs content, a target artifact that loads, a database containing millions of
entities, or a feature-specific endpoint that responds proves only its declared
boundary. A capability is admitted only after its complete public route passes through
the shared registry, context, recipe compiler, ISA, batch, presence, deposition,
receipt, and exception lifecycle.

At least three materially unrelated whole operations must prove that lifecycle. A
deliberate mutation routes one through a private dispatcher, staging path, persistence
boundary, or opaque internal-error translation and must fail. This is the cohesion
proof that distinguishes one machine from a collection of reusable-looking islands.

## 2. One semantic implementation

C/C++ and PostgreSQL server integration own every semantic operation. PostgreSQL
supplies durable state, transactions, constraints, indexes, statistics, partitions,
and plan-visible set routing. SQL composes and invokes generated typed operations; C#
orchestrates external lifecycle. Neither SQL nor C# contains another cognition/search
engine.

Scalar operations lower to the vector-first implementation. Serial, partitioned,
parallel, direct-native, PostgreSQL, SQL, and managed routes produce identical logical
results and receipt meaning. An optimization may change a physical plan, never the
operation. Failure of a preferred physical plan may not silently select a different
semantic algorithm.

## 3. Generics, interfaces, and reuse

Stable repeated behavior belongs in typed generic abstractions, interfaces, concepts,
abstract lifecycle bases, or generated contracts as appropriate to the language. The
common framework owns registry, complete-program validation, immutable context,
canonical vector/set/run/AST batches, working-set planning, sinks and providers,
resources, cancellation, replay, merge, progress, epochs, activation, and receipts.

A module supplies only the varying law: grammar/codec metadata, typed validation,
lowering or recomposition rules, a semantic kernel, persistence schema declarations,
an access law, and conformance vectors. Source, language, modality, and model names may
select declarations or witnessed state; they may not select another engine branch.

Inheritance or templates are not goals by themselves. An abstraction is admitted when
at least two materially different consumers retain the same invariant and a deliberate
provider substitution proves the shared lifecycle. A generic abstraction cannot erase
type, order, authority, evidence, loss, or completion distinctions.

## 4. Grammars, recipes, and the universal AST

Every digital structure lowers to the typed universal AST whose canonical persistent
form is the Merkle DAG. Tree-sitter parsers, external scanners, standards parsers, and
media codecs are replaceable concrete-syntax providers. Laplace-native grammars are
first-class product languages. None can mint private identity or own semantic state.

Recipes are immutable typed executable laws. They declare grammar inputs, lowering and
recomposition, validation, canonicalization, loss, inverse data, required epochs,
authority, effects, resources, vectors, completion, and receipts. Recipes compile to
the shared ISA before execution. Opaque callbacks and importer-owned semantic switches
are forbidden.

Exact round trips reproduce original bytes. Intentional transformations create new
content, preserve unchanged subtree identities, and record typed edits and relations.
Unknown nodes, fields, errors, ambiguities, unsupported operations, and lossy paths are
explicit outcomes.

An implementation that edits addressable state must classify the state class before
mutation. Witness/provenance/interpretation, occurrence/physicality role,
relation/standing, canonical constituent/value, and intentional AST transformation
have different identity and epoch consequences. Invertible recipes descend to exact
content; probabilistic regeneration cannot impersonate exact recomposition.

## 5. Native code and ABI

- Use fixed-width types at persisted, generated, wire, and ABI boundaries.
- Make ownership, lifetime, nullability, optionality, capacity, alignment, byte order,
  overflow behavior, and error atomicity explicit.
- C ABI structures are versioned and generated where contracts define them. C++
  internals use RAII and typed spans/containers without leaking exceptions across C or
  PostgreSQL boundaries.
- Warnings required by the repository are errors. Narrowing, sign conversion, shadowing,
  aliasing, undefined behavior, and unchecked arithmetic require explicit treatment.
- Numeric recipes declare compiler/provider constraints; ambient fast-math or nested
  thread settings cannot change semantics.
- Resource use derives from the conserved topology grant. Libraries, workers, and
  tools cannot independently oversubscribe the machine.

## 6. PostgreSQL and SQL

PostgreSQL extension and SPI code are normal native engine surfaces. Plans are
schema-qualified, prepared, parameterized, set-oriented, reusable, and measured at
representative cardinality. Per-row SQL, SPI, process, language, network, or
transaction crossings are forbidden on primary batch paths.

SQL owns transactions, constraints, indexes, set routing, program composition, and
result projection. It does not implement recursive semantic algorithms in SQL text.
Public functions use explicit namespaces and typed generated bindings, avoid hidden
session settings, expose planner-visible structure where required, and retain exact
`EXPLAIN (ANALYZE, BUFFERS, WAL, SETTINGS, TIMING)` evidence for performance claims.

For cognition/search paths specifically:

- frontier expansion is set-wise and bounded;
- the compiled query chooses admissible provider families and hard filters are pushed
  into indexes/perfcaches when semantically safe;
- exact native predicates remain authoritative after candidate generation;
- an index/perfcache miss cannot prove semantic absence without a completeness proof;
- one SQL/SPI call per frontier state is forbidden when the same transition family can
  be expanded as one bounded set operation;
- cursors, RBAR, caller-driven per-row loops, recursive SQL/CTEs as the graph/search
  engine, and unbounded adjacency loads are forbidden;
- dynamic SQL generated per candidate, frontier row, relation, or fallback is
  forbidden on the semantic hot path;
- failure or absence of a declared batch/index plan may not silently fall back to a
  scalar/RBAR algorithm; return a typed failure/why-not or select an explicitly
  declared semantically equivalent physical plan with its own receipt and acceptance;
- semantic repair, deferred-write completion, cache-build, vacuum-like repair, or
  maintenance drains may not be prerequisites for otherwise valid admitted state to
  participate in an interactive query. Epoch-pinned readers finishing on a retired
  immutable perfcache generation after atomic handoff are a distinct legitimate
  concurrency lifecycle;
- dynamic DDL/package/schema construction is allowed only inside its explicit
  nonsemantic administrative boundary and cannot become an execution language for
  cognition.

## 7. Typed filtered indexed cognition/search

The canonical native cognition/search physical-plan discipline is:

```text
resolve exact identities / altitude
-> compile finite typed guidance and search state
-> select only admissible transition providers
-> push safe filters into indexed candidate generation
-> form a bounded typed frontier
-> typed filtered indexed A* or declared best-first law
-> fold only program-declared channels
-> update bindings trajectory deficits and completion obligations
-> repeat only while finite resources and unresolved obligations permit
-> semantic act / exact typed why-not
-> realization/effect + complete receipt
```

Use the name A* only when the implementation executes actual `g+h` and satisfies the
admissibility, consistency/reopen, complete typed-state dominance, deterministic tie,
path-multiplicity, finite-boundary, and completion-certificate contracts. Otherwise
name the actual best-first/bounded law. A raw hop, fixed fanout, lookup, KNN/ANN result,
selected topic, or scalar priority is not cognition.

The active frontier is a bounded working set over exact persistent state, not a license
to materialize world-all-pairs adjacency. Exact identity, Merkle-DAG/subtree reuse,
trajectory-prefix/run reuse, transposition/convergent-state reuse, per-epoch result
reuse, and measured typed acceleration are exploited before repeated computation.
Claims that corpus growth is decoupled from active inference work require measured
representative evidence.

## 8. Managed orchestration

C# owns source, session, service, product API, and external-system lifecycle. It uses
generated types and a generic transport/lifecycle spine, batches work, applies
backpressure, propagates cancellation and authority, and preserves native receipts.
It does not normalize semantics, reinterpret errors, rebuild ASTs, or implement a
private cognition path.

## 9. Persistence, acceleration, and effects

Canonical content/AST state, occurrences/testimony, reproducible derived epochs,
retained inference/working state, acceleration artifacts, and external effects have
separate authorities and publication boundaries. Presence is proven set-wise over the
whole working set. Durable mutation is transactional, idempotent under its declared
replay law, and never inferred from row count alone.

Perfcaches are typed compiled execution planes with module-specific access laws. They
must retain canonical semantic parity, whole-artifact validation, coherent activation,
reader pinning, and independent replacement. They cannot become a second authority.

Sparse state is the default. Absence, unobserved state, unknown, contradiction, and a
small numeric value remain distinct; code may not allocate an all-pairs surface to
erase that distinction. Every proposed index, materialization, and perfcache includes
a representative paired with/without workload, selected plan, build/write/WAL/storage/
bloat cost, read CPU/I/O/latency benefit, work avoided, rebuild law, observation
window, and removal condition. Acceleration removal may fail a performance budget but
must preserve semantics and receipt meaning.

## 10. Tests and evidence

Tests execute the production implementation at the boundary they claim. Important
tests have a deliberate mutant that changes the mechanism and must fail for the exact
reason. Mocks may isolate a physical provider but cannot establish engine semantics,
PostgreSQL integration, installation, conversation, model behavior, or performance.

Receipts reconcile inputs, outputs, losses, unknowns, persistent effects, derived
effects, resources, timings, and completion. Performance evidence includes the command,
machine, real input, samples, timing boundary, CPU, memory, I/O, database calls, WAL,
and durable output counts. Small-fixture extrapolation is prohibited.

Representative cognition/search receipts additionally include the compiled query and
boundary, selected providers/indexes/partitions/perfcache generations, frontier states
in/out, candidates generated/rejected/accepted, rows examined, relevant heap/table
fetches, server/SPI/native crossings, `g`/`h` evaluations, prune classes, reopen events,
path counts, peak frontier and working memory, CPU, I/O/buffers, elapsed time, and the
completion/optimality/upper-bound disposition. A logically correct result reached by a
forbidden physical plan fails scalable-search acceptance.

Deliberate search/database mutants include per-frontier-state SPI, recursive SQL,
dynamic per-candidate SQL, scalar/RBAR fallback, giant pre-filter adjacency, dropped
hard filters, accelerator-miss-as-absence, and semantic maintenance drain. Each must
turn the exact owning acceptance gate red.

## 11. Build, generation, and repository discipline

Contracts generate identifiers, ABI declarations, bindings, documentation tables, and
other repeated authority. Generated files identify their generator and exact source
contract and are reproducible. Dependency inputs come only from verified locks and
checksums. Builds are out of tree; packages install into versioned roots; activation is
atomic and independently verified.

Historical implementation stays outside build, include, package, and test discovery.
New code is authored from product requirements, independent standards, and verified
upstream dependencies. Unrelated work is preserved, and concurrent changes use
isolated worktrees.
