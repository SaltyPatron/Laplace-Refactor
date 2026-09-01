# Laplace execution to finish

This document is the current forward implementation path. It is loaded after stable invention/architecture authority and before dated continuation state. It tells implementation agents what to build next and how to prove the product path.

## Governing sources

Use `contracts/authority-stack.json` for authority and load order. The primary implementation references are:

- `docs/product/CONSTITUTION.md`
- `docs/product/INVENTION_MODEL.md`
- `docs/architecture/OPERATIONAL_MODEL.md`
- `docs/architecture/ISA.md`
- `docs/architecture/BOUNDARIES.md`
- `docs/architecture/ENGINEERING_STANDARDS.md`
- `requirements/product.yaml`
- `contracts/operation-model.json`
- `contracts/recipe-model.json`
- `contracts/source-profile-model.json`
- `contracts/source-admission.json`
- `docs/architecture/ACCEPTANCE.md`
- whole-product program #23

Direct inventor corrections supersede this execution projection.

## Delivery state

Active work uses four states:

- **implementation obligation** — repository/agent work to complete;
- **external prerequisite** — a condition outside repository/agent control, recorded with exact evidence, owner and satisfaction action;
- **failed acceptance** — implementation exists but executable behavior fails;
- **delivered** — authoritative branch + required package/install/activation/readback + required acceptance + operator-visible result agree.

Supporting artifacts are produced only when they advance the executable vertical slice they protect.

## Vertical slice A — one real machine operation through every layer

Owners: #7, #10, #12 and the ISA/operation-model requirements.

Required path:

`typed input → recipe/program → native batch ISA → PostgreSQL server route → durable state/readback → receipt/exception → public client/API result`

Acceptance:

- one canonical semantic implementation;
- scalar route delegates to vector/batch semantics;
- fixed SQL/C# orchestration only;
- no RBAR/dynamic-SQL/recursive-executor alternate algorithm;
- exact native/PostgreSQL parity;
- package-installed execution, not only build-tree tests;
- deliberate defect proving route drift is detected.

Repeat this pattern for each operation family instead of building control-plane infrastructure in isolation.

## Vertical slice B — complete source profile to durable admitted world state

Owner: #53.

A source profile must enumerate the complete selected physical artifact graph: release, archive, members, files, sidecars and streams. Every constituent has an explicit disposition and exact digest/size/role. A local path is discovery input; the selected profile defines the admitted boundary.

Execution requirements:

1. source estate/profile resolves the exact selected artifacts;
2. generic scheduler opens each independent artifact once;
3. parser/grammar/codec exposes exact concrete structure;
4. recipe lowers it to the universal AST/Merkle DAG;
5. shared native whole-working-set presence/deposition reuses common substructure;
6. evidence/provenance/dependence/reference state is deposited through one semantic owner;
7. readback/recomposition verifies the admitted result;
8. source receipt reconciles every selected artifact, byte/node/record disposition and durable output.

Representative profiles must include structurally different estates: Unicode/UCD/UCA, a multi-file lexical source, UD treebanks, a frame/predicate source, a large monolith, a media/code/model source and a state-transition/game source.

Hidden-file, alternate-release, duplicate-packaging and inventory-vs-execution-set mutants must fail.

## Vertical slice C — query and cognition over admitted state

Owners: #17, #60, #129, #132, plus the cognition execution contract.

Required path:

`query/goal → exact query composition → typed admissible state/relation planes → indexed bulk frontier generation → query-relative standing/geometry/evidence calculation → semantic act → result/why-not receipt`

Requirements:

- ordinary admitted UAX29/grammar content already supplies exact sequence/position/containment/continuation observations;
- seeded semantic sources contribute separate typed facts rather than acting as a prerequisite for ordinary content;
- trajectory, containment, precedence, co-occurrence and related deterministic structure are calculated from physicality/AST state rather than redundantly flattened into permanent semantic edges;
- A* or declared best-first search owns completion semantics;
- KNN/ANN/raw hops/permanent embeddings/arbitrary top-k do not replace typed query-relative calculation;
- perfcache/indexes reuse deterministic calculations without changing semantic authority;
- generated operator programs derive from the active witnessed/calculated planes.

