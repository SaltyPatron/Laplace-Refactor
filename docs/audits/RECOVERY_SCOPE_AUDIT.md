# Recovery scope audit

## Finding

The recoverable work is substantially larger than one SQL change or the visible dirty
working tree. Current Git reachability alone contains hundreds of commits outside
`origin/main`, and the preserved session archive contains reported changes, temporary
worktree activity, build/install actions, and work claims that cannot be reconstructed
from branch names alone.

The recovery outcome is not a mass merge into the new repository. The outcome is:

1. preserve every byte and Git object;
2. reconstruct what was attempted and what survived;
3. retain inventor-authored requirements, facts, measurements, and counterexamples;
4. write the new implementation from the established product laws;
5. prove that every previously exposed defect class is detected by the new acceptance
   system.

## Preservation completed

The dated preservation directory is:

`/home/ahart/Projects/Laplace-archive-2026-08-24/preservation`

Recorded artifacts:

| Artifact | Evidence |
| --- | --- |
| Git bundle | 328 refs; verified bundle; 28 MiB |
| Git bundle SHA-256 | `126d99f03dd1af4f87988a7babaee8c94f7adbffc2859c7a0cd42a128c1ea011` |
| Complete Git bundle after object recovery | 337 refs; verified bundle; 28 MiB |
| Complete Git bundle SHA-256 | `ee3bb75cfc0b60da9783dea1b79c7cdced9ec27babb1b1a7ae4bd2a346c58a31` |
| Formerly unreachable object pack | 5,446 objects; verified pack and index |
| Object pack SHA-256 | `e7da95535980f7bccddc9409ada53ae5a6952439cebf332058ff759ba3381fc5` |
| Object index SHA-256 | `f1406ce28de1795e459dcde31a6b36e1798d553238dfc0f64f607c8962488938` |
| Claude session archive | 49 JSONL files; approximately 75 MiB |
| Current Claude system archive SHA-256 | `0fe0c3e7bf3f2d6a630a1e83abfbef781ec07c8796de185da0cec89131e96049` |
| Current Claude archive inventory | 298 regular files; 80,947,088 bytes; zero read errors; manifest SHA-256 `8a00f09a1aff37ea11be6c94d7473c09195a889df8c321210b59e8519b770fd6` |
| Current Claude temporary-state archive | 3,221 regular files; archive SHA-256 `b08bff544d58a9865baf29fb6c073c606ac3a6a9c51da98e7b357f4985c3c944` |
| Temporary-state inventory | 3,078 distinct file contents; zero read errors; manifest SHA-256 `f5a4537a0e97897f85d9353569953e1703c4752cf337d7d420fccccc5c38a2dc` |
| `/vault/.claude` archive | 3,966 regular files; archive SHA-256 `63c133e493f9496e108e01f6b2d55782b2abe98ae1e5f15b7d2339ccb0a4b66b` |
| `/vault/.claude` inventory | 2,931 distinct file contents; zero read errors; manifest SHA-256 `a610458cebd2891dc379d605099fe3de21bb128bb946381fa5e95c978a23fa51` |
| `/vault/AI_Sabotage` archive | 79,252 regular files; archive SHA-256 `6fb0d99895011dbb0ef70e088f781602ece92371bdd1d04cb9995c07a0d05900` |
| `/vault/AI_Sabotage` inventory | 51,240 distinct file contents; zero read errors; manifest SHA-256 `1c1e77db95c6ef3292d50a279b3a2293d2dc855675deeefb34e99335376bb59d` |
| Cross-corpus Claude index | 1,309 Laplace logs; 1,308 distinct contents; zero archive mismatches; manifest SHA-256 `7218dd4c94c494899f4e588b49d5c4d76af7b2b30b50da13a86daca2975120a4` |
| Deterministic Claude evidence spine | 460,928 unique-content events; 54,561 selected text messages; 79,637 transcript-scoped tool actions; 31,043 artifact references; manifest SHA-256 `3c3002280901df9c17aafa323579aee7ce71f6fc813138d5dc8822752d674f82` |
| Cross-corpus file-history recovery | 39,278 session/path/version-scoped references; 6,542 distinct bodies; 2,711 bodies absent from the Git object database; manifest SHA-256 `1f5b65cdd0a88b10e30ce5f2d91ad71340adf3d095e82666a3166eb3e17bca69` |
| Destructive-state correlation | 1,002 invocation dispositions; 10 reflog-correlated invocations; two causally corroborated file-removal invocations; three discarded Git-unpublished bodies observed and recovered; zero unrecoverable unique-state losses observed; manifest SHA-256 `2d44c0405a687b780bb488a5f320a3fba6ab8e6861605d701e822d5e14ebc5c2` |
| Uncommitted workspace archive | inventor changes, rescue material, and worktree recovery state |
| Workspace archive SHA-256 | `e445a2e9f9a16457b12358c1ff01bdcf3258c7d94bb14fb754c7f22a98df7ef5` |
| Final historical worktree inventory | 115,827 entries; 96,047 regular files; 12,419,034,844 regular-file bytes; zero read errors; two stable passes |
| Final historical worktree manifest SHA-256 | `6b49e2d94791703e47fd2f9771f51dd6f6eb14a615f9f09456ba7bb33f64d322` |
| Final historical worktree summary SHA-256 | `79645ce30b6c27caf8831a126b9924ac530cc903828ccd39c2fc7b236a75d421` |

