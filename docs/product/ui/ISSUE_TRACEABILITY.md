# Review issue traceability and concurrent supplements

Status: **draft review linkage, not implementation or approval**. This supplements [the packet index](README.md) after newer issue detail appeared during the same review. Broad review vehicle: #272. Existing product parents: #68/#21.

Do not create competing implementations, parallel authority models, or duplicate feature epics from these documents. The parent-level ownership in the index remains valid; the narrower issues below carry operator/protocol-specific work. Their existing detail is preserved, not replaced by the shorter acceptance matrix here.

## Owning issues and criteria

| Existing issue | Review boundary | Local review criteria / retained obligations |
|---|---|---|
| #68 | Product navigation, inspection, complete capability inventory, visual and interaction review | UX-01..08, EVO-01..03; preserve #172/#174/#176 |
| #64 | SSO, external account mapping, sessions/credentials, grants, recovery and disclosure | AUTH-01..10, DATA-01..03; real Microsoft personal/work-school and other configured provider acceptance |
| #264 | Database/runtime lifecycle and management that survives an absent/recreated product database | OPS-01, OPS-03..06, QA-01/03; retain its reset/restore, retained-state and recovery cases |
| #265 | Administrator source catalog, selected manifests, profiles, scheduling, ingestion and exact readback | ING-01..08, COST-01..03; native source owners #53/#112/#115/#195/#223 remain unchanged |
| #266 | Durable jobs/attempts, resource observations, logs, dependency health and alerts | ING-04..07, OPS-01..03/06, UX-05, QA-05; retain fenced leases, event-gap, alert and failure-history requirements |
| #268 | Native HTTP, generated operation/capability contracts and UI/backend co-development | INT-01/02/08/09, EVO-01..03; shared engine/binding/lifecycle owners #5/#10/#58 remain unchanged |
| #270 | MCP wire protocol, authorization/discovery and canonical operation exposure | INT-03/04 plus applicable AUTH criteria; exact revision and client profiles must be reconciled before implementation |
| #271 | OpenAI-compatible wire profiles and developer experience | INT-05..09 plus applicable AUTH criteria; cognition/discourse remains #18-owned |
| #62 | Audience-authorized worlds, user/data views and data lifecycle | DATA-01..06 with #64; instance management is not automatic private-content disclosure |
| #145 | Preflight/actual-cost and entitlement visibility | COST-01..03; the UI consumes the existing economic and resource contracts |
| #22 / #54 | Installed acceptance and change-sensitive proof | QA-01..05 plus applicable criteria; review prose and screenshots alone are not execution evidence |

#267 and #269 were marked as reconciled into #264/#265/#266 by concurrent review work. Their historical acceptance details remain linked evidence, not completed implementation and not permission to delete unique requirements.

## Operator recovery and observability supplement

Draft PR #273 contains focused review material at commit `90ee1b49ebca168c0577a1a1ca5f86e4fe8ab8c4`:

- [Operator recovery and observability review](https://github.com/SaltyPatron/Laplace-Refactor/blob/90ee1b49ebca168c0577a1a1ca5f86e4fe8ab8c4/docs/product/OPERATOR_RECOVERY_AND_OBSERVABILITY_REVIEW.md)
- [Operator recovery review cases](https://github.com/SaltyPatron/Laplace-Refactor/blob/90ee1b49ebca168c0577a1a1ca5f86e4fe8ab8c4/docs/product/OPERATOR_RECOVERY_REVIEW_CASES.json)

This packet links that focused work rather than overwriting it or declaring it accepted. Its database-recreation blast-radius, retained-state, database-independent management, durable logging and per-stage recovery obligations must survive any consolidation of the review documents. #272 supplies broader navigation, SSO, user/data and transport acceptance; #273 supplies additional focused operator scenarios. Neither PR constitutes permission to perform a reset, seed run, provider registration or deployment.

## Reconciliation required before implementation

1. Select one reviewed permissions matrix. Proposed role names differ between draft documents; they are alternatives for review, not parallel canonical account types or automatic combined grants.
2. Reconcile overlapping acceptance IDs by document namespace. `OPS-01` in this packet is not automatically the same case as a similarly named case in another document. Later executable test IDs must have an unambiguous source and owner.
3. Reconcile database-independent recovery access with #64's current-grant and restored-backup rules. A down database cannot make every endpoint anonymous or restore revoked authority.
4. Pin the precise MCP revision and verify its actual transport/session lifecycle. Language inherited from older protocol revisions is not authority to invent a session handshake in a newer profile; durable application jobs/conversations remain separately modeled.
5. Preserve #265's schedule requirements: explicit timezone, overlap/concurrency, missed-run handling and pinned manifest/recipe selection. These supplement the interactive ingestion journey rather than creating a different ingestion engine.
6. Preserve #266's alert lifecycle and telemetry boundaries; acknowledgement is not recovery and a monitoring alert does not authorize an unrequested destructive action.
7. Record the approved visual designs, interaction scope, identity-provider setup, compatibility floor, retention/recovery policy and performance fixtures once. One approved revision must supersede draft alternatives explicitly.

No checkbox is completed by this linkage. Application implementation remains outside the current task until the inventor finishes the UI/acceptance review and explicitly approves that scope.
