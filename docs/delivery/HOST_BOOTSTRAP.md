# Linux host bootstrap and CI/CD handoff

Laplace-Refactor has one human host-bootstrap boundary and one recurring product-delivery boundary. They are intentionally different operations.

## One-time human bootstrap

Run once on a Linux product host:

```sh
sudo bash scripts/setup-host.sh
```

`setup-host.sh` is infrastructure establishment only. It converges:

- the `laplace-runner` service identity;
- persistent **parent** roots under `/build`, `/etc/laplace`, `/opt/laplace`, `/pgtemp`, `/var/lib`, and `/var/log`;
- the root-only product activation key without replacing an existing valid key;
- the immutable root-owned product activation gateway generation;
- the exact `laplace-runner` sudoers entries for gateway `probe` and `execute-request`;
- product service-state systemd units used for boot enablement/readback; and
- an exact bootstrap receipt at `/opt/laplace/receipts/deployments/product-host-bootstrap.json`.

The bootstrap finishes by probing the installed v2 gateway both directly as root and through the exact passwordless `laplace-runner -> sudo -> gateway probe` handoff.

The bootstrap does **not** build Laplace, compose a product package, create PostgreSQL instance leaf state, initialize a database, seed Unicode, activate the Highway, or select a product generation.

## Cluster state ownership

The host bootstrap owns only parent roots. The PostgreSQL cluster activation module exclusively creates and owns these instance leaf paths:

```text
/opt/laplace/pgdata/refactor/data
/var/lib/pgwal/refactor
/pgtemp/refactor
/opt/laplace/pgdata/refactor/perfcache
/var/log/laplace/postgresql/refactor
/opt/laplace/receipts/postgresql/refactor
```

This is required by the real activation implementation: `clusterctl.apply_plan()` accepts a fresh product only when its selected instance leaf state is absent. Host convergence therefore must not pre-create those paths. Existing activated product state is preserved on replay/repair and is never recursively replaced by bootstrap.

## CI/CD owns product delivery after bootstrap

After `setup-host.sh` succeeds, accepted pushes to `main` are the normal DEV/BAT product-delivery path:

```text
push to main
  -> product-path
  -> selected hosted/custom-stack/PostgreSQL/package proof
  -> dev-bat-deployment
  -> product-activation for that exact main SHA
  -> persistent PostgreSQL activation
  -> Unicode activation
  -> Highway activation
  -> service enablement / restart / readback
```

`product-path` does not treat dispatch as completion. The accepted-main run waits for the exact `product-activation` workflow run and requires its successful conclusion.

The activation workflow executes as `laplace-runner`. It crosses the root boundary only through `/opt/laplace/deployment/current/bin/laplace-product-activate`, whose installed sudo policy grants only `probe` and `execute-request`. Once a v2 gateway has been bootstrapped, authenticated gateway generations can self-upgrade through that bounded request path without another human root install.

## Real installed-product readback

A deployment is not established by unit/fixture tests or by a disposable `$RUNNER_TEMP` PostgreSQL cluster. The persistent product must exist at the declared host paths and answer through its installed service:

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

`.github/workflows/live-product-probe.yml` performs this class of direct installed-host readback. Product activation also retains exact package, cluster, Unicode, Highway, service-enablement, and boot/readback receipts under `/opt/laplace/receipts`.

## Local/customer install path

`sudo ./install` remains the convergent administrator/customer install path for an accepted source checkout or extracted standalone installer. It is not the recurring CI/CD bootstrap mechanism. On the development/BAT host, the intended operational sequence is:

```text
sudo bash scripts/setup-host.sh   # once
push/merge accepted main work     # thereafter; CI/CD owns deployment
```
