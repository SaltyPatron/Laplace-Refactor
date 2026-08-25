# Self-hosted runner receipt

Observed: 2026-08-25 UTC

## Registration and package

- Repository: `SaltyPatron/Laplace-Refactor`
- Runner ID/name: `2` / `hart-server-refactor`
- Runner group: `Default`
- OS/architecture: Linux/X64
- Package: GitHub Actions runner 2.336.0
- Package SHA-256:
  `04cf0be1aff4c3ec3554466c39124ca250e3effd8873bb7e8d68535aa9505d5d`
- Package identity lock: `dependencies/artifact-lock.json`

Registration tokens and credential files are intentionally excluded. GitHub API
observation reported the runner online and idle after the first successful custom
workflow.

## Service and storage

- Service user: `laplace-runner`
- Service unit:
  `actions.runner.SaltyPatron-Laplace-Refactor.hart-server-refactor.service`
- Service state observed: active/running
- Runner root:
  `/var/lib/agents/laplace-runner/actions-runner-refactor`
- Work root:
  `/var/lib/agents/laplace-runner/actions-runner-refactor-work`
- Runner-root ownership/mode: `laplace-runner:laplace-runner`, `2750`
- Work-root ownership/mode: `laplace-runner:laplace-runner`, `2770`

`/var/lib/agents` is the agent workspace. `/opt/laplace` remains the declared
dependency, build, log, artifact, install, and other product-output root. Neither is a
Git worktree output directory.

## Advertised and probed capabilities

The registered labels are:

```text
self-hosted Linux X64 laplace-refactor oneapi postgres-18 dotnet-10 avx2
```

The custom workflow verifies on every run:

- execution as `laplace-runner` and a writable isolated runner temporary directory;
- custom PostgreSQL major version 18 through
  `/opt/laplace/pgsql-18/bin/pg_config`;
- locked BLAKE3 and GoogleTest source roots;
- Intel oneAPI `icx` through `/opt/intel/oneapi/compiler/latest/bin/icx`;
- .NET SDK major version 10; and
- AVX2 in the executing host CPU flags.

The workflow currently compiles the PostgreSQL integration profile with GCC. Presence
of the oneAPI toolchain is not evidence that TBB/MKL resource grants or the optimized
IntelLLVM profile are complete; those remain under issues #3 and #4.

## Handoff evidence

The first two custom runs exposed and preserved infrastructure defects:

1. run `32812590947` rejected shared dependency roots as dubiously owned;
2. run `32813192228` built the project and passed 89 of 91 tests, then exposed the
   PostgreSQL Unix-socket path limit under the correctly relocated long runner path.

Run `32813599200` on commit `38b4385` passed dependency verification, configuration,
out-of-tree build, all 91 PostgreSQL-profile tests, and registry verification under
the service identity. The short Unix socket is transport only; retained test evidence
remains beneath the runner temporary workspace.
