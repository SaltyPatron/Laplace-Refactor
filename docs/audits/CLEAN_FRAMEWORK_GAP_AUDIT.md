# Clean framework gap audit

## Finding

The clean repository has a real native component baseline, but it does not yet contain
the complete Laplace execution framework. Earlier status language grouped implemented
components too closely and did not make their missing framework dependencies prominent
enough.

## Implemented on current main

- fixed BLAKE3-128 identity primitives and cross-runtime vectors;
- Unicode-position validation and enumeration, but not Unicode/UCD/DUCET seeding;
- composite and run identity calculation;
- exact 212-bit trajectory carrier encode/decode and calculated local occurrence facts;
- exact merge-order-independent four-dimensional arithmetic-centroid accumulation;
- two hard-coded ISA operations with complete preflight and basic deterministic
  program/input/output receipts;
- topology observation, conserved CPU/memory/I/O grants, and work-plan calculation;
- a contract-generated native operation-descriptor registry;
- an immutable framework context and fingerprint over selected epochs, authority, and
  conserved resource grants;
- a partition-independent canonical stream fingerprint and typed sink ABI that stages
  the same calculated stream into multiple providers, seals only after complete
  preflight, and aborts every staged sink on failure;
- a generic immutable perfcache file/mapping/publication foundation with distinct
  dense-direct, sorted-fixed, and module-defined access laws; and
- native package/consumer and dependency-verification foundations.

These are component facts. They do not establish a canonical database, universal
ingestion, a Unicode root, hot perfcache epochs, conversation, cognition, model
compilation, or the complete product.

## Implemented on this change branch but not yet accepted on main

- a PostgreSQL extension/SPI vertical slice for the two current ISA operations;
- generated PostgreSQL binding constants and package/build wiring;
- perfcache access-law changes distinguishing dense direct, sorted fixed, and
  module-defined planes;
- the initial framework registry, execution context, canonical stream, staged sink,
  and receipt slice; and
- executable requirements for the whole operation graph, Unicode bootstrap, glome
  geometry, and hot activation.

The staged SPI test contains test-fixture SQL rather than production substrate
persistence. It does not prove set-oriented canonical deposition or issue #7. The
branch perfcache work does not implement a perfcache module registry, dependency
graph, prefaulting, reader epoch pins, or restart-free generation handoff.

## Missing framework pieces

1. Operation descriptors are generated, but the ISA execution dispatcher still uses a
   source-code switch over two opcodes; module handlers and every generated transport
   do not yet consume the registry as their sole dispatch authority.
2. The execution context type and fingerprint exist, but current ISA and PostgreSQL
   execution do not yet require and receipt that context.
3. Resource grants are planned but not applied across TBB, MKL, PostgreSQL, managed,
   and tool routes.
4. Canonical byte streams and staged sinks exist, but there is no decomposer producer,
   typed record-codec registry, database/perfcache provider implementation, or coherent
   epoch activation coordinator.
5. There is no canonical persistence provider, transaction/effect admission boundary,
   or replay/progress state machine.
6. There is no perfcache module/epoch registry, hot-generation protocol, or complete
   loaded-artifact diagnostic surface.
7. C ABI and PostgreSQL constants are partly generated; the SQL and C# surfaces are
   not generated as one complete contract.
8. Current receipts omit most source, recipe, epoch, dependency, numeric, resource,
   authority, persistence, and loaded-object facts required by the product.
9. There is no authoritative Unicode/UCD/DUCET source manifest in the dependency lock.
10. There is no Super-Fibonacci/Hopf/Hilbert Unicode seed implementation in the clean
    engine.
11. There is no canonical entity/physicality/composition/occurrence/testimony schema
    or set-oriented deposition implementation.
12. There is no leaf-to-trunk working-set compose/dedup or O(tiers) durable-presence
    engine.
13. Evidence epochs, search, guidance, cognition, realization, Goedel/OODA, AImaps,
    and model compilation remain requirements and research/implementation programs.

## Framework-first gate

No feature-specific PR may claim product progress merely by adding another opcode or
surface. Before the Unicode proving module lands, the reusable operation registry,
execution context, canonical batch pipeline, sink/provider boundary, epoch/receipt
lifecycle, resource application, and generated binding path must execute in tests.

Unicode then proves the framework can calculate one root stream, create its bootstrap
artifact, populate canonical PostgreSQL state in bulk, verify parity, activate a warm
epoch without restart, and expose the same result through every orchestrator.

## Current repository disposition

Current main is `b0e0d0a` from merged PR #9. Issues #2 through #8 are open. Issue #4
was correctly reopened because TBB/MKL application and cross-route parity remain.
Issue #8 is stale relative to the now-running `hart-server-refactor` service and needs
fresh capability/workflow evidence before closure. The framework-first obligation is
tracked by issue #10; this branch supplies only its first native slice and cannot close
that issue.
