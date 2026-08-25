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

The PostgreSQL integration layer supplies typed datum conversion, memory-context
ownership, transaction integration, planned server access, set-returning execution,
catalog registration, and error translation. Server queries are schema-qualified,
parameterized, planned, and set-based.

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
