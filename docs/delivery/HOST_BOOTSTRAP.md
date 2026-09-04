# Linux host bootstrap and CI/CD handoff

Laplace-Refactor has one human host-bootstrap boundary and one recurring product-delivery boundary. They are intentionally different operations.

## One-time human bootstrap

Run once on a Linux product host:

```sh
sudo bash scripts/setup-host.sh
```

`setup-host.sh` is infrastructure establishment only. Its final contract is limited to prerequisites the service account cannot establish for itself:

- create/verify the `laplace-runner` service identity;
- establish persistent service-owned roots under `/build` and `/opt/laplace` plus the selected data/WAL/log/config/receipt parents;
- install the static system-level service definition and boot policy required by the host;
- install exact passwordless sudo entries only for the narrow system-service operations that actually require host privilege;
- retain an exact bootstrap receipt.

The bootstrap does **not** build Laplace, compose or select a product package, initialize PostgreSQL, activate Unicode, activate the Highway, write normal product receipts, or execute a product deployment as root.

## Inventor correction — recurring root activation is not architecture

The prior version of this document described a root-owned product activation gateway and `laplace-runner -> sudo -> gateway execute-request` as the recurring delivery path. That boundary was incorrect and is superseded by inventor direction on 2026-09-04 and the corrected #120 contract.

`laplace-runner` is the recurring OS/service/CI execution identity under #12. Root authority must not become the owner of package selection, PostgreSQL state, Unicode/Highway activation, receipts, or product semantics merely because systemd and a few host operations require privilege.

If an operating-system action still requires privilege after bootstrap, CI may invoke only the exact narrow sudo/system-service operation required for that action. A privileged helper may not parse or execute the whole product activation program.

## Persistent state ownership

After bootstrap, the product/runtime is service-owned. At minimum:

```text
package/build/release state       laplace-runner
PostgreSQL PGDATA                 laplace-runner
PostgreSQL WAL                    laplace-runner
perfcache/runtime state           laplace-runner
product/cluster receipts          laplace-runner
Unicode/Highway receipts          laplace-runner
product execution                 laplace-runner
```

System-owned files are limited to actual host policy such as the static systemd unit and sudo policy. Configuration that changes with a product generation must not force the whole deployment back across a root boundary.

## CI/CD owns product delivery after bootstrap

After `setup-host.sh` succeeds, accepted pushes to `main` are the normal DEV/BAT product-delivery path:

```text
push to main
  -> product-path
  -> selected hosted/custom-stack/PostgreSQL/package proof
  -> dev-bat-deployment as laplace-runner
  -> persistent package/release selection as laplace-runner
  -> PostgreSQL activation/repair as laplace-runner
  -> Unicode activation as laplace-runner
  -> Highway activation as laplace-runner
  -> narrow system-service operation only where the OS requires privilege
  -> service restart/readback
```

`product-path` does not treat dispatch as completion. The accepted-main run must wait for the exact persistent deployment result and require its successful conclusion.

## Current implementation defect on PR #215

At the time of this correction, the branch still contains a root-owned activation-gateway implementation and the workflow still invokes `sudo ... laplace-product-activate execute-request`. That implementation is **not** accepted as the final boundary and PR #215 must not merge on the basis of that path. The PR exit condition now requires recurring delivery to be owned by `laplace-runner` with root narrowed to bootstrap/system-host operations.

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

`.github/workflows/live-product-probe.yml` performs this class of direct installed-host readback. Product activation must retain exact package, cluster, Unicode, Highway, service-enablement, and boot/readback receipts under persistent service-owned state.

## Local/customer install path

A customer/administrator installer may legitimately require administrator authority to establish machine-wide prerequisites. That is distinct from the recurring DEV/BAT CI identity. On the development/BAT host, the intended sequence is:

```text
sudo bash scripts/setup-host.sh   # once: prerequisites only
push/merge accepted main work     # thereafter: laplace-runner owns delivery
```
