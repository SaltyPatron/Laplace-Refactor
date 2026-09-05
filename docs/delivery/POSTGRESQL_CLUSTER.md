# Persistent PostgreSQL product cluster

The clean refactor PostgreSQL estate is a persistent product service, not a CI test
fixture. Its machine-readable authority is
[`contracts/postgresql-cluster.json`](../../contracts/postgresql-cluster.json). The
selected DEV/BAT product is owned by `laplace-runner`; administrator/root authority is
limited to one-time host bootstrap and the exact operating-system service operations
that cannot be performed unprivileged.

Disposable PostgreSQL proof clusters remain useful for merge-time verification, but
they are not deployment and cannot substitute for this persistent state.

## Selected boundary

| Item | Selected value |
| --- | --- |
| PostgreSQL | 18.6 from the verified immutable product package |
| Service | `laplace-refactor-postgresql.service` |
| Operating-system owner | `laplace-runner:laplace-runner` |
| Database / roles | `laplace_refactor`; `laplace_admin`; `laplace_app` |
| Port / socket | `55433`; `/run/laplace-refactor-postgresql` |
| Data | `/opt/laplace/pgdata/refactor/data` |
| WAL | `/var/lib/pgwal/refactor` |
| Temporary | `/pgtemp/refactor` |
| Durable perfcaches | `/opt/laplace/pgdata/refactor/perfcache` |
| Configuration | `/etc/laplace/instances/refactor` |
| Logs | `/var/log/laplace/postgresql/refactor` |
| Receipts | `/opt/laplace/receipts/postgresql/refactor` |
| Immutable package | `/opt/laplace/releases/<package-id>` |
| Candidate/served runtime link | `/opt/laplace/runtime/refactor` |
| Proven committed package link | `/opt/laplace/current` |

The database remains a set-oriented orchestration and durable-state surface over the
same native Laplace engine used by direct consumers. SQL does not reimplement
identity, trajectory, ISA, evidence, receipt, cognition, or other native semantics.
One SQL batch must remain one native batch across SPI.

## One-time host bootstrap

The administrator runs:

```sh
sudo bash scripts/setup-host.sh
```

That command establishes the operating-system envelope and exits. It creates or
verifies `laplace-runner`, persistent parent roots, the static systemd unit, and the
exact sudo policy for:

```text
systemctl start   laplace-refactor-postgresql.service
systemctl stop    laplace-refactor-postgresql.service
systemctl restart laplace-refactor-postgresql.service
```

Bootstrap installs and enables the unit but does **not** start PostgreSQL. The unit is
safe to enable before first activation because it has `ConditionPathExists` guards for
both the selected runtime PostgreSQL binary and initialized `PG_VERSION`.

Bootstrap does not build or select a product package, run `initdb`, migrate or seed the
database, activate Unicode, activate the Highway, install an activation HMAC key, or
execute any package/database/semantic program as root. The exact unit and sudo-policy
digests are written to `/opt/laplace/receipts/bootstrap/host.json`.

See [`HOST_BOOTSTRAP.md`](HOST_BOOTSTRAP.md) for the complete host/CI handoff.

## Static service envelope

The bootstrap-owned unit never contains a package ID or per-deployment resource grant.
Its executable path is stable:

```text
/opt/laplace/runtime/refactor/pgsql-18/bin/postgres
```

`laplace-runner` atomically controls the runtime symlink. During first activation:

```text
/opt/laplace/runtime/refactor -> ../releases/<candidate-package-id>
```

Only after live package/configuration/process identity and restart proof succeed does
the controller commit:

```text
/opt/laplace/current -> releases/<candidate-package-id>
```

A failed uncommitted candidate restores the prior runtime target and leaves the
committed `current` pointer untouched. CI never rewrites the systemd unit merely to
select another content-addressed package.

Dynamic processor allocation and PostgreSQL resource settings belong to the typed
resource observation, PostgreSQL configuration, and execution receipts. They are not
root-written `AllowedCPUs`, `MemoryHigh`, or `MemoryMax` fields in a generated unit.

## PostgreSQL authorization

The recurring operating-system identity is intentionally one product owner:

```text
OS identity         laplace-runner
administrator role  laplace_admin
application role    laplace_app
```

The PostgreSQL roles remain separate authorization identities even though the same OS
product owner can request either role through explicit peer maps. This avoids making a
human account or root process the database execution owner while preserving distinct
administrative and application privileges.

Runtime authentication remains fail-closed:

- `laplace-runner -> laplace_admin` only through the declared administrator peer map;
- `laplace-runner -> laplace_app` only through the declared application peer map;
- unmatched local users are rejected;
- TCP connections are rejected for the initial local product cluster;
- `trust` authentication is forbidden;
- ambient `LD_LIBRARY_PATH` and `LD_PRELOAD` are forbidden.

## Runner-owned package and cluster lifecycle

`tools/postgresql/cluster_core.py` contains the shared package/plan/PostgreSQL/receipt
mechanics. `tools/postgresql/clusterctl.py` supplies the selected physical policy for
the persistent product.

The live lifecycle is:

1. **Compose/select** — accepted-main CI composes or revalidates the exact
   content-addressed product package and native resource observation.
2. **Install package** — `laplace-runner` verifies every package entry, digest, mode,
   capability, activation gate, internal symlink, and loaded-object declaration, then
   atomically installs the immutable release at `/opt/laplace/releases/<package-id>`.
   The installed tree remains owned by `laplace-runner`.
3. **Inspect collisions** — the live controller proves that candidate PGDATA, WAL,
   temp, perfcache, configuration, log, socket, port, and process state do not already
   represent a competing fresh cluster. The bootstrap-installed static unit and the
   persistent receipt namespace are expected host state and are not fresh-cluster
   collisions.
4. **Plan** — the controller binds the exact package, topology/resource receipts,
   storage observations, PostgreSQL settings, configuration, bootstrap SQL, and
   command program. A live plan does not contain a systemd unit and does not contain
   `runuser`.
5. **Stage** — `laplace-runner` creates only previously absent candidate configuration
   and state directories. The persistent receipt root is not candidate state.
6. **Select candidate runtime** — `/opt/laplace/runtime/refactor` atomically points to
   the candidate release before the service is started.
7. **Initialize** — `laplace-runner` runs the packaged `initdb` directly against the
   persistent PGDATA/WAL paths with checksums and peer/reject authentication.
8. **Start** — the only privilege crossing is `sudo -n systemctl start` for the exact
   static unit installed by bootstrap.
9. **Bootstrap database** — packaged `psql` runs as `laplace-runner` requesting
   `laplace_admin`; it creates the application role/database, extensions, grants, and
   effect boundary.
10. **Live loaded-object proof** — the controller observes the active systemd PID and
    PostgreSQL backend, inspects `/proc/<pid>/exe` and `/proc/<pid>/maps`, and requires
    the postmaster, Laplace extension, `pg_stat_statements`, and native engine paths and
    bytes to equal the package manifest.
11. **Restart proof** — CI uses only the exact stop/start service privileges, requires a
    different postmaster PID with the same positive PostgreSQL system identifier, and
    repeats loaded-object/configuration proof.
12. **Commit** — only after restart proof does `/opt/laplace/current` atomically point
    to the proven release.
13. **Unicode activation/readback** — existing native/SQL Unicode operations execute
    under `laplace-runner`, persist the full root/perfcache state, restart through the
    same narrow service capability, and prove cold `laplace_app` readback plus reverse
    inversion.
14. **Highway activation/readback** — the canonical numerical Highway is admitted and
    activated through the same runner-owned product boundary, followed by restart and
    cold application-role readback.
15. **Persistent product receipt** — package, cluster, Unicode, Highway, repository
    commit, and execution-owner identities are retained under persistent receipt state.

The recurring product result must explicitly record:

```json
{
  "execution_owner": "laplace-runner",
  "root_product_executor": false
}
```

## What still uses sudo

Only the actual host-service actions selected by `setup-host.sh`:

```text
start
stop
restart
```

Package installation, package selection, `initdb`, PostgreSQL SQL execution, loaded
object inspection, Unicode, Highway, product receipts, and application readback do not
cross into root merely because systemd itself requires administrator authority.

A recurring command shaped like either of these is a regression:

```text
sudo ... laplace-product-activate execute-request
sudo python3 ... product/database/Unicode/Highway controller
```

The earlier root-owned activation gateway/HMAC design remains historical implementation
evidence only. It is not the selected DEV/BAT deployment architecture and must not be
used as a reason to reintroduce root product ownership.

## Isolated fixture coverage versus live policy

The large PostgreSQL mutation suite still uses temporary roots and typed fixture plans.
Those fixtures intentionally retain a generated package-bound service definition so
existing tests continue exercising:

- package postmaster path binding;
- resource-derived `AllowedCPUs`/memory fields;
- ambient-loader rejection;
- service mutation detection;
- stage/commit/remove behavior below a temporary root.

That fixture representation is not the live host plan. A live collision observation
selects the static-service runner policy, and the live plan is rejected if it attempts
to rewrite `/etc/systemd/system/laplace-refactor-postgresql.service`.

