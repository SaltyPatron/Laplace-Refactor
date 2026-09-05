# Linux host bootstrap and CI/CD handoff

Laplace-Refactor has one administrator bootstrap boundary and one recurring product-delivery boundary. The product lifecycle does not require administrator authority after bootstrap.

## One-time human bootstrap

Run once, and rerun only to reconcile host prerequisites:

```sh
sudo bash scripts/setup-host.sh
```

`setup-host.sh` establishes operating-system infrastructure only. It does **not** execute product delivery.

It establishes:

- the `laplace-runner` system identity;
- persistent product/runtime parent roots owned by `laplace-runner` under `/build`, `/opt/laplace`, `/pgtemp`, `/var/lib/pgwal`, and `/var/log/laplace`;
- the administrator-owned `/etc/laplace` namespace and setgid `root:laplace-runner` `/etc/laplace/instances` parent;
- a static `laplace-refactor-postgresql.service` unit for optional host-boot integration, installed and enabled but not started;
- a narrowly scoped service-control sudo policy retained for optional systemd administration;
- a durable prerequisite receipt at `/opt/laplace/receipts/bootstrap/host.json`.

The bootstrap does **not** build PostgreSQL or Laplace, compose/install/select a product package, run `initdb`, migrate or seed PostgreSQL, start PostgreSQL, activate Unicode, activate the Highway, install an activation HMAC key, or install/invoke a whole-product root activation gateway.

The bootstrap receipt explicitly records `product_activated=false`, `postgresql_initialized=false`, `activation_gateway_installed=false`, and `service_envelope.started_by_bootstrap=false`.

The DEV/BAT host has physically completed this bootstrap generation. There is no remaining manual prerequisite for normal accepted-main delivery.

## Recurring ownership

The recurring product owner is `laplace-runner`.

```text
accepted main
  -> CI running as laplace-runner
  -> compose/select exact package
  -> install content-addressed /opt/laplace/releases/<package-id>
  -> /opt/laplace/runtime/refactor -> candidate package
  -> initialize/reverify persistent PostgreSQL
  -> packaged pg_ctl start/stop/status
  -> pg_isready + psql + /proc loaded-object proof
  -> restart with new postmaster PID and same system identifier
  -> /opt/laplace/current -> proven committed package
  -> activate/reverify Unicode and Highway
  -> write durable receipts
  -> query/read back the installed product
```

Recurring product delivery requires **no sudo, no root gateway, and no systemd lifecycle operation**. Package, database, Unicode, Highway, receipt, socket, and semantic execution state remain owned by `laplace-runner`.

## PostgreSQL lifecycle provider

The PostgreSQL package itself contains the lifecycle provider:

```text
/opt/laplace/releases/<package-id>/pgsql-18/bin/pg_ctl
```

The product controller uses packaged `pg_ctl` to start, stop, and prove the selected persistent cluster. Live process identity is derived from `PGDATA/postmaster.pid` and `/proc`; readiness and queryability are independently proven with the same package's `pg_isready` and `psql`.

The selected Unix socket is runner-owned state:

```text
/opt/laplace/runtime/postgresql/refactor
```

It therefore does not depend on a systemd-created `/run` directory.

## Optional systemd integration

The static unit installed by bootstrap remains useful for host-boot integration. It is not product lifecycle authority and CI does not require or invoke it.

The unit points at the runner-owned runtime selection:

```text
/opt/laplace/runtime/refactor/pgsql-18/bin/postgres
```

`ConditionPathExists` guards keep the enabled unit inert until both a selected PostgreSQL binary and initialized `PG_VERSION` exist. The unit runs PostgreSQL as `laplace-runner`; it never grants root ownership of PostgreSQL or Laplace state.

The optional sudo policy is therefore an operating-system convenience, not a prerequisite for package installation, PostgreSQL activation, Unicode/Highway activation, restart proof, or queryability.

## Candidate and committed packages

`laplace-runner` owns both product pointers:

```text
/opt/laplace/runtime/refactor -> ../releases/<package-id>  # candidate/served generation
/opt/laplace/current          -> releases/<package-id>     # post-proof committed generation
```

The runtime pointer is selected before candidate execution. `/opt/laplace/current` changes only after exact package bytes, generated configuration, loaded objects, PostgreSQL system identity, and restart have been proven.

If uncommitted activation fails, rollback first proves that the candidate postmaster is stopped. It may then remove only candidate-owned state. A committed cluster is never converted into disposable test state.

## PostgreSQL identities

One operating-system identity owns the product while PostgreSQL roles remain distinct:

```text
OS product owner    laplace-runner
administrator role  laplace_admin
application role    laplace_app
```

Peer mappings admit `laplace-runner` to the explicitly requested database role. Role separation remains a PostgreSQL authorization boundary without requiring CI to impersonate a human account or execute database work as root.

## Persistent state

The selected refactor product uses durable paths from `contracts/postgresql-cluster.json`:

```text
PGDATA      /opt/laplace/pgdata/refactor/data
WAL         /var/lib/pgwal/refactor
socket      /opt/laplace/runtime/postgresql/refactor
perfcache   /opt/laplace/pgdata/refactor/perfcache
config      /etc/laplace/instances/refactor
logs        /var/log/laplace/postgresql/refactor
receipts    /opt/laplace/receipts/postgresql/refactor
```

The durable receipt namespace is not fresh candidate state and survives deployment/reverification.

## Real installed-product acceptance

A build, fixture cluster, disposable `$RUNNER_TEMP` database, or Actions artifact is not deployment evidence. Delivery requires the actual host state to remain alive and queryable after CI:

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

Acceptance also requires exact installed package identity, a real restart with a different postmaster PID and unchanged positive system identifier, loaded-object/config parity, and durable Unicode/Highway/product receipt readback.

## Human/CI boundary

```text
sudo bash scripts/setup-host.sh   # one-time host envelope; already completed on DEV/BAT
accepted main                     # thereafter CI/CD owns product delivery
```

If recurring CI requires root, sudo, a root/HMAC activation gateway, systemd to make PostgreSQL queryable, or a human OS account for database administration, the product boundary has regressed.
