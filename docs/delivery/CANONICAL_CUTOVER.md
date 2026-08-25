# Canonical repository cutover receipt

Date: 2026-08-25 UTC

## Disposition

The clean `SaltyPatron/Laplace-Refactor` checkout is active at the canonical product
path:

`/home/ahart/Projects/Laplace`

The complete former checkout was moved, without a cross-filesystem copy, to:

`/home/ahart/Projects/Laplace-archive-2026-08-24/historical-worktree`

The live database was not copied into either repository. It remains separately
addressed evidence and runtime state.

## Historical-tree preservation

Before the move, a two-pass stable inventory read every filesystem entry and every
regular-file body. It recorded type, mode, owner, group, size, nanosecond timestamps,
device, inode, link count, allocated blocks, extended attributes, symbolic-link
targets, and SHA-256 for every regular file.

| Fact | Result |
| --- | ---: |
| Entries | 115,827 |
| Directories | 19,709 |
| Regular files | 96,047 |
| Symbolic links | 71 |
| Regular-file bytes | 12,419,034,844 |
| Read errors | 0 |
| Stability passes | 2 |

The inventory artifacts are retained outside the product repository:

| Artifact | SHA-256 |
| --- | --- |
| `preservation/historical-worktree-final.jsonl` | `6b49e2d94791703e47fd2f9771f51dd6f6eb14a615f9f09456ba7bb33f64d322` |
| `preservation/historical-worktree-final.summary.json` | `79645ce30b6c27caf8831a126b9924ac530cc903828ccd39c2fc7b236a75d421` |

The manifest digest was independently recalculated and matched the digest bound in
the summary. All 31 SHA-256-bound artifacts named by
`preservation/RECOVERY_RECEIPT.md` were also rehashed immediately before cutover and
matched their recorded values.

## Admission preflight

Immediately before the move:

- no process working directory, root, or open file descriptor resolved into the
  historical tree;
- no chess, PGN, or ingest process was observed;
- the archive target did not exist;
- historical, staging, and archive parents were on the same filesystem device;
- the clean worktree was clean;
- clean `HEAD` equaled `origin/main` at
  `0b7e43d27f7b9f91c51d5e0d8a7c5ca9f2d31a4c`; and
- the clean remote was exactly `https://github.com/SaltyPatron/Laplace-Refactor.git`.

The cutover moved the historical directory first and contained an explicit rollback
of that move if clean-path activation failed. Activation succeeded; rollback was not
needed.

## Post-activation proof

From `/home/ahart/Projects/Laplace` after activation:

- `main` was clean and equal to `origin/main`;
- the origin remote was `SaltyPatron/Laplace-Refactor`;
- the requirement graph verified 51 product requirements, 24 alignment domains, 316
  direct requirements, 225 evidence targets, 130 scenarios, 91 registered tests, 11
  operational stages, and 51 operationally mapped product requirements;
- all 10 requirement-verifier mutation tests passed;
- a fresh out-of-tree PostgreSQL-enabled Release build used the verified BLAKE3 and
  GoogleTest source roots plus PostgreSQL 18 `pg_config`;
- all 91 registered implementation, deliberate-defect, PostgreSQL/SPI, packaging,
  dependency, recovery, and traceability tests passed; and
- the PostgreSQL-profile registry matched all 91 executed tests.

The self-hosted runner remains independent at
`/var/lib/agents/laplace-runner/actions-runner-refactor`, and product build output
remains outside Git under `/opt/laplace` or bounded temporary/runner work roots.

## Authority boundary

The historical tree is evidence and negative-control material. It is not a build,
include, package, test, schema, or source authority for the clean product. The clean
repository's constitution, contracts, requirements, implementation, and executable
acceptance now define Laplace.
