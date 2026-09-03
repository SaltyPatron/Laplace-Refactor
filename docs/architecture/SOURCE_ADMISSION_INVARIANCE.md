# Source admission: physical-plan semantic invariance

Status: **P0 clean-product admission law**  
Executable owner: #171  
Profile/template owners: #112, #115  
World/substrate owners: #53, #7, #15

## One source, many legal physical plans, one admitted world

A source profile binds authority, release, artifact graph, provider/grammar/codec generation, recipes, field-role mappings, evidence rules, reconstruction law and completion boundary.

The execution engine may choose different legal physical plans for that exact profile based on topology, memory, CPU, storage, parser capabilities and measured resource state. Those choices may change resource receipts. They may not change admitted semantics.

```text
artifact/file occurrence
!= transport read or parser-feed chunk
!= parser/codec source object, record, CST or AST node
!= canonical content/composition/physicality/occurrence/testimony
!= presence/probe/deposit/merge batch
```

No implementation convenience promotes one boundary into another.

## Provider versus machine ownership

Providers recover exact mechanism and source structure:

- artifact/container decoding;
- grammar/codec/standards parsing;
- fields, nodes, spans, ordinals, errors, recoveries and source-object boundaries;
- source-specific academic distinctions needed by the profile.

The common machine owns:

- canonical identity and Merkle/universal-AST composition;
- physicality and trajectory laws;
- typed occurrence/reference/testimony state classes;
- whole-working-set presence/deduplication;
- resource planning and scheduling;
- deposition/merge semantics;
- receipts, replay and completion meaning.

A parser record is therefore an observed source object. The accepted recipe must state whether its contents or structure become canonical content, occurrence, reference, provenance, testimony, deterministic calculation, packaging/reconstruction state or an unresolved obligation.

## Whole working set versus streaming

Neither `load everything` nor `stream everything` is a semantic law.

If a complete source object, its AST/Merkle DAG and required scratch fit the admitted memory/resource envelope, retaining the complete working set can be the best physical plan. If a source is large, continuous, codec-driven, or otherwise benefits from incremental processing, streaming can be the best plan.

When both are legal, they owe the same semantic fingerprint.

This is especially important for Laplace because content-addressed reuse, trunk/leaf presence, typed indexes and sparse execution are intended to avoid brute-force work. Arbitrary chunking must not destroy the higher-order structure that makes those mechanisms possible.

## Required equivalence matrix

For one exact fixture/profile/provider generation, vary applicable physical dimensions:

```text
transport reads      one-byte / adversarial / ordinary / whole payload
parser feed chunks   inside multibyte scalars, quotes, delimiters, nested constructs
record batches       one / small / ordinary / whole admitted record set
presence probes      one / small / ordinary / whole admitted root set
workers              one / physical-core points / selected SMT points
scheduling           deterministic legal permutations
persistence          deposit / merge batch sizes and partition ownership
cache state          cold / warm where cache is only acceleration
```

The normalized semantic receipt must remain identical for:

- canonical content ids;
- universal-AST/Merkle composition;
- physicality trajectories, constituents, ordinals, gaps and recurrence;
- source occurrence identity and multiplicity;
- typed external references;
- testimony/evidence identity, dependence roots and observation cardinality;
- source/profile/provider provenance;
- exact reconstruction or the declared loss result.

Physical receipts are expected to differ:

- elapsed time and CPU/core time;
- RSS/arena/memory residency;
- storage/network bytes;
- PostgreSQL/native workers and crossings;
- cache hits/misses;
- temporary staging and merge shape;
- energy and physical provider selection.

## Why this is P0

A performance optimization is invalid if changing a read size, worker count or batch size changes what Laplace believes exists.

This law therefore precedes throughput tuning, parallel apply, commercial resource calibration and a final foundational seed. It is the proof that physical-plan optimization can be aggressive without becoming a semantic rewrite.

## Historical counterexample

The old iteration's OpenSubtitles lane (`SaltyPatron/Laplace#1180`) demonstrated the forbidden shape: an arbitrary 512-pair ingestion batch participated in durable content construction, so rebatching could change which content objects existed.

That is retained as a deliberate negative control. The clean implementation does not copy that code; it must reject the behavior through #171.

## Profile activation requirement

A source profile cannot be activated merely because one parser run succeeds. Qualification must include:

1. exact artifact/provider/recipe identity;
2. field/AST-role disposition coverage;
3. reconstruction or declared loss;
4. malformed/ambiguous/unsupported fixtures;
5. provider substitution where applicable;
6. physical-plan semantic equivalence;
7. idempotent cancellation/restart/replay;
8. coverage/amplification/resource receipts.

The machine-readable source-profile contract is `contracts/source-profile-model.json`.

## Non-success

Reject:

- transport buffer or parser callback boundaries becoming durable content;
- a source-format row becoming a canonical composition merely because it is convenient;
- batch/segment/worker ids entering canonical identity;
- worker or scheduling order changing evidence cardinality;
- persistence batch shape changing occurrence or testimony identity;
- a cache/perfcache miss changing exact logical semantics;
- mandatory tiny streaming when the admitted whole working set is cheaper and safe;
- mandatory whole-file residency when the provider/resource plan requires a lawful incremental path;
- source-named code owning a separate content/dedup/persistence engine.

## Acceptance

#171 closes only when a reusable conformance harness demonstrates this law across materially different source families and deliberate mutants, and source-profile activation refuses a profile whose physical-plan equivalence boundary has not been proven.
