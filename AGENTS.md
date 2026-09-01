# Working agreements

## Projection authority

- This file is a verified agent-facing projection of governing direct evidence,
  stable product law, and executable requirements. It is not itself inventor-direct
  evidence, and editing it cannot create, supersede, or narrow a Laplace requirement.
- Product authority projects `LP-PRODUCT-001`, `LP-UNIVERSAL-001`, `LP-AST-001`,
  `LP-GOVERNANCE-001`, `LP-FIRMWARE-001`, and `LP-CLEANROOM-001`.
- Architecture projects `LP-ISA-001`, `LP-FRAMEWORK-001`, `LP-REUSE-001`,
  `LP-COHESION-001`, `LP-RECIPE-001`, `LP-BULK-001`, `LP-HIGHWAY-001`, `LP-ADMISSION-001`,
  `LP-COGNITION-004`, `LP-QUERY-001`, `LP-SEARCH-001`, `LP-CONNECTION-001`,
  `LP-LIMITS-001`, and `LP-EXCEPTION-001`.
- Product-surface and node symmetry project `LP-APPLICATION-001`,
  `LP-ENTITY-WEB-001`, `LP-ENTITLEMENT-001`, `LP-NODE-001`,
  `LP-FEDERATION-001`, and `LP-PLACEMENT-001`.
- Evidence and persistence project `LP-STORAGE-001`, `LP-MATERIALIZATION-001`,
  `LP-PERFCACHE-001`, `LP-ACTIVATION-001`, `LP-DOC-001`, and the exact acceptance and
  continuation boundaries loaded by `contracts/authority-stack.json`.
- `docs/architecture/COGNITION_EXECUTION.md` and
  `contracts/cognition-execution.json` are the consolidated stable/executable
  projection for native cognition execution. An agent implementing query, search,
  conversation, model-operator generation, or a related database hot path must load
  them before selecting a physical plan.

## Product authority

- The user's direct requirements define Laplace.
- The current-implementation archive is not a specification, design template, source
  library, or completeness boundary.
- Do not copy code, SQL, schemas, tests, scripts, workflows, layouts, names, comments,
  or configuration from the current implementation.
- Use independent standards and clean upstream dependency sources.
- Historical material may supply a behavioral counterexample only. Restate the
  counterexample without importing its implementation.

## Scope

- Laplace is the complete universal product described in
  `docs/product/CONSTITUTION.md`.
- Sequencing work does not reduce product scope.
- Do not silently omit a modality, language, model family, product surface, platform,
  operation, requirement, or acceptance condition.
- Do not substitute plans, issue lists, documentation, scaffolding, or status reports
  for an implementation outcome.

## Architecture

- C/C++ and PostgreSQL server integration own the engine and every semantic operation.
- PostgreSQL supplies durable state, transactions, constraints, statistics, indexes,
  partitions and plan-visible set routing. SQL composes and invokes typed operations.
  SQL and C# are orchestrators; they do not contain another cognition/search engine.
- All product behavior executes through the typed substrate instruction set.
- Every operation has one canonical implementation.
- Batch and bulk forms are primary and must preserve exact single-item semantics.
- Every digital structure is represented as a typed universal AST whose canonical
  persistent form is the content-addressed Merkle DAG.
- Grammars and recipes decompose, transform, and recompose typed trees through the
  common ISA; a parser, codec, source adapter, or template is not a semantic engine.
- Centralize common behavior in typed generic interfaces, generated contracts, and
  reusable lifecycle bases. Providers may vary physical mechanism, not meaning.
- Text is not architecturally privileged.
- Language, modality, source, and model are witnessed dimensions, not engine branches.
- People, organizations, accounts, entitlements, achievements, interfaces, nodes, and
  federation are not application exceptions. They use the same referential state,
  AST, testimony, governance, recipe, effect, and receipt machinery.
- Profiles, feeds, résumés, portfolios, and personal webs are audience-authorized
  materializations over entity worlds, not opaque account fields or private engines.
