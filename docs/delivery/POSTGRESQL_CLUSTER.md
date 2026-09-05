# Persistent PostgreSQL product cluster

The clean refactor PostgreSQL estate is persistent product state, not a CI fixture. Its machine-readable authority is [`contracts/postgresql-cluster.json`](../../contracts/postgresql-cluster.json). The selected DEV/BAT product is owned by `laplace-runner` from package installation through PostgreSQL, Unicode, Highway, receipts, and query readback.

Administrator/root authority is confined to one-time host bootstrap. The recurring database lifecycle does not depend on root, sudo, systemd, an activation gateway, or a human OS account.

Disposable PostgreSQL proof clusters remain useful merge-time evidence. They are not deployment and cannot substitute for this persistent state.

## Selected boundary

| Item | Selected value |
| --- | --- |
| PostgreSQL | 18.6 from the verified immutable product package |
| Recurring lifecycle provider | packaged `pgsql-18/bin/pg_ctl` |
| Optional boot integration | `laplace-refactor-postgresql.service` |
| Operating-system owner | `laplace-runner:laplace-runner` |
| Database / roles | `laplace_refactor`; `laplace_admin`; `laplace_app` |
| Port | `55433` |
| Unix socket | `/opt/laplace/runtime/postgresql/refactor` |
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

The database remains a set-oriented orchestration and durable-state surface over the same native Laplace engine used by direct consumers. SQL does not reimplement identity, trajectory, ISA, evidence, receipt, cognition, or other native semantics. One SQL batch must remain one native batch across SPI.

## One-time host bootstrap

The administrator runs:

```sh
sudo bash scripts/setup-host.sh
```

That command establishes the operating-system envelope and exits. It creates or verifies `laplace-runner`, persistent parent roots, and optional host-boot integration. It does **not** build/select a package, run `initdb`, start PostgreSQL, migrate or seed the database, activate Unicode/Highway, or execute product semantics.

The DEV/BAT host has completed this bootstrap generation. There is no remaining manual prerequisite for normal accepted-main product delivery.

The static systemd unit and narrow service-control sudo policy remain available for boot/host integration. They are not PostgreSQL lifecycle authority and are not consumed by accepted-main activation.

See [`HOST_BOOTSTRAP.md`](HOST_BOOTSTRAP.md) for the exact host/CI handoff.

## PostgreSQL authorization

The recurring operating-system identity is one product owner:

```text
OS identity         laplace-runner
administrator role  laplace_admin
application role    laplace_app
```

The PostgreSQL roles remain distinct authorization identities even though the same OS product owner may request either role through explicit peer mappings. This avoids making a human account or root process the database execution owner while preserving separate administrative and application permissions.

Runtime authentication remains fail-closed:

- `laplace-runner -> laplace_admin` only through the declared administrator peer map;
- `laplace-runner -> laplace_app` only through the declared application peer map;
- unmatched local identities are rejected;
- TCP connections are rejected for the initial local product cluster;
- `trust` authentication is forbidden;
- ambient `LD_LIBRARY_PATH` and `LD_PRELOAD` are forbidden.

## Runner-owned package and cluster lifecycle

`tools/postgresql/cluster_core.py` retains isolated/reference package-plan-receipt machinery used by fixture coverage. `tools/postgresql/clusterctl.py` is the selected physical provider for the persistent DEV/BAT product.

The live lifecycle is:

1. **Compose/select** — accepted-main CI composes or revalidates the exact content-addressed product package and native resource observation.
2. **Install package** — `laplace-runner` verifies package entries, digests, modes, capabilities, activation gates, internal symlinks, and loaded-object declarations, then installs the immutable release at `/opt/laplace/releases/<package-id>`.
3. **Inspect collisions** — the live controller proves that fresh candidate PGDATA, WAL, temp, perfcache, configuration, socket, port, and process state do not conflict. Optional systemd files and the durable receipt namespace are not fresh-cluster state.
4. **Plan** — the controller binds exact package identity, topology/resource receipts, storage observations, PostgreSQL settings, generated configuration, bootstrap SQL, and command program. A live plan contains no generated systemd unit and no `runuser`.
5. **Stage** — `laplace-runner` creates only previously absent candidate configuration/state. The persistent receipt namespace is preserved independently.
6. **Select candidate runtime** — `/opt/laplace/runtime/refactor` atomically selects the candidate release before execution.
7. **Initialize** — packaged `initdb` executes directly as `laplace-runner` against the durable PGDATA/WAL paths with checksums and peer/reject authentication.
8. **Start** — packaged `pg_ctl` starts the postmaster directly as `laplace-runner`.
9. **Readiness** — packaged `pg_isready` proves the declared socket/port is accepting the selected cluster.
10. **Bootstrap database** — packaged `psql` executes as `laplace-runner` requesting `laplace_admin`; it creates the application role/database, extensions, grants, and application effect boundary.
11. **Live loaded-object proof** — `pg_ctl status`, `PGDATA/postmaster.pid`, PostgreSQL inspection SQL, and `/proc/<pid>/exe`/`maps` prove the live postmaster, Laplace extension, `pg_stat_statements`, and native engine match the selected package bytes.
12. **Restart proof** — packaged `pg_ctl stop` then `pg_ctl start` must produce a different postmaster PID with the same positive PostgreSQL system identifier and identical loaded-object/configuration state.
13. **Commit** — only after restart proof does `/opt/laplace/current` atomically select the proven release.
14. **Unicode activation/readback** — native/SQL Unicode operations execute as `laplace-runner`; the product provider maps the controller's restart obligation to packaged `pg_ctl stop/start`, then proves cold `laplace_app` readback and reverse inversion.
15. **Highway activation/readback** — the canonical numerical Highway is admitted under the same runner-owned boundary; its restart obligation is likewise satisfied by packaged `pg_ctl`, followed by cold application-role readback.
16. **Persistent product result** — package, cluster, Unicode, Highway, repository commit, lifecycle provider, and execution owner are retained under durable receipt state.

The recurring product result must explicitly establish:

```json
{
  "execution_owner": "laplace-runner",
  "postgresql_lifecycle_provider": "pg_ctl",
  "root_product_executor": false
}
```

The cluster result additionally records `restart_proven=true` and `service_integration_required=false`.

## No recurring privileged lifecycle

The accepted-main PostgreSQL path does not execute `sudo` or `systemctl`.

The earlier root-owned activation gateway/HMAC path is historical implementation evidence only. It is not selected deployment architecture. These are regressions:

```text
sudo ... laplace-product-activate execute-request
sudo python3 ... product/database/Unicode/Highway controller
sudo systemctl ... as a prerequisite for product queryability
```

Systemd remains optional host-boot integration only.

## Optional systemd integration

`packaging/systemd/laplace-refactor-postgresql.service` is installed/enabled by bootstrap so a host may start the persisted product automatically after boot. It runs PostgreSQL as `laplace-runner`, points at `/opt/laplace/runtime/refactor`, and is guarded until a candidate binary and `PG_VERSION` exist.

It does not own package selection, database initialization, restart proof, Unicode/Highway activation, or product receipts. CI neither requires nor invokes it.

The product Unix socket is under `/opt/laplace/runtime/postgresql/refactor`; it does not depend on systemd `RuntimeDirectory` creation.

## Isolated fixture coverage versus live policy

The large PostgreSQL mutation suite uses temporary roots and typed fixture plans. Those fixtures may retain generated service/resource representation to continue exercising historical invariants such as package postmaster binding, ambient-loader rejection, resource-plan constraints, stage/commit/remove behavior, and deliberate mutations.

Fixture representation is not the live host plan. A live plan rejects generated systemd state and uses the package's `pg_ctl` lifecycle. Fixture success does not claim the product is installed.

## Failed candidate and retry law

A failed **uncommitted** candidate must not permanently wedge the host.

Before removing candidate state, rollback must prove that the postmaster is stopped. The live provider uses packaged `pg_ctl status`, `PGDATA/postmaster.pid`, and process evidence rather than systemd service state.

Only then may state created by that staged candidate be removed. The content-addressed package release and persistent failure evidence remain. A committed/active database is never converted into disposable candidate state.

## Exact replay and persistent state

An exact already-activated package does not become a fresh-install request merely because CI runs again. The runner provider:

- loads the durable cluster result;
- executes packaged `pg_ctl status`;
- starts it with packaged `pg_ctl` if it is stopped;
- verifies readiness and loaded-object/configuration identity;
- requires the PostgreSQL system identifier to equal the durable cluster receipt;
- continues Unicode/Highway exact replay or readback without deleting committed PGDATA.