Fixture success proves controller invariants; it does not claim the product is installed.

## Failed candidate and retry law

A failed **uncommitted** candidate must not permanently wedge the host.

Before deleting candidate state, rollback must prove:

- the systemd service is inactive/failed;
- `MainPID=0`;
- no `/proc` command line references the candidate PGDATA or socket;
- the candidate did not become `/opt/laplace/current`.

Only then may candidate PGDATA/WAL/temp/perfcache/log/config files created by that staged
transaction be removed. The content-addressed package release and persistent failure
evidence remain. A committed/active database is never treated as disposable candidate
state.

## Unicode product activation

The Unicode product root remains a distinct receipted state. The package must contain
`bin/laplace_unicode_activation_identify`, which derives the activation identifier,
epoch fingerprint, authority fingerprint, and framework epochs from the exact
canonical request using domain-separated BLAKE3 preimages. Python orchestration does
not mint those native identities.

For a fresh product the operation requires empty Unicode/perfcache control state and
absent generation paths. It executes the root build/activation transaction and checks
the complete expected result, including:

- 2,230,150 canonical frames;
- 1,114,112 atom records;
- exact DUCET/normalization cardinalities;
- direct and reverse artifact digests/sizes;
- durable producer/deposit/admission receipts;
- exact active epoch identity.

After commit the service is restarted through the narrow sudo capability and a fresh
`laplace_app` backend must resolve selected Tier-0 positions and invert their exact
content-identity/preimage pairs. Exact committed state is recoverable; partial or
unrelated state is rejected.

The normal operator does not invoke `unicodectl.py` with `sudo`. The accepted-main
runner provider composes this operation after cluster activation.

## Highway product activation

Highway activation consumes the exact cluster and Unicode receipts, the selected
registry version and predecessor, and the native/SQL implementation. It is likewise
executed as `laplace-runner`. Restart uses the same exact systemd capability, and the
cold application role must read back the active registry before the Highway receipt is
accepted.

## Resource derivation

The co-resident resource policy declares bounds, not fixed host CPU IDs. Native
execution authority measures topology, subtracts externally owned resources, issues a
conserved grant, selects logical processors, and observes the backing filesystems for
data, WAL, and temporary state. The PostgreSQL plan consumes those receipts rather than
reimplementing topology discovery in Python.

For the typed 12-GiB/four-slot acceptance fixture the current deterministic settings
include:

- 2 GiB `shared_buffers` and 6 GiB `effective_cache_size`;
- 16 connections, 8 worker processes, 2 parallel workers, and 2 autovacuum workers;
- 32 MiB `work_mem`, 512 MiB maintenance memory, and 256 MiB autovacuum memory;
- 2/8 GiB minimum/maximum WAL with two-times observed storage headroom;
- checksums, `fsync`, full-page writes, synchronous commit, replica WAL, I/O timing,
  `io_uring`, and `huge_pages=try`.

Changing hardware observations cannot silently enlarge the contract grant. Changing an
admitted grant changes the plan/receipt identities.

## Persistent installed-product acceptance

A merge proof, fixture cluster, `$RUNNER_TEMP` database, or Actions artifact is not
product delivery. Accepted-main deployment must leave the real host able to prove:

```sh
systemctl is-enabled laplace-refactor-postgresql.service
systemctl is-active laplace-refactor-postgresql.service

/opt/laplace/current/pgsql-18/bin/pg_isready \
  --host /run/laplace-refactor-postgresql \
  --port 55433 \
  --dbname laplace_refactor

/opt/laplace/current/pgsql-18/bin/psql \
  -X -w -A -t -v ON_ERROR_STOP=1 \
  --host /run/laplace-refactor-postgresql \
  --port 55433 \
  --dbname laplace_refactor \
  --username laplace_app \
  -c "select current_database(), current_user, current_setting('server_version'), (select extversion from pg_extension where extname='laplace');"
```

Acceptance additionally requires:

- `/opt/laplace/runtime/refactor` and `/opt/laplace/current` resolve to the proven
  content-addressed release;
- PostgreSQL reports 18.6;
- the database is `laplace_refactor` and application role is `laplace_app`;
- the Laplace extension is installed from the selected package;
- process restart is proven rather than inferred from PID presence;
- Unicode and Highway receipts are durable/readable;
- host restart does not erase the selected service/data/package/receipt state.

A later accepted-main run must reverify or explicitly advance this durable product. It
must not uninstall PostgreSQL, delete committed PGDATA, or convert the persistent
cluster into another fresh integration fixture merely to obtain a green check.