The complete former 12 GiB directory is now preserved at
`/home/ahart/Projects/Laplace-archive-2026-08-24/historical-worktree`. The clean
`SaltyPatron/Laplace-Refactor` checkout is active at the canonical
`/home/ahart/Projects/Laplace` path. The final move and post-activation verification
are bound in `docs/delivery/CANONICAL_CUTOVER.md`. The current database remains
excluded.

## Claude state and temporary-state expansion

The additional Claude locations materially expand the audit. The archive-bound corpus
index found 1,309 Laplace Claude event-log files containing 461,059 parsed records and
1,118,217,354 bytes. Exact-content grouping produced 1,308 distinct log contents and
248 session IDs. One duplicate worker log exists twice inside `/vault/.claude`; no
cross-corpus files are byte-identical. One session ID occurs in both the current and an
older vault corpus with different bytes and is retained as two content variants.

The event logs contain 18,511 human-message records, 47,742 Bash tool calls, and at
least 6,271 Bash calls whose command contains `psql`. Those 6,271 actions are not yet a
final invocation count because an action may start more than one client and because
PowerShell and raw database MCP tools are separate surfaces. The database-corpus pass
therefore reconstructs invocations and SQL inputs rather than equating tool-call count
with executed statement count.

The three JSONL files under current `/tmp/claude-1000` are not Claude event logs: two
are scratch products and the 1.145 GB file is lexical source data. They remain in the
byte archive and inventory but are excluded from event counts by a tested structural
classifier. This prevents a large dataset from inflating session or message totals.

Across all three new archives, every regular member was streamed and hashed. No tar
member read errors occurred. The cross-corpus pass rehashed each selected live source
and matched all 1,309 accepted log files to their archive inventory entries.

The deterministic evidence spine parses the 1,308 distinct event-log contents once
and retains all 1,309 archive occurrences. Its 460,928 unique-content events expand to
461,059 source-occurrence events because one 131-record log exists twice. Generated
notices and tool-result envelopes are classified separately from 14,327 direct human
text messages. The index scopes tool IDs to transcript content, distinguishes single
pairs, repeated records, unpaired calls, and unpaired results, and does not count
repeated records as independent actions.

Artifact references are resolved against all 2,960 Git commits rather than only the
recovery subset, then separately marked against the 433 recovered commits. This makes
event-to-commit and event-to-path joins reproducible without asking an agent summary or
the running Laplace database to certify them. The index files and manifest were
independently rehashed after publication.

## Complete Git topology

Fresh read-only observations against the current repository:

| Fact | Count or state |
| --- | --- |
| Local branches | 160 |
| Remote refs | 160 |
| Stashes | 1 |
| Linked worktrees | 3 |
| Distinct recovered commits outside `origin/main` | 433 |
| Non-merge commits in that set | 404 |
| Merge commits in that set | 29 |
| Local and remote refs not merged into `origin/main` | 265 |
| Current local main ahead of remote main | 0 |
| Current local main behind remote main | 101 |

The existing rescue inventory created 114 rescue branches for 113 dangling commits and
one stash. Its stable patch-identity pass classified 58 patches as present in main and
55 patches as absent from main. That pass covers the rescued dangling set, not the full
recovered graph.

