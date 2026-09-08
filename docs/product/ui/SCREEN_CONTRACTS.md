# Screen and workflow contracts

Status: **draft for review**. Read [review authority and decisions](README.md) first. Route names, role labels, layout choices, and wireframes below are proposals, not an installed UI or approved visual design. Acceptance IDs resolve in [ACCEPTANCE.md](ACCEPTANCE.md).

## 1. Product navigation and common interaction

Proposed primary navigation: **Home, Converse, Explore, Workbench, Foundry, Activity, Administration**. Administration is a coherent area within Laplace, not a separate product requiring a second login. A workspace/world selector, environment and readiness indicator, global search, activity indicator, and account menu remain consistently reachable.

Administration contains **Sources & ingestion, Jobs & workers, Application & runtime, Users & workspaces, Security & integrations, Data & storage, Entitlements & usage, Audit**. These groups may share a detail workspace rather than producing a card or page for every RPC.

Converse retains durable conversations and multimodal artifacts. Explore retains entity/world browsing, structure, evidence, standing, provenance and geometry. Workbench retains query, connection, comparison, calculation and approved effect workflows. Foundry retains model-source inspection and selected-substrate target compilation as different operations. Games/chess, Knowledge Arena, software-development workflows, firmware/recipes, personal webs and federation retain their existing owners and visible navigation dispositions; admin-first sequencing does not delete them.

The default interaction is **browse -> inspect -> act -> verify**. A person must be able to discover available sources, worlds, jobs and capabilities without guessing a search term, identifier, filename, SQL query, or route. Search, filters, saved views and command search accelerate that path.

Every screen specifies its authorized query, actions, empty/loading/denied/error/stale states, deep-link behavior and post-action receipt. A visible label does not confer authority. Do not expose private object names merely to explain that a caller cannot access them.

## 2. Home: an operational cockpit

Proposed route: `/`.

Show current environment, selected workspace/world, application/engine package identities, actual readiness by subsystem, active and interrupted work visible to the caller, recent durable results, and explicit next actions. Distinguish service reachable, database ready, seed ready, capability ready, authorized, and acceptance passed. A heartbeat cannot turn every readiness indicator green.

An unseeded installation gives an authorized administrator a source/setup journey. It does not invent example knowledge or permit ordinary production content to bypass the existing clean-seed gate. Public/ordinary users see only the appropriate limited readiness message.

```text
LAPLACE     [workspace/world]     [environment]        Search     Account
Home | Converse | Explore | Workbench | Foundry | Activity | Administration

Readiness: <observed subsystem states>       Observed: <timestamp/generation>
Attention: <blocked or interrupted work with permitted recovery action>

Active work                          Recent durable results
<job / phase / measured progress>    <result / world epoch / receipt>

[Open source catalog] [Inspect runtime] [Resume selected work]
```

Angle-bracket values are specification placeholders, not mock production metrics. Acceptance: UX-01..04, OPS-01, QA-03.

## 3. Sources & ingestion: catalog and release inspection

Proposed routes: `/admin/sources` and `/admin/sources/{sourceId}`.

The catalog must represent the complete configured seed manifest and its dispositions, not a hand-written list of familiar corpora or a scan of everything under a directory. Browse by family, source, release, language/modality, selection status, artifact readiness, profile readiness, admission state, last run and active epoch. Provide server-side paging/filtering, persistent selection, and explicit selection scope across pages.

Separate source authority, source release, physical artifact, profile/recipe, job and admitted world state. Display selected, historical, superseded, equivalent packaging, excluded-with-reason, unsupported-with-why-not and absent source dispositions without converting them all into one red/green status. A new download does not silently replace the selected release.

Source detail tabs:

| Tab | Required visible material |
|---|---|
| Overview | Authority, release, license/provenance references, configured role, state and exact missing prerequisites |
| Artifacts | Complete manifest graph, sidecars/archive members, lengths/digests, acquisition location class, verification and availability |
| Profile & recipe | Accepted/candidate versions, grammar/provider identities, exact field mappings, unresolved semantics, loss/reconstruction obligations |
| Plan & run | Selected boundary, prerequisite graph, target world/epoch, resource grant, quote when applicable and confirmation |
| Coverage & readback | Denominators, disjoint dispositions, exact persisted outputs, representative reconstruction, unresolved references and proof receipts |
| History | Prior runs, release/profile changes, activation/supersession, actor, authority and immutable receipts |

