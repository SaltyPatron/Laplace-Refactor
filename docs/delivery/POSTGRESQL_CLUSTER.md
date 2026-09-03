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
| Durable perfcaches | `/opt/laplace/pgdata/refactor/perfcache` |
| Configuration | `/etc/laplace/instances/refactor` |
| Logs / receipts | `/var/log/laplace/postgresql/refactor`; `/opt/laplace/receipts/postgresql/refactor` |
| Immutable package | `/opt/laplace/releases/<package-id>` |
| Committed package link | `/opt/laplace/current` |

The product extension exposes Unicode through two ordered, bounded batch
operations: `unicode_tier0_resolve_batch` and
`unicode_identity_reverse_resolve_batch`. Each call requires the exact active
epoch identity and fingerprint, acquires one generation pin, and delegates the
entire access law to the native Tier-0 or reverse module. PostgreSQL does not
decode the cache format or recreate its lookup semantics. The application role
receives these read operations; root construction and activation remain an
administrative effect.

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
5. **Restart proof, boot enablement, and commit** stops and starts the candidate, requires a different
   postmaster PID with the same positive PostgreSQL system identifier, and repeats the
   complete loaded-object and generated-configuration observation. It then enables the
   exact candidate unit and proves `systemctl is-enabled` before
   `/opt/laplace/current` is switched atomically. Failure before that commit disables
   and stops the candidate when possible, leaves the prior pointer untouched,
   preserves database state, and writes a typed failure receipt. Boot enablement is
   necessary but does not replace the separately required physical reboot proof.
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

Real activation is one explicit privileged operation. The low-level controller shown
below is the root implementation boundary, not the normal product operator surface.
It requires the complete
PostgreSQL 18.6 product package, selected recursive ELF closure, native resource
observation, root ownership, and an unoccupied declared cluster boundary:

```sh
sudo python3 tools/postgresql/clusterctl.py activate-product \
  --authorize-system-root \
  --contract contracts/postgresql-cluster.json \
  --package-manifest /path/to/package-manifest.json \
  --resource-observation /path/to/native-resource-observation.json \
  --evidence-directory /opt/laplace/receipts/postgresql/refactor/cluster-activation/<package-id> \
  --output /opt/laplace/receipts/postgresql/refactor/cluster-activation/<package-id>/activation-result.json
```

The evidence directory must be addressed by the selected package ID. Completed
ownership, collision, plan, staging, command, initial-load, restart-load, failure, and
activation evidence is retained there. The command will not treat an existing cluster,
configuration, socket, service, or inaccessible collision target as fresh state. No
current service or cluster is a prerequisite or activation target.

### CI/CD activation gateway

Normal product activation is dispatched by
`.github/workflows/product-activation.yml` on the protected `product` environment and
executes as `laplace-runner`. The workflow compiles one request from the exact
accepted PostgreSQL receipt and the current clean `main` checkout. It composes or
revalidates the corresponding content-addressed Laplace product package, observes the
native resource allocation against those staged bytes, and binds the main commit,
workflow run, product-package receipt, package-manifest bytes, staged source root,
resource-observation bytes, Unicode source root, and gateway contract. It authenticates
that canonical request with the protected deployment key; an older package recorded by
a continuation checkpoint cannot substitute for the package built from the dispatched
commit.

The runner can cross the root boundary only through the immutable executable at
`/opt/laplace/deployment/current/bin/laplace-product-activate`. Its sudo policy names
exactly `probe` and `execute-request`; it grants no shell, Python, repository path,
wildcard argument, systemctl, file-copy, or arbitrary controller authority. The root
gateway verifies its own root-owned content-addressed bundle before accepting bounded
stdin, verifies the request authentication and time window, then invokes the bundled
copies of `clusterctl.py`, `unicodectl.py`, and `highwayctl.py` with paths derived from
the signed package identity. The gateway first installs or exactly revalidates the
signed content-addressed package through `clusterctl.py install-package`; it then
activates the isolated cluster, Unicode root, and Highway. Repository checkout code
never executes as root.

The gateway installs once through
`tools/delivery/install_product_activation_gateway.py`. That bootstrap creates the
root-owned controller bundle, root-only deployment key, exact sudoers entry, and
version pointer. It is infrastructure establishment, not a recurring product install.
Subsequent package composition, installation, PostgreSQL, Unicode, and Highway
activations run through CI/CD. Exact replay returns the existing content-addressed
result; a changed key, request, route, package receipt, package source root, resource
receipt, cluster restart proof, Unicode readback proof, or Highway active-readback
proof fails closed.

