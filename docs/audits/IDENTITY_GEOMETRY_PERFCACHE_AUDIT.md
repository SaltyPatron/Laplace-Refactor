# Identity, geometry, and perfcache audit

Date: 2026-08-24

This is a read-only evidence audit of the preserved implementation and installed
artifacts. It establishes defects the clean-room implementation must detect. It does
not authorize reuse of historical product code, schema, build logic, or tests.

## 1. Confirmed invention invariants

The preserved evidence consistently supports these required behaviors:

- content identity excludes source, language, modality, model, trust, tier, ordinal,
  storage, coordinate, and time;
- a multi-constituent identity derives from the ordered constituent-ID sequence;
- a one-constituent composition resolves to its constituent identity;
- Unicode and DUCET anchor the atom geometry while UTF-8 canonical content anchors
  codepoint identity;
- DUCET rank selects deterministic super-Fibonacci placement on S3;
- composite arithmetic centroids organize constituent multisets;
- Hilbert keys index locality and do not establish identity or meaning;
- trajectories store ordered constituent IDs and realized curves resolve current
  coordinates;
- perfcaches are immutable derived acceleration planes with distinct typed key spaces;
- PostgreSQL remains authoritative for testimony and durable substrate state.

## 2. Artifact inventory

The installed perfcache directory contains five artifact kinds:

| Installed artifact | Format | Records | Bytes | SHA-256 |
|---|---:|---:|---:|---|
| `laplace_t0_perfcache_17.0.0.bin` | 2 | 1,114,112 | 89,580,004 | `1f311febe40729f983cc62fd41b9a10cfb3d78cba9275b9493366d6ed63a2de9` |
| `laplace_highway_perfcache.bin` | 1 | 223 | 10,785 | `a2e28c350f54905ca834af2a3c5beb86214a4a2f00e839ed7e1118377d881477` |
| `laplace_modality_number_perfcache.bin` | 1 | 256 | 20,624 | `a41c864a2c5050108f3f0fb33e83bde86c19050cbc67d38882d69f3b583699a3` |
| `laplace_chess_position_perfcache.bin` | 1 | 239,014 | 19,121,264 | `badd56e924f050fb0f280222b24e0753dc0179ac3acedd9e43fb7f40b08889a5` |
| `laplace_chess_transition_perfcache.bin` | 1 | 7,870 | 251,920 | `9e5a77d0fb1f4e571809a677d97a97e009073204bb0b5c599a31041eb0c04e60` |

The installed files exactly match artifacts in the `agent-trace` worktree, not one
coherent artifact set produced by the repository root.

The repository-root build differs materially:

- highway: 214 records, 10,349 bytes, SHA-256
  `d17174598ba77094aa270bbe6191fc0b3798a191f69ac29d9769cbe1b2465b46`;
- chess position: 9,405 records, 752,544 bytes, SHA-256
  `d0f9b7bf9e4a7a1e06d0a8448241a8f0568a5b61bac26dca5c226e20e3c008b2`;
- T0 and canonical-number artifacts match the installed copies;
- chess-transition bytes differ from the root build but match `agent-trace`.

There is no artifact-set manifest proving that the five installed files, installed
extension, native library, relation manifest, database state, and generator recipes
belong to one activation epoch.

## 3. Identity implementation findings

### Confirmed behavior

The native multi-child hash excludes tier and hashes a domain byte plus the ordered
child IDs. The generic composer collapses a one-child node to the child ID. Codepoint
IDs derive from UTF-8 bytes and do not read DUCET rank or geometry.

### Defects in delivery

1. The fixed SIMD-optimized BLAKE3-128 identity serialization is not declared as one
   generated contract across native, PostgreSQL, and C# surfaces.
2. An empty generic composition becomes a zero ID, while empty content, absent content,
   and a sentinel are not established as separate encoded states.
3. Numerous managed sites manufacture namespaced strings and hash them directly for
   operational, player, analysis, event, and source identities. Those call sites do
   not prove that content identity, referential identity, and operational identity are
   different types.
