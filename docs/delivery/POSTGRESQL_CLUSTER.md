# Isolated PostgreSQL product cluster

The first clean PostgreSQL product cluster is admitted entirely from positive
package, instance, security, resource, and live collision contracts. It never
identifies another installation by a product-specific blacklist. The
machine-readable authority is
[`contracts/postgresql-cluster.json`](../../contracts/postgresql-cluster.json); this
document explains its lifecycle.

## Boundary

The database is a set-oriented orchestration and durable-state surface over the
same native Laplace engine used by direct consumers. SQL does not reimplement
identity, trajectory, ISA, receipt, or other native semantics. One SQL batch must
remain one native batch across SPI.

The candidate instance is intentionally disjoint:

| Item | Candidate value |
| --- | --- |
| PostgreSQL | 18.6 from the verified immutable product package |
| Service | `laplace-refactor-postgresql.service` |
| Operating-system identity | `laplace-runner:laplace-runner` |
| Database / roles | `laplace_refactor`; `laplace_admin`; `laplace_app` |
| Port / socket | `55433`; `/run/laplace-refactor-postgresql` |
| Data / WAL / temporary | `/opt/laplace/pgdata/refactor/data`; `/var/lib/pgwal/refactor`; `/pgtemp/refactor` |
| Configuration | `/etc/laplace/instances/refactor` |
| Logs / receipts | `/var/log/laplace/postgresql/refactor`; `/opt/laplace/receipts/postgresql/refactor` |
| Immutable package | `/opt/laplace/releases/<package-id>` |
| Committed package link | `/opt/laplace/current` |

Before staging and again immediately before real activation, a generic collision
probe proves that the declared service, port, socket, state paths, and matching
process targets are unoccupied. The candidate listens on Unix sockets only. Peer
maps permit the human administrator to become `laplace_admin` and the runner to
become only `laplace_app`; all unmatched local and all TCP connections are
rejected. `trust` is forbidden.

## Package and activation state machine

`tools/postgresql/clusterctl.py` implements a fail-closed product lifecycle:

1. **Install package** verifies every source manifest entry, digest, mode,
   capability, activation gate, internal symlink, and loaded-object declaration,
   copies into a temporary content-addressed release tree, re-verifies that tree,
   and atomically places it only when the immutable destination is absent. Exact
   replay returns the same receipt; an existing divergent release is never
   overwritten.
2. **Plan** validates the contract, native topology/root-grant/partition and
   processor-allocation receipts, a storage observation, a generic collision
   observation, and a content-addressed package manifest. The package ID is SHA-256 over the
   canonical manifest payload excluding its derived ID and release-root fields.
   Planning renders configuration, bootstrap SQL,
   service definition, resource settings, and exact commands. No package bytes
   means a useful dry-run plan whose activation remains blocked.
3. **Apply/stage** requires every manifest file, digest, mode, required capability, and
   loaded-object declaration to verify. It stages only previously absent,
   manifest-owned files and creates dedicated state directories. The low-level
   command never starts PostgreSQL or invokes systemd.
4. **Activate product** qualifies the content-addressed package as `root:root`,
   repeats the live collision inspection with system authority, generates and stages
   the exact plan, initializes the checksummed cluster, reloads systemd, starts the
   immutable-package postmaster, and bootstraps the roles, database, extensions, and
   application effect boundary. A dedicated administrator backend loads both product
   extensions while the controller identifies its PID through `pg_stat_activity` and
   inspects `/proc/<pid>/exe` plus `/proc/<pid>/maps`. The postmaster, extension,
   statistics extension, and native engine paths and bytes must equal the package
   manifest.
5. **Restart proof and commit** stops and starts the candidate, requires a different
   postmaster PID with the same positive PostgreSQL system identifier, and repeats the
   complete loaded-object and generated-configuration observation. Only after both
   observations pass is `/opt/laplace/current` switched atomically. Failure before
   that commit stops the candidate when possible, leaves the prior pointer untouched,
   preserves database state, and writes a typed failure receipt.
6. **Commit (low-level)** requires a separately acquired loaded-state observation proving the
   service, system identifier, cluster paths, generated configuration hashes, and
   exact executable/shared-object hashes. Only then is `/opt/laplace/current`
   switched atomically.
7. **Remove** first requires an independent observation that the candidate service
   is inactive and no candidate postmaster remains. It then restores the prior
   active link and removes only unchanged generated files named in the receipt.
   Database, WAL, temporary, log, and receipt state is preserved. A changed file
   stops removal rather than deleting operator state.

