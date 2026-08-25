# SQL and PostgreSQL execution-surface audit

Date: 2026-08-24 UTC

## Scope and evidence boundary

This audit reconstructs database-client activity, observes the running PostgreSQL
product without mutation, and converts demonstrated defect classes into acceptance
conditions for the new implementation. Historical SQL, routine bodies, schemas, and
names are evidence about behavior and failure. They are not source material for the
new engine.

The live observation ran as one `BEGIN TRANSACTION READ ONLY` program. It did not copy
the database, alter a session parameter, invoke ingest, or execute a product write.

| Evidence artifact | SHA-256 |
| --- | --- |
| Latest-session SQL activity manifest v2 | `30868a72b5a5a19824e30055f521b7f054f992d299f573e1437147c2e9615bb8` |
| Complete deduplicated Claude-corpus database manifest v6 | `a20f30318de15ae55709e40e04bca12438a51f521d8defdd30c8d881d2ab315b` |
| Read-only observation program | `0d8e5053a924be71547617d65003d8abeeb9b0c66e5a6653876ae303ba20f205` |
| Read-only observation result | `5e87cd145caff531568a0558cb9082c1b2a638f0583da6e838c8c26ff933d81a` |
| Read-only epistemic-layering program | `93564fcd4784f36a4602e05b376eb8806069b091518bb6080756f0da7527fde9` |
| Read-only epistemic-layering result | `ee48697a86ae13b2f10f0cc453e43d8d5158f4fc6c9fb6d13bd23f35ac967b13` |

## Complete deduplicated corpus activity

The final cross-corpus parser inspected 1,308 distinct accepted log contents selected
from the current Claude state, `/vault/.claude`, and `/vault/AI_Sabotage`. It resolves
shell variables, Windows `psql.exe` paths, PowerShell continuations, command options,
heredocs, file inputs, pipeline files, generated pipelines, and MCP SQL fields. Client
version/help checks are excluded because they do not connect to a database. Combined
database-list operations such as `-lqt` are retained as typed client catalog operations
rather than fabricated SQL text.

| Measurement | Final observed result |
| --- | ---: |
| Attempted database-client invocations | 8,779 |
| Invocations with confirmed tool execution | 8,252 |
| SQL/client input sources | 12,693 |
| Attempted parsed statements | 16,534 |
| Statements attached to confirmed execution | 15,585 |
| Session-setting statements | 3,825 |
| `EXPLAIN ANALYZE` statements | 181 |
| PostGIS or four-dimensional statements | 172 |
| Client time-limit declarations | 1,248 |
| Declared client time total / maximum | 418,500 s / 1,800 s |
| Timed-out invocations | 48 |
| SQL errors embedded in tool results | 886 |
| Repeated exact groups / executions in those groups | 1,107 / 4,521 |
| Repeated shape groups / executions in those groups | 1,235 / 5,737 |

The 12,693 source classifications are 10,552 command options, 915 heredoc bodies, 429
file options, 412 pipeline file inputs, 20 generated pipeline inputs, 336 MCP inputs,
19 database-list operations, one redirected input file, and nine unresolved stdin
invocations. Those nine retain their exact command and tool-result hashes but do not
have enough captured input evidence to claim an executed SQL body. Five malformed
source records are reported separately; there are zero invalid content-addressed
payload objects and zero result-ID conflicts.

The latest-session measurements below remain useful for chronology, but they are a
small subset of these final cross-corpus totals.

## Reconstructed latest-session activity

The exact shell and SQL reconstruction found:

| Measurement | Observed result |
| --- | ---: |
| Successful or attempted `psql` invocations reconstructed | 445 |
| SQL input sources | 471 |
| Parsed statements | 743 |
| `SET` statements | 153 |
| `EXPLAIN ANALYZE` statements | 13 |
| PostGIS or four-dimensional statements | 8 |
| Distinct normalized statements | 502 |
| Distinct structural statement shapes | 454 |
| Repeated exact groups / executions in those groups | 43 / 131 |
| Repeated shape groups / executions in those groups | 62 / 198 |
| Declared client time limits | 94 |
| Sum of declared client time | 19,020 seconds |
| Maximum declared client time | 900 seconds |
| SQL errors present inside otherwise successful shell results | 68 |