The first full-graph inventory found 424 reachable commits and 5,446 unreachable Git
objects. Nine of those objects were commits missing from the 328-ref bundle. Before
creating new refs, all 5,446 objects were written to a standalone pack. The nine commits
were then anchored under exact recovery refs and a new 337-ref bundle was created. A
second inventory now reports 433 recovered commits and no unreachable commits. The
remaining 5,424 unreachable blobs and trees are retained by the standalone pack.

## Content and deletion accounting

Stable patch comparison against `origin/main` found 283 non-merge commits with an
equivalent baseline patch. Before the nine commits were anchored, 116 non-merge changes
had no baseline equivalent. Those 116 changes span 1,566 file-change events, 120,339
insertions, and 82,641 deletions. Their domains overlap because one change can affect
several product surfaces:

| Domain | Non-merge changes without a baseline-equivalent patch |
| --- | ---: |
| Tests | 55 |
| Native engine | 48 |
| Managed orchestration | 51 |
| Documentation | 36 |
| Delivery and operations | 26 |
| SQL | 15 |
| Other | 15 |
| Web | 5 |
| Dependencies | 1 |

The recovered graph also records 175 deletion events covering 156 distinct prior blob
objects. Ten of those events affect SQL-identified paths, each with its exact prior blob
identity. This is content-addressed recovery evidence, not an inference from filenames.

The SQL search campaigns found 73 commits touching SQL, including 20 with parsed atomic
bodies, 74 commits whose full trees contain session namespace mutation, 35 containing
index creation, and 14 containing prepared SPI plans. Campaign membership can overlap.
No full-tree occurrence of SQL Server row-count loop syntax was observed. These counts
locate implementation history; they do not certify behavior or architectural fitness.

## Independent reconstruction proof

Archive verification was performed in a newly initialized blank bare repository:

1. unbundle the 337-ref bundle into the blank object store;
2. import the standalone 5,446-object pack;
3. resolve all 433 commit IDs from the post-recovery inventory as commit objects;
4. resolve all 5,446 object IDs from the pre-recovery inventory;
5. independently count 5,446 typed objects in the pack index.

All 433 commit checks and all 5,446 object checks succeeded. This proves the archive can
reconstruct the inventoried Git content without relying on the current repository's
object database. Artifact hashes and the exact result are bound in
`preservation/RECOVERY_RECEIPT.md`.

The worktree directory named for agent tracing is currently attached to an unrelated
rating-correction branch. A second worktree is attached to a test branch. This name,
branch, and artifact mismatch is one reason recovery must use exact object identity
rather than directory names or agent narratives.

## High-confidence recovered work

### SQL parsed-body conversion

Commit `3999680f307b4a893da86364557766c0872228a9` changes 201 SQL function files from
string bodies to parsed atomic bodies. It is not an ancestor of `origin/main` and is
retained by multiple refs, including the remote SQL-fix branch.

This proves that a large SQL refactor existed and was stranded. It does not make its
implementation a source for the new repository. The new SQL contract independently
requires parsed bodies, explicit namespaces, no hidden session state, set execution,
reusable surfaces, analyzed plans, and native ownership of semantics.

The final deduplicated cross-corpus SQL parser reconstructed 8,779 attempted PostgreSQL
client calls, 8,252 with confirmed tool execution, 16,534 attempted statements, 15,585
confirmed statements, 3,825 session-setting statements, 1,248 client time limits
totaling 418,500 seconds, 48 timed-out invocations, and 886 SQL errors embedded in tool
results. It found 1,107 repeated exact groups covering 4,521 executions and 1,235
repeated structural shapes covering 5,737 executions. Nine stdin invocations retain
command and result identity without enough captured input to assert SQL text. The
latest-session 445-call reconstruction remains a chronological subset. Complete plan,
index, temp-table, schema-layering, and live-operation findings are recorded in
`docs/audits/SQL_EXECUTION_SURFACE_AUDIT.md`.

The read-only live observation measured a roughly 273.2 GB database, approximately
124.0 million rows across the eight default consensus partitions, minute-scale mean
times for consensus bulk operations, millions of temporary-block operations, and
multi-gigabyte index families with 0–4 scans per partition. It also observed that the
chess ingest was running again through the CI runner; no process or database state was
changed by the audit.

### Agent-session ingestion

Commit `34195736cf2ce2bcd79aad491637c89b346a9122` is an ancestor of `origin/main`. It
adds 2,916 lines across 28 files for agent-session ingestion, including six named
provider adapters, a generic JSON adapter, a decomposer, an emitter, source contracts,
relation declarations, tests, CLI registration, and ingest scripts.

