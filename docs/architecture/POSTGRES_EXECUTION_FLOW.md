# PostgreSQL execution flow

Status: required architecture; implementation and complete acceptance remain open.

## Ownership

```mermaid
flowchart LR
    SQL[SQL orchestrator]
    CS[C# orchestrator]
    ISA[Generated typed ISA binding]
    PG[PostgreSQL extension entry point]
    SPI[Prepared SPI plans]
    Native[Native vector kernels]
    Cache[Typed perfcache mappings]
    Store[(Canonical PostgreSQL state)]
    Receipt[Execution receipt]

    SQL --> ISA
    CS --> ISA
    ISA --> PG
    PG --> Native
    PG --> SPI
    Native <--> Cache
    SPI <--> Store
    Native <--> Store
    PG --> Receipt
    Receipt --> SQL
    Receipt --> CS
```

SQL and C# submit and receive typed programs. Neither contains identity, geometry,
relation, evidence, cognition, conversation, or model semantics. Generated bindings
derive from the ISA contract, so an instruction cannot exist for only one orchestrator.

## One vector execution

```mermaid
sequenceDiagram
    participant O as SQL or C# orchestrator
    participant E as PostgreSQL extension
    participant V as ISA validator
    participant K as Native vector engine
    participant P as Prepared SPI layer
    participant D as Canonical database state
    participant R as Receipt builder

    O->>E: typed instruction vector + execution envelope
    E->>V: validate version, types, bounds, capabilities, and envelope
    V-->>E: validated vector or exact typed rejection
    E->>K: execute validated vector
    K->>P: bounded set requests
    P->>D: prepared plans / bulk transfer
    D-->>P: ordered typed state
    P-->>K: vector results
    K->>R: results + provenance + measurements
    R-->>E: content-addressed receipt
    E-->>O: typed outcomes + receipt identity
```

Validation precedes mutation. A rejected vector produces an exact rejection outcome
without partial state. One-element execution is the same sequence with vector length
one.

## Batch state machine

```mermaid
stateDiagram-v2
    [*] --> Received
    Received --> Rejected: contract or envelope invalid
    Received --> Validated: complete vector valid
    Validated --> Resolved: existing canonical state resolved in bulk
    Resolved --> Calculated: native identity, geometry, and ISA kernels complete
    Calculated --> Deposited: missing canonical state and witnessed relations written in bulk
    Deposited --> Adjudicated: evidence and consensus updated in bulk
    Adjudicated --> Committed: transaction and durable progress commit
    Committed --> Receipted: counts, timings, artifacts, and provenance reconcile
    Receipted --> [*]
    Rejected --> [*]
```

No state transition permits a client round trip per record. Resource-derived vector
partitioning can create several bounded internal chunks, but each chunk preserves the
same ordered semantics and the receipt accounts for all chunks.

## Troubleshooting and audit flow

```mermaid
flowchart TD
    Raw[Archived logs, Git objects, tool payloads, and live read-only observations]
    Hash[Exact hashes and archive inventories]
    Dedup[Content deduplication and session variants]
    Parse[Provider and database activity decomposition]
    Query[Read-only Laplace diagnostic surfaces]
    Compare[Independent comparison]
    Finding[Observed, corroborated, contradicted, missing, or unresolved finding]
    Test[Acceptance scenario and deliberate defect]

    Raw --> Hash --> Dedup --> Parse
    Parse --> Compare
    Query --> Compare
    Compare --> Finding --> Test
```

Laplace may answer questions about its witnessed state, relations, ingest receipts, and
execution history. Those answers remain one evidence source. Raw event payloads, Git
identity, PostgreSQL counters, loaded binaries, and direct implementation tests provide
independent comparison so the system cannot establish correctness merely by describing
itself as correct.
