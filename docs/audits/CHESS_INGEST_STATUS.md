# Chess ingest status

Observed read-only: 2026-08-24

The separate [`CHESS_REFERENTIAL_IDENTITY_AUDIT.md`](CHESS_REFERENTIAL_IDENTITY_AUDIT.md)
captures a later repeatable-read snapshot of the live player query surface. It proves
that current normalized name-key lookup merges incompatible Fischer occurrence
clusters and establishes referential resolution as a product acceptance requirement.

## Cancelled run

The cancelled `ChessPgn` run is
`7829d738-e6ab-4a0d-b0fb-64c886296573`.

| Field | Journal value |
| --- | ---: |
| Started | 2026-08-24 10:20:11.749523 UTC |
| Ended | 2026-08-24 17:54:19.134354 UTC |
| Elapsed | 27,247 seconds — 7h 34m 7s |
| Input units complete | 7,231,925 |
| Input units declared | 10,581,098 |
| Completion at cancellation boundary | 68.3476% |
| Input units remaining at that boundary | 3,349,173 |
| Work units attempted | 187 |
| Work units applied | 187 |
| Work units failed | 1 |
| Entity counter | 38,972,786 |
| Physicality counter | 16,235,934 |
| Testimony counter | 151,856,113 |
| Evidence-persisted flag | true |

The three output counters are journal work counters. They are not current unique row
counts and cannot be added to current table counts because content deduplicates and
partial files persisted some output.

The work-unit counters also are not disjoint: attempted is 187 while applied plus
failed is 188. That is a journal reconciliation defect, not arithmetic to normalize
away.

## File boundary

The latest file journal state is:

| PGN range | Bytes | Latest state | Records recorded | Result |
| --- | ---: | --- | ---: | --- |
| 1970–1989 | 499,141,530 | cancelled | 0 | No complete marker in the journal. |
| 1990–1999 | 1,327,944,648 | cancelled | 0 | No complete marker in the journal. |
| 2000–2004 | 1,074,542,972 | cancelled | 0 | No complete marker in the journal. |
| 2005–2009 | 1,227,440,013 | cancelled | 0 | No complete marker in the journal. |
| 2010–2014 | 1,475,657,733 | cancelled | 0 | No complete marker in the journal. |
| 2015–2019 | 1,410,239,491 | failed | 976,275 | PostgreSQL reported an operating-system cancellation while reading relation blocks. |
| 2020–2024 | 838,803,213 | ok | 983,765 | Completed in the cancelled run before the run ended. |
| 2025 | 256,162,073 | skipped-complete | 0 on the check run | A resume run completed the remaining 26,379 records after 221,634 had been recorded before cancellation. |

The 2025 file therefore has 248,013 recorded records across its partial and resume
runs. The journal does not provide exact record positions for the other cancelled
files; a zero in those file rows means the terminal per-file write never arrived, not
that no records from the file reached durable state.

## Misleading source status

`ops.source_status(NULL)` currently reports `ChessPgn` as `ingested=true` with a last
run status of `ok`. That status reflects the later small run and the existence of
source state. It does not prove that all eight declared files completed.

The defensible current statement is:

- two of eight file identities have complete markers: 2020–2024 and 2025;
- one file is recorded failed: 2015–2019;
- five files are recorded cancelled;
- partial durable output exists from more than the two completed files;
- exact record-level disposition is unavailable for the five file rows whose terminal
  write never arrived;
- source-level `ingested=true` is not a completion contract.

## Product implication

The new ingest journal must derive source completion from the complete declared file
set and exact record dispositions. A later successful run over one file cannot replace
the status of an earlier multi-file run. Run, file, segment, batch, and record receipts
must form a reconciled hierarchy, and every terminal counter must partition its parent
count without overlap.
