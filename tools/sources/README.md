# Source acquisition and admission

`laplace-source-admit` acquires one locked source profile, verifies its exact
artifacts, calculates the source graph through the installed native engine, calls
the canonical PostgreSQL admission operation, commits, and reads the persisted
profile, canonical root, and world admission back. The installed entry point is
`bin/laplace-source-admit`. Its native C launcher selects the packaged C# transport
and pinned .NET runtime; it does not require Python, a development checkout, or a
machine-wide .NET installation. The package carries the declarations and contracts.
The graph and admission entry points share native rule validation. C# owns file
acquisition, descriptor marshaling, SQL transport, and durable command receipts;
it does not implement decomposition, semantic inference, or canonical identities.

Required arguments are `--profile`, `--source-root`, `--context`,
`--maximum-input-bytes`, `--timeout-seconds`, and `--receipt`. The selected cluster
contract supplies the default package, Unix socket, port, database, and role.
Connection overrides are `--host`, `--port`, `--database`, and `--user`.

The context is a JSON object matching the public `execution_context` transport:

- `epochs`: an object containing `source`, `identity`, `geometry`, `evidence`,
  `firmware`, `dependency`, `database`, `perfcache`, `numeric`, and `package`, each
  encoded as a 32-byte hexadecimal fingerprint;
- `authority_fingerprint`: the caller's 32-byte authority fingerprint;
- `memory_bytes`, `cpu_slots`, `io_slots`, and `epoch_mask`;
- `framework_major`, `framework_minor`, and `flags`.

These values come from the selected execution authority and epoch state. The
command does not generate fixture epochs or infer a grant from available RAM.
The PostgreSQL operation validates the context again before admission.

`--transport copy` is the default. It streams exact artifact bytes into binary
COPY without hex expansion, then constructs the existing typed artifact array.
The complete array must fit PostgreSQL's varlena transport limit.

`--transport server-files` carries small typed descriptors for server-visible
files and supports source profiles larger than that array limit. The source tree
defaults to `--source-root` for Unix socket connections; a remote connection needs
`--server-source-root`. The server reader requires `pg_read_server_files`, validates
regular-file extents, creates private snapshots through PostgreSQL's temporary-file
owner, and maps those snapshots into the same native admission implementation.
Source names, bytes, grammar declarations, reference rules, and mapping rules stay
identical across transports. The POSIX snapshot provider is required for this mode.

Both transports use the same SQL function and C entry point. File mappings are
released with the query memory context, and PostgreSQL owns snapshot cleanup and
temporary disk accounting. Total input is bounded before acquisition and again by
the server's supplied resource grant; native plan and deposition limits still
apply. A file transport does not imply that an arbitrary full AST fits memory.

The command compares the deposited graph, selected boundary, and declared byte,
file, record, field, claim, and mapping denominators before COMMIT. It emits
`admitted_and_read_back` only after committed readback succeeds. A failed or
interrupted database call records `outcome_unknown`, since a disconnect can occur
after COMMIT. The durable command receipt retains output and errors for recovery.
It is separate from the native semantic receipts returned by PostgreSQL.

Acquisition receipts certify exact files and archive membership. A source-graph
calculation certifies its native identity. Neither alone certifies world admission,
source qualification, inference, realization, or complete product activation.

`laplace-source-admit activate-unicode --request FILE --receipt FILE` transports
an initial Unicode activation through the packaged `sql/unicode-activate.sql`.
The request contains an absolute `native_identity_request` path, the supplied `context`, `activation_epoch_id`,
`activation_epoch_fingerprint`, and absolute server paths `source_root`,
`spool_directory`, `tier0_path`, and `reverse_path`. Activation identity values
must come from the native activation identity provider for the selected request.
The command invokes that packaged provider and compares both activation identities,
the context authority, and every context epoch with its output before admission.
The command uses the selected cluster's administrator connection. Its source and
output directories must be accessible to that PostgreSQL instance.

The SQL transaction calls native `unicode_root_build_and_activate`, verifies
the declared root, deposition receipts, tier-zero and reverse artifacts, and
active generation, then commits. C# passes parameters and records the result;
it contains no activation transaction SQL. The command receipt phase
`native_committed_and_read_back` records this transaction. The separate product
activation receipt still requires restart and cold direct/reverse readback.
