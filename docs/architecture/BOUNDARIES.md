# Architecture boundaries

## Authority flow

```text
 external content and services          direct SQL callers
               |                               |
               v                               v
 C# lifecycle/transport orchestration   SQL transaction/set orchestration
               |                               |
               +------------+------------------+
                            |
                            v
             generated typed ISA bindings
                            |
                            v
       PostgreSQL native extension/server integration
                            |
                            v
              C/C++ substrate instruction engine
                            |
                            v
        universal persistent state, indexes, and receipts
```

SQL and C# are peer orchestrators. Neither is a semantic layer above the other and
neither becomes the engine. Both reach the same generated native instruction contract.

## Substrate state layering

```text
Entity
  exact canonical content identity
    |
    v
Physicality
  immutable calculated realization under a recipe and epoch
    |
    v
Occurrence and attestation
  observed use, source claims, calculated claims, relations, context, lineage
    |
    v
Consensus epoch
  immutable adjudicated view over an exact testimony boundary
    |
    v
Inference
  derived claim with a provenance DAG and root-observation set
```

The arrows describe dependency, not mutation. Attestations can address any prior layer
or another attestation. Inference can produce a new derived attestation, but it cannot
become an independent observation or rewrite entity content or physicality.

## Native engine

The native engine owns:

- canonical content encoding and identity;
- complete Unicode/DUCET ordering and universal atom resolution;
- typed composition and trajectory construction;
- run-span expansion semantics, sparse deposition, and Merkle DAG reuse;
- arithmetic-centroid physical representation, S3 placement, Hilbert locality,
  packed trajectories, realized curves, and typed distance calculations;
- relation and trust calculations;
- occurrence and testimony construction, derivation lineage, evidence-dependence
  calculation, and immutable consensus-epoch adjudication;
- instruction validation, scheduling, and execution;
- indexed traversal, correlation, selection, and composition;
- logical cognition-program compilation, physical plan costing, typed operator
  generation, evidence-metric calculation, matrix-free field solution, and numerical
  certification;
- typed relation, boundary, motif, contradiction, innovation, and counterfactual defect
  calculation;
- AImap spectral generation, gauge-invariant comparison, epoch alignment, and coherent
  derived-artifact publication;
- modality-independent result construction;
- model decomposition and execution-target construction;
- induced-model-operator comparison under basis, scale, head, and neuron symmetries;
- canonical deposition, derived-state calculation, and coherent epoch publication;
- deterministic receipts;
- bulk memory layouts, algorithms, and concurrency.

The native engine also owns the common perfcache mapper, typed module registry,
dependency graph, integrity checks, coherent activation epoch, batch lookup ABI, and
loaded-artifact report. A perfcache module supplies derived acceleration data to a
canonical engine operation. It cannot define identity, geometry, relation semantics,
or private mutable product state.

## Common execution abstractions

The native engine is organized around one reusable execution spine rather than
feature-owned loops. The spine owns complete-program validation, typed buffer views,
resource estimation, chunk selection, bounded arenas, parallel scheduling, error
translation, atomic effect admission, and receipt construction. An operation kernel
supplies its typed preconditions, shape law, calculation, and result contract; it does
not reimplement the spine.

The stable cross-language boundary is a generated C ABI with opaque handles, typed
vectors, descriptors, and provider function tables. Internal C++ uses templates,
concepts, abstract interfaces, and reusable base implementations where they preserve
one semantic operation while permitting specialized storage, index, numerical, or
hardware providers. Provider substitution cannot change logical results, evidence
kind, ordering law, completion, or receipt meaning.

Generated C# bindings expose generic interfaces for sessions, sources, batches,
program submission, result streams, and receipts. Abstract base implementations own
common lifecycle, cancellation, telemetry, bounded retry, and transport behavior;
dependency injection selects concrete transports and services. No derived class may
own a private identity, relation, trust, ranking, cognition, or realization algorithm.

SQL uses generated domains, composite types, functions, procedures, operators, and
set-returning bindings from the same contract. SQL has no handwritten semantic copy of
the native interfaces. Single-item calls are one-element vectors through the batch
path, and parallelism or chunk size is a physical-plan decision below the logical ISA.

The PostgreSQL integration layer supplies typed datum conversion, memory-context
ownership, transaction integration, planned server access, set-returning execution,
catalog registration, and error translation. Server queries are schema-qualified,
parameterized, planned, and set-based.

## Hardware topology and resource authority

One native topology service inventories processors, cores, hardware threads, NUMA
nodes, cache hierarchy, memory capacity, affinity constraints, vector ISA, and selected
accelerator capabilities. It publishes an immutable topology snapshot and derives a
resource plan for each execution receipt. SQL, C#, kernels, and external tools consume
that plan; they do not probe the host independently or invent unrelated thread and
memory limits.

The physical plan declares worker arenas, CPU sets, NUMA placement, memory domains,
batch width, chunk shape, I/O concurrency, PostgreSQL connection and worker ownership,
and nested-library thread budgets. TBB arenas, MKL kernels, PostgreSQL workers, managed
workers, and tool processes share one accounted budget. A parallel region that invokes
another parallel provider must transfer or subdivide its grant so work cannot multiply
the host thread count invisibly.

Exact operations preserve bit-identical results across lawful worker counts, chunks,
routes, and restarts. Floating-point operations declare their reproducibility class,
compiler and ISA profile, contraction and reduction rules, MKL compatibility controls,
thread count, and accepted comparison contract. Faster hardware execution is a
physical-plan choice and cannot change identity, evidence kind, ordering, completion,
or semantic results.

## SQL orchestration

SQL owns:

- schemas, types, tables, indexes, constraints, and extension declarations;
- transaction and statement composition;
- typed native bindings;
- relational restriction and indexed set routing;
- program submission and result projection;
- installation and upgrade declarations.
- append-oriented canonical and testimony storage plus transactional publication of
  validated derived epochs.

SQL does not implement identity, ranking, traversal algorithms, trust mathematics,
conversation, modality behavior, model behavior, or any second copy of an instruction.
SQL functions used for composition have parsed bodies, explicit namespaces, and
measured plans.

SQL iteration uses bounded affected-row set operations when iteration is semantically
required. It does not use cursors or recursive row execution to replace a native bulk
kernel.

## C# orchestration

C# owns:

- process and service lifecycle;
- source discovery and bounded work scheduling;
- streaming encoded source blocks to native execution;
- sessions, API protocols, authentication, authorization, billing, and UI services;
- progress, cancellation requests, telemetry, and product diagnostics;
- package and service commands where managed code is the product entry point.

C# does not calculate substrate identities, relations, trust, trajectories,
adjudication, candidate ranking, model operations, or conversation semantics. It uses
generated typed bindings rather than handwritten semantic SQL.

Progress reported by C# is derived from reconciled run, file, segment, batch, record,
database, and receipt state. A later successful sub-run cannot replace the incomplete
status of its parent source.

## Edge codecs

Media libraries decode and encode format containers. The engine converts decoded data
to universal content, composition, physicality, relations, testimony, and receipts.
A codec does not define a modality-specific substrate.

## Generated contracts

One versioned contract source generates:

- C and C++ public types;
- PostgreSQL types and native declarations;
- C# transport types and SQL bindings;
- instruction documentation;
- identity, Unicode, geometry, trajectory, and perfcache test vectors;
- typed perfcache module descriptors and activation manifests;
- ABI compatibility tests;
- package manifests.

Generated output is reproducible and checked against its source contract.
