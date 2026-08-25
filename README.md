# Laplace

Laplace is a complete replacement execution architecture for the transformer. It
executes through SQL over a universal persistent substrate, with all semantic and
computational behavior implemented by a C/C++ engine integrated with PostgreSQL
through its extension and server programming interfaces.

Text, images, audio, video, code, games, model state, and future media use the same
identity, composition, relation, evidence, trust, execution, and materialization
laws. Language, modality, source, and model are witnessed properties. They do not
partition the intelligence into separate systems.

Laplace has no token context window and does not use softmax to invent a continuation.
Any addressable substrate state can be execution input. Every result must be supported
by witnessed state or a reproducible calculated operation and carry a receipt.

## Status

This repository is being established from first principles. It makes no claim that
Laplace product behavior exists until the implementation-level acceptance suite proves
it. A successful build, a nonempty result, an artifact of a particular size, or a
response with the expected shape is not product acceptance.

The executable baseline currently contains the fixed BLAKE3-128 identity contract,
complete Unicode-position encoding, ordered recursive composition, run-span identity
calculation, deterministic four-dimensional arithmetic-centroid accumulation, and the
shared typed perfcache format/mapping/publication/batch-lookup layer. The first public
trajectory ABI carries 212 exact bits through four finite binary64 slots, reconstructs
typed 128-bit lanes, ordinals, runs, and 52-bit metadata, and calculates composition
order, multiplicity, tier, atom, containment, precedence, and co-occurrence directly
from decoded structure. The first two public ISA instructions execute codepoint
identity and composition-trajectory decoding through pinned opcodes and value types,
validate the complete program before writing results, and emit BLAKE3-256
context/program/input/output/receipt fingerprints. Every program requires an explicit
immutable framework context, and the generated operation registry owns native
validation and execution dispatch. The PostgreSQL routes accept that same typed
context explicitly; no hidden session or database context is substituted. The
optimized and address/undefined-behavior builds currently expose 107 registered
core-profile tests and 109 in the PostgreSQL profile. Staged canonical streams remain
inert until a writable authority admits an exact staged receipt and atomically
publishes a compare-and-swap epoch transition; stale epochs and altered receipts are
rejected before the activation provider runs. A native execution authority now
observes caller-affinity CPU/package/core/NUMA/cache structure, hybrid core classes,
usable and constrained memory, page size, and runtime ISA capabilities into an
immutable caller-owned snapshot. It subtracts resources already owned by PostgreSQL,
managed processes, and tools; partitions CPU, memory, and I/O grants without creating
resources; and plans chunks, outer workers, and inner math-library threads inside the
same conserved grant. Those
tests include exact cross-runtime vectors, enumeration of all 1,114,112 codepoint
positions, 100,000-element expanded/run identity and centroid comparisons, exact
cancellation and subnormal rounding checks, ISA type/capacity/overlap/atomicity checks,
perfcache corruption and semantic checks, same-directory publication, immutable mapped
readers, an installed external identity-trajectory-and-ISA consumer, and fourteen injected
implementation defects that must fail for their exact expected reasons. The dependency
tests also construct unresolved edges, competing loader
candidates, incompatible ABI generations, altered archives, and escaping members and
prove each defect is rejected. This is component evidence; it is not a claim that the
complete product acceptance has passed.

The native implementation installs as one versioned `laplace_engine` shared object
with a restricted `laplace_*` symbol surface and a consumable `Laplace::Engine` CMake
package. Repeated installation must produce the same byte fingerprint before the
external consumer is compiled and executed against the installed package.

Thirteen upstream source trees are pinned by exact Git revision, Git-archive SHA-256,
version, upstream URL, and license-file SHA-256 in `dependencies/lock.json`. The clean
source importer copies without hardlinks and verifies the source before and after the
copy. The build verifies each dependency it consumes again during configuration.
Another lock binds twenty official release archives. In addition to PostgreSQL 18.6,
PostGIS 3.6.4, GDAL 3.13.3, GEOS 3.14.1, and PROJ 9.8.1, it now selects the current
PostgreSQL runtime leaves and build/test tools: ICU, OpenSSL, libxml2, zlib, LZ4,
Zstandard, liburing, binutils, make, Bison, Flex, Perl, pkgconf, IPC::Run, and IO::Tty.
Every archive is bound by URL, archive and expanded-tree SHA-256, size, member counts,
and license digests. A 299-repository grammar lock independently
binds generated parsers and scanners. The ELF closure analyzer records exact selected
paths and hashes and rejects incomplete or conflicting package graphs.

## Required architecture

- C/C++ and PostgreSQL server integration form the engine.
- SQL is the typed execution and transaction orchestration surface.
- C# orchestrates sources, sessions, services, product APIs, and lifecycle.
- The substrate instruction set is executable, versioned, typed, vector-first, and
  receipt-producing.
- All ingestion, query, conversation, analysis, and materialization paths use the same
  instruction set and engine contracts.
- Batch and bulk operations are the primary forms. Single-item calls use the same
  canonical implementation.