Only 13 analyzed plans were captured for hundreds of client queries. Increasing client
time limits was therefore much more common than proving and correcting plan behavior.
Repeated calls covered conversation, lexical realization, relation ranking, structural
inspection, and ingest surfaces that should have had stable typed server interfaces and
one-call diagnostics.

## Running product observation

At `2026-08-24 21:43:03 UTC`, the current `laplace` database reported PostgreSQL 18.3,
273,220,400,831 bytes, PostGIS 3.6.3, and installed `laplace_substrate` and
`laplace_geom` extensions. The current database is preserved only by its own existing
storage; it is excluded from the archive.

The process and database views showed a chess ingest active through the CI runner
against the 1950–1969 PGN. Sixteen other Laplace backends were visible during the
snapshot, including concurrent consensus calls waiting on WAL locks. The audit did not
interrupt them.

### Measured hot operations

The cumulative statement view reported:

| Operation | Calls | Mean execution | Total execution |
| --- | ---: | ---: | ---: |
| Consensus upsert entry point | 1,784 | 61.950 s | 110,518.598 s |
| Bulk consensus `MERGE` | 1,790 | 55.120 s | 98,665.446 s |
| Binary attestation `COPY` | 798 | 57.356 s | 45,770.016 s |
| Binary physicality `COPY` | 911 | 27.898 s | 25,415.071 s |
| Attestation-existence bitmap | 590 | 27.009 s | 15,935.037 s |
| Entity-existence bitmap | 25,468 | 378.115 ms | 9,629.840 s |
| Entity-present ordinals | 25,488 | 355.333 ms | 9,056.720 s |

The two entity-existence forms each accumulated about 12.81 million temporary-block
reads and writes. The statement view also contained 56,492 `DISCARD TEMP` executions
and 18,419 executions of each of two temporary-table creation shapes. This is direct
evidence that nominal batches still pay repeated temporary-object, planner, catalog,
and client/server lifecycle costs.

### Table and index state

The eight default consensus partitions together contained approximately 124,012,909
rows and occupied 96,574,889,984 bytes, including 78,654,324,736 bytes of indexes. The
eight default attestation partitions contained approximately 141,828,612 rows and
occupied 63,781,871,616 bytes.

One global ranking index family occupied 15,135,850,496 bytes across eight partitions
and had exactly two recorded scans per partition. A second subject/type/rank family
occupied about 9.78 GB and had 0–4 scans per partition. The pure type/subject family
occupied about 7.39 GB and had zero scans on all eight partitions. This corroborates the
historical claim that very large indexes were built without becoming the selected
execution path; it also gives the exact current sizes and scan counts rather than
relying on that claim.

The installed non-system type inventory exposes one product enum but no Laplace-owned
four-dimensional base type. The non-system operator-class inventory contains PostGIS
geometry/geography classes only. There is no Laplace-owned operator class proving that
all four S3 components participate in equality, ordering, distance, containment, and
index planning.

### Epistemic layering in the live schema

A second read-only transaction inspected base relations and columns and rolled back.
The live entity relation stores tier, type, first-observer, and creation fields and uses
`(id, tier)` as its primary key. That permits persistence identity to vary by tier even
though the content hash itself is intended to remain tier-independent.

The live physicality relation combines entity reference, type, four-component point,
Hilbert value, trajectory, constituent count, source dimension, residual, radius, and
observation time. The live attestation relation stores subject/object/source/context,
outcome, accumulated values, observation count, opponent rating state, last-observed
time, and routing data. The live consensus relation stores one mutable rating,
deviation, volatility, witness count, and last-observed value per proposition shape.