- Typed connection search declares admissible relations, direction, time, standing,
  dependence, boundary, and completion. Raw hops, KNN, ANN, fixed fanout, lookup, and
  a found path cannot impersonate semantic distance, optimum, cognition, or complete
  answerability.
- Every program is finite. Partial, upper-bound, unsupported, denied, exhausted,
  contradicted, and unknown results remain distinct and include an exact why-not
  receipt.
- Hardware faults, provider unavailability, resource exhaustion, invalid programs,
  implementation defects, authority denial, semantic contradiction, and epistemic
  unknowns are different machine conditions. Their generated exception law declares
  priority, restart, retry, reroute, replay, durability, and publication behavior.
- Hardware, operating system, storage, placement, and federation providers may change
  validated physical plans and performance, never ISA meaning or logical results.
- Model export names and independently tests the exact invariant it preserves. Do not
  accept an unqualified claim of `faithful`, expected shape, loadability, correlation,
  or one academic fixture as semantic or behavioral proof.
- A capability exists only after its complete public program traverses the shared
  registry, context, recipe, ISA, batch, presence, deposition, receipt, and exception
  lifecycle. Component counts, screenshots, lookups, historical behavior, and private
  feature routes cannot promote a missing whole operation.
- Model behavior is experiment-scoped witnessed state. Formal nonzero support is not
  material support; top-k, pruning, or thresholding requires a versioned measured
  causal/behavioral law, loss boundary, withheld probes, and receipts under
  `LP-MODEL-006`.

## Cognition execution and database hot paths

- The canonical native cognition spine is:

  ```text
  resolve exact identities + structural altitude
    -> orient from goal/context/authority/obligations
    -> compile finite typed guidance/search state
    -> select admissible indexed provider families
    -> push semantically safe hard filters into candidate generation
    -> generate a bounded set-wise typed frontier
    -> typed filtered indexed A* or the declared best-first law
    -> fold only program-declared channels
    -> update bindings/trajectory/deficits/completion obligations
    -> repeat while obligations and finite resources permit
    -> semantic act or exact typed why-not
    -> exact realization/effect
    -> complete execution receipt
  ```

- `typed filtered indexed search` is the umbrella. Use the name `A*` only when the
  implementation actually executes `g+h` and proves the declared admissibility,
  typed-state dominance, reopen/tie, finite-boundary, path-multiplicity and completion
  laws. Otherwise name the actual bounded/best-first semantics.
- Exact canonical identity is resolved before contextual meaning is selected. Do not
  flatten identity, physicality, occurrence, testimony, interpretation, dependence,
  standing, query-relative operator state, or realized/target artifacts into one
  node/edge/score plane.
- Ordinary physicality/occurrence state is already queryable knowledge. Containment,
  ordinals, gaps, precedence/following, recurrence, co-occurrence and continuations may
  be calculated from trajectories without materializing redundant semantic testimony.
  Seeded facts/testimony remain a separate eligible plane.
- Candidate providers are selected from the compiled query. Push source, context,
  time, world, visibility, authority, evidence, relation, direction, trajectory,
  geometry, dependence-root and standing-lane predicates into indexes/perfcaches when
  semantically safe. Exact native validation remains authoritative.
- An index or perfcache miss is not semantic absence unless the exact accelerator and
  selected boundary prove completeness.
- Frontier expansion is set-wise. A logical batch may be physically partitioned for
  finite resources, but scalar execution remains a one-element batch of the same
  operation and cannot become a private fallback algorithm.
- The primary semantic/search path forbids cursor cognition, RBAR, caller-driven
  per-row traversal, one SQL/SPI query per frontier state when set-wise expansion is
  available, recursive SQL/CTEs as the native search engine, dynamic SQL generated per
  candidate/frontier row/relation, silent scalar fallback from failed batch/indexed
  execution, unbounded whole-corpus scans, giant pre-filter adjacency materialization,
  unbounded in-memory edge loading, row-trigger cache maintenance, and hidden semantic
  repair/deferred-write/cache-build/vacuum-like drains required before an otherwise
  valid interactive query can use admitted state.