4. The historical code contains multiple identity implementations and type-name caches
   rather than generated bindings over one identity ABI.
5. The 128-bit byte order, storage representation, collision handling, and canonical
   test vectors are not delivered as one product identity epoch.
6. Current tests prove selected hash calls but do not prove route convergence across
   native, SQL, C#, every modality, every batch size, persistence, and materialization.

## 4. DUCET and Unicode findings

The T0 artifact header declares UCD 17.0.0 and UCA 17.0.0. Its source fingerprint bytes
are `5d654bebdc3c299b9470d086ebb3e3c5`. The build inputs currently resolve to:

- UCDXML archive SHA-256
  `0466e880488c08f7f5373ada4da5746f2223b654c622d98fae7fb9edb7c5fd01`;
- extracted UCDXML SHA-256
  `b40907541f68f6cee76d2f2631f6371fb2f16c7496ea8bfbac30ad6ea9fdc117`;
- DUCET `allkeys.txt` SHA-256
  `2503d09367c2639a4fb8fd55e81aaacb0d9fb4ea26600333329bd12456b99ecd`;
- configured generator fingerprint `08bc889d93ea23b7`.

The generator improved source invalidation by including the XML, DUCET, versions,
scope, and selected generator-source fingerprints. The runtime loader, however, only
checks artifact format and whole-body BLAKE3. It does not compare the header source
fingerprint with a declared runtime recipe.

The DUCET implementation is not exact:

1. It ignores every multi-codepoint left-hand entry.
2. For a one-codepoint entry, it reads only the first collation element's primary,
   secondary, and tertiary weights.
3. It does not retain the complete lexicographic collation-element sequence.
4. It resolves equal retained keys by codepoint number.
5. Its Hangul handling assigns only the leading-jamo key and then relies on codepoint
   order for the remainder.

Therefore the generated rank is a project-specific approximation, not the complete
DUCET total order. The new implementation requires an independent full-weight rank
generator plus explicit and implicit weight, contraction, expansion, Hangul,
canonical-equivalence, and total-permutation verification.

The UCD flag record also stores only a narrow subset of properties. That can be a valid
typed plane scope, but the header must declare the exact property schema and the source
fingerprint must cover every generator and table that affects it.

## 5. S3 and Hilbert findings

The live substrate proves that canonical physicalities use arithmetic centroids of
actual live constituent coordinates. A read-only measurement on entity
`ab8b29c8d65f298c7239cb5444ae4f0d` expanded five constituents and reproduced the four
stored coordinate components with absolute deltas from zero to `5.55e-17`. The stored
radius `0.36815353696049563` matched the measured centroid norm
`0.3681535369604957`.

The chess implementation and its managed peer instead use an intrinsic mean that
projects composites back onto S3. That is a disconnected contradictory side
implementation, not uncertainty in Laplace's geometry.

That split breaks all of the following:

- universal geometry semantics;
- cache/reference parity;
- composite radius meaning;
- cross-modality placement comparison;
- one canonical implementation per physical operation.

The T0 super-Fibonacci calculation uses floating-point square root, sine, and cosine.
Compiler flags constrain contraction and fast-math behavior, but the artifact contract
does not identify the math-library implementation or prove byte-identical coordinates
across supported builders. Geometry artifacts therefore need a declared numeric recipe
and byte-determinism test across the supported build matrix.

The Hilbert implementation quantizes four coordinates to 32 bits each and packs a
128-bit byte-sortable key. Its current unit tests do not establish complete boundary,
monotonic locality, encode/decode cell, endianness, and cross-language parity across
the packaged product.

The earlier suspicion of a duplicate `POINT4D pa, pb` declaration in the geometry
extension was checked against the source and is false. There is one declaration inside
each relevant function.

## 6. Shared-floor violation in chess

The chess position generator explicitly states that it has no dependency on the
Unicode floor. It constructs a private 128-value byte coordinate table, hashes each
encoded byte directly, and composes typed chess atoms from those private byte IDs.
The managed chess implementation mirrors the same private byte atom system.

