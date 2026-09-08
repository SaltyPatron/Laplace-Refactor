# Review issue traceability and consolidated operator specification

Status: **draft review linkage, not implementation or approval**. Canonical review vehicle: #272, branch `docs/68-ui-admin-acceptance-review`. Product parents: #68/#21. Start with [the packet index](README.md).

The broader UI review and focused database-recovery review are now in this one branch. Do not create competing implementations, parallel authority models, or duplicate feature epics. The narrower issues below own operator/protocol boundaries; existing native semantic owners are unchanged.

## Owning issues and criteria

| Existing issue | Review boundary | Local review criteria / retained obligations |
|---|---|---|
| #68 | Product navigation, inspection, complete capability inventory, visual and interaction review | UX-01..08, EVO-01..03; preserve #172/#174/#176 |
| #64 | SSO, external account mapping, sessions/credentials, grants, recovery and disclosure | AUTH-01..10, DATA-01..03; real Microsoft personal/work-school and other configured provider acceptance |
| #264 | Database/runtime lifecycle and management that survives an absent/recreated product database | OPS-01, OPS-03..06, QA-01/03; OC-01..10 and OC-30; retain reset/restore, retained-state and recovery cases |
| #265 | Administrator source catalog, selected manifests, profiles, scheduling, ingestion and exact readback | ING-01..08, COST-01..03; OC-12..16; native source owners #53/#112/#115/#195/#223 remain unchanged |
| #266 | Durable jobs/attempts, resource observations, logs, dependency health and alerts | ING-04..07, OPS-01..03/06, UX-05, QA-05; OC-17..25; retain fenced leases, event-gap, alert and failure-history requirements |
| #268 | Native HTTP, generated operation/capability contracts and UI/backend co-development | INT-01/02/08/09, EVO-01..03, OC-29; shared engine/binding/lifecycle owners #5/#10/#58 remain unchanged |
| #270 | MCP wire protocol, authorization/discovery and canonical operation exposure | INT-03/04 plus applicable AUTH criteria; exact revision and client profiles must be reconciled before implementation |
| #271 | OpenAI-compatible wire profiles and developer experience | INT-05..09 plus applicable AUTH criteria; cognition/discourse remains #18-owned |
| #62 | Audience-authorized worlds, user/data views and data lifecycle | DATA-01..06 with #64; instance management is not automatic private-content disclosure |
| #145 | Preflight/actual-cost and entitlement visibility | COST-01..03; the UI consumes existing economic and resource contracts |
| #22 / #54 | Installed acceptance and change-sensitive proof | QA-01..05 plus applicable criteria; review prose and screenshots alone are not execution evidence |

OC-11/26/27 additionally belong to #64; OC-28 belongs to #68. The two matrices overlap intentionally: 58 broad criteria and 30 focused cases are not a claim of 88 distinct implemented tests. All remain unexecuted.

#267 and #269 were reconciled into #264/#265/#266. Their historical acceptance details remain linked evidence, not completed implementation and not permission to delete unique requirements.

## Operator recovery and observability — absorbed into this review

The two focused files from PR #273 at commit `90ee1b49ebca168c0577a1a1ca5f86e4fe8ab8c4` are preserved byte-for-byte in this branch:

- [Operator recovery and observability review](../OPERATOR_RECOVERY_AND_OBSERVABILITY_REVIEW.md)
- [Thirty operator recovery review cases](../OPERATOR_RECOVERY_REVIEW_CASES.json)

Original blob identities are `b4ffa9d3f419f98c2bbad2525cedaabf3bce9bb9` and `e981e217e5e352457db3a6d7aa025b881c914e6c`, respectively. Consolidation preserves the source commit as an additional parent; it does not silently rewrite or discard unique acceptance. #273 is an absorbed historical review vehicle, not a second implementation queue and not a completed feature.

The focused material adds database-recreation impact and retention, staged replacement versus explicit in-place reset, database-independent management, backup versus restore verification, per-stage recovery, useful-progress versus heartbeat, correlated logging and alert lifecycle. These obligations are in the same review as navigation, SSO, user/data controls and public transports. Neither publication nor consolidation permits a reset, seed run, provider registration or deployment.

## Reconciliation required before implementation

1. Select one reviewed permissions matrix. Proposed role names differ between documents; they are alternatives, not parallel canonical account types or automatic combined grants.
2. Preserve acceptance namespaces. `OPS-01` in the broad matrix and `OC-01` in the focused cases identify different declarations. Later executable test IDs must have an unambiguous source, owner and evidence class.
3. Resolve database-independent recovery access with #64's current-grant and restored-backup rules. A down database cannot make every endpoint anonymous or restore revoked authority. Whole control-host/storage failure is a different disaster-recovery boundary; same-host placement cannot promise availability after loss of that host.
4. Pin the precise MCP revision and its actual transport lifecycle. The published 2026-07-28 Streamable HTTP profile removed protocol sessions and GET event streams, uses per-request metadata and POST-scoped responses, and treats closing a response stream as request cancellation. Durable Laplace jobs/conversations are separate application state. A durable job is explicitly admitted and returned by identity; monitoring that job must not invent a resumable MCP connection/session. Older-client behavior is a separately tested profile. Reference: https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http .
5. Preserve #265's schedule requirements: explicit timezone, overlap/concurrency, missed-run handling and pinned manifest/recipe selection. These supplement interactive ingestion rather than creating a different engine.
6. Preserve #266's alert and telemetry boundaries; acknowledgement is not recovery and a monitoring alert does not authorize an unrequested destructive action.
7. Choose the exact reset/staging/cutover policy and retained-state table, including user mappings and personal data actually stored in the target. State not held outside the target needs an explicit export/restore or disposable-loss decision; it cannot be assumed retained. Restore reconciles surviving revocation/deletion restrictions before traffic reopens.
8. Resolve proposed numeric budgets and retention by one recorded decision. The documents' measurement profiles are alternatives for review, not simultaneous approved limits. Signature-valid software is not automatically authorized or safe: release approval, provenance, isolated credentials, constrained runtime/node authority and rollback/revocation need explicit acceptance alongside corrupted-artifact checks.
9. Record approved visual designs, interaction scope, identity-provider setup, compatibility floor, recovery/retention policy and performance fixtures once. One approved revision must supersede draft alternatives explicitly. Textual screen contracts are not visual approval.

No checkbox is completed by this linkage. Application implementation remains outside the current task until the inventor finishes the UI/acceptance review and explicitly approves that scope.
