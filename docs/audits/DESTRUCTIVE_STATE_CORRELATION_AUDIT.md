# Destructive-state correlation audit

Date: 2026-08-25 UTC

## Finding

The cross-corpus causal pass observed two file-removal invocations that account for
three present-to-absent path transitions. Each path had an exact pre-action body that
was absent from the Git object database, a non-error tool result, a later same-session
missing-path observation, and no competing corpus-visible mutator between the before
and after observations.

All three exact bodies were recovered from preserved file history. The supported
disposition is therefore:

```text
observed discarded Git-unpublished worktree state, recovered
```

The pass observed zero unrecoverable unique-state losses. It does not infer that the
other classified invocations mutated state merely because their tools reported
success.

## Reproducible package

The published package is:

`/home/ahart/Projects/Laplace-archive-2026-08-24/preservation/claude-destructive-state-correlation-v1`

Its manifest SHA-256 is
`2d44c0405a687b780bb488a5f320a3fba6ab8e6861605d701e822d5e14ebc5c2`.
Two clean executions over the same pinned inputs produced directory trees that
compare byte for byte, including 1,002 invocation dispositions, 3,046 parsed reflog
events, two executable-semantics records, and 701 content-addressed tool-result
objects.

The package binds these input manifests:

| Input | Manifest SHA-256 |
|---|---|
| Deterministic event spine | `3c3002280901df9c17aafa323579aee7ce71f6fc813138d5dc8822752d674f82` |
| Destructive command index | `1a3f0b1942d9f1de3cc4216c99c0947a91cb6aaf95a0964140e3d9d9f0acfec3` |
| Cross-corpus file history | `1f5b65cdd0a88b10e30ce5f2d91ad71340adf3d095e82666a3166eb3e17bca69` |

It also binds the exact client executables used to interpret null file-history states:

| Client | Platform | Executable SHA-256 |
|---|---|---|
| 2.1.152 | linux-x64 | `5155bdca27f754aba0d2fe2f80336f5fd4793224561c234a723f0ccef654a8e8` |
| 2.1.206 | win32-x64 | `d5072b25b9a20bffb24625d36129a05ed2be4d2eb7e35625aad6aa35596892c2` |

Both exact executables contain the code paths that serialize a missing tracked path
with a null backup reference and interpret that null state as path absence during
rewind. The historical sessions report the matching client versions. Executable
semantics and session observations remain separately receipted evidence.

## Disposition ladder

The correlator keeps these findings separate:

```text
classified executable syntax
tool transport outcome
independently corroborated execution
observed state mutation
discarded prior worktree state
recoverability
unrecoverable unique-state loss
```

The corpus narrows as follows:

| Stage | Count |
|---|---:|
| Transcript-scoped candidate tool actions | 808 |
| Classified destructive or potentially overwriting invocations | 1,002 |
| Targets with a same-session history match | 157 |
| Targets with a prior body absent from the Git object database | 34 |
| Invocations corroborated by raw reflog events | 10 |
| Reflog-correlated invocations with an object-ID change | 5 |
| File-removal invocations corroborated by a present-to-absent transition | 2 |
| Discarded Git-unpublished target bodies observed | 3 |
| Recovered target bodies | 3 |
| Unrecoverable unique-state loss observed | 0 |

Of the 1,002 invocations, 884 have a tool-reported success without independent state
corroboration, 106 have a failed or unknown transport disposition, ten are
corroborated by reflog evidence, and two are corroborated by present-to-absent file
history. The ten reflog cases and two filesystem cases account for the twelve
invocations with an independently observed state mutation; the three-target count is
specific to the two filesystem invocations.

## Byte-level findings

| Action time | Path | Before observation | After observation | Body SHA-256 | Bytes |
|---|---|---|---|---|---:|
| 2026-05-27 20:53:36.582 UTC | `app/Laplace.Decomposers.Abstractions/UniversalT0Pipeline.cs` | 20:53:02.677 | 20:54:34.547 | `f07a962123891c10af40cb9ab344b4416b099c24c27529e93c729c2d3d900c6a` | 4,788 |
| 2026-07-10 05:24:12.652 UTC | `app/Laplace.Decomposers/Model/ModelFactorBlob.cs` | 05:23:42.302 | 05:24:49.136 | `74bf65b268b763ab7e6733f56813085b9dd6bf5a3b53b46ceaea6b084af85992` | 7,337 |
| 2026-07-10 05:24:12.652 UTC | `app/Laplace.Decomposers/Model/ModelFactorCodec.cs` | 05:23:42.302 | 05:24:49.136 | `e3e36241321505247d337c3818aab46133fa878dc0456c943f1da24e7cebab9c` | 2,489 |

The first interval contains eight other corpus tool calls, but no same-target mutator
and no unresolved mutator. The two model-factor intervals contain no intervening tool
calls. Each action has a result envelope without an error, and each following history
state is a null-reference missing-path observation under the matching client-version
semantics.

The exact action and result locators, before and after state-version IDs, source lines,
session IDs, candidate-mutator scan, body object identities, and recovery dispositions
are in `invocation-dispositions.jsonl`.

## Product and historical disposition

The three recovered bodies are preserved continuity evidence, not clean-product
source. Their architectural dispositions are recorded in
`TRAJECTORY_PAYLOAD_AND_INDEX_AUDIT.md`. In particular, the factor codec's use of a
128-bit host carrier for exact float32 patterns is a typed trajectory-ABI design choice,
not an entity-identity defect.

This causal avenue now meets the historical stop rule. It recovered the exact unique
material, established its mutation and recovery disposition, corrected its product
classification, and produced no additional unresolved product defect class. Further
command-by-command excavation is not required for clean Laplace implementation. The
separate provider-interaction study can use this package without holding the product
track open.