- There is one implementation for each semantic fact.
- Native cognition calculates and generates typed operator applications from exact
  layered substrate state, relation laws, evidence, context, and goals. It does not
  flatten meaning into a permanent graph, embedding, or model.
- The layer-metrized operator, typed defect/innovation calculus, semantic-act
  selection, and modality realization are distinct receipt-producing stages.
- Query and cognition compile indexed A-star or declared best-first search over typed
  states. Exact structure, multiple geometric metrics, Glicko-2 standing, trust,
  context, and relation law generate bulk frontier candidates; no KNN, ANN, lookup, or
  permanent score defines universal relevance.
- The Gödel engine uses persistent typed incompleteness as a discovery signal. It
  predicts constrained vacancies, generates and tries to disprove candidate facts,
  laws, operators, firmware operations, and cognition programs, and versions accepted
  calculus extensions without treating prediction or self ancestry as observation.
- OODA is a typed feedback execution: Observe exact structure and claims, Orient with
  generated calculation and search, Decide through isolated acts, Act through a
  receipted realizer or effect, and return the consequence as a new observation.

## Repository map

- `docs/product/` — invention constitution and clean-room rules.
- `docs/architecture/` — engine, instruction set, data, boundary, and native cognition
  mathematics specifications.
- [`docs/architecture/OPERATIONAL_MODEL.md`](docs/architecture/OPERATIONAL_MODEL.md) —
  whole-product execution order and the framework shared by every module.
- [`contracts/operation-model.json`](contracts/operation-model.json) — mechanically
  validated operational-stage graph joined to every product requirement.
- [`docs/audits/CLEAN_FRAMEWORK_GAP_AUDIT.md`](docs/audits/CLEAN_FRAMEWORK_GAP_AUDIT.md)
  — implemented, staged, and missing framework state without component-to-product
  inflation.
- `docs/delivery/` — dependency, package, installation, and release requirements.
- `requirements/` — machine-readable requirements and acceptance identifiers.
- `engine/` — new native engine implementation.
- `postgres/` — PostgreSQL extension and SQL execution surface.
- `orchestrator/` — C# orchestration and product services.
- `dependencies/` — verified clean-upstream dependency supply chain.
- `packaging/` — Linux and Windows product packages.
- `tests/` — native, PostgreSQL, orchestration, and complete-product acceptance.

Every implementation file in this repository is authored from the product contracts
and verified upstream standards and dependency sources.

## Reproducible native build

The repository stores dependency intent and exact locks, never dependency source trees
or submodules. Resolved source lives under `/opt/laplace/external`; builds, logs,
generated artifacts, and installed outputs remain under `/opt/laplace`. The commands
below verify the source cache, configure the SIMD BLAKE3 native build, execute the
implementation suite, and prove that every discovered CTest test has a machine-readable
evidence entry.

```bash
export LAPLACE_DEPENDENCY_LOCK_SHA="$(sha256sum dependencies/lock.json | awk '{print $1}')"
export LAPLACE_VERIFIED_SOURCE_ROOT="/opt/laplace/external/source-generations/$LAPLACE_DEPENDENCY_LOCK_SHA"
./tools/dependencies/verify-lock.sh "$LAPLACE_VERIFIED_SOURCE_ROOT"

cmake --preset linux-dev \
  -DLAPLACE_BLAKE3_SOURCE="$LAPLACE_VERIFIED_SOURCE_ROOT/blake3/c" \
  -DLAPLACE_GTEST_SOURCE="$LAPLACE_VERIFIED_SOURCE_ROOT/googletest" \
  -DLAPLACE_EIGEN_SOURCE="$LAPLACE_VERIFIED_SOURCE_ROOT/eigen" \
  -DLAPLACE_SPECTRA_SOURCE="$LAPLACE_VERIFIED_SOURCE_ROOT/spectra"
cmake --build --preset linux-dev --parallel
ctest --preset linux-dev
./tools/tests/verify-registry.sh /opt/laplace/work/laplace-refactor/build/Laplace/linux-dev
```

The instrumented build uses the same imported sources:

```bash
cmake --preset linux-sanitize \
  -DLAPLACE_BLAKE3_SOURCE="$LAPLACE_VERIFIED_SOURCE_ROOT/blake3/c" \
  -DLAPLACE_GTEST_SOURCE="$LAPLACE_VERIFIED_SOURCE_ROOT/googletest" \
  -DLAPLACE_EIGEN_SOURCE="$LAPLACE_VERIFIED_SOURCE_ROOT/eigen" \
  -DLAPLACE_SPECTRA_SOURCE="$LAPLACE_VERIFIED_SOURCE_ROOT/spectra"
cmake --build --preset linux-sanitize --parallel
ctest --preset linux-sanitize
./tools/tests/verify-registry.sh /opt/laplace/work/laplace-refactor/build/Laplace/linux-sanitize
```

The importer can populate an empty external source root from an independently acquired
source set. It refuses an existing destination, preserving already-verified trees
instead of silently replacing them.
