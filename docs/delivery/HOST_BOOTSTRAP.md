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
- persistent product/runtime roots owned by `laplace-runner` under `/build`, `/opt/laplace`, `/pgtemp`, `/var/lib/pgwal`, and `/var/log/laplace`;
- the exact instance configuration directory `/etc/laplace/instances/refactor`, delegated to `laplace-runner` while `/etc/laplace` remains administrator-owned;
- one static `laplace-refactor-postgresql.service` systemd envelope from `packaging/systemd/laplace-refactor-postgresql.service`;
- exactly three passwordless recurring service operations for `laplace-runner`: `systemctl start`, `stop`, and `restart` for that one unit;
- a durable prerequisite receipt at `/opt/laplace/receipts/bootstrap/host.json`.

The bootstrap does **not** build PostgreSQL or Laplace, compose/install/select a product package, initialize or migrate PostgreSQL, seed Unicode, activate the Highway, write normal product activation receipts, install an activation HMAC key, or install/invoke a whole-product root activation gateway.

The bootstrap receipt explicitly records `product_activated=false` and `activation_gateway_installed=false`.

## Recurring ownership

After bootstrap, the recurring product owner is `laplace-runner`, consistent with #12 and corrected #120.

```text
accepted main
  -> CI running as laplace-runner
  -> compose/select exact package
  -> install/select persistent /opt/laplace release
  -> initialize or reverify persistent PostgreSQL state
  -> activate Unicode / Highway / later product epochs
  -> write durable receipts
  -> query/read back the installed product
```

Root is not the package, database, Unicode, Highway, receipt, or semantic execution owner. When the operating system requires privilege, CI may invoke only the static service lifecycle commands installed by bootstrap. A privileged process must not parse or execute an authenticated whole-product request.

## Static PostgreSQL service envelope

The systemd unit is installed once by bootstrap and points at the stable product selection path:

```text
/opt/laplace/current/pgsql-18/bin/postgres
```

CI owns `/opt/laplace/current` selection and the PostgreSQL configuration/state underneath the delegated product roots. The systemd unit is not regenerated for every package or resource observation. Dynamic PostgreSQL/resource decisions belong in product configuration and execution receipts, not in a root-rewritten service file.

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

The setup script may remove an exact candidate leaf only when it is empty stale bootstrap residue. Nonempty product state is preserved.

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

The accepted deployment must also prove exact installed package identity, PostgreSQL restart persistence, and durable Unicode/Highway/product receipt readback.

## Human/CI boundary

The development/BAT sequence is intentionally short:

```text
sudo bash scripts/setup-host.sh   # once: host prerequisites only
push/merge accepted main work     # thereafter: CI/CD owns product delivery
```

If recurring CI requires another broad root program, the privilege boundary has regressed and the implementation is incomplete.
