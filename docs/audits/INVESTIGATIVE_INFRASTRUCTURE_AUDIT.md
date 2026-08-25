# Existing investigative infrastructure audit

Date: 2026-08-24 UTC

## Answer

Existing Laplace infrastructure can materially assist the investigation, but each
instrument has a narrower proven boundary than its name suggests. It is used as a
corroborating parser or query surface, never as the authority that judges its own
correctness. No existing decomposer has been pointed at the current database during
this audit.

## Repository decomposer

The observed repository decomposer enumerates files in one current working tree,
filters build/vendor paths, selects files that have a registered grammar, reads their
current bytes, and relates them to one repository root. It does not parse Git commits,
trees, refs, reflogs, patch identities, branches, worktrees, rename history, or deleted
content. It explicitly rejects a root containing nested repositories rather than
representing each repository independently.

That makes it useful later for comparing a single materialized source tree with a
canonical content inventory. It cannot answer what was deleted, which branch retained
an implementation, whether two commits have the same patch, or which unreachable Git
objects survive. The new recovery inventory supplies those Git-specific facts directly
from Git objects and verifies them in a blank object store.

No implemented Git-history decomposer was found in the source search. Historical prose
describing one is not executable evidence.

## Agent-session decomposer

Recovered commit `34195736cf2ce2bcd79aad491637c89b346a9122` contains a shared agent-session model,
emitter, decomposer, tests, and adapters for Claude Code, Codex, Gemini, Antigravity,
Copilot, Cursor, and generic role-shaped JSON. The attached agent-trace worktree now
contains a larger adapter registry, but that worktree is attached to an unrelated
rating-correction branch and is not equivalent to the named task or to an accepted
product release.

The design can parse provider envelopes into sessions, messages, tool calls, tool
results, metadata, and relationships. That is relevant to this audit. It is not safe to
invoke against the current database merely because an internal `DryRun` flag exists:
initialization performs provider bootstrap writes before ordinary record composition,
and the exposed command route has not been proven read-only end to end.

The clean recovery tools therefore parse the preserved JSONL directly and compare
their counts with the recovered adapter model. This retains exact bytes, source line,
tool identity, result identity, archive hash, and provider-specific fields while
avoiding database mutation. A later comparison can run the recovered parser against
the same archived fixtures in an isolated test database and require exact event parity.

## Deterministic event-to-artifact spine

The clean indexer now parses 1,308 distinct log contents while preserving 1,309 source
occurrences and verifying every source against the cross-corpus archive manifest. It
emits 460,928 unique-content events, 54,561 selected text messages, 79,637
transcript-scoped tool actions, and 31,043 artifact references. The published manifest
SHA-256 is `3c3002280901df9c17aafa323579aee7ce71f6fc813138d5dc8822752d674f82`.

Tool IDs are not treated as globally unique. The action key includes transcript
content identity, repeated copies of the same action record remain dependent records,
and unpaired or conflicting states remain explicit. Exact and abbreviated Git
references are resolved against all 2,960 commits while the 433 recovered commits are
reported as a separate property. Generated user notices and tool-result envelopes are
excluded from direct-human-message counts.

All four emitted data files were independently rehashed against the manifest. This
establishes a reproducible join substrate for session, message, tool, path, digest, and
commit evidence. It does not decide whether an action caused unique-work loss or
whether a service-state transition caused later engineering outcomes; those require
the next semantic and causal joins.

The cross-corpus file-history pass adds the exact pre-state source needed by that next
join. It indexes 39,278 session/path/version-scoped references from 14,750 snapshots
and 2,314 deltas, resolving a backup name only inside its session. It recovered 6,542
distinct bodies totaling 79,144,951 bytes; 2,711 bodies are absent from Git's object
database. The manifest SHA-256 is
`1f5b65cdd0a88b10e30ce5f2d91ad71340adf3d095e82666a3166eb3e17bca69`.
Two complete executions produced byte-identical manifests, streams, and body objects.
Null body names, named bodies absent from the inventories, body absence from Git, and
proven action-caused loss remain separate dispositions.

