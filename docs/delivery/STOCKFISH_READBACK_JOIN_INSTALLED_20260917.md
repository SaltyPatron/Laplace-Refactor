[Main run 35257154254](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35257154254)
completed successfully on September 17, 2026. The installed source is
`32f65922abfe358b9d690645781af676e8a8f575`, tree
`54f5da72b2cd067925f2f4d2f1187eb77036dbed`, package
`c2e6432f85c72d390b2cfd3cc343106f4fe7a4d34238b48e2cda2bebbc9758a6`.
The [machine-readable receipt](STOCKFISH_READBACK_JOIN_INSTALLED_20260917.json)
retains the complete observed acceptance result, command identities and service projections.

The native readback joins every canonical witness for the selected profile with
every execution witness for the selected receipt using a full join on profile,
artifact and span. Unmatched rows remain visible to the existing rejection
logic, including a same-receipt row bearing the wrong profile. This replaces the
stale-statistics left-join plan that could perform excessive work; no witness,
content, byte, hash or provenance comparison was removed.

Actual normal-main qualification passed 48 custom-stack core tests in 242.841625
seconds and the verified C++/physicality boundary in 191.029021 seconds.
The retained structural execution receipt explicitly reports all 18 corruption
controls and two schema controls passing, including missing execution, missing
canonical and extra wrong-profile execution rows. It preserves historical rows,
receipts and observations and verifies zero current-provider replay growth.
The same build also includes the explicit unsigned-byte comparator for Unicode
payload canonicalization; its three actual hosted tests and deliberate-mutant
qualification are retained separately.

The unchanged official Stockfish input is commit
`edb0d9db6731067ec50ce619ff372b463bc4dd5d`, tree
`418af042b3c0aade628c1c98f13942659e67e64d`, with manifest SHA-256
`2346d3970f5ae3619b7ad69bbc80939c816bb14af94249be5c20a44c5ac33adc`.
The current source profile remains
`c6ec7000cd7d923361f0f2505c35852cc78c21205598af726706e7a3ef25ddab`.
The selected grammar was not changed by this installation.

| Actual installed observation | Result |
| --- | --- |
| Files / source bytes / C++ files | 119 / 1,172,144 / 72 |
| Canonical structural witnesses | 571,627 |
| Current source-occurrence rows | 239 |
| Retained execution-observation rows / v4 receipts | 1,143,254 / 2 |
| Current installed grammar diagnostics | 95, retained in the separate current-profile coordinate audit |
| Admission plus complete exact readback | 57.821330 seconds |
| Identical-provider repeat plus complete exact readback | 57.228730 seconds |
| Grammar qualification | 6.760262 seconds |
| Repeat Entity / Physicality / Attestation deltas | 0 / 0 / 0 |
| Repeat source-occurrence / canonical-witness deltas | 0 / 0 |
| Repeat execution-observation / execution-receipt deltas | 0 / 0 |

Both complete JSON outputs are byte-identical, SHA-256
`d00a2ba185d9f2f8e519863fd32626d5e66b283eebdcf23852dccc161c3e75d0`.
All 119 source files reconstruct to the expected bytes and SHA-256. The two
retained execution receipts represent distinct earlier provider observations;
this repeat added neither observations nor receipts. The initial cross-package
write delta was not measured, so zero amplification applies to the measured
repeat. Actual final counts are 1,172,710 Entities, 1,231,471 Physicalities and
1,114,927 Attestations; these are different denominators.

The current execution receipt is
`3b997679354ebbf2d9766f72b99d62ea4a0a37ea01790beb080b0ea602e1094c`;
its preserved historical v3 receipt is
`62420b53fdd162b11feebe07427f1e13dccefc6951c8fc9a171edd061bf58ca7`.
The canonical witness fingerprint is
`ba472cecbaafd886e2a2d604b29d79e0b91faf4c48747baf9fa8cbb81ed6144b`
and execution witness fingerprint is
`d593f2d56d66d521d5a0888fde35dbb3a1313368f6e983062e4fa8a52989f3dd`.

[Authenticated reader 35259527992](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35259527992)
verified the exact source artifact
[10514425502](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35257154254/artifacts/10514425502),
3,010,349 bytes, ZIP SHA-256
`cb3fe07f41392eff98238400da489cf88caf70f6e7ba4f67f1d56f001bb2bc0b`,
and matching service artifacts. The corpus command finished at 18:31:39.855 UTC;
the final physical job completed at 18:31:46 UTC. Historical failed readback and
the [preceding installed replay](STOCKFISH_INSTALLED_REPLAY_20260917.md) remain
separate evidence.

The same package's `laplace-refactor-cognition.service` is enabled and
active/running in the laplace-runner user manager. Authenticated health identifies
this package and reports Unicode and Highway ready. Anonymous protected health
and MCP return HTTP 401; operator-bearer health returns HTTP 200 and the stream
returns a complete authenticated package-bound product-snapshot event.
Native cognition, OpenAI streaming/nonstreaming and MCP passed. Warm restart to
verified health was 0.866559 seconds, with no cold-boot claim.
The [surface guide](COGNITION_SURFACE.md) gives the existing
https://hart-server:8443/refactor/ entry point and separates the preceding
package-bound server-local TLS observation from this package's loopback proof.
A remote browser was not tested.

These complete CLI timings are not isolated insertion rates, chess-game
recording rates, an uncontended capacity sweep or a controlled speedup comparison.
The persisted 95 syntax diagnostics remain unresolved. The separately
[qualified GNU-alias experiment](STOCKFISH_GNU_ALIAS_QUALIFICATION_20260917.md)
reduced its native candidate count to 71 but was not installed or admitted.
Exact byte reconstruction and replay do not establish C++ preprocessing,
name/type resolution, executable Stockfish semantics or chess strength.