The preserved parent session also reports adapters for additional providers in later
work. No corresponding live ingest proof was established before the session ended.
The existence of source and tests therefore proves that implementation effort landed;
it does not prove lossless provider coverage, bulk throughput, physical session
structure, restart correctness, or successful product ingestion.

The new product requirement is broader: every discovered provider, every metadata
field, generic typed event preservation, physical and relational turn/session
structure, exact receipts, bounded parallelism, and real ingest acceptance.

### Current installed binaries

The dependency audit proves two non-identical installed Laplace PostgreSQL extension
binaries, with the PostgreSQL `$libdir` copy older and linked to temporary build paths.
That is surviving implementation work in the wrong activation state. Both binaries and
their hashes are retained as evidence; neither becomes the new engine implementation.

## Recovery domains requiring complete reconciliation

The Git and session records expose work in all of these coupled domains:

1. **SQL cohesion and planning** — parsed bodies, namespaces, set inlining, relation
   families, traversal, query surfaces, planner evidence, hot-relation indexes, and
   parameterized SPI plans.
2. **Identity and composition** — tier-independent identity, canonical constituent
   order, structural clustering, trajectory ordinals, collection identity, and removal
   of duplicated derived structures.
3. **Physicality and traversal** — angular locality, Hilbert indexing, realized-curve
   distance, geometry successors, mask routing, and graph orientation.
4. **Evidence and adjudication** — negative evidence, asserted absence, source rating,
   opponent evidence, source independence, rating spread, refutation behavior, and
   relation-type promotion.
5. **Conversation** — prompt resolution, case and language resolution, joint election,
   topic and sense selection, bounded presentation, references, graph walks, generation,
   and documented conversation defects.
6. **Ingestion** — generic decomposers, batch sizing, parallel file processing, exact
   resume, connection ownership, index cycles, progress, receipts, source-specific
   normalization, and source completion.
7. **Chess** — typed trajectories, playing records, move readback, openings, Syzygy,
   search, pagination, performance, and the canceled long-running ingest.
8. **Model work** — model payload checks, universal tensor testimony, QK/OV/FFN
   relation operators, exact numeric projection, target generation, recipe trajectories,
   substrate-native vocabularies, multilingual realization, GGUF writing, external
   runtime execution, and model format handling. The preserved direct compilation and
   Japanese realization evidence is reconciled in
   `docs/audits/MODEL_COMPILATION_EVIDENCE_AUDIT.md`.
9. **Native execution** — ISA operations, native relation rules, deterministic numeric
   behavior, vector kernels, extension ABI, and exact loaded-artifact identity.
10. **Build and dependencies** — external superbuilds, forced rebuild correctness,
    source caches, custom geospatial linkage, compiler stacks, host tuning, extension
    installation, and stale artifact detection.
11. **Tests and measurements** — deliberate defects, topology fixtures, content-law
    tests, relation symmetry, exact test selection, database performance suites, writer
    phase measurement, throughput, and package identity.
12. **Product and operations** — CLI, HTTP, MCP, web, diagnostics, log placement,
    services, deployment, configuration, package activation, and support evidence.

These domains are not independent patches to transplant. They are evidence that the
current implementation repeatedly rediscovered coupled product laws without keeping
one coherent implementation and acceptance authority.

### Model and target-compilation campaign size

A path-scoped history census over `engine/synthesis`,
`app/Laplace.Decomposers.Model`, `FoundryCommands.cs`, and `FoundryExport.cs` finds 199
non-merge commit objects across all refs and 152 distinct stable patch identities from
May 22 through August 23. The commit objects contain 682 path-scoped file-change events,
39,023 added lines, and 24,573 removed lines; those churn totals include equivalent
patches present in parallel histories and therefore measure activity rather than unique
final source.

The current local `main` contains 151 model-path commit objects and 150 patch
identities. Forty-eight commit objects exist outside `main`, but forty-six are
patch-equivalent to changes already in `main`. Two distinct model patches remain
outside it:

- `a31c3179579feb0fcb94f9500f2946c74a174ab8` changes token-by-token OV/FFN
  bilinear emission in `WeightTensorETL.cs` with 271 additions and 61 removals; and
