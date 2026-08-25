# Product infrastructure

## Dependency supply chain

Laplace builds its required native stack from verified upstream sources. Existing
clean upstream trees are valid acquisition assets after identity, source-tree,
license, and build-contract verification. This includes PostgreSQL, PostGIS, GDAL,
GEOS, PROJ, and every other library classified as a product runtime dependency. The
current installed graph also requires explicit treatment of BLAKE3, ICU, libxml,
OpenSSL, zlib, LZ4, Zstandard, liburing, tree-sitter and grammars, oneAPI runtimes,
MKL, TBB, .NET, Node, and every direct and transitive native object selected by the
declared product features.

Dependency means every external input capable of changing a calculation, not only a
linked library. Downloaded corpora and standards under the declared data root,
conventional models and code datasets under the declared model root, NNUE networks,
Syzygy tables, invoked tools, and service responses enter the same dependency graph.
Their types determine how they are acquired, versioned, licensed, verified, executed,
and interpreted; they do not exempt them from dependency identity or provenance.

`dependencies/roots.json` defines discovery roots without making machine-local paths
canonical. The observed Linux roots include `/vault/Data` for corpora and standards,
`/vault/models` for model snapshots and code corpora, `/opt/laplace` for installed
product observations, and `/opt/intel/oneapi` for Intel toolchain assets. Environment
or package configuration may select another physical location while preserving the
same logical root and manifest contracts.

Each dependency record contains:

- upstream project and canonical source URL;
- exact release and source commit;
- archive checksum and source-tree checksum;
- license and notice requirements;
- ordered Laplace patches with written necessity;
- build system, toolchain, flags, features, and dependency edges;
- produced files and ABI facts;
- security and support status;
- reproducibility evidence.

The update policy selects the latest supported stable upstream release. A version pin
makes one build reproducible; it is not permission to remain stale. Automated checks
report newer releases, while an older selection is accepted only with a written defect
or compatibility finding and an expiry or review condition.

Source acquisition, verification, patching, configuration, compilation, testing,
staging, and packaging are distinct commands with machine-readable receipts. A build
can run without network access after verified acquisition.

`dependencies/intent.json` records why each direct, transitive, toolchain, test, and
acceleration family exists, which product profile consumes it, and the boundary it may
not cross. The same catalog covers invoked engines, versioned datasets, network
services, external witnesses, standards data, model registries, and independent target
runtimes. A component cannot enter the build merely because it existed historically or
is convenient. Its selected capabilities must satisfy a declared product use, and the
dependency verifier joins every locked source to that intent record.

Compiled linkage is only one external relationship. Stockfish is an attributable UCI
calculation provider; Syzygy files are versioned data consumed through a declared probe;
cutechess executes matches; Lichess transports live observations and authorized acts;
FIDE, PGN publishers, and linguistic corpora provide source-scoped testimony; Unicode
data defines versioned standard recipes; conventional model registries and runtimes
provide artifacts and independent target execution. Every interaction binds exact
inputs, versions, authorization, outputs, provenance, and evidence kind. No external
provider becomes canonical identity, referential identity, cognition, or truth.

The numerical CPU profile has three distinct responsibilities:

- IntelLLVM and the oneAPI compiler runtimes define the optimized native build profile,
  vector ISA selection, deterministic floating-point flags, diagnostics, and required
  runtime objects across Linux and Windows.
- oneMKL supplies measured BLAS, LAPACK, sparse, factorization, eigensolver-support,
  fitting, and tensor kernels used by generated operator and model calculations. MKL
  compatibility modes, thread count, precision, and reduction behavior are receipt
  inputs; MKL never defines semantic meaning.
- oneTBB supplies bounded task arenas, topology-aware parallel loops, chunk execution,
  and the MKL threading layer so nested native work shares one CPU budget instead of
  oversubscribing the host. Partition and merge contracts must preserve the canonical
  serial result where exact parity is required.

Eigen supplies typed dense and sparse matrix structures plus reference calculations;
Spectra supplies iterative spectral/Krylov machinery over declared operators. They are
calculation providers, never a permanent representation of the substrate. PostgreSQL,
PostGIS, GEOS, PROJ, and GDAL likewise retain separate persistence, indexed geometry,
topology, coordinate-system, and format-ingestion roles. Packed trajectory carriers
cannot be passed to geometric distance code merely because the storage datatype is
spatial.

Release archives are accepted only through `dependencies/release-lock.json` and the
archive verifier. Git sources are accepted only through `dependencies/lock.json` and
the Git verifier. Binary model and data inputs use `dependencies/artifact-lock.json`;
Stockfish's two NNUE files are locked independently from its source because either can
change engine behavior. Grammar repositories use their separate 299-repository lock.
Import is published atomically only after the complete source set has passed identity,
license, path, link, tree, and extracted-content checks.

The staged package is inspected with `tools/dependencies/elf-closure.py`. A package
cannot pass when any edge is unresolved, a non-platform feature library resolves from
the host cache or another prefix, a search path names the working directory or a build
tree, one SONAME selects different binaries by caller, or one process closure contains
incompatible generations of a library family. Runtime tests then execute the real
installed capabilities; metadata resolution alone is not acceptance.

