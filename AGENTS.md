# Working agreements

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
- SQL and C# are orchestrators. They do not contain another engine.
- All product behavior executes through the typed substrate instruction set.
- Every operation has one canonical implementation.
- Batch and bulk forms are primary and must preserve exact single-item semantics.
- Text is not architecturally privileged.
- Language, modality, source, and model are witnessed dimensions, not engine branches.

## Evidence

- Tests execute the implementation and assert exact behavior.
- Each important test has a deliberate break proving the test detects the defect.
- Performance claims include the measured command, input, machine, sample count,
  timing boundary, CPU, memory, I/O, database calls, and durable output counts.
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

- Continue active work after corrections, status questions, and refinements.
- The user controls when work stops or changes direction.
- Report failed checks and incomplete acceptance precisely, then keep making safe
  in-scope progress.