`tools/delivery/product_host.py` is the administrator-facing convergent entrypoint.
`converge` creates or repairs only the declared service identity, persistent runner
roots, and immutable gateway. `install`, `initial-run`, and `repair` additionally
accept either one authenticated CI request or one verified PostgreSQL publication.
For a local publication, the entrypoint drops package composition and native resource
observation to `laplace-runner`; the installed root-owned gateway then derives an
authenticated local-administrator request from the package receipt and package-bound
resource observation. The local route is not present in sudoers. Both routes compose
the package, cluster, Unicode, and Highway modules without a general root shell or a
second semantic implementation. Repair changes only declared directory metadata and
exact gateway bytes before exact product replay; it does not recursively rewrite
PGDATA, WAL, logs, receipts, or package generations.

Fresh local installation, initial run, and repair are the same convergent administrator
command. The source commissioning entrypoint resolves the exact accepted publication
selected by `state/product-publication-selection.json`; this narrowly scoped selection
is distinct from the continuation checkpoint and is consumed identically by local and
CI package composition. A customer installer carries
the accepted package, controls, and source payload in its own immutable manifest.
Neither route asks a person to supply an internal receipt path or content hash. The
activation key is generated once at its final root-only path and exact replay never
replaces it:

```sh
sudo ./install
```

The package-product workflow emits a deterministic tar archive and adjacent SHA-256
file rather than uploading a directory. This preserves the executable mode and
symlinks required by the standalone product. After verifying and extracting that
archive, installation is also `sudo ./install` from the extracted directory.

## Product Unicode activation

Cluster activation and Unicode activation are separate receipted product states.
`tools/postgresql/unicodectl.py` consumes the exact activated cluster plan,
cluster-activation receipt, package manifest, Unicode source contract, all 33 verified
Unicode 17 source files, and the generated PostgreSQL binding contract. Its product law
is [`contracts/unicode-product-activation.json`](../../contracts/unicode-product-activation.json).

The successor package must contain
`bin/laplace_unicode_activation_identify`. That native executable derives the
activation identifier, epoch fingerprint, authority fingerprint, and all ten framework
epochs from one canonical request using independent domain-separated BLAKE3 preimages.
The orchestrator's exact bytes are also bound into the request. Python does not mint
these typed native identities.

For a new product, the controller requires an empty Unicode/perfcache control state and
absent generation paths. It executes `unicode_root_build_and_activate` exactly once in
one transaction, then asserts the complete 2,230,150-frame result, all 1,114,112 atom
and DUCET-position rows, all normalized families, sibling artifact digests and sizes,
the generated plan manifest, the durable generation/deposit receipts, and the exact
active epoch before commit. An exact already-committed epoch is a distinct recovery
state; every partial or unrelated state is rejected.

After commit, the controller verifies both artifact files, restarts the product service,
requires the same positive system identifier with a different postmaster PID and the
same loaded package bytes, and enters through `laplace_app` in a fresh backend. That
cold application route must resolve positions `0`, `65`, and `1114111`, then invert
their content-identity/full-preimage pairs through the reverse module under the same
epoch. The artifact bytes must remain unchanged across restart and readback. Only then
does a content-addressed `laplace.unicode-product-activation-receipt/v1` exist. Failures
emit a typed receipt and require state reinspection before retry.

Once the successor package and product cluster receipts exist, the underlying Unicode
controller command is:

```sh
sudo python3 tools/postgresql/unicodectl.py \
  --authorize-system-root \
  --package-manifest /path/to/package-manifest.json \
  --cluster-plan /opt/laplace/receipts/postgresql/refactor/cluster-activation/<package-id>/cluster-plan-<digest>.json \
  --cluster-activation-receipt /opt/laplace/receipts/postgresql/refactor/cluster-activation/<package-id>/activation-complete-<digest>.json \
  --source-root /vault/Data/UCD/Public/UCD/latest \
  --output /opt/laplace/receipts/postgresql/refactor/unicode-activation-result.json
```

The CI/CD gateway composes this controller immediately after exact cluster activation;
an operator does not have to discover receipt filenames or invoke it separately. The
existence of the controller, gateway, and fixture acceptance does not claim that the
current machine has activated the product. Product activation still requires the exact
successor package, live cluster, restart, public readback, and durable receipt.

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
