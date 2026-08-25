# Dependency asset audit

## Scope

This audit records existing upstream source and installed custom runtime assets. It
does not inspect database contents and does not treat current Laplace implementation
code as reusable product source.

## Installed product tree

`/opt/laplace` occupies approximately 744 MiB and currently contains:

- a custom PostgreSQL installation under `/opt/laplace/pgsql-18`;
- custom PostGIS extension libraries in the PostgreSQL library directory;
- custom GDAL, GEOS, PROJ, and tree-sitter prefixes;
- Laplace native libraries and PostgreSQL extension binaries;
- deployed .NET applications, endpoints, runtimes, and web assets;
- logs, chess working directories, configuration, secrets, and the current database
  location.

The installed dependency versions observed through their own tools and control files
are:

| Component | Installed version | Installed prefix |
| --- | --- | --- |
| PostgreSQL | 18.3 | `/opt/laplace/pgsql-18` |
| PostGIS | 3.6.3 | `/opt/laplace/pgsql-18` |
| GDAL | 3.9.3 development state | `/opt/laplace/gdal` |
| GEOS | 3.12.2 | `/opt/laplace/geos` |
| PROJ | 9.4.1 | `/opt/laplace/proj` |
| tree-sitter | 0.25 and an older 0.22 ABI file | `/opt/laplace/tree-sitter` |

PostgreSQL 18.3 records this build configuration:

```text
CC=gcc
CXX=g++
CFLAGS=-O3 -march=haswell -fno-fast-math -ffp-contract=off
CXXFLAGS=-O3 -march=haswell -fno-fast-math -ffp-contract=off
--prefix=/opt/laplace/pgsql-18
--with-icu
--with-ssl=openssl
--with-zlib
--with-uuid=e2fs
--with-libxml
--without-readline
--with-python
--with-lz4
--with-zstd
--with-liburing
```

The installed Laplace extension also links Intel oneAPI compiler runtime, MKL, and TBB
libraries. The dependency graph therefore includes both the GCC-built PostgreSQL stack
and the native engine toolchain/runtime stack.

## Upstream source trees

The current repository has approximately 2.3 GiB of dependency sources under
`external/`. All direct Git source trees inspected were clean at these exact commits:

| Source | Revision | Description |
| --- | --- | --- |
| BLAKE3 | `95e42b84fc4709974c7b23c7ae885989ab36c31e` | 1.5.4 |
| cutechess | `e471973ad41c9c891da1de08d8b9ad630f001168` | v1.5.1 plus two commits |
| Eigen | `3147391d946bb4b6c68edd901f2add6ac1f31f8c` | 3.4.0 |
| Fathom | `c9c6fef0dddc05d2e242c183acf5833149ab676d` | v1.0 plus 103 commits |
| GDAL | `501754d048e07b07932ec268b6ed3c43ea0819e3` | v3.9.3 |
| GEOS | `b74fefb7be4ba6c833760106f95f0f495b832c43` | 3.12.2 |
| GoogleTest | `b514bdc898e2951020cbdca1304b75f5950d1f59` | v1.15.2 |
| PostGIS | `3d12666588a84b23a3147618eaa9b40b0fe5e796` | 3.6.3 |
| PostgreSQL | `62d6c7d3df6287f1bd83199c1a746e50d31571a0` | REL_18_3 |
| PROJ | `875a485fa5ef435c57f7682f904dd31ca92253ab` | 9.4.1 |
| Spectra | `6841bcbacaa0f0a8446210314e682057a084be4e` | v1.2.0 |
| tree-sitter | `da6fe9beb4f7f67beb75914ca8e0d48ae48d6406` | v0.25.10 |

`external/tree-sitter-grammars` is also present and requires a separate grammar-level
revision inventory because it contains many grammar sources rather than one direct Git
root.

That grammar inventory is now complete. `dependencies/tree-sitter-grammars.lock.json`
records 299 independent repositories, 314 generated parser files, 162 external scanner
sources, exact revisions, Git-archive digests, and available root license files. All
299 repositories have been imported into the ignored dependency source area and then
verified again with zero dirty worktrees. Five repositories do not commit a generated
parser and therefore require a declared generation operation. Twenty-nine declare a
license identifier without carrying a root license file; they cannot enter a package
until corresponding license evidence is pinned.

These upstream trees are existing dependency assets. They may enter the new dependency
supply chain after upstream identity, clean status, license, source-tree checksum, and
build contract verification. Their existence does not authorize copying any Laplace
implementation from the current repository.

## Selected official release inputs

Five newer release archives already present in the clean repository cache were
redownloaded from their official PostgreSQL or OSGeo HTTPS endpoints. Every fresh
download produced the same SHA-256 as the cached bytes:

| Component | Version | Archive SHA-256 | Expanded tree SHA-256 |
| --- | --- | --- | --- |
| PostgreSQL | 18.6 | `555610c24d53e4316da5b7d3fc25c279d96856d5e0e23ee308c328c5fa881d9f` | `2db816c62c120392afcb72a9bbed431c31c0410af42ca3efde0adf0697de2051` |
| PostGIS | 3.6.4 | `ed8dc6679f1e06f7b113592b04cde2a7e00f1b1e681294c8ca2204058990cec6` | `51e1f318301505b2436be198bc9bc8de7958d93c4f66a5c1124aec5ec351310f` |
| GDAL | 3.13.3 | `5e0c388d83da2d686cc00a40272882432cdb54edff43d4af173e532844a0a0ea` | `f786f1bca4e3e1871f1f42e0deb43fbfe88427f27c67e449c189179757efb379` |
| GEOS | 3.14.1 | `3c20919cda9a505db07b5216baa980bacdaa0702da715b43f176fb07eff7e716` | `9a00076c9d9a4a5358ec9277a30a21a8f77da842a0ec59e651c1064f7b3aad9c` |
| PROJ | 9.8.1 | `af5b731c145c1d13c4e3b4eeb7d167e94e845e440f71e3496b4ed8dae0291960` | `c28be4a194a4a149ea590260e6ffcee62c4ff2ef53bbaf22167434022b59bf94` |

`dependencies/release-lock.json` also binds exact archive size, member count, regular
file count, expanded byte count, top directory, source URL, and license-file digests.
`tools/dependencies/release-assets.py` rejects altered archives, duplicate or escaping
members, privileged mode bits, unsafe links, undeclared extracted paths, changed file
contents or modes, and an existing publication destination. The five extracted trees
were independently checked against their archives after import.

This selects source identity only. The stack becomes a product dependency only after
its declared secondary sources, build features, ABI closure, runtime operations, and
package evidence also pass.

## Proven artifact-identity defects

Two different Laplace PostgreSQL extension binaries are installed:

| Path | SHA-256 | Observed build origin |
| --- | --- | --- |
| `/opt/laplace/pgsql-18/lib/laplace_substrate.so` | `8ba99f71fae00f073f64057aa7d46362919cd6260d8c4fe0670c05eaec212d69` | older binary with temporary-worktree runpaths |
| `/opt/laplace/lib/postgresql/18/laplace_substrate.so` | `8ca31e0f3daf56df4cd1c77b14a6bb864a6d64306efe4e640a6114e36d60a714` | newer binary with installed-prefix runpaths |

PostgreSQL reports `/opt/laplace/pgsql-18/lib` as `$libdir`. The binary in that
directory embeds absolute paths into
an archived temporary worktree's `build-gcc/engine/{dynamics,core}` directories. It is older than the
second installed binary and is not byte-identical to it. A build or source-tree test
cannot establish which implementation a live SQL call executes until the package and
activation system proves the loaded object hash.

The installed PostGIS binary declares runpaths to the custom GEOS, PROJ, and GDAL
prefixes. The current shell environment contains a very large `LD_LIBRARY_PATH`; under
that ambient state, loader inspection selected the system GEOS 3.10.2. With an explicit
custom prefix path, it selected custom GEOS 3.12.2. GDAL currently resolves both the
system PROJ 22 ABI and custom PROJ 25 ABI through its transitive graph. The service
runtime must therefore use a generated, minimal loader environment and verify every
loaded object rather than trusting installed filenames.

Selected installed artifact hashes recorded during this audit:

| Artifact | SHA-256 |
| --- | --- |
| PostgreSQL server | `ffbf042d6898f6ecf459bd172b11d54d956a4062d4d39e3d7fb4c63cc12da777` |
| PostGIS core | `1282f11a137440f6b4c8f0d7dcafa2e628ee72985758ccb7b3ff8865ff012861` |
| GDAL | `498835eef2ff6709290aba04e2fd114721be4ba9e38fea05cf46616b3bf33ad8` |
| GEOS | `663914c2ce142d1444012c78d865c1e3c3b425c0417f1af5efe58b20543da199` |
| PROJ | `f14c7a6e12e71e85b48286c8f1145e364846fd6b915d2adfd4cf53f740a1f26c` |
| Laplace native core link | `1226c0791723e841b490f20f51e9e20063735e2a93ffe25fe18af81f38107e06` |

## Host-native ELF closure

`tools/dependencies/elf-closure.py` reads ELF headers, dynamic sections, notes, and
loader diagnostics without loading a target object. It resolves each `DT_NEEDED` edge
through declared path precedence, records every compatible competing candidate, binds
the selected real path to its SHA-256, follows the transitive graph, and detects two
classes of ABI-family conflict per root process closure.

The 2026-08-24 host-native scan produced
`out/audit/installed-linux-x64-elf-closure.json` with SHA-256
`3cac9e82e5a91d97ac93cb588bf18686601aa6da500a4f2413696f824c098683`.
The report contains:

| Measurement | Result |
| --- | ---: |
| Installed ELF roots | 217 |
| Distinct closure objects | 280 |
| Product-prefix objects | 190 |
| Host objects | 81 |
| Other toolchain-prefix objects | 9 |
| Resolved edges | 785 |
| Unresolved edges | 0 |
| Parse errors | 0 |
| Product-to-host edges | 380 |
| Product-to-other-prefix edges | 37 |
| Edges with more than one compatible candidate | 42 |
| Caller-dependent SONAME resolutions | 2 |
| Root aliases with an ABI-family conflict | 41 |
| Distinct root binaries with an ABI-family conflict | 35 |