### Installed Intel provider selection

`dependencies/installed-lock.json` selects the inventor-host Intel runtime 2026.1.1,
oneTBB 2023.1.0, and oneMKL 2026.1.0 by immutable versioned roots. It binds exact
package versions, headers, runtime objects, SONAMEs, CMake inputs, license and notice
files, byte sizes, and SHA-256 digests for the Linux x86-64 self-hosted profile. No
`/opt/intel/oneapi/*/latest` symlink participates in the selection.

Clean CI validates the lock structure, complete role sets, provider fingerprints,
immutable paths, and authority boundaries without requiring Intel software. The
self-hosted profile additionally verifies every installed byte, package version, and
runtime SONAME before CMake emits exact imported provider targets. Those targets are
available only when `LAPLACE_VERIFY_ONEAPI_INSTALLED_PROVIDER=ON`; exposing them does
not link a product target, activate a product runtime, implement Unicode, or authorize
scheduling. Product activation still requires a package receipt and verification of
the objects actually loaded by the final process.

## Build graph

One declared graph owns Linux and Windows builds. Platform files supply toolchain facts
without creating a second product build definition.

The graph produces:

1. verified dependency source;
2. custom dependency runtime and development packages;
3. native engine libraries;
4. PostgreSQL extension and SQL contracts;
5. generated identity, ISA, binding, schema, and perfcache contracts;
6. typed perfcache modules and one coherent activation manifest;
7. C# orchestrators and product services;
8. web and CLI assets;
9. tests and symbols;
10. installable product packages;
11. checksums, provenance, licenses, symbols, and SBOMs.

Build outputs never share mutable state across branches or CI jobs. Caches are addressed
by complete inputs and verified before use.

## Toolchains

Compiler, linker, CMake, Ninja, .NET SDK, Node toolchain, PostgreSQL extension ABI, and
package tools are pinned. Release builds record every version and flag. Warnings in
Laplace-owned code are errors. Debug symbols are produced for optimized packages.

Deterministic numeric behavior has explicit compiler and instruction-set requirements.
Hardware-specific packages declare their minimum ISA and never masquerade as portable
packages.

## Installation layout

Packages separate immutable versioned product files from machine state:

- versioned binaries and libraries;
- custom PostgreSQL and geospatial runtime;
- configuration;
- persistent product state;
- logs and diagnostics;
- package and schema receipts.

Activation changes a single version pointer only after file, ABI, extension, schema,
configuration, service, and behavior verification passes. Failed activation leaves the
previous product version selected and records the exact failure.

The activation manifest binds the native engine, PostgreSQL extension, SQL catalog,
managed services, custom dependencies, perfcache planes, configuration, and test
receipts. Every running process reports the exact loaded path, build ID, and full hash
for each bound object. Filename and install location alone are never accepted as
artifact identity.

Service loader environments are generated from the manifest and contain only declared
product directories. Ambient library search paths cannot select system GEOS, PROJ, or
another undeclared ABI in place of a custom product object.

## Idempotency

Install, upgrade, repair, configuration, extension registration, schema application,
service creation, and removal are declarative state transitions. Every action supports
plan, apply, and verify modes and records a receipt.

Repeated application with identical inputs changes nothing. Repair restores exact
packaged state. Removal deletes only files and services owned by the package manifest
and preserves user-owned content according to the command contract.

## Database lifecycle

The product owns the custom PostgreSQL binary distribution, extension ABI, cluster
configuration templates, initialization, service identity, schema version, and
extension version.

Schema changes are forward-declared, transactional where PostgreSQL permits, and
verified against actual catalog dependencies. Extension install and upgrade scripts
are generated from the same contract as native bindings. Every object is
schema-qualified.

The current implementation database is not an input to this repository or its package
process.

## CI

CI stages use isolated workspaces and packaged inputs:

1. repository and contract checks;
2. dependency acquisition verification;
3. dependency build and tests;
4. native build and tests;
5. custom PostgreSQL installation;
6. extension installation and PostgreSQL behavior tests;
7. C# build and orchestration tests;
8. complete-product acceptance;
9. deliberate broken-implementation verification for every critical acceptance;
10. package construction;
11. install, repeat-install, upgrade, repair, service, and removal tests;
12. loaded-object and perfcache-epoch verification;
13. performance runs on declared hardware;
14. signed release publication.

No required suite is skipped because a fingerprint says a previous revision passed.
Input-addressed build reuse may save compilation, but behavior is executed for the
candidate package.

## Operations and support

The product exposes structured health, readiness, version, dependency ABI, schema,
instruction-set, queue, database, storage, resource, and receipt diagnostics. Readiness
must fail when a required substrate operation cannot execute.

Support bundles contain configuration with secrets removed, versions, manifests,
receipts, logs, crash data, and bounded diagnostic queries. Product logs distinguish
requested, accepted, executing, durable, complete, cancelled, and failed states.

## Security

Packages use least-privilege service identities, explicit filesystem ownership,
separated secrets, TLS-capable network configuration, authenticated product APIs,
authorization scope passed into ISA programs, dependency vulnerability tracking, and
signed release artifacts.
