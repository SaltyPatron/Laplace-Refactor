# Verified Git source corpus

`laplace-admit-source verified-git-code` admits an exact selected Git checkout as
source observations through the existing native source, composition, and world
admission owners. The C++ provider is the locked Tree-sitter runtime and grammar
built from their verified upstream source. A source checkout and an engine build
are separate objects: building Stockfish does not admit its source to Laplace.

## Inputs and commands

Read the configured checkout and exact commit from the dependency build receipt.
The configured Stockfish checkout can be `/vault/External/Stockfish/SF_19`; the
admission tools do not relocate or modify it. Select an explicit upstream commit,
then write a new frozen snapshot inside the configured source estate:

```sh
python3 tools/sources/verified_git.py \
  --checkout /vault/External/Stockfish/SF_19 \
  --upstream https://github.com/official-stockfish/Stockfish \
  --commit "$STOCKFISH_COMMIT" \
  --output "$SOURCE_ESTATE/stockfish-$STOCKFISH_COMMIT"
```

The snapshot checks actual tracked bytes and executable modes against the Git
objects, independently of index flags, and disables replacement objects. It
retains every selected named regular file, including repeated content under
different names. This route currently requires nonempty UTF-8 files without NUL,
at least one C++ file, and its declared file/byte limits. Unsupported inputs fail.

For a configured checkout produced by the verified local import workflow, also
pass `--expected-archive-sha256` with the selected product lock's Git archive
SHA-256. The tool checks that archive and all tracked bytes, records the actual
local origin, and leaves the remote unchanged. Admission verifies the selected
upstream, commit, and archive against the installed dependency lock.

Qualify the C++ provider using the exact dependency lock and an already verified
Tree-sitter runtime checkout. `--acquire` obtains the locked grammar commit in a
new checkout; compilation uses a frozen copy of verified tracked inputs:

```sh
python3 tools/sources/qualify_grammar.py --acquire \
  --grammar-root "$GRAMMAR_CHECKOUT" --runtime-root "$TREE_SITTER_CHECKOUT" \
  --grammar-lock dependencies/tree-sitter-grammars.lock.json \
  --dependency-lock dependencies/lock.json --grammar tree-sitter-cpp \
  --compiler "$C_COMPILER" --output "$GRAMMAR_BUILD"
```

With the current product activated, use its installed admission command. The
existing source-estate guard remains in force:

```sh
laplace-admit-source verified-git-code \
  "$SOURCE_ESTATE/stockfish-$STOCKFISH_COMMIT/files" \
  --git-manifest "$SOURCE_ESTATE/stockfish-$STOCKFISH_COMMIT/manifest.json" \
  --grammar-receipt "$GRAMMAR_BUILD/build-receipt.json" --pretty
```

The native provider checks the actual library bytes against the supplied build
receipt and retains the verified file descriptor for the lifetime of the loaded
grammar. The installed source contract locks must match the qualified build.
Missing providers, stale identities, unsupported hosts, and parse failures fail
explicitly.

The declared grammar identity binds its locked source and ABI, excluding physical
build directories and command transcripts. The executed provider identity also
binds the actual library SHA-256. Different compiled library bytes can therefore
produce a different execution/source fingerprint and structural witness receipt,
even when source content and its declared interpretation agree. Exact canonical
content reuse and replay with the same qualified provider are checked; execution
fingerprint equality across different parser builds is not claimed.

## What the receipts prove

Successful admission retains exact named source inputs, native canonical source
roots, concrete syntax spans and field roles, the qualified grammar identity,
and native profile/composition/world receipts. Repeated named files can reuse one
canonical content root. Source observations carry zero semantic claims and use
absent evidence lineage/testimony receipts; no placeholder evidence is created.

Tree-sitter ERROR and MISSING observations remain explicit and contribute to the
profile error count. An actual zero-width syntax wrapper that the parser does not
classify as missing has a separate `SYNTAX_EMPTY` flag. These absent spans have
no canonical content entity. The complete source bytes remain independent of
the syntax tree and are reconstructed exactly, including non-ASCII text.

The command then performs a batch readback that validates the full retained
structural receipt, native profile identity, canonical identity witnesses, and
the selected GEOMETRY/PERFCACHE epochs. Each output must match the selected Git
file's SHA-256 and byte count. Its success phase is
`source-admitted-and-exactly-read-back`. Readback occurs after the admission
transaction commits; a subsequent readback failure does not undo the already
committed admission and does not produce an overall success receipt.

Repeat verification compares exact canonical identities and scoped occurrence
and structural-witness counts. `readback.database_row_counts` is a fresh
postcommit observation; unrelated concurrent admissions can change those global
counts and must be considered when assessing amplification.

This route proves retained source content and observed syntax. It does not prove
C++ symbol resolution, executable behavior, chess search quality, PGN ingestion,
or playing strength. Work/output bounds are explicit; they are not measurements
of total backend memory consumption.

## Acceptance gates

The native tests execute the actual qualified library, preserve duplicate named
content and parser errors, and reconstruct the selected files. The separate
PostgreSQL gate `postgres.verified-cpp-source-contract` exercises real admission,
zero-claim closure, UTF-8 batch readback, immutable replay, provider failures,
corrupted retained witnesses/profile, epoch mismatch and output bounds. Its
successful log must contain `LAPLACE_QA_RECEIPT verified_cpp_source_admission`.
A configured gate without its actual qualified provider fails.

Local parser/source-plan checks and PostgreSQL admission are separate evidence
phases. A completed fixture or configured-machine receipt is required before
claiming that the corresponding database or full Stockfish corpus acceptance
has passed.
