# Linux host bootstrap and CI/CD handoff

Laplace-Refactor has one human host-bootstrap boundary and one recurring product-delivery boundary. They are intentionally different operations.

## One-time human bootstrap

Run once, and rerun only to reconcile host prerequisites:

```sh
sudo bash scripts/setup-host.sh
```

`setup-host.sh` is infrastructure establishment only. It may use administrator authority because it creates operating-system identities and installs static host policy. It does **not** execute product delivery.

It establishes only:

- the `laplace-runner` system identity;
- persistent product/runtime parent roots owned by `laplace-runner` under `/build`, `/opt/laplace`, `/pgtemp`, `/var/lib/pgwal`, and `/var/log/laplace`;
- the administrator-owned `/etc/laplace` namespace and a setgid `root:laplace-runner` `/etc/laplace/instances` parent so recurring CI can create the selected instance configuration without recurring administrator execution;
- one static `laplace-refactor-postgresql.service` systemd envelope from `packaging/systemd/laplace-refactor-postgresql.service`, installed and enabled but not started;
- exactly three passwordless recurring service operations for `laplace-runner`: `systemctl start`, `stop`, and `restart` for that one unit;
- a durable prerequisite receipt at `/opt/laplace/receipts/bootstrap/host.json`, including the exact installed service-unit digest and sudo policy.

The bootstrap does **not** build PostgreSQL or Laplace, compose/install/select a product package, initialize or migrate PostgreSQL, seed Unicode, activate the Highway, write normal product activation receipts, install an activation HMAC key, or install/invoke a whole-product root activation gateway.

The bootstrap receipt explicitly records `product_activated=false`, `postgresql_initialized=false`, `activation_gateway_installed=false`, and `service_envelope.started_by_bootstrap=false`.

## Recurring ownership

After bootstrap, the recurring product owner is `laplace-runner`, consistent with #12 and corrected #120.

```text
accepted main
  -> CI running as laplace-runner
  -> compose/select exact package
  -> install content-addressed /opt/laplace/releases/<package-id>
  -> /opt/laplace/runtime/refactor -> candidate/served package
  -> initialize or reverify persistent PostgreSQL state
  -> sudo systemctl start/stop/restart only
  -> prove live process/package/config identity and restart
  -> /opt/laplace/current -> proven committed package
  -> activate/reverify Unicode and Highway
  -> write durable receipts
  -> query/read back the installed product
```

Root is not the package, database, Unicode, Highway, receipt, or semantic execution owner. When the operating system requires privilege, CI may invoke only the static service lifecycle commands installed by bootstrap. A privileged process must not parse or execute an authenticated whole-product request.

## Static PostgreSQL service envelope

The systemd unit is installed once by bootstrap and points at the runner-owned runtime selection path:

```text
/opt/laplace/runtime/refactor/pgsql-18/bin/postgres
```

`/opt/laplace/runtime/refactor` is an atomic `laplace-runner`-owned symlink used to start and prove a candidate package before it becomes committed product state. `/opt/laplace/current` is a separate post-proof committed package pointer. For a successfully activated generation both links resolve to the same content-addressed release:

```text
/opt/laplace/runtime/refactor -> ../releases/<package-id>
/opt/laplace/current          -> releases/<package-id>
```

If candidate activation fails before commit, the runtime link returns to its prior target while the committed `current` link remains unchanged. The systemd unit is not regenerated for every package or resource observation and contains no package ID, `AllowedCPUs`, `MemoryHigh`, or `MemoryMax` selection. Dynamic PostgreSQL/resource decisions belong in product configuration and execution receipts, not in a root-rewritten service file.

## PostgreSQL identities

The recurring operating-system owner is `laplace-runner` for both administrative and application database connections to this private Unix-socket product cluster. PostgreSQL roles remain separate:

```text
OS product owner    laplace-runner
administrator role  laplace_admin
application role    laplace_app
```

Peer maps bind the same OS product owner to the explicitly requested database role; role separation remains a PostgreSQL authorization boundary without requiring CI to impersonate the human operator or execute database work as root.

## Persistent state

The selected refactor product uses durable paths from `contracts/postgresql-cluster.json`, including:

```text
PGDATA      /opt/laplace/pgdata/refactor/data
WAL         /var/lib/pgwal/refactor
perfcache   /opt/laplace/pgdata/refactor/perfcache
config      /etc/laplace/instances/refactor
logs        /var/log/laplace/postgresql/refactor
receipts    /opt/laplace/receipts/postgresql/refactor
```

The setup script may remove an exact candidate leaf only when it is empty stale bootstrap residue. Nonempty product state is preserved. Failed **uncommitted** candidate state may be rolled back only after the activation controller proves the service/postmaster is stopped; a committed cluster is never converted into a fresh-test cleanup target.

## Real installed-product acceptance

A build, fixture cluster, disposable `$RUNNER_TEMP` database, or Actions artifact is not deployment evidence. Delivery requires the real host state to survive the workflow and answer through the installed product:

```sh
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

The accepted deployment must also prove exact installed package identity, runner ownership, PostgreSQL process restart persistence, static-service boot enablement, and durable Unicode/Highway/product receipt readback.

## Human/CI boundary

The development/BAT sequence is intentionally short:

```text
sudo bash scripts/setup-host.sh   # once: static host envelope only
push/merge accepted main work     # thereafter: CI/CD owns product delivery
```

If recurring CI requires another broad root program, an HMAC activation gateway, a human OS identity for database administration, or root-written product state, the privilege boundary has regressed and the implementation is incomplete.