The destructive-state correlator now implements the next bounded join. It scans all
79,637 corpus tool actions between each candidate before and after observation,
requires exact target resolution, rejects intervals with a same-target or unresolved
mutator, binds null-history semantics to the exact historical client version, checks
Git-object membership, and records recovery independently from mutation. The pinned
package contains 1,002 invocation dispositions. Only two file-removal invocations
survive the complete causal contract, covering three Git-unpublished bodies; all three
are recovered. The manifest SHA-256 is
`2d44c0405a687b780bb488a5f320a3fba6ab8e6861605d701e822d5e14ebc5c2`.

## Current Laplace query surfaces

The running database exposes typed operational, conversation, evidence, taxonomy,
realization, chess, and diagnostic functions through PostgreSQL and MCP. Historical
corpora contain hundreds of raw SQL MCP calls and calls to chat, operation, facts,
inference, recall, taxonomy, translation, walk, health, and source-status surfaces.

These are useful in two ways:

1. Read-only catalog and diagnostic calls reveal what the running product can expose,
   its loaded extensions, active work, relation sizes, selected plans, cumulative
   timings, and index use.
2. Preserved call/result pairs allow deterministic replay tests and show where the
   same input produced errors, timeouts, contradictions, or different output.

The first bounded read-only observation has already identified minute-scale bulk
operations, temporary-block spills, repeated temporary-object lifecycle, large nearly
unused index families, and the absence of Laplace-owned four-component operator
classes. Its exact query and result hashes are recorded in the SQL execution-surface
audit.

## Evidence-comparison flow

| Question | Primary observation | Independent comparison |
| --- | --- | --- |
| What event was present? | Raw archived JSONL line and content hash | Agent-session parser output |
| What command was proposed? | Exact tool input payload | Shell and database-client reconstruction |
| What did it return? | Tool-result payload and hash | Parsed errors, timings, counts, and replay |
| What source survived? | Git blob/tree/commit identity | Filesystem history and exact Write/Edit payload |
| What is loaded now? | PostgreSQL catalog and process state | Installed-file hash and ELF dependency closure |
| What does Laplace claim? | Typed read-only query or MCP response | Raw substrate tables, receipts, logs, plans, and behavioral tests |
| Does the feature work? | Real public interface execution | Expected exact result and deliberate defect |
| Which event cites which artifact? | Deterministic evidence-spine event and reference records | Complete Git commit set, archive occurrence identity, and exact digest resolution |
| Which destructive syntax executed? | Content-verified Bash and PowerShell action index | Executable-position parsing, inert-content negative controls, result locators, exact command objects, and duplicate-run equality |
| What exact non-Git file state survives? | Session-scoped cross-corpus file-history index | Archive-inventory verification, content-addressed bodies, Git-object membership, null references, conflicts, and duplicate-run equality |
| Did an action discard unique state? | Pinned per-action causal disposition | Pre-action bytes, post-action absence, complete interval-mutator scan, Git/reflog membership, client-version history semantics, recovery objects, and explicit unresolved cases |

## Required implementation for the new product

The useful concepts become one generic agent/repository evidence framework in the new
engine:

- provider adapters preserve every recognized and unrecognized field as witnessed
  structure;
- sessions, branches, compactions, messages, tool calls, results, files, repositories,
  commits, trees, refs, patches, builds, tests, database observations, and claims have
  typed identities and relations;
- content deduplication never collapses distinct occurrences, sources, times, roles, or
  testimony;
- repository ingestion includes working trees and complete Git topology rather than
  treating the current directory as history;
- archive parsing, provider parsing, and Laplace-native ingestion produce the same
  event identities and exact occurrence counts;
- read-only analysis is enforced before connection startup and is verified with a
  transaction-level negative test;
- bulk parsing and deposition meet the complete ingest performance boundary.

This is new implementation work derived from the invention and direct requirements.
No existing parser, schema, naming layout, or emitter is copied into the clean product.