The manifest cannot request an ambient `LD_LIBRARY_PATH` or `LD_PRELOAD`. The
service invokes the immutable package postmaster directly, sets inner OpenMP/MKL
thread counts to one, and receives explicit CPU and memory limits. Package or
configuration shape is not enough: the bytes actually loaded must match.

## Unprivileged proof and privileged activation

Planning and the complete fixture lifecycle run unprivileged today:

```sh
python3 tools/postgresql/clusterctl.py install-package \
  --contract contracts/postgresql-cluster.json \
  --package-manifest /path/to/package-manifest.json \
  --package-physical-root /staged/root \
  --root /fixture/root \
  --receipt /tmp/laplace-product-package-installation.json

python3 tools/postgresql/clusterctl.py observe-resources \
  --contract contracts/postgresql-cluster.json \
  --package-manifest /path/to/package-manifest.json \
  --package-physical-root /fixture/root \
  --output /tmp/laplace-postgresql-resources.json

python3 tools/postgresql/clusterctl.py inspect-collisions \
  --contract contracts/postgresql-cluster.json \
  --output /tmp/laplace-postgresql-collisions.json

python3 tools/postgresql/clusterctl.py plan \
  --contract contracts/postgresql-cluster.json \
  --package-manifest /path/to/package-manifest.json \
  --resource-observation /path/to/native-resource-observation.json \
  --collision-observation /path/to/collision-observation.json \
  --package-physical-root /fixture/root \
  --output /tmp/laplace-postgresql-plan.json

python3 tools/postgresql/clusterctl.py apply \
  --plan /tmp/laplace-postgresql-plan.json \
  --contract contracts/postgresql-cluster.json \
  --root /tmp/laplace-activation-fixture \
  --receipt /tmp/laplace-postgresql-staged.json
```

The acceptance suite supplies a fake immutable package and performs
plan/apply/commit/remove entirely below a temporary root. It deliberately rejects
trust authentication, any live collision with the declared targets,
runner-to-admin mapping, ambient loader state, grants outside declared policy,
non-package `pg_config`, package tampering, loaded-object drift, invalid system
identity, and removal after operator modification.

Real activation is one explicit privileged operation. It requires the complete
PostgreSQL 18.6 product package, selected recursive ELF closure, native resource
observation, root ownership, and an unoccupied declared cluster boundary:

```sh
sudo python3 tools/postgresql/clusterctl.py activate-product \
  --authorize-system-root \
  --contract contracts/postgresql-cluster.json \
  --package-manifest /path/to/package-manifest.json \
  --resource-observation /path/to/native-resource-observation.json \
  --evidence-directory /opt/laplace/receipts/plans/<package-id> \
  --output /opt/laplace/receipts/plans/<package-id>/activation-result.json
```

The evidence directory must be addressed by the selected package ID. Completed
ownership, collision, plan, staging, command, initial-load, restart-load, failure, and
activation evidence is retained there. The command will not treat an existing cluster,
configuration, socket, service, or inaccessible collision target as fresh state. No
current service or cluster is a prerequisite or activation target.

## Resource derivation

The co-resident policy declares bounds, not this machine's CPU IDs or available
resources. Native execution authority measures topology, subtracts external
ownership, issues and partitions a conserved root grant, and binds the concrete
processor allocation. The cluster planner consumes the exact receipts and does
not reimplement sibling, NUMA, or resource-allocation semantics in Python. It
also binds storage observations for the declared data, WAL, and temporary paths.
PostgreSQL settings are deterministically derived from the admitted grant, not
from the unrestricted host or a permanent machine layout. For the typed
12-GiB/four-slot acceptance fixture this produces:

- 2 GiB `shared_buffers` and 6 GiB `effective_cache_size`;
- 16 connections, 8 worker processes, 2 parallel workers, and 2 autovacuum workers;
- 32 MiB `work_mem`, 512 MiB maintenance memory, and 256 MiB autovacuum memory;
- 2/8 GiB minimum/maximum WAL with two-times observed storage headroom;
- checksums at initialization, `fsync`, full-page writes, synchronous commit,
  replica WAL, I/O timing, `io_uring`, and `huge_pages=try`.

Changing the hardware observation cannot silently enlarge the contract grant.
Changing the grant changes the plan and receipt identities.

The live observation is issued by the package's
`bin/laplace_resource_observe` executable. It calls the native execution
topology, root-grant, and partition APIs; binds the selected logical processors;
observes the backing filesystems for the declared data, WAL, and temporary
paths; and emits separate topology, root-grant, partition, processor-allocation,
and storage receipts. `clusterctl.py observe-resources` verifies that executable
against the package manifest, invokes it without ambient loader variables, and
adds only the canonical orchestration receipt before validating the result.
