# Operator recovery and observability — design review

Status: **draft for inventor review; application implementation not authorized by this document**.

This document expands the operator workflows requested on 2026-09-07. It is a review supplement to the broader UI/auth/API design, not another product architecture, implementation queue, or claim that the console exists. Current user requirements are binding; proposed mechanisms and numerical budgets below remain explicitly reviewable. No database reset, seed run, deployment, identity-provider registration, payment, or application implementation is performed by publishing this review.

Canonical owners: [database lifecycle #264](https://github.com/SaltyPatron/Laplace-Refactor/issues/264), [ingestion cockpit #265](https://github.com/SaltyPatron/Laplace-Refactor/issues/265), [operations center #266](https://github.com/SaltyPatron/Laplace-Refactor/issues/266), [shared API/capability contract #268](https://github.com/SaltyPatron/Laplace-Refactor/issues/268), [identity/authority #64](https://github.com/SaltyPatron/Laplace-Refactor/issues/64), [product surfaces #68](https://github.com/SaltyPatron/Laplace-Refactor/issues/68). Parent #21, installed acceptance #22, semantic cohesion #70.

Supplemental scenario identifiers and explicit not-run status are in [OPERATOR_RECOVERY_REVIEW_CASES.json](OPERATOR_RECOVERY_REVIEW_CASES.json). They are acceptance specifications, not implemented tests.

## 1. The product requirement

The admin panel is how a person operates Laplace. It must support database recreation, seed selection and execution, useful progress tracking, log inspection, application management, user/data controls and recovery without assembling shell commands or GitHub Actions dispatches.

The user's SolarWinds analogy means integrated operational visibility and control: understand the installation, see dependencies and changes, identify what is broken, drill into its evidence, and execute a permitted corrective operation from that same context. It does not mean copying SolarWinds implementation or assuming any console is invulnerable.

This is not a decorative dashboard placed after the engine. Every public operation needs its corresponding usable control/readback surface while that operation is developed. Full cognition, model compilation and federation do not have to finish before an operator can initialize a database or run available seed operations. Conversely, exposing a missing action as unavailable does not satisfy the action's implementation acceptance.

The minimum user journey is:

    open installed application -> sign in -> select installation
    -> inspect database state -> recreate and seed -> follow durable operation
    -> investigate/recover a failure if present -> inspect actual admitted output

After supported installation and identity-provider setup, this journey must not require SSH, hand-written SQL, environment-variable edits, browser storage surgery, undocumented flags, manual Actions dispatch or developer assistance. A terminal remains an optional diagnostic/automation interface, never the missing implementation of the admin panel.

## 2. Information architecture

The following navigation groups are a proposed review layout. They describe tasks, not backend module names.

| Area | Main question answered | Required primary interactions |
| --- | --- | --- |
| Overview | What is running, ready, unhealthy or blocked? | Select environment/node; inspect dependency status; open failed job or permitted recovery |
| Database & recovery | What state is installed, and how do I initialize, replace or recover it? | Verify, initialize, backup, restore, recreate, repair, migrate, start/stop/restart |
| Seeds & sources | Which exact sources are selected and admitted? | Edit/review manifest; inspect artifacts/recipes; preflight; run/schedule; inspect admitted results |
| Jobs | What work is happening and what happened previously? | Filter, open stage timeline, inspect checkpoint, pause where supported, resume, cancel, retry failed scope |
| Logs & events | Why did this operation fail or stall? | Correlated tail/search, pause display, inspect context, copy/export permitted range |
| Health & alerts | Which condition needs attention? | Inspect condition/evidence; acknowledge; silence for maintenance; open recovery plan |
| Users & access | Who can do what, in which scope? | Inspect effective grants; invite/link; scoped grant/revoke; sessions/credentials; audit changes |
| Data & worlds | What is owned, shared, retained or deleted? | Browse authorized scopes; share/revoke; export; preview retention/deletion effects |
| Application | Which packages, configuration and providers are active? | Validate/test configuration; inspect diff; apply/restart; inspect loaded versus desired generation |
| Developer | How do clients use this installation? | Native API docs/examples, MCP configuration, compatibility matrix, scoped credentials, request diagnostics |

Overview, Jobs, Logs and Database are one connected working surface. Opening a failure must preserve the selected environment and time scope, and link to the same job, source, generation and receipt. Dense details expand in place under #176; expansion alone must not recalculate a semantic query.

### Always-visible context

Show environment, installation/node, selected database generation, current signed-in principal and effective role/scope. Visually distinguish production from disposable development without relying on color alone. Deep links preserve selection but never carry secrets or grant authority. Changing environment clears incompatible selected actions/confirmations and revalidates permissions.

### Required screen states

Each page must specify and render loading, ready, genuinely empty, permission denied, unsupported/unimplemented, disconnected, stale, partial, failed and recovering states where applicable. An unavailable measurement is not zero. A denied object must not leak its existence or metadata beyond the approved disclosure policy. Disabled actions explain their missing condition when that explanation is permitted.

## 3. Survive the database being managed

A panel that stores all login, job and recovery state solely in the database it recreates is not a functioning recovery interface.

The management service needs a separately durable installation-scoped recovery boundary: enrolled target identity, validated recovery grants, identity-provider configuration references, operation admission/checkpoints, essential logs, current revocation information and package/backup references. Static UI delivery and limited authenticated recovery must remain available while the selected product database is missing, stopped or replaced.

This boundary must not become a second person/entitlement/knowledge/cognition engine. Product semantics remain in the native framework/ISA/PostgreSQL owners. Management records describe physical lifecycle and observed effects. On recovery, their exact operation and native receipts are reconciled into the available product view without manufacturing testimony. Normal data/cognition access fails closed while its owner is unavailable.

The exact durable provider is an architectural review decision, not permission to silently introduce another database or anonymous recovery API. A signed, installation-scoped, limited recovery grant may support bootstrap operations without waiting for full semantic world readiness. It must remain independently revocable; missing revocation authority cannot silently broaden it. An identity-provider outage is different from a product-database outage. A separate explicitly enrolled recovery credential may be selected for the former; no universal master password or first-browser-visitor administrator.

### Retention/reset boundary

| State | Ordinary recreate-and-seed treatment |
| --- | --- |
| Selected product database generation | Replaced or explicitly destroyed according to reviewed plan |
| Canonical knowledge/user data in that generation | Impact is enumerated; retained in prior generation/backup or explicitly identified as lost |
| Perfcaches and derived indexes | Invalidated/rebuilt against the new exact generation; not accepted merely because a file remains |
| Seed-source original artifacts and manifests | Retained; not deleted by database recreation |
| Management login/IdP configuration and recovery grants | Retained in their separate lifecycle; permissions revalidated |
| Operation, attempt, audit and essential log history | Retained with old/new generation identifiers according to explicit retention policy |
| Backup artifacts and restore evidence | Retained unless a separately authorized backup-retention operation selects them |
| Other databases, unrelated directories and external systems | Out of scope; zero mutation |
| Current revocations | Remain authoritative; restoring an old backup cannot resurrect revoked access |

## 4. Database actions and recreation journey

Actions must stay semantically distinct: initialize empty, recreate empty, recreate and seed, verify, repair/reconcile, migrate/upgrade, backup, restore, rebuild derived state, start, stop and restart. Rebuilding a cache must never drop canonical content. A failed migration must never trigger an implicit reset.

### Step A — choose exact target and intent

Select an enrolled installation/database, not a free-text path or arbitrary connection string. Show actual versus configured generation and recent readback time. The plan identifies active jobs, readers/writers, affected worlds/users, disk/WAL requirements and selected package/schema/seed versions.

### Step B — plan and review

Present a human-readable sequence with exact effects and retained/deleted state. Prefer creating a replacement generation beside the current one, verifying it, and then switching explicitly. Existing data remains protected while the replacement is incomplete. Staging capacity, storage provider and generation-switch capability must be proven; if unavailable, offer a separately reviewed destructive alternative rather than silently selecting it.

Show backup policy and actual evidence: never backed up; backup created; integrity verified; restore tested; last recoverable boundary. A backup uploaded five seconds ago is not automatically restore-tested. A failed required backup prevents a protected destructive action. A disposable-development data-loss decision can be explicitly scoped to that exact target; it is not a production default or a compulsory two-person ceremony for a single-owner development install.

Preflight binds operation type, target/generation, package/configuration, manifest/recipe, data impact, resource ceiling, applicable price ceiling, confirmation expiry and actor authority. Resource estimates remain estimates. Preflight does not perform and discard the actual expensive ingestion.

### Step C — confirm and admit once

One clear destructive confirmation displays the exact database/environment and consequences. Confirmation applies to the reviewed plan, not arbitrary future bodies. A changed target, generation, loss boundary or privilege invalidates it. Reopening an already admitted job does not ask the user to reconfirm or launch it again.

Admission returns a durable operation ID. Duplicate delivery and two browser tabs use idempotency and generation concurrency checks. Idempotency is scoped to principal, operation and target; using the same key with changed intent returns conflict. An exclusive generation-fenced destructive lease prevents overlapping replacement.

### Step D — execute with visible checkpoints

A proposed stage graph is:

    verify target/grant -> backup/waiver -> drain and fence writes
    -> stage/recreate -> schema and extension installation
    -> Unicode/Highway bootstrap -> selected seed DAG
    -> reference/derived closure -> durable readback -> activation/cutover

These stages compose existing owners; they are not a shell script that duplicates engine semantics. Each stage has inputs, outcome, checkpoint, effect receipt, retryability and recovery action. The browser can close; execution belongs to the durable server job.

### Step E — verify, then use the result

The result page must read the new actual database and loaded package/extension/schema identity. It displays the configured manifest, Unicode/Highway epochs, profile closure, output counts by type, unresolved requirements and available capabilities. It links directly to selected letters' real four-component coordinates and canonical identities, a lexical/document entity, source spans and exact receipts. Coordinates are not fabricated in the frontend, and a projected glome view does not replace numerical readback.

Successful staging, successful parsing, successful worker exit and successful activation are separate facts. A required profile that has not closed keeps the requested whole-manifest operation incomplete. Partial output remains inspectable without receiving a success label.

### Failure and recovery table

| Failure boundary | Required disposition and permitted recovery |
| --- | --- |
| Preflight target/authority/capacity failure | No destructive effect; explain exact change needed |
| Required backup failure | Do not destroy protected state; inspect/retry backup |
| Drain timeout | Name blocking jobs/readers; operator chooses permitted cancellation or abort; no silent force-kill |
| Worker lost before effect | Reassign under fenced lease and durable checkpoint if replay-safe |
| Response lost after possible effect | Reconcile observed target/effect identity; do not blindly rerun reset |
| New schema/extension fails | Replacement remains inactive; show error and permitted repair/abort; old running generation stays unchanged where staging permits |
| One seed profile fails | Retain completed work and precise failed scope; retry only legal missing work |
| Verification fails | No success/cutover; show mismatched readback and restore/repair options |
| Failure after cutover | Record whether new writes occurred; switchback only if the declared consistency/data-loss conditions hold |
| Control journal unavailable | No new destructive admission; existing effect remains reconcilable, not erased or guessed |

No generic promise of exactly-once external execution is made. Acceptance requires idempotent effects where available, leases, durable receipts and explicit reconciliation for indeterminate effects.

## 5. Seed cockpit and reusable jobs

A catalog row identifies source authority, release, manifest membership, artifacts, license/secret references, provider/grammar/recipe versions, dependency readiness, acquired/admitted/active coverage, last run and exact blocker. Unsupported, absent, excluded, historical, superseded and equivalent packaging remain distinguishable.

Source detail tabs: Overview; Artifacts; Schema/recipe mapping; Dependencies; Coverage; Runs; Errors; Admitted results. Preview is a bounded declared sample, not full admission disguised as validation. Generated recipe proposals remain candidates until the existing authority accepts them.

Run one source, run a selected batch and run a manifest submit the same canonical operation. Schedules declare timezone, overlap and missed-run behavior, exact manifest-version policy and resource authority. Refreshing discovery does not silently change an approved release or download unselected artifacts.

Pause/resume appears only when the selected provider has a real safe checkpoint. Cancel-requested is not cancelled. Retry-failed and new-run are different actions. Active work respects one resource grant and genuine dependency frontiers; source-family ordering must not become an artificial global waterfall.

For every stage, show input unit, processed numerator, known denominator or indeterminate state, accepted/reused/rejected/quarantined/unresolved/loss dispositions, durable output and last useful progress. Do not mix bytes, records and entities into one percentage. A source with zero semantic attestations may still have valid structural observations; zero testimony is not automatically failed ingestion.

## 6. Jobs, logs and causal inspection

The durable operation is distinct from HTTP request, worker attempt and child stage. Job detail binds all of them to principal/scope, node, target generation, manifest, program/recipe, resource ceiling, events, outputs and receipt.

Timeline states come from the shared lifecycle owner. The UI must distinguish waiting, running, pausing, paused, cancelling, cancelled, failed, partial, succeeded and indeterminate/reconciling without inventing route-local meanings. The first terminal failure survives a later cancellation.

Show last heartbeat separately from last useful progress. A responsive worker blocked on a missing provider is not making ingestion progress. A silent worker is not necessarily proven dead until the lease/health policy says so. Event sequence and dependency identity establish ordering; unsynchronized wall clocks do not invent causality.

Logs support live tail, pause-display, severity/component/node/source/stage/attempt/time/text filters, context before/after, selection/copy and authorized redacted export. Scrolling up pauses follow; new events do not steal focus or move the viewport. Filter and expansion changes preserve job identity. Logs and long lists are bounded and paginated/virtualized with accessible semantics; no full-world scan to render a dashboard.

Each error opens a plain description, exact failing stage/artifact, current durable boundary, allowable next action, and raw diagnostic details. Expected operators should not have to reconstruct the failure from thousands of lines.

Reconnect uses a durable cursor and snapshot boundary. Replayed duplicate events cannot double counters. Retention gaps trigger an explicit new snapshot with a visible gap. Dropped, sampled, redacted, truncated, unavailable and empty are separate log conditions.

## 7. Health, alerts and runtime changes

Overview should display a dependency map from entry points through services/workers to database, native package, storage and selected providers. Distinguish desired configuration, installed artifact and actually loaded generation. Health checks report time, observation source and expiry. Process alive, API responsive, database reachable, seed complete and a particular cognition capability ready must never share one undifferentiated green indicator.

Alert rules cover at least disk/WAL pressure, worker loss, useful-progress stall, seed failure, provider/authentication expiry, failed backup/restore verification, package mismatch and unhealthy entry point. Each alert includes scope, severity, first/last evidence, condition/version, affected jobs, acknowledgement, silence interval and resolution. Acknowledging changes operator workflow, not system health. Notifications are optional providers; local operation cannot require an external subscription.

Application management uses typed operations for validate/apply configuration, connection checks, credential rotation, restart, package staging/activation, rollback and support bundles. Show proposed diff and restart/maintenance impact; concurrent edits conflict rather than overwrite. Unsaved edits are preserved safely or explicitly discarded by the user. Secrets are write-only references with status/rotation, not casually readable config values. Monitoring configuration cannot open arbitrary shell execution.

## 8. Access and blast-radius boundaries

These are proposed control profiles for review, not identity claims supplied by the IdP:

| Profile | Allowed scope | Not implied |
| --- | --- | --- |
| Viewer | Authorized health, job and redacted log reads | Read all private data or mutate anything |
| Seed operator | Selected sources/worlds, resource-bounded run/cancel/resume | Reset database, grant roles, change host policy |
| Database administrator | Explicit enrolled database lifecycle scope | Arbitrary host shell, unrelated database deletion |
| Access administrator | Scoped account/session/grant administration | Automatic knowledge-content access or unrestricted execution |
| Installation owner | Enroll targets, define recovery/configuration policy | Bypass generation, confirmation, audit or resource contracts |
| Service principal | Explicit operation/resource scopes and expiry | Inherit all permissions of the human who created it |

A single-owner installation can combine profiles explicitly. Enterprise separation is configurable, not a compulsory obstacle to ordinary local work.

Microsoft organizational and personal accounts, and separately configured OIDC providers, should enter through #64's validated authentication and account-mapping route. SSO proves control of a credential, not an administrator role. Do not merge people on equal email, accept arbitrary issuers, use caller-provided tenant headers as authority, or treat an ID token as a general API access token. OAuth security requirements are grounded in [RFC 9700](https://www.rfc-editor.org/rfc/rfc9700.html); Microsoft account support is documented in [Microsoft OIDC](https://learn.microsoft.com/en-us/entra/identity-platform/v2-protocols-oidc). Provider registrations/redirects/secrets remain actual deployment configuration; a setup wizard should make them inspectable and testable.

Use authenticated, least-privilege service boundaries. Public inference credentials cannot invoke destructive administration. A management worker receives typed allowlisted operations against enrolled targets, not arbitrary commands, file paths or URLs. Source-fetch jobs need explicit egress/SSRF and archive/path boundaries; parsing untrusted data cannot trigger provider installation or execute code. UI source labels/logs require inert rendering and injection controls.

Supply-chain acceptance must include verified artifact identity/provenance, isolated build/release credentials, reviewable updates, least-privilege runtime identities, secret redaction and observed loaded-generation checks. A valid signature alone is not a guarantee of a trustworthy build. Backups and audit retention must not rely only on the same writable target being replaced. This reduces impact; it is not a claim that a compromised console or host is harmless.

## 9. Measurable UX and load review proposals

The following values are proposed acceptance budgets, not measured performance or unilateral inventor-approved limits. They must be accepted or replaced with explicit values before the affected implementation is called complete.

| Dimension | Proposed review target and measurement |
| --- | --- |
| Operator control response | p95 at most 1 second for authenticated local/LAN status, admission acknowledgement and cancellation acknowledgement; measure at least 200 samples under declared concurrent ingest |
| Progress freshness | New useful-progress event visible within 2 seconds p95 under normal connectivity; stale after three missed declared heartbeat intervals |
| UI interaction | Filter/selection/expand interaction feedback within 200 ms p95 on pinned client/browser fixture; underlying work may remain pending |
| Long history | 100,000 retained job records and 1,000,000 log events in test store; bounded page/stream behavior, not rendering every row |
| Isolation | One heavy permitted ingestion plus four monitoring clients cannot consume the reserved control capacity or starve cancellation/status |
| Log retention | Propose 30 days searchable operation metadata, 7 days detailed logs, separately configured backups/audit retention; show storage cost and purge preview; never silently purge unexpired evidence |
| Accessibility | WCAG 2.2 AA target; keyboard-only primary journey, screen-reader labels/status, visible focus, zoom/reflow, non-color state and reduced motion |
| Display coverage | Desktop 1440x900, narrow 390x844 and 200% zoom fixtures; wide dense tables may scroll intentionally, not hide controls |

A responsive acknowledgement is not proof that cancellation completed. Report action effect latency separately under the relevant provider's checkpoint/cancellation contract. A performance failure requires repair or explicit review of the budget; it cannot be hidden by weakening a test after the result.

Accessibility reference: [W3C WCAG 2.2](https://www.w3.org/TR/WCAG22/). Automated checks alone do not establish the whole accessibility target.

## 10. How UI grows with the app

For each public user/operator capability, #268's one descriptor binds typed inputs/results, scope, readiness, effect class, validation, progress, legal actions, errors, receipts and documentation. Purpose-built screens compose reusable components around those contracts. Generating a form is not, by itself, a usable workflow; every operation needs a discoverable place and meaningful result/recovery view.

An implementation change includes its applicable native ownership, binding, API, UI, permission, job/log/receipt presentation, docs and positive/negative acceptance together. An internal-only operation has an explicit reason for no direct UI action. Missing product controls are not deferred to a later cosmetic pass.

There must be one command owner, not separate implementations for browser, HTTP, MCP and OpenAI-compatible requests. Protocols expose only appropriate supported operations: an ordinary chat completion is not a database-reset interface. Native HTTP admin operations are independently scoped; MCP administrative tools require explicit enrollment and the same confirmation envelope. Compatibility claims use a pinned field/feature/client matrix rather than promising every provider feature.

## 11. Review and delivery evidence

The companion cases require real installed-browser and independent read/effect evidence. Synthetic fixtures may test failure handling and deterministic boundaries, but fabricated UI counters, mocked success APIs and screenshots alone do not close a workflow. No test has run merely because its scenario is written here.

Before implementation review settles: navigation/action wording; reset/staging defaults; recovery storage/provider; enrollment and grant profiles; protected versus disposable backup policy; retention; latency/load/accessibility budgets; exact seed-manifest fixtures; and cross-route exposure. These decisions refine mechanisms and defaults, not whether the requested operator capabilities exist.

Before delivery, each case binds requirement, issue, actual test location, accepted source/package generation, environment, fixture, result, readback, observed effects and durable evidence. Missing tests/results remain not implemented/not run. Browser success with incorrect database effects fails; correct backend output behind an unusable workflow also fails.

## 12. Reconciliation record

During concurrent intake #267 overlapped #264/#265 and #269 overlapped #266. Their full requirements remain retained as linked acceptance evidence; they were closed as duplicates, not completed implementations. Canonical issues received explicit references to their additional positive/recovery/counterexample cases. Do not reopen parallel implementation owners for the same boundary.

Historical evidence used only as counterexamples: [old Laplace #1012](https://github.com/SaltyPatron/Laplace/issues/1012), operator endpoints without usable authenticated administration; [old Laplace #609](https://github.com/SaltyPatron/Laplace/issues/609), a pathological receipt query and retries cascading across the UI. Existing #172/#174/#176 preserve panel sizing, projection correctness and context-preserving inspection. No legacy code/schema/layout is imported by this review.
