# Model compilation and Unicode realization evidence

## Scope

This audit separates behavior already demonstrated in preserved execution transcripts
from capabilities that still require implementation and proof in the clean repository.
No historical implementation is an implementation source. The preserved behavior and
defects establish acceptance inputs and negative controls.

## Preserved sources

| Source | SHA-256 | Relevant record |
| --- | --- | --- |
| Windows Laplace session `6e7cdee8-dedf-45e0-9bac-8b2d103734aa` | `9d2332a2c97e1c8a82787fe98cd52c014df3c9f9773a18d50e117af4b1b4a1d8` | line 625, queued human transcript at `2026-07-01T13:23:12.898Z` |
| Same session artifact listing | same file hash | line 537, tool result at `2026-07-01T13:15:38.350Z`, event `e94509a7-9656-4eb8-b617-fb8baf09e5dd` |
| Windows Laplace session `3e877c12-7c68-4fea-a8d4-720c95808d9f` | `bfc056c0cc5b9752b59649cbb4d4919bff14d067958eb8aeb7ff9120a32f6eb8` | line 400, queued human transcript at `2026-07-05T18:06:35.215Z` |

The first and third records preserve earlier execution transcripts supplied during
later sessions. They are not relabeled as original tool events. The separate artifact
listing is a captured tool result and corroborates the existence, size, and timestamp
of the generated file.

## Demonstrated direct target compilation

The preserved transcript records a release build followed by a substrate synthesis
program with a 2,048-dimensional target, two layers, sixteen query heads, sixteen key
and value heads, and a 2,048-dimensional feed-forward target. It reports:

- 678 substrate word entities, 256 byte-floor entries, and three special entries, for
  a 937-entry native vocabulary;
- an identity embedding matrix;
- an attestation log-odds output matrix;
- matrix multiplication serving as the attestation lookup;
- a 127 MB reported GGUF emitted in approximately 0.1 seconds; and
- deterministic external-runtime execution at temperature zero.

The captured artifact listing independently reports `kfix.gguf` at 143,933,280 bytes
with a June 16 timestamp. The apparent size difference is compatible with decimal or
binary display and reporting conventions; the preserved evidence does not contain the
artifact bytes needed to calculate its digest.

The external runtime outputs include clear substrate-correlated signal for prompts such
as `king`, `gold`, `queen`, and `cat`. Other prompts exhibit irrelevant terms, cycles,
or weak continuation. Therefore the exact demonstrated result is:

```text
witnessed substrate relations
  -> numeric embedding and output operators
  -> GGUF serialization
  -> independent conventional runtime
  -> relation-correlated and defect-revealing output
```

This is an existence proof for direct substrate-to-model compilation. It is not
evidence that the useful semantics in that run resided in query, key, value, or output
attention projections. The reported mechanism places the useful lookup in the identity
embedding and attestation output matrix. Relation-kernel fitting into attention and
other architecture tensors remains a required extension of an already demonstrated
compiler path.

## Demonstrated Japanese composition and realization

The preserved Japanese transcript reports the exact constituent rows:

```text
1  事
2  件
3  の
4  こ
5  と
6  な
7  ん
8  だ
9  け
10 ど
11 .
12 .
13 .
14 space occurrence
```

The same query renders `事件のことなんだけど...`. This demonstrates ordered CJK
composition and realization through tier-1 constituents without an English word-token
dependency. The trailing recorded space is occurrence or container state and is not
part of the displayed sentence realization.

The record establishes the observed behavior. It does not establish chronology against
every English experiment, nor does it prove the clean implementation. The new product
must independently reproduce the behavior through its public native, PostgreSQL, SQL,
and C# contracts.

## Required proof in the clean product

1. Reproduce the direct identity-embedding and attestation-output operator as a named
   target recipe with complete value provenance.
2. Run the exact deterministic prompt set and preserve every result, including cycles
   and irrelevant output, as a behavioral baseline.
3. Show that removal, permutation, transposition, or source contamination changes the
   expected mechanism and is detected by a deliberate-defect test.
4. Implement typed substrate relation kernels and independently prove their fitted
   query/key and value/output operators on withheld observations.
5. Reconcile every target tensor from selection through external-runtime execution.
6. Reproduce the Japanese constituent order and exact realization through every public
   orchestrator over the one native implementation.
7. Include both results in complete conversation and model-export acceptance; neither
   a parseable target file nor a rendered stored string is sufficient evidence.