Acceptance uses materially different queries and state mutations to prove the result changes for the right structural/evidentiary reason.

## Vertical slice D — standing, learning and Gödel extension

Owners: #16, #19 and related typed-standing/learning requirements.

- Glicko-2 state is arena-scoped, chronological, evidence-derived and uses real opponent state.
- Dependence/copy relationships prevent duplicated evidence from multiplying support.
- Contextual importance is calculated for the active program and remains distinct from identity/truth/default standing.
- Incompleteness produces typed candidate structures/facts/laws/operators/firmware/programs with ancestry.
- Candidate activation follows evidence/counterexample/authority law; prediction never becomes observation by being generated.

Acceptance must prove chronological invariance, dependence reduction, return legs and candidate rejection/activation behavior.

## Vertical slice E — reusable product surfaces

Owner: #21.

Required generic product pattern:

`browse → rank → entity/profile → relations/evidence/trajectory → neighboring/ranked set → materialize/share`

Required surfaces:

- structural altitude/tier: codepoints, graphemes, words, sentences, documents, higher compositions;
- typed entity/relation/source/world/domain families;
- stable paginated/cursor leaderboards over declared arena/measure/context/epoch;
- profile/evidence/provenance/trajectory views;
- same query/ranking semantics through SQL, CLI, API and web;
- reusable components for board/table/profile/navigation/domain presentation;
- no hard top-K product ceiling and no private UI scoring engine.

Representative domain acceptance includes chess/player/game navigation while proving the generic substrate surface can serve non-chess entity worlds with the same contracts.

## Vertical slice F — installed product acceptance

Owner: #22 with whole-product program #23.

The final acceptance path starts from a clean supported host and ends with an operator using the installed product:

1. acquire/verify dependencies;
2. package/install/activate exact product generation;
3. admit configured heterogeneous world profiles;
4. execute representative bulk substrate operations;
5. query/cognize/learn over that state;
6. use product browse/rank/profile/domain surfaces;
7. restart/replay/recover and reproduce the result/receipts;
8. prove package/object/schema/epoch/semantic parity and bounded performance.

## Phase sequencing rule

The #23 phase graph remains dependency guidance, but each phase must produce the minimum executable behavior required to cross its boundary. An earlier phase may implement a supporting capability for a later vertical slice when doing so is the shortest path to end-to-end proof.

Do not postpone all operator-visible behavior until Phase 7/8. Do not keep expanding contracts, receipts, package controls, exception registries or QA infrastructure without immediately exercising the product behavior they govern.

## Open-PR discipline

Before opening another branch/PR:

1. map the work to one owning issue/vertical slice;
2. inspect existing open PRs for overlapping scope;
3. fold superseded documentation/scaffolding into the implementation owner or close it;
4. keep one mergeable delivery path per accepted behavior;
5. update generated contracts/tests in that same path;
6. merge after required checks and continue to installed/operator proof.

Documentation-only PRs should exist only when documentation is the requested output or when an ambiguity must be removed immediately before implementation.

## Session execution loop

1. Load authority.
2. Select the earliest relevant implementation obligation.
3. Inspect current main, issue owner and physical runtime/source estate.
4. Implement the narrowest coherent vertical slice.
5. Run focused tests, then required integration/product acceptance.
6. Fix failed acceptance and rerun.
7. Merge through repository policy.
8. Install/activate/read back when required.
9. Record the demonstrated result on the owning issue.
10. Continue through the accepted scope until delivered or the user explicitly changes/stops it.

Status updates are execution handoffs: obligation, affected path/operation, acceptance result and next code action.