This is not a harmless cache specialization. It creates a second atom universe and a
second geometry recipe. It prevents a scalar or symbol in chess from converging with
the same canonical content elsewhere and contradicts the universal modality floor.

The new implementation must represent chess fields through universal typed
compositions over shared atom identities. A chess perfcache can accelerate those
compositions, but it cannot define another atom identity or physical frame.

## 6.1 Run encoding exists but product paths bypass it

The native trajectory format correctly packs the 128-bit entity identity, ordinal,
16-bit run length, and metadata into four binary64 mantissas. Its run encoder splits
runs longer than 65,535 across vertices and tests reconstruct the exact total.

Repository-wide caller analysis finds no product caller of the native run encoder.
Only the declaration, implementation, managed wrapper, and tests reference it. Plain
trajectory construction is used by deposition paths.

The live database contains 23,185,955 physicalities and 22,071,715 trajectories. Its
widest observed composition has 1,341,046 constituents and 1,341,046 stored vertices,
confirming that the current product did not apply run encoding to that million-scale
path. This is implemented machinery that was not integrated, while tests still passed.

The clean ISA must make logical run input a primary batch form. Hashing, centroid
accumulation, trajectory encoding, sparse deposition, readback, containment, and
receipts must consume the same run spans and prove equality with the expanded sequence.

## 7. Perfcache format and loader findings

### T0 Unicode atom plane

Strengths:

- fixed header and record sizes;
- dense codepoint records;
- decomposition and composition sections;
- full-body BLAKE3 trailer verification;
- source fingerprint generated from major inputs;
- direct and reverse lookup surfaces.

Defects:

- runtime does not verify source fingerprint against an active recipe;
- byte order is assumed from native struct layout and is not validated;
- offset arithmetic lacks overflow-safe helpers;
- section non-overlap, exact final length, alignment, reserved bytes, and internal
  decomposition bounds are not fully validated;
- codepoint/index, full DUCET-rank permutation, content ID, coordinate, Hilbert, flags,
  decomposition, and composition semantics are not fully checked by the loader;
- emitter writes changed output directly to the destination rather than publishing a
  verified temporary artifact atomically;
- the reverse index is rebuilt independently in native core and the PostgreSQL
  extension, with different allocation ownership.

### Relation-highway plane

The root manifest SHA-256 begins `2a81fa3c903f2679`, while the installed artifact
fingerprint is `5fc49b1a34f2a85d`, matching the `agent-trace` worktree. A checked-in
generated highway artifact has fingerprint `86802877fb923f46`, matching neither the
root manifest nor the installed manifest.

The highway loader checks magic, version, counts, and broad section ends. It does not:

- verify a whole-artifact checksum;
- compare the eight-byte source fingerprint to the active relation manifest;
- validate exact file length, section overlap, alignment, names, string termination,
  bit uniqueness, bit ordering, band membership, parent validity, or duplicate keys;
- validate the generated C header and binary as one artifact contract;
- publish changed bytes atomically.

The installed database can therefore execute relation routing metadata from a
different worktree without the loader detecting that difference.

### Canonical-number plane

The loader verifies format, dense count, record size, whole-body BLAKE3, and
`record.value == index`. It does not compare its source fingerprint to the loaded T0
plane, validate scope text, validate every ID and geometry against canonical numeric
composition, or reject an artifact whose records are semantically wrong but whose
checksum was recomputed.

The emitter's source fingerprint includes the T0 source fingerprint, generator tag,
scope, and value count, but does not automatically fingerprint all implementation
sources that calculate identity and geometry. It also writes directly to the final
destination.

The PostgreSQL extension does not expose this plane in its perfcache configuration,
readiness, prewarm, diagnostics, or SQL surfaces. Native modality code can use it while
the installed server cannot report or manage it coherently.

### Chess-position plane

The loader verifies format, broad record bounds, and a whole-body BLAKE3 trailer. It
does not verify source recipe compatibility, scope, sorted order, unique IDs, record
semantics, exact file length, or parity with canonical composition.

The source fingerprint covers catalog-surface bytes and manually maintained generator
tags. It does not fingerprint the complete generator implementation, native math,
identity implementation, or managed mirror. Direct destination writes expose readers
to partial changed artifacts.

