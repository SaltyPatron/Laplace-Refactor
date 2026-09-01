# Laplace implementation-agent execution contract

This file projects the governing invention and executable contracts into concrete implementation behavior. It is a **verified agent-facing projection** of higher authority; editing it cannot create, remove, or narrow product authority or requirements.

## Required authority load order

Before selecting or changing work, load `contracts/authority-stack.json` and follow its order:

1. `docs/audits/DIRECT_REQUIREMENT_EVIDENCE.md`
2. `docs/product/CONSTITUTION.md`
3. `docs/product/INVENTION_MODEL.md`
4. this `AGENTS.md`
5. `docs/architecture/OPERATIONAL_MODEL.md`
6. `docs/architecture/ISA.md`
7. `docs/architecture/BOUNDARIES.md`
8. `docs/architecture/ENGINEERING_STANDARDS.md`
9. `requirements/product.yaml`
10. `contracts/operation-model.json`
11. `contracts/recipe-model.json`
12. `contracts/source-profile-model.json`
13. `contracts/source-admission.json`
14. `docs/architecture/ACCEPTANCE.md`
15. `docs/product/ROADMAP.md`
16. dated audit/continuation evidence
17. `state/continuation.json`

Direct inventor corrections outrank every derived artifact. Stable product law outranks roadmap, issue, branch, current implementation and observed runtime state. When derived sources disagree, repair the lower source instead of asking the user to re-specify the invention.

### Machine-joined authority requirements

This projection must remain joined to the executable requirement graph. At minimum, implementation work preserves and traces the authority represented by `LP-AST-001`, `LP-GOVERNANCE-001`, `LP-RECIPE-001`, `LP-COHESION-001`, `LP-MODEL-006`, `LP-ADMISSION-001`, `LP-CONNECTION-001`, `LP-LIMITS-001`, `LP-EXCEPTION-001`, `LP-ENTITY-WEB-001`, `LP-FEDERATION-001`, `LP-MATERIALIZATION-001`, and `LP-ACTIVATION-001`. These joins are requirement references, not permission for this file to redefine them.

## Delivery accountability

An implementation agent that accepts work owns that implementation obligation until the required behavior is delivered or the user explicitly stops/replaces the scope.

Use only these active delivery states:

- **implementation obligation** — repository/agent work to complete;
- **external prerequisite** — a condition genuinely outside repository/agent control, recorded with exact evidence, owner and satisfaction action;
- **failed acceptance** — implementation exists and a required executable check/product behavior fails;
- **delivered** — implementation is merged to the authoritative branch, required package/install/deployment/readback is complete, required acceptance is green, and the requested operator-visible behavior is demonstrated.

Do not use `blocker` as a generic explanation for unfinished work. Missing code, plumbing, APIs, tests, CI sequencing, package wiring, branch/PR state, scheduler design, performance defects, source coverage, deployment code or stale generated artifacts are implementation obligations unless an external prerequisite is specifically proven.

A plan, issue, PR, contract, receipt, manifest, test declaration, status report, screenshot or component count is supporting work unless that artifact itself is the requested output. Supporting work must advance an executable vertical product slice.

When acceptance fails, fix the cause and continue the same workstream. A user correction expands or corrects the implementation obligation; it is not a reason to stop at analysis or open an unrelated tangent.

## Product scope

- Implement the complete universal product defined by the constitution and invention model.
- Sequencing changes order, not scope.
- Preserve every applicable language, modality, model family, product surface, platform, operation and acceptance condition.
- Sources provide operands/evidence/constraints/realizations; they are not the product architecture.
- Product behavior must be executable through the same public machine lifecycle rather than private feature implementations.

## Semantic ownership and execution

- C/C++ plus PostgreSQL server integration own every semantic operation and calculation.
- SQL composes fixed typed programs/queries and owns transactional orchestration; it is not a second engine.
- C# orchestrates sources, sessions, services, APIs and lifecycle; it is not a second engine.
- Every semantic operation has one canonical implementation.
- Batch/vector/set forms are primary. Scalar forms delegate to the same canonical implementation.
- A method named `batch` is insufficient if it loops through scalar, dynamic-SQL, recursive-query or SPI calls underneath.
- Generic reusable typed interfaces own common behavior. Providers may replace physical mechanism without changing meaning.
- Dynamic SQL, RBAR, recursive executor traversal, source-specific semantic dispatch and adapter-private algorithms are rejected from semantic hot paths unless the governing contract explicitly defines them as the physical operation.
- Perfcache/index/materialization reuses deterministic work and remains reconstructible acceleration, never semantic authority.

## Universal structure

- Exact digital structure lowers through typed universal AST recipes into the content-addressed Merkle DAG.
- Canonical content identity excludes source/path/parser/span metadata.
- Occurrence, order, containment, gap, trajectory, provenance, source, language, modality and model state remain typed witnessed structure.
- Same content converges to the same identity; different occurrences remain distinct where occurrence identity is part of the required observation.
- Parsers/codecs/source adapters expose concrete structure through common recipe/ISA contracts and do not own private semantics.

## Complete source-estate execution

Every selected source profile declares the complete selected physical artifact graph before admission:

- release/archive/file/member/sidecar/stream identity and exact digest/size;
- explicit disposition for every constituent: admitted, equivalent packaging, superseded, excluded-with-reason, unsupported-with-why-not or absent;
- grammar/parser/codec/recipe dependencies;
- exact scheduling/resume/journal grain;
- semantic grouping such as release, language, treebank, split or corpus as witnessed/dependency metadata.

Execution opens each independent physical artifact once through the generic scheduler, streams read → parse → lower/compose, and hands bounded finalized sets to the shared native/PostgreSQL deposition path. Internal segmentation may distribute expensive work without changing physical artifact completion identity.

Inventory, execution, journal and source receipt must reconcile the same selected artifact set. Source completion cannot be inferred from a subset chosen by one adapter.

## Query, cognition and search

- Query/cognition compiles typed programs over exact structure, geometry, evidence, standing, relation law, context and goals.
- Typed search declares admissible relations/direction/time/dependence/resource/completion conditions.
- KNN, ANN, raw hop count, permanent embedding, one scalar adjacency, arbitrary top-k or lookup-only behavior cannot replace query-relative calculation.
- A* or declared best-first execution generates frontier candidates in bulk using the common native engine and indexes/perfcaches.
- UAX29-only observations are already valid witnessed sequence/continuation/position/context input; seeded semantic sources add separate typed facts rather than becoming a prerequisite for ordinary content to exist as knowledge.
- Gödel/incompleteness work generates typed candidates with ancestry and attempts disproof through the same engine; activation follows the governing evidence/authority law.

## Product surfaces

Product surfaces are reusable views over the same machine state and operations.

- Provide generic browse → rank → entity/profile → relations/evidence/trajectory → neighboring/ranked-set navigation.
- Support structural altitude/tier navigation across codepoints, graphemes, words, sentences, documents and higher compositions plus typed domain/entity worlds.
- Leaderboards declare arena/measure/context/epoch and use stable cursor/pagination over the complete selected set. Internal bounded chunks must not become a hard top-K product ceiling.
- Web/API/CLI/SQL use the same ranking/query semantics.
- Domain products such as chess specialize presentation and domain operands while reusing the generic query, ranking, paging, identity, evidence and component surfaces.
- Reuse board/profile/table/navigation components instead of creating feature-specific duplicates.

## Evidence and acceptance

- Tests execute the implementation and assert the required behavior.
- Important acceptance has a deliberate defect proving that the test detects the named failure.
- Performance acceptance records exact input, machine, CPU/memory/I/O/database calls, sample count, timing boundary and durable output counts.
- Measure complete installed-path behavior at the requested scale; do not extrapolate small fixtures into product throughput.
- Every route that claims semantic parity executes the same canonical implementation or proves byte/semantic equivalence to it.
- Required artifact/package/receipt checks support product acceptance; they do not replace the behavioral result.

## Finish-line sequencing

Prefer vertical capability over additional control-plane scaffolding. The program order is the dependency graph in #23, but every phase must end in executable behavior:

1. product/runtime foundation supports the engine rather than becoming the end product;
2. universal framework/ISA executes real bulk operations;
3. substrate storage/presence/deposition/readback operates through one native semantic owner;
4. complete heterogeneous source profiles execute through one generic source-estate/scheduler/deposition contract (#53);
5. query/answerability, realization and learning execute against admitted world state;
6. model-independent compilation/execution uses the same machine;
7. reusable entity-world/product surfaces expose browse/rank/drill-down and domain workflows (#21);
8. complete installed-product acceptance exercises the whole path.

An earlier phase may implement the minimum supporting capability required by a later vertical slice when that is the shortest route to an end-to-end result. Phase sequencing must never be used to postpone all operator-visible behavior until the end.

## Repository/PR discipline

- Prefer finishing the existing owning issue/branch/PR over creating parallel partial work.
- Do not maintain overlapping open PRs for one accepted scope.
- A PR title/body states the complete behavior advanced, implementation owner, executable acceptance and operator result.
- Documentation-only work is valid when documentation is the requested deliverable or is required to remove an ambiguity that prevents immediate implementation; follow it with the implementation in the same accepted workstream.
- Keep generated contracts/registries/inventories synchronized with the implementation in the same change.
- Preserve unrelated user work and use isolated worktrees for concurrent implementation.
- Dependency input comes from verified locks/upstream sources; historical implementation is behavioral evidence only.
- Do not enable/request automatic Copilot code review.

## Continuation and communication

- A direct user stop/pause/cancel halts tool calls and mutations until the user explicitly resumes.
- Corrections, status questions and refinements do not stop an active accepted workstream; incorporate them and continue.
- Read `state/continuation.json` after stable authority and verify it against the physical tree/runtime before using it for continuation.
- Update continuation/roadmap/issue acceptance when implementation changes the actual execution state.
- Status prose is an execution handoff: state the implementation obligation, affected operation/path, acceptance command/result, and next code action.
- Treat failed checks as implementation obligations: diagnose, fix, rerun and continue toward the requested operator-visible result.
- The user controls scope and stop conditions; the implementation agent owns execution quality within accepted scope.