- `3e060c0366fb7ba43e4ca00f08d1c3c1c14c303a` repairs target recipes that could
  silently omit declared attention and residual operators.

This establishes that model construction was a large repeated engineering campaign,
not a speculative future feature. Git retention prevents those two patches from being
byte-lost. Neither patch becomes clean implementation authority; both are defect and
acceptance evidence for the independent native ISA implementation.

## Claim-to-artifact reconciliation

The event spine now provides deterministic source occurrences, event lines, selected
messages, transcript-scoped tool actions, and exact or contextual artifact references.
The remaining claim-disposition pass must join those records with:

- session, parent event, timestamp, provider, model, runtime, and compaction state;
- user requirement or correction;
- agent claim;
- tool invocation and complete result;
- filesystem path and before/after hash when available;
- commit, tree, patch identity, branch, worktree, and remote reachability;
- build artifact, installed artifact, and loaded artifact hashes;
- test command, exact selected test count, result, and deliberate-defect result;
- database or service target identity without copying database contents;
- disposition: observed, corroborated, contradicted, superseded, missing, or unresolved.

A generated task notice is not a user requirement. An agent summary is not execution
evidence. A commit is not live behavior. A passing source-tree test is not proof of the
installed artifact. These distinctions are mandatory in the index schema.

The current spine does not yet classify every claim by material outcome, establish
unique-work loss for every destructive command, or complete the matched pre/post
service-behavior analysis. Those are evidence joins over the spine, not reasons to
weaken its completed byte and identity reconciliation.

The cross-corpus destructive-shell index is now a completed input to that join. Its
manifest SHA-256 is
`1a3f0b1942d9f1de3cc4216c99c0947a91cb6aaf95a0964140e3d9d9f0acfec3`.
Two independent runs produced identical manifests, identical 1,983,063-byte action
streams, and identical sets of 791 content-addressed command bodies. The pinned result
contains 808 transcript-scoped candidate tool actions and 1,002 destructive or
potentially overwriting invocations across Bash and PowerShell. Its quote-aware parser
leaves zero destructive candidate lines unresolved. Candidate action, tool-reported
completion, actual state mutation, discarded state, and unique-state loss remain five
separate findings; the index proves only the first two where its result records permit.

The cross-corpus file-history index is now a second completed input. It parses 14,750
snapshot records and 2,314 delta records from the verified content groups, resolves
backup names only within their session, and retains all source occurrences and record
lines. It recovered 6,542 distinct bodies totaling 79,144,951 bytes; 2,711 have no
corresponding object in the preserved Git object database. It also preserves 29,746
versioned references with no named body and 595 named references absent from the
supplied archive inventories. Neither class is guessed into a file body or a deletion
event. Two complete runs reproduced the manifest, all three index streams, and every
content object byte for byte.

The file-history result proves that substantial exact content survives outside Git.
The completed causal join then evaluates every classified invocation against its tool
result, 3,046 valid reflog events, same-session file history, all 79,637 corpus tool
actions in each candidate interval, Git-object membership, exact client-version
file-history semantics, and recovery objects. Ten invocations have raw reflog
corroboration; five of those changed an object ID. Two file-removal invocations survive
the complete present-to-absent causal contract and affect three exact bodies that were
outside Git. All three bodies were recovered, so unrecoverable unique-state loss is not
observed for them. Two independent builds of the final package were byte-identical.
Full findings are in `docs/audits/DESTRUCTIVE_STATE_CORRELATION_AUDIT.md`.

## Clean-room use of recovery evidence

Recovery evidence enters the new work in three forms:

1. direct inventor requirements become product laws;
2. externally observable defects become deliberate-defect acceptance cases;
3. measured hardware, dependency, and product facts become declared test inputs.

Current Laplace code, SQL, tests, scripts, file organization, and documentation prose do
not enter the new implementation. Verified upstream dependency sources are handled by
the separate dependency contract.

## Completion condition

Recovery is complete only when:

- the raw archive and every manifest verify;
- all 433 recovered non-main commits have an exact reachability and content
  disposition;
- the 113 rescued dangling commits and stash reconcile with the broader set;
- every direct user requirement in the preserved sessions maps to the new requirement
  system or is explicitly marked as a current-implementation operational request;
- every material agent work claim links to corroborating artifacts or is marked
  unproved;
- every material defect class has a new acceptance case and deliberate defect;
- the final current-directory archive preserves all remaining bytes before canonical
  repository activation.