The private byte atom and intrinsic-mean design make the artifact semantically invalid
for the universal substrate even when its file checksum is correct.

### Chess-transition plane

This format is implemented in C#, not the common native perfcache layer. It has no
source fingerprint, scope, dependency declaration, recipe ID, or generator identity.
It verifies a BLAKE3 trailer and supports temporary-file publication, but a single span
length limits the body to `INT_MAX` and the product has no declared sharding contract
for larger sets.

The runtime silently ignores load errors during automatic discovery. It also places
new transitions in a mutable process-local dictionary. That dictionary changes lookup
state outside PostgreSQL, outside an immutable perfcache artifact, outside a shared
activation epoch, and outside durable receipts.

### Missing modular foundation

T0, highway, canonical-number, chess-position, and chess-transition each implement
mapping, global state, validation, lifecycle, and errors differently. The PostgreSQL
extension hardcodes three path variables instead of reading a typed perfcache registry.
There is no generic dependency graph, coherent activation epoch, artifact-set receipt,
hot replacement protocol, or process-wide loaded-artifact report.

## 8. Activation and installed-artifact defects

The installed PostgreSQL `$libdir` resolves an older `laplace_substrate.so` with SHA-256
`8ba99f...`, while another installed copy has SHA-256 `8ca31e...`. The older copy also
contains temporary-worktree runtime paths. The active extension can therefore differ
from the newer installed artifact.

Perfcache prewarm logs a warning and continues cluster startup when loading fails.
Managed cache discovery also suppresses entry-point and load failures. These behaviors
permit the same request to execute different semantic or performance behavior based on
which process touched which artifact first.

The custom PostGIS/GDAL/GEOS/PROJ stack has similar loader ambiguity: ambient library
resolution can select system GEOS, and the observed GDAL graph includes both system and
custom PROJ sonames. Package verification must record the exact loaded object path,
build ID, and full hash for every native dependency and perfcache.

## 9. Test defects

Current tests cover useful local mechanics, but they do not prove the product contracts:

- T0 tests sample a few codepoints and properties rather than proving the complete
  generated permutation and standards input;
- canonical-number tests compare five values and do not corrupt a real semantic record
  while recomputing the artifact checksum;
- highway has no complete loader corruption, source-staleness, or manifest-parity suite;
- chess-position lacks complete sortedness, uniqueness, source, recipe, and canonical
  composition parity tests;
- chess-transition round trips its own writer and reader but does not prove parity with
  native state transition semantics or durable substrate state;
- determinism tests compare regenerated bytes with prior bytes from the same
  implementation, which cannot detect a shared semantic defect;
- no packaged test asks every running process which exact artifact and library hashes
  it loaded;
- no deliberate defect suite proves that perfcache use cannot change IDs, geometry,
  ordering, misses, or errors relative to canonical execution.

## 10. Required clean-room architecture

The new implementation must provide:

1. one generated identity and physicality contract across native, PostgreSQL, SQL, and
   C# bindings;
2. one exact full-DUCET T0 generator with independent verification;
3. one arithmetic-centroid composite geometry implementation over live constituent
   points and one Hilbert codec;
4. one shared Unicode atom floor for every modality;
5. one native perfcache mapping, validation, publication, epoch, registry, and
   diagnostics foundation;
6. separate typed cache modules for each key and value space;
7. complete source and recipe fingerprints validated at activation;
8. coherent artifact-set activation bound to exact native and dependency hashes;
9. exact canonical-operation parity and deliberate semantic corruption tests;
10. PostgreSQL readiness and SQL inspection surfaces for every active plane;
11. immutable readers, atomic publication, independent module replacement, and
    receipt-bound lifecycle;
12. batch lookup APIs that avoid per-record language, process, and database crossings;
13. run-span and sparse-deposition kernels integrated into every repeated-content path,
    with exact expanded-sequence parity.

No installed artifact from the preserved implementation satisfies this complete
contract. All five are evidence and test inputs for defect scenarios, not source assets
for the clean implementation.
