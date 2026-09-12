# DEV/BAT recovery acceptance

This recovery is not complete until the accepted `main` generation has been activated on the self-hosted DEV/BAT machine and the installed product proves all of the following through the persistent runtime path:

1. `/opt/laplace/current` and `/opt/laplace/runtime/refactor` select the same physical release generation.
2. The packaged PostgreSQL 18.6 instance is running from that selected generation as `laplace-runner`.
3. The application database is `laplace_refactor` and the public readback role is `laplace_app`.
4. Unicode and Highway activation evidence is retained under the selected package's canonical `cluster-activation/<package-id>` receipt generation.
5. `laplace-live-substrate 'Aé中Ω'` reads the persisted Unicode identities and S3 coordinates from PostgreSQL rather than fixtures or a build workspace.
6. The installed cognition service and client execute from `/opt/laplace/runtime/refactor/bin` and retain an installed cognition proof for the selected package.

A green hosted build or package composition by itself does not satisfy this acceptance boundary.