```text
Sources > <source> > <selected release>          [Selection scope] [Review plan]
Overview | Artifacts | Profile & recipe | Plan & run | Coverage | History

<artifact tree/table>                   <selected artifact or field inspector>
Required / present / verified          Exact identity, provenance, disposition

Selected: <exact list/count>            Target: <workspace/world and epoch>
[Save selection] [Validate] [Preflight selected] [Run authorized plan]
```

Actions are declarative calls to the common source and execution machinery: refresh discovery, select a release, inspect artifacts, validate/qualify a profile, review candidate corrections, preflight a selected boundary, submit an ingestion, and inspect its result. They must not launch arbitrary shell strings or silently broaden a selection.

A preview/probe may perform explicitly bounded work and report its own receipt. It cannot perform a full expensive ingest, discard it, then perform it again under the label of preflight. Candidate recipes stay inactive until their required review/acceptance and activation authority succeed.

Before submission show exact selection, target, active/expected versions, read/write/effect classes, loss/unknown obligations, resource ceiling, and any charge. Confirming binds the reviewed plan identity. If input, recipe, world, entitlement or price changes, the app revalidates/requotes rather than running a different plan.

Acceptance: ING-01..04, COST-01, AUTH-09.

## 4. Ingestion/job workspace

Proposed routes: `/admin/ingestions/{jobId}` and `/activity/jobs/{jobId}`. These may be two audience-scoped entries to the same job component and query, not two job systems.

Show the actual dependency graph and current phases, admitted resource grant, worker/resource observations, attempt history, reconnect status, structured errors, bounded logs, outputs and receipts. Browser lifetime does not own the job. Navigating away, refreshing, losing a connection or signing in again must not duplicate or erase durable work.

Job state and source state are different. Proposed job states: queued, blocked, validating, running, pause-requested, paused, cancel-requested, cancelled, interrupted, succeeded, partial and failed. The accepted backend contract determines which states and transitions each operation supports. Do not offer pause/resume when no durable safe checkpoint exists. Cancellation requested is not cancellation completed; committed output is not silently erased by cancellation.

Progress reports separate units: bytes acquired/verified/read, artifacts/files/records/fields examined, canonical content reused/new, occurrences, testimony, unresolved/rejected/lossy fields, deposition, verification and activation. Unknown denominators remain unknown. A parser reaching 100% is not world admission or configured-seed completion. Each count has a defined unit, scope, source and observation time.

Retry starts an explicitly related attempt or reuses a valid durable boundary according to the common lifecycle. Idempotency prevents a double click or ambiguous network retry from producing two effective jobs. Bulk actions show exact eligible/ineligible selections and partial per-item outcomes.

A durable result links directly to source coverage, activated state where applicable, reconstructable samples, affected entities/relations and the execution receipt. Logs are helpful diagnostics, not the sole evidence of completion.

Acceptance: ING-04..08, OPS-02..03, COST-02..03, UX-05, QA-01.

## 5. Application & runtime management

Proposed route: `/admin/application`.

Provide understandable summaries with expandable technical detail for package/build/commit, loaded engine and extension identities, database/schema, active epochs, source and perfcache readiness, provider availability, service/worker state, resource grants, queue health, storage pressure, deployment history and compatibility. Observations carry timestamps and distinguish unknown, stale, degraded, failed and healthy.

Configuration is edited through validated, documented fields with effective value, source, scope, restart requirement, proposed diff and expected revision. Secrets are write-only references with rotation/test actions, not readable configuration values. Two administrators cannot unknowingly overwrite each other's changes.

Service start/stop/restart, activation, upgrade, rollback, maintenance and repair are explicit typed management operations with plan/impact, authority, resource ownership, durable receipt and recovery behavior. A browser must not receive root credentials or direct database superuser access. A management operation that stops the API serving it must retain durable status and a defined reconnect/recovery path.

Authentication/setup and nonsemantic host diagnostics must be possible before the semantic runtime is seeded. This bootstrap boundary is not permission to create a second canonical account, entitlement, source or job engine. The final implementation must classify operational/session state separately and bind later canonical execution through the common authority and receipt contracts.

Acceptance: OPS-01..06, AUTH-07..10.

## 6. Users, workspaces, data and privacy

Proposed routes: `/admin/users`, `/admin/workspaces`, `/settings/account`, `/admin/data`.

An external account mapping proves credential control. It is not the canonical person or a reason to merge people with equal names/emails. Expose linked providers, verified mapping identity, grants by scope, effective capabilities, source of grants, sessions, credentials and revocation. Show safe claim summaries and provenance, never bearer/refresh tokens.

