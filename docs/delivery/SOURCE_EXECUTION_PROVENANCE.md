# Structural witnesses and execution observations

A source profile identifies the selected source, grammar declaration and recipe. A physical parser build has its own verified module hash. Building the same parser in another directory must not remint canonical content or physicality merely because an embedded source path changed.

The installed diagnostic on 2026-09-17 established this distinction directly. Run 35198079402 installed package 19567269059da67118a3de9715358a375d313c192b965b31adba4fe2e4eb9ade and passed PostgreSQL, Unicode, Highway and cognition activation. The subsequent 119-file admission failed at the first C++ artifact root because only its execution trace differed. Entity 8ffc52fab6b184f39b9ef15f25d535af, physicality f2ad26412c86ff2892ceb7845ce16d5623c214a3590ec97031ac564a23a462de and every structural/physicality metadata field matched. The authenticated diagnostic is Git blob 39595c24063449a327bb6007da51a8d82e3775c7.

The independent historical ELF comparison, blob 7b75873f6dc56d532bccc1718507877500ca8580, found exactly 27 changed bytes between two 5,587,920-byte provider libraries: seven build-directory digits in an embedded scanner source path and the 20-byte GNU build ID. Every other byte matched. The verified loader correctly bound those distinct physical libraries into provider fingerprints, and the artifact-wide decomposition trace correctly retained those fingerprints. The defect was requiring that execution metadata to equal the first observation stored under a canonical span key.

## Stored roles

- `source_structural_witness` retains the original canonical span binding and historical trace/provider metadata. Existing rows and v1–v3 receipts remain evidence; they are not rewritten to match another build.
- `source_structural_witness_execution` contains a current receipt ID, profile/span key and actual trace/provider fingerprints. It refers to existing canonical span rows and does not duplicate Entity or Physicality records.
- A v4 structural receipt binds the current execution witness set, a separate canonical witness fingerprint and one explicit authenticated v3 baseline. Identical profile, composition and observation inputs reuse their existing receipt and observation rows.
- A current readback verifies the current observation, canonical structure and historical v3 baseline in one bounded 128-row cursor. A baseline must be v3; v4/self baselines and execution metadata attached to historical receipts are refused.

## Exact hash preimages

All hashes here use BLAKE3-256. Each byte string is preceded by its unsigned 64-bit little-endian byte length. Integer coordinates are unsigned 64-bit little-endian; the last three flags are unsigned 32-bit little-endian. SQL NULL canonical content for a genuine zero-width missing/empty syntax node hashes the existing all-zero E/P byte arrays.

The canonical row preimage, in order, is Entity ID (16 bytes), Physicality ID (32 bytes), artifact index, span index, parent span index, byte start, byte end, kind, grammar kind, field kind, sibling ordinal, media bytes, depth, flags and syntax flags. Every syntax/content field remains bound. The canonical set domain is `laplace.source-canonical-witness-set/v1`, followed by profile ID (32 bytes), witness count and those ordered rows.

The historical v3 set preimage is unchanged: domain `laplace.source-structural-witness-set/v3`, profile ID (32 bytes), witness count and each ordered row with trace fingerprint (32 bytes) and provider fingerprint (32 bytes) preceding the canonical row fields.

The unchanged v3 receipt preimage is domain `laplace.source-structural-witness-receipt/v3`, profile ID, composition working-set receipt ID, v3 witness fingerprint and witness count. The v4 receipt uses domain `laplace.source-structural-witness-receipt/v4`, the same current fields, then the canonical witness fingerprint and explicit historical baseline receipt ID. Timestamp, directory name and run nonce are absent from canonical identity; actual module-derived execution fingerprints remain recorded.

## Public result and verification

Successful source admission returns its exact current structural receipt ID in `source_admission_last_execution_metrics().last.structural_execution_receipt_id`. The admission CLI captures that result in the same backend and selects the exact v4 receipt by ID, profile and composition. It does not infer an observation from the latest database row. The readback result retains separate canonical and execution fingerprints, baseline/current receipt IDs and execution row/receipt counts.

The compiler owner normalizes embedded source locations with `-ffile-prefix-map=<frozen source root>=.`; its receipt still records actual compiler identity, arguments, source paths and final module hash. Actual hosted run 35201348488 / job 105136583074 passed 34 tests with zero skips, including full library equality across two directories, callable source/header location macros, a live assertion, changed semantic source and removal of only prefix normalization as a deliberate failure control. This reproducibility repair alone does not make historical unnormalized module bytes identical.

The PostgreSQL contract compiles a second real provider from the same authenticated parser/header inventory in another directory with only prefix normalization removed. It then checks same-profile E/P reuse, unchanged historical evidence, exact old/current readback, one distinct observation and zero additional rows on identical replay. Deliberate corruptions cover both traces, both provider fingerprints, canonical syntax/content/physicality/geometry and receipt bindings. Current native qualification and installed same-profile 119-file verification remain required before claiming this repair is deployed or the corpus replay succeeds. Source reconstruction does not prove executable C++ semantics or chess strength.