A different package generation requires an explicit durable advance path; it must not masquerade as a fresh disposable cluster.

## Unicode product activation

The Unicode product root remains a distinct receipted state. The package must contain `bin/laplace_unicode_activation_identify`, which derives activation identifiers, epoch fingerprints, authority fingerprint, and framework epochs from the exact canonical request using domain-separated BLAKE3 preimages. Python orchestration does not mint those native identities.

For fresh state the operation checks the complete expected result, including:

- 2,230,150 canonical frames;
- 1,114,112 atom records;
- exact DUCET/normalization cardinalities;
- direct and reverse artifact digests/sizes;
- durable producer/deposit/admission receipts;
- exact active epoch identity.

After commit, a packaged `pg_ctl` restart must preserve the PostgreSQL system identity while producing a new postmaster PID. A fresh `laplace_app` backend must resolve selected Tier-0 positions and invert their exact content-identity/preimage pairs. Exact committed state is recoverable; partial or unrelated state is rejected.

The normal operator does not run `unicodectl.py` with sudo. The accepted-main runner provider supplies SQL, restart, readiness, and live-loaded-state providers.

## Highway product activation

Highway activation consumes the exact cluster and Unicode receipts, selected registry version/predecessor, and native/SQL implementation. It executes as `laplace-runner`. Its restart obligation is fulfilled through packaged `pg_ctl`, and the cold application role must read back the active registry before the Highway receipt is accepted.

## Resource derivation

The co-resident resource policy declares bounds, not fixed host CPU IDs. Native execution authority measures topology, subtracts externally owned resources, issues a conserved grant, selects logical processors, and observes backing filesystems for data, WAL, and temporary state. The PostgreSQL plan consumes those receipts rather than reimplementing topology discovery in Python.

For the typed 12-GiB/four-slot acceptance fixture, deterministic settings include:

- 2 GiB `shared_buffers` and 6 GiB `effective_cache_size`;
- 16 connections, 8 worker processes, 2 parallel workers, and 2 autovacuum workers;
- 32 MiB `work_mem`, 512 MiB maintenance memory, and 256 MiB autovacuum memory;
- 2/8 GiB minimum/maximum WAL with two-times observed storage headroom;
- checksums, `fsync`, full-page writes, synchronous commit, replica WAL, I/O timing, `io_uring`, and `huge_pages=try`.

Changing hardware observations cannot silently enlarge the contract grant. Changing an admitted grant changes plan/receipt identities.

Dynamic processor/memory decisions belong in PostgreSQL configuration and execution receipts, not in a root-written systemd unit.

## Persistent installed-product acceptance

A merge proof, fixture cluster, `$RUNNER_TEMP` database, or Actions artifact is not product delivery. Accepted-main deployment must leave the real host able to prove:

```sh
package_id="$(basename "$(readlink -f /opt/laplace/current)")"
package_root="/opt/laplace/releases/$package_id"

"$package_root/pgsql-18/bin/pg_ctl" \
  -D /opt/laplace/pgdata/refactor/data status

"$package_root/pgsql-18/bin/pg_isready" \
  --host /opt/laplace/runtime/postgresql/refactor \
  --port 55433 \
  --dbname laplace_refactor

"$package_root/pgsql-18/bin/psql" \
  -X -w -A -t -v ON_ERROR_STOP=1 \
  --host /opt/laplace/runtime/postgresql/refactor \
  --port 55433 \
  --dbname laplace_refactor \
  --username laplace_app \
  -c "select current_database(), current_user, current_setting('server_version'), (select extversion from pg_extension where extname='laplace');"
```

Acceptance additionally requires:

- `/opt/laplace/runtime/refactor` and `/opt/laplace/current` resolve to the proven content-addressed release;
- PostgreSQL reports 18.6;
- database is `laplace_refactor` and application role is `laplace_app`;
- Laplace extension is loaded from the selected package;
- restart is proven with a new postmaster PID and unchanged positive system identifier;
- generated configuration and loaded-object bytes remain exact;
- Unicode and Highway receipts are durable/readable.

Host-boot/systemd integration may be tested separately, but failure or absence of that optional integration must not be confused with failure to install, run, or query the product through its canonical `pg_ctl` lifecycle.

A later accepted-main run must reverify or explicitly advance this durable product. It must not uninstall PostgreSQL, delete committed PGDATA, or convert the persistent cluster into another fresh integration fixture merely to obtain a green check.