The repeated edges are retained because different callers are independently affected.
The two caller-dependent SONAMEs are `libpq.so.5` and `libtbb.so.12`. The competing
families are `libOpenCL.so.1`, `libgeos_c.so.1`, `libpq.so.5`, and `libtbb.so.12`.
The OpenCL edge currently selects CUDA 12.6 through the host loader cache. Intel
compiler runtime, MKL, and TBB objects enter from oneAPI prefixes. The installed
PostgreSQL extension also carries one working-directory runpath entry and two absolute
runpath entries naming deleted build-tree directories.

Thirty-three root aliases contain distinct SONAME generations from one library family.
Eight contain different binaries carrying the same SONAME, which makes load order
decide which implementation satisfies callers.

## Reproduced GDAL crash

`/opt/laplace/gdal/bin/gdalinfo --formats` exits with status 139 after listing only the
first seven formats. A debugger trace stops in `free()`, called by
`osgeo::proj::common::UnitOfMeasure::~UnitOfMeasure()` from host `libproj.so.22` during
process finalization.

The metadata graph identifies both load chains:

```text
gdalinfo
  -> custom libgdal.so.35
     -> custom libproj.so.25
     -> host libgeotiff.so.5
        -> host libproj.so.22
```

The custom GDAL build therefore places PROJ 9.4.1 and the host's older PROJ generation
in one process. This is not a theoretical packaging concern; the installed executable
crashes on an ordinary capability query. The native engine has a second conflict:
its direct oneAPI TBB selection and MKL's host-cache TBB selection name two different
binaries with the same `libtbb.so.12` SONAME.

## Required dependency outcome

The new product build must:

1. Inventory the complete direct and transitive source graph already present.
2. Verify clean upstream identity, licenses, checksums, patches, features, and ABI.
3. Select versions as one compatible stack rather than five independent version
   choices.
4. Build every component in isolated source, build, stage, and package directories.
5. Generate loader paths and service environment from the package manifest.
6. Reject unexpected system-library resolution for dependencies declared custom.
7. Install one versioned copy of each product binary and activate it by one verified
   version pointer.
8. Record the exact object hashes loaded by PostgreSQL and product services.
9. Prove PostGIS, GDAL, GEOS, and PROJ compatibility through runtime operations.
10. Preserve compiler, numeric, hardware ISA, MKL, TBB, and oneAPI requirements in the
    package identity.
11. Declare a narrow platform-runtime ABI set; every feature library outside that set
    is built and packaged under the versioned product prefix.
12. Use package-relative loader paths and reject working-directory, deleted build-tree,
    host-cache, and undeclared-prefix selection for product feature libraries.
13. Reject distinct library generations and same-SONAME binary conflicts within every
    executable, PostgreSQL server, extension, and service process closure.
14. Execute real capability and shutdown checks for GDAL, PostGIS, PROJ, GEOS,
    PostgreSQL, MKL, TBB, and product extensions from the staged package.

## 2026-08-25 live-host reconciliation

A read-only reconciliation against the active Linux host added the following bounded
observations. They identify recovery and acquisition assets; they do not select clean
product implementations by themselves.

- `/opt/laplace/external` is the resolved external-dependency root. Verified upstream
  trees, grammar sources, release trees, and acquisition caches were relocated there;
  the clean repository contains locks and acquisition/verification tools only.
- IntelLLVM 2026.1.1, oneMKL 2026.1.0, and oneTBB 2023.1.0 are installed under
  `/opt/intel/oneapi`. The deployed native dynamics and synthesis libraries directly
  require MKL LP64, MKL core, MKL's TBB threading provider, TBB 12, and Intel compiler
  runtime objects.
- The custom PostgreSQL 18.3 binary was built with precise contraction controls,
  Haswell ISA selection, ICU, OpenSSL, zlib, XML, LZ4, Zstandard, and liburing. The
  host PostgreSQL 18.4 package also exposes NUMA, LLVM, ICU, LZ4, Zstandard, and
  liburing capabilities, but it is a different build and cannot stand in for the
  custom package.
- The host chess tools are Stockfish 14.1 and cutechess 1.5.1. The clean dependency
  selection instead pins the latest official Stockfish 18 source release and its two
  exact NNUE model artifacts, plus the latest stable cutechess 1.5.1 release source.
- `/vault/Data` is a downloaded dependency root containing chess records and Syzygy
  tables, Unicode and language-standard inputs, lexical and semantic resources,
  multilingual corpora, code-authority snapshots, tree-sitter sources, and media
  fixtures.
- `/vault/models` is a downloaded dependency root containing conventional model
  snapshots across text, code, vision, audio, embedding, and reranking families;
  GGUF artifacts; local and archived code corpora; Stack-derived partitions; and
  Tiny-Codes data.

The clean contract consequently defines dependency as any external code, binary,
model, corpus, standards dataset, tool, service, or response capable of changing a
calculation. Compiled libraries, downloaded evidence, model weights, invoked engines,
and live services share dependency identity and provenance obligations even though
their execution and epistemic classes remain distinct.