- Dynamic construction is acceptable only inside an explicitly nonsemantic
  administrative DDL/package/schema boundary. It may not become a cognition language
  or hot-path fallback.
- `drain` is not globally forbidden. Old readers may legitimately finish against a
  pinned immutable perfcache generation after an atomic generation switch. That
  concurrency lifecycle is not semantic maintenance in the cognition path.
- An exact transformation must classify which state changed. Witness/provenance,
  occurrence/physicality role, relation/standing, canonical constituent/value, and
  intentional AST edits have different identity and epoch consequences. Invertible
  recipes descend to exact declared content; probabilistic regeneration cannot
  impersonate exact recomposition.
- A representative cognition/search receipt records the compiled program and boundary,
  provider/index/perfcache choices, frontier in/out, candidate rows generated/rejected/
  accepted, rows examined, server/SPI/native crossings, `g`/`h` evaluations,
  dominance/filter/dependence/authority/contradiction prunes, reopens, path count, peak
  frontier and memory, CPU, I/O/buffers, elapsed time, and the completion/optimality/
  upper-bound disposition. PostgreSQL routes include representative plan and
  `pg_stat_statements`/equivalent evidence.
- Do not claim that corpus scale is decoupled from forward-pass work until measured
  receipts demonstrate the selected indexed/filtering/reuse law at representative
  cardinality. Complexity is an acceptance contract, not an adjective.

## Evidence

- Tests execute the implementation and assert exact behavior.
- Each important test has a deliberate break proving the test detects the defect.
- Performance claims include the measured command, input, machine, sample count,
  timing boundary, CPU, memory, I/O, database calls, and durable output counts.
- Cognition/search performance tests additionally report candidate generation,
  filter/prune selectivity, frontier width, crossings, selected plans/indexes, and
  completion-proof cost; result parity without physical-plan evidence cannot certify
  the scalable path.
- Do not extrapolate a small fixture into a throughput claim.
- Do not claim conversation from a lookup, model correctness from artifact shape, data
  quality from row counts, or installation from files merely existing.
- Unknown and unsupported results are explicit typed outcomes.

## Repository changes

- Keep the current-implementation archive outside every build, include, package, and
  test search path.
- Every new implementation file must be authored for this repository from the new
  requirements.
- Generated files identify their generator and source contract.
- Dependency source comes from the verified lock and checksum records.
- Preserve unrelated work. Use isolated worktrees for concurrent changes.

## Persistence

- A direct user stop, pause, cancel, or equivalent instruction halts every tool call,
  repository or external mutation, read-only investigation, and automatic goal
  continuation. Resume only after a later direct user instruction explicitly resumes,
  continues, or replaces the paused task.
- A permission, environment, connector, runner, goal-ledger, or other external state
  change is an observation, not a human resume instruction. Noticing a transition is
  not proof that it completed and cannot be used to shortcut the requested boundary.
- At the start of a resumed work session, load and validate
  `contracts/authority-stack.json` in its declared order. The whole invention governs
  before current branch, source inventory, issue state, or implementation pattern.
- Read `state/continuation.json` only after stable product and executable authority.
  Verify its base, dirty fingerprint, source roots, and live runtime observations; it
  is observed development state and never product law.
- Read `docs/product/ROADMAP.md` as the current program execution projection after
  the operation and acceptance contracts. Its issue state, priorities, and sequence
  cannot create product law or promote requirements into implementation.
- Capability, controlled integration, installed product activation, per-source-profile
  world admission, configured foundational seeded state, and release are distinct.
  Never promote one state into another without its exact receipt.
- The current branch is not sufficient continuation authority. Direct corrections,
  supersession, the unpublished-work fingerprint, runtime state, and the complete
  dependency graph govern continuation together.
- Update or invalidate the continuation checkpoint in the same change that publishes,
  completes, or supersedes its interrupted boundary.
- Continue active work after corrections, status questions, and refinements.
- The user controls when work stops or changes direction.
- Report failed checks and incomplete acceptance precisely, then keep making safe
  in-scope progress.