Separate instance management, workspace administration, source operation and private data access. An instance administrator does not silently gain authority to read every private conversation, dataset or artifact. The matrix in [AUTH_AND_TRANSPORTS.md](AUTH_AND_TRANSPORTS.md) is a proposed starting point.

Data views show worlds/datasets, owners/authorized audiences, source lineage, storage and retention class, sharing grants, exports, deletion/withdrawal status and exact dependencies. Global deduplication must not leak private content existence through lookup errors, counts, suggestions, hashes, job logs, exports or caches.

Distinguish unlinking an external account, revoking access, withdrawing a witness/source from current use, deleting a private occurrence, retiring a derived cache and physically collecting unreferenced bytes. Do not promise that withdrawing one user's occurrence erases shared canonical content or immutable evidence retained under another authority. The UI must show exact affected scope and any retention constraint; privacy/erasure policy remains an explicit review decision, not a hidden implementation choice.

Destructive actions require a preview of the target, affected scope, irreversibility/recovery, dependencies and authority. Confirmation is bound to the reviewed revision; restore/export jobs obey the same access and receipt laws.

Acceptance: DATA-01..06, AUTH-02..05, OPS-04..06.

## 7. Security, integrations and developer experience

Proposed route: `/admin/security` with `/settings/developer` for a user's own clients/credentials.

Provide provider discovery/configuration/test status; allowed audiences and tenant/enrollment policy; linking and recovery; active sessions; service principals; client registrations; narrowly scoped credentials; revocation/rotation; and redacted audit history. Secret entry must explain what is stored and where, without asking ordinary users to paste operator credentials into the app.

An integration page exposes actual base URLs, auth instructions, protocol versions, supported operations/parameters, current readiness and ordinary SDK/client examples for the selected environment. Documentation and capability metadata must come from the same installed generation. A successful authenticated compatibility call links to its canonical operation, job/session and receipt.

Authentication/provider setup is an administrator installation task with a guided path. After setup an ordinary user signs in and works without SSH, SQL, environment-variable rituals, hand-edited configuration or a special debug route. Copyable developer examples use only the documented endpoint, normal credentials and standard client options.

Acceptance: INT-01..09, AUTH-01..10, QA-03.

## 8. Inspection and the real Laplace state

Explore and job outputs must support drill-through to canonical identity, exact structural content, occurrence/source, testimony, dependence, typed standing, selected calculus/recipe, world/epoch and receipt. A human label may change without reminting identity; unresolved labels have an explicit fallback and inspectable full ID.

For Unicode and geometry inspection, distinguish real four-component `physicality.coord`, packed address/ordinal/RLE trajectory payloads and realized coordinate curves. Display exact machine-readable values alongside human presentation and declare projection/loss. Composite centroid/radius semantics must not be falsely presented as every composite lying on the Tier-0 unit sphere.

An expanded workbench preserves entity/result/receipt/epoch identity, selected ordinal, filters, loaded neighbors, camera and 4D rotation state. Expansion alone neither runs cognition again nor changes the selected world. A user-requested expansion of semantic scope is a different, explicitly receipted operation.

Acceptance: DATA-04..05, UX-06..08; detailed geometry and workbench owners remain #174/#176.

## 9. Visual, accessibility and performance review

Use one typography, spacing, table, form, status, log, inspector and navigation system. Favor readable blue surfaces with clear text hierarchy; exact colors, density and typography need visual review. Do not replace information architecture with a grid of disconnected cards. Status cannot be color-only. Production screens cannot quietly include fake metrics or sample results.

Every dense table/log supports bounded rendering and an accessible nonvisual equivalent. Keyboard users can browse, inspect, select, confirm, cancel and close; focus is restored correctly. Long Unicode text, combining characters, right-to-left content, supplementary characters, long IDs and translated labels must not destroy layout. Values and log content are rendered as untrusted text unless processed by an explicitly safe renderer.

Target WCAG 2.2 AA with manual keyboard and assistive-technology review as well as automated checks. Reference: https://www.w3.org/TR/WCAG22/ . Detailed budgets and browser fixtures are proposals in acceptance, not measurements.

Required visual-review deliverables before D4: annotated Home, source catalog/detail, running/interrupted job, runtime/configuration, users/grants and data-impact views, including narrow/wide layouts and at least denied, empty and failed states. The textual layouts above do not substitute for that visual review.