The catalog contains no base relation or relevant column naming occurrence, derivation
lineage, evidence dependence, root observations, or consensus epoch. This is direct
schema evidence for the clean separation now required: exact entity identity,
immutable calculated physicality, contextual occurrence, attributable testimony,
lineage-bearing inference, dependence-aware adjudication, and immutable consensus
epochs.

## Required execution architecture

The replacement is one system, not a collection of client scripts:

1. C/C++ and PostgreSQL server integration own identity, geometry, relation algebra,
   evidence, consensus, ISA execution, and every other semantic kernel.
2. SQL and C# are generated peer orchestrators over the same typed ISA contracts.
3. Bulk requests cross into the server once per bounded batch and execute through
   prepared native plans or native vector kernels. One-element requests use the same
   operation with a one-element vector.
4. Reusable read projections are views; composable calculations are typed functions;
   lifecycle operations with transaction semantics are procedures; domain values use
   product types, operators, casts, and operator classes. Selection is based on actual
   semantics, not a blanket preference for one PostgreSQL object kind.
5. Diagnostic and status questions use versioned server surfaces with bounded work and
   machine-readable receipts. Product operation must not require repeated catalog
   exploration from `psql`.
6. Session mutation is not part of routine correctness. Namespaces, volatility,
   parallel safety, planner support, privileges, search resolution, and resource
   behavior are explicit object contracts.
7. The custom PostGIS stack supplies exact four-component S3 operators and planner
   support. Tests containing points that differ only in the fourth component must fail
   any implementation or index class that silently executes as 2D or 3D.
8. Indexes are admitted by measured workload and plan evidence. Publication records
   build cost, bytes, write amplification, selected plans, scan counts, and the query
   contract each index serves.

## Falsifiable acceptance conditions

### Bulk parity and crossings

Given one million heterogeneous ISA records and the same records executed one at a
time, the durable identities, coordinates, runs, testimony, consensus, receipts, and
readback must match exactly. The complete bulk run must meet the declared throughput
boundary, and instrumentation must show the declared bounded number of client/server
crossings. A deliberate per-record invocation must fail both the crossing and
performance assertions.

### Planner and index proof

For every published index contract, the acceptance fixture must include representative
cardinality and distribution, execute the real typed server surface, capture the
analyzed plan and runtime counters, and prove that the intended operator family is
selected. Replacing the operator with a three-component implementation, changing sort
or null semantics, or removing the index must make the exact test fail.

### Reusable diagnostics

Every status value needed by install, ingest, conversation, performance analysis, and
support must be returned by a documented server interface in one bounded call. The
test records statement count and catalog queries. Replacing that call with repeated
ad hoc discovery must exceed the crossing boundary and fail.

### No session-state dependency

The same operation must produce the same result from new pooled connections with
different search paths and unrelated caller settings. Captured statements must contain
no correctness-critical `SET`, and product routines must have declared namespace and
planner properties. A routine whose success depends on caller session state must fail
the isolation test.

### Four-component geometry

Identity-independent geometry fixtures must vary X, Y, Z, and M separately. Scalar,
batch, indexed, sequential, SQL, C#, and native routes must return the same distance,
ordering, neighborhood, centroid, radius, trajectory, and Hilbert results. A mutant
that omits M must be rejected by every route that claims four-component semantics.

### Complete performance boundary

Database-tier tests time the complete operation: decode, identity, geometry,
existence resolution, writes, evidence and consensus updates, receipts, commit, and
durable progress. The report includes input bytes and records, output counts, CPU,
memory, I/O, WAL, temporary bytes, statement calls, batch sizes, and loaded artifact
hashes. A fast inner loop cannot satisfy the gate while the complete boundary fails.

## Disposition

The current product demonstrates valuable substrate concepts and exposes useful query
surfaces, but its observed execution does not satisfy the required bulk, plan, index,
four-dimensional type, or bounded-crossing contracts. Those observations become
deliberate-defect tests and measurements. No historical SQL body is carried into the
new implementation.
