# Screen and workflow contracts

Status: **draft for review**. Read [review authority and decisions](README.md) first. Route names, role labels, layout choices, and wireframes below are proposals, not an installed UI or approved visual design. Acceptance IDs resolve in [ACCEPTANCE.md](ACCEPTANCE.md).

## 1. Product navigation and common interaction

Proposed primary navigation: **Home, Converse, Explore, Workbench, Foundry, Activity, Administration**. Administration is a coherent area within Laplace, not a separate product requiring a second login. A workspace/world selector, environment and readiness indicator, global search, activity indicator, and account menu remain consistently reachable.

Administration contains **Sources & ingestion, Jobs & workers, Application & runtime, Users & workspaces, Security & integrations, Data & storage, Entitlements & usage, Audit**. These groups may share a detail workspace rather than producing a card or page for every RPC.

Converse retains durable conversations and multimodal artifacts. Explore retains entity/world browsing, structure, evidence, standing, provenance and geometry. Workbench retains query, connection, comparison, calculation and approved effect workflows. Foundry retains model-source inspection and selected-substrate target compilation as different operations. Games/chess, Knowledge Arena, software-development workflows, firmware/recipes, personal webs and federation retain their existing owners and visible navigation dispositions; admin-first sequencing does not delete them.

The default interaction is **browse -> inspect -> act -> verify**. A person must be able to discover available sources, worlds, jobs and capabilities without guessing a search term, identifier, filename, SQL query, or route. Search, filters, saved views and command search accelerate that path.

**Direct scope clarification: this applies to everything in Laplace, not just Unicode.** Section 10 makes collection-first browsing, schema/result-driven controls, multiple selection, comparison and linked inspection cross-cutting product behavior. Unicode is one example; it is not the completeness boundary. Rich graph, glome, board, document, media and other viewers plug into that workspace rather than becoming single-item search islands. See [data-browser contracts](DATA_BROWSER_AND_SOURCE_LINKS.md) and the user's [SpecEditor/CIEDigital engineering references](SCHEMA_DRIVEN_INTERFACE_REFERENCES.md).

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

Acceptance: INT-01..09, AUTH-01..10.

## 8. Inspection and the real Laplace state

Explore and job outputs must support drill-through to canonical identity, exact structural content, occurrence/source, testimony, dependence, typed standing, selected calculus/recipe, world/epoch and receipt. A human label may change without reminting identity; unresolved labels have an explicit fallback and inspectable full ID.

For every applicable content type, distinguish real four-component `physicality.coord`, packed address/ordinal/RLE trajectory payloads and realized coordinate curves. Display exact machine-readable values alongside human presentation and declare projection/loss. Composite centroid/radius semantics must not be falsely presented as every composite lying on the Tier-0 unit sphere. Unicode is an entry example, not the only consumer of these controls. Words, documents, code, game trajectories, media structure and other admitted compositions use the same typed physicality inspection.

An expanded workbench preserves entity/result/receipt/epoch identity, selected ordinal, filters, loaded neighbors, camera and 4D rotation state. Expansion alone neither runs cognition again nor changes the selected world. A user-requested expansion of semantic scope is a different, explicitly receipted operation.

Acceptance: DATA-04..05, UX-06..08; detailed geometry and workbench owners remain #174/#176.

## 9. Visual, accessibility and performance review

Use one typography, spacing, table, form, status, log, inspector and navigation system. Favor readable blue surfaces with clear text hierarchy; exact colors, density and typography need visual review. Do not replace information architecture with a grid of disconnected cards. Status cannot be color-only. Production screens cannot quietly include fake metrics or sample results.

Every dense table/log supports bounded rendering and an accessible nonvisual equivalent. Keyboard users can browse, inspect, select, confirm, cancel and close; focus is restored correctly. Long Unicode text, combining characters, right-to-left content, supplementary characters, long IDs and translated labels must not destroy layout. Values and log content are rendered as untrusted text unless processed by an explicitly safe renderer.

Target WCAG 2.2 AA with manual keyboard and assistive-technology review as well as automated checks. Reference: https://www.w3.org/TR/WCAG22/ . Detailed budgets and browser fixtures are proposals in acceptance, not measurements.

Required visual-review deliverables before D4: annotated Home, source catalog/detail, running/interrupted job, runtime/configuration, users/grants and data-impact views, including narrow/wide layouts and at least denied, empty and failed states. Add the collection/selection/compare workspace and its graph, glome and chess integrations from section 10. The textual layouts above do not substitute for visual approval.

## 10. Universal collection workspace: every data type and capability

### 10.1 Scope and the reusable unit

The inventor explicitly corrected the scope to **everything**, not just Unicode. Preserve the liked graph/glome interactions shown in the two supplied legacy screenshots and the existing chess capabilities, while removing dependence on isolated single-item searches. This is a functional scope requirement, not an instruction to copy the old implementation, hide everything behind a generic JSON viewer, or make every domain look identical.

The reusable unit is:

    collection or typed result
      -> data/schema-driven fields, filters and ordering
      -> result set and persistent selection
      -> detail / compare / compatible visualization / applicable action
      -> linked collection or result
      -> the same workspace again

Individual record pages are one useful state of that workspace, not its only entrance. Lists, related lists, joined projections, grouped results and calculation outputs all describe their own available fields. A zero-row result retains its schema; a heterogeneous result exposes its union/variants and explicit not-applicable fields rather than guessing its schema from the first row.

The effective descriptor combines authorized installed storage metadata, generated native record/operation/type metadata and the selected result schema. A supported new field must not require a new page, endpoint, handwritten DTO or custom filter. A genuinely new semantic type needs one reusable provider/renderer integration, not separate integrations for each source. Discovery is automatic where metadata already supplies meaning; ordinary users configure views, not the backend's semantic registry.

SpecEditor demonstrates physical schema discovery and dependent controls. CIEDigital demonstrates reflected model-property filters, polymorphic editor templates and generic query expressions; these are complementary, positive references selected by the inventor. Their precise source paths and protection mechanisms are recorded in [SCHEMA_DRIVEN_INTERFACE_REFERENCES.md](SCHEMA_DRIVEN_INTERFACE_REFERENCES.md). The public repositories were inspected as source, not built or launched here.

### 10.2 Workspace behavior

A persistent collection selector and filter/sort/field bar stays available beside the active viewer. The working selection is independent of the focused record: one record may be open while many records stay selected for comparison or an operation. Selection explicitly distinguishes selected IDs, a page, the requested top-N and the full matching query at its declared read boundary.

Single, multiple and batch are declared operation cardinalities. A multi-input comparison is not a hidden loop of unrelated single-item requests. It receives the selected set, comparison basis, versions and resource boundary through the common native operation. Batch effects preserve per-item outcome and shared admission semantics. Mixed or ineligible selections receive a clear per-item disposition, not silent omission.

Users can pin records, open two or more compatible items in comparison panes, and switch among list, details, graph, structure, geometry, timeline, board or media views without rebuilding their query. Multi-object views identify each displayed item and expose which selected items are outside the current render window. Filtering data, narrowing a view, hiding labels and changing camera are different actions with different effects.

Selection is linked across views: choose a grid row, node, curve vertex, move or source span and highlight the corresponding typed record/occurrence in the other open panels. The selection key includes the necessary occurrence, ordinal and version; one canonical entity appearing repeatedly must not make every occurrence indistinguishable. Navigation never silently widens world/source/authority scope.

Table preferences, query and selected IDs/ranges, pin/comparison state, history, camera/rotation, active move/time position and disclosure scope survive expansion and return navigation. Each viewer has local loading/error/cancellation. Page resize, label decluttering and opening an already loaded panel do not trigger ingestion or rerun cognition.

### 10.3 Whole-product coverage, not a Unicode checklist

This matrix groups existing scope; it is not an exhaustive fixed list of future types. New registered families inherit the same interaction contract. Missing required operators/viewers stay tracked as implementation gaps rather than making the family disappear.

| Data/capability family | Collection and query entrance | Rich views composed with the shared controls |
|---|---|---|
| Atoms, n-ary compositions, physicalities and structural paths | Type, exact content, shape/size, constituent, ordinal, recipe and selected epoch | Raw values, structure, packed address view, real placement, realized curves and typed metrics |
| Lexical entries, senses, frames, roles, references and mappings | Language/source/release, relation/role, endpoint, mapping status and exact occurrence | Related grids, aligned comparisons, frame/role layout, graph and source passage |
| Evidence, testimony, dependence, standing and consensus | Proposition, source, context/time/world, arena/recipe/epoch and disposition | Evidence/dependence graph, uncertainty/history, comparable ratings and original observations |
| Documents, datasets, code and repository history | Work/revision/artifact, structural role, source span, language/grammar and linked symbol | Document/code viewer, diff, AST/structure, source mapping, diagnostics and execution links |
| Images, audio, video and other admitted modalities | Artifact/type, source, time/segment, declared annotation and observation | Thumbnails, image/region viewer, audio/video player, timeline/alignment and structural inspection |
| Games/chess, players, events, positions, moves and openings | Player/source/date/time control, result, role, position/line and witnessed or calculated attributes | Board replay, move list, clocks, career/head-to-head comparisons, graph, timeline and related documents |
| Places, people, organizations and referential worlds | Typed references, affiliations, time/source/world and allowed relationships | Profile/world views, geographic map where appropriate, hierarchy and linked occurrences |
| Conversations, observations, goals and semantic acts | Session/source, participants, time, parent/result and selected firmware | Conversation, multimodal attachments, goal/result inspection, provenance and related state |
| Recipes, firmware, calculus, models and Foundry artifacts | Program/type/revision, inputs/outputs, dependencies and deployment/readiness | Program/recipe editor where permitted, diff, dependency views, model/shard/tensor inspection and compile results |
| Experiments, calculations, measurements and execution results | Operation, dataset/cohort, provider, configuration, measured unit and outcome | Comparable tables, plots, timelines, exact inputs/outputs and links to the affected content |
| Sources, manifests, ingestions, jobs, workers, logs and alerts | State, source/release, installation, actor, dependency, time and resource use | Plan/stage graph, correlated logs, progress, artifact inspector and recovery actions |
| Users, memberships, grants, sessions, entitlements and usage | Authorized principal/workspace/resource, role/scope, expiry and activity | Account/grant relationships, policy/usage history and scoped management; secrets remain protected |
| Installations, nodes, providers, storage, configuration and federation | Location/provider, loaded version, readiness, dependency and permitted placement | Topology, configuration diff, health/history, capacity and typed management operations |

A board remains a board, a document remains readable, and a waveform remains a media control. Generic behavior is shared discovery/query/selection/navigation/effect handling, not forcing specialized content into a table forever. Raw structure and exact identities remain reachable from every rich view.

### 10.4 Screenshot-derived graph/glome preservation and repairs

The two user-supplied images show the same `transformer` record with (1) a dense labelled 2D/3D graph and (2) separate packed and placement panels with four-dimensional rotation controls. They are positive visual/interaction references. Static screenshots establish visible layout only, not correctness of counters, graph relations, coordinates, operation effects or the current deployed version. No browser chrome or screenshot file is republished by this document.

Preserve labelled navigable graphs, recentering, adjustable expansion, multiple coordinated representations, four-dimensional inspection and the ability to see the same object as graph, structure, links and provenance. Extend these to selected sets and comparison workspaces; do not discard the rich views to deliver a table-only administrator app.

Concrete visible problems to address: many graph labels overlap or are truncated; content reaches/crosses viewport edges; tiny instruction text competes with the graph; the wide `Unlock (m)` control overlays both geometric panels; the two panels' controls and viewport tops are not aligned; decorative graphics overlap the content area; the presented detail leaves no master list or selection workspace alongside it.

Required repair behavior:

- Graph labels have zoom/focus-aware density, stable hover/selection labels, explicit pinning and readable contrast. Suppressing a label changes presentation only; actual result truncation remains separately disclosed. The selected node and edge are inspectable in the linked table even when their label cannot fit on canvas.
- Each graph edge exposes its type, direction/roles and context on selection. Effective source/relation/expansion settings and the currently rendered subset are readable in one place. Domain clustering is labelled presentation or a named calculation, never an implicit merge of meaning.
- Packed hash/address geometry, real placement and realized coordinate curves remain distinctly labelled with their proper units/types. Shared selection can link corresponding constituents; it does not declare their numerical coordinates interchangeable. Optional synchronized cameras have explicit on/off and per-panel reset.
- Graph/glome panes support bounded sizes, resizable splits, expand/fullscreen and stacking on narrow displays. Controls occupy their own toolbar rather than obscure data. Keyboard help is discoverable and shortcuts are scoped to the focused viewer, so opening two boards or graphs does not control both at once.
- A selection can overlay multiple compatible trajectories with a clear legend or use small-multiple panes. A metric comparison names the selected objects, coordinate class, metric/variant and units. Exact equality remains separate: `King` and `king` retain different content and a shared suffix even when a comparison groups them.
- Decorations must not cover controls, observations or selected objects. Existing accessibility and layout requirements (#172/#174/#176, UX-05..08) apply to every viewer, not only a Unicode demo.

### 10.5 Chess as an end-to-end example of the universal workflow

Read-only source inspection at legacy commit `4336f709606c4d4517db4eb81ac6c3b495bde0d5` found useful existing pieces, not a claim that every old page is single-item-only:

- [`PlayersIndex.tsx`](https://github.com/SaltyPatron/Laplace/blob/4336f709606c4d4517db4eb81ac6c3b495bde0d5/web/src/chess/db/PlayersIndex.tsx) already has paged player lists, URL-persisted filters, sortable game/rating/RD columns and clickable careers.
- [`PlayerPage.tsx`](https://github.com/SaltyPatron/Laplace/blob/4336f709606c4d4517db4eb81ac6c3b495bde0d5/web/src/chess/db/PlayerPage.tsx) exposes provider profiles/aliases, overall and colour-split records, games/opponents, a replay and a link to the substrate entity.
- [`GameBoard.tsx`](https://github.com/SaltyPatron/Laplace/blob/4336f709606c4d4517db4eb81ac6c3b495bde0d5/web/src/chess/db/GameBoard.tsx) supplies replay, move stepping/scrubbing, flip and clock/speed handling with exact-position links. These are inspected source mechanisms, not freshly tested live behavior.
- [The legacy chess integration guide](https://github.com/SaltyPatron/Laplace/blob/4336f709606c4d4517db4eb81ac6c3b495bde0d5/docs/guides/chess-graph-integration.md) records the intended player -> playing/line -> position/move -> other playing/player/source connections. Its particular old identity canonicalizers and schemas do not define the clean implementation.

Preserve those capabilities and generalize their infrastructure. Required journey: browse players -> filter by a declared source/time/rating scope -> select multiple careers -> compare on the same basis -> open their shared or filtered game collection -> open one or several games -> step to a position -> show other games reaching that exact position -> open its permitted continuations, calculation or grounded source passage -> return to the selected careers without losing state.

The same board control must work on a game page, player detail, experiment result and comparison pane. Move/position selection synchronizes the board, move list, occurrence/provenance and graph. A replayed occurrence, a hypothetical variation and an executed move are distinct; inspecting or scrubbing must not play a move, launch an engine or publish testimony. Existing chess-native owners supply rules/calculations; the UI neither duplicates them nor takes away legitimate game-specific interaction.

This is one cross-domain acceptance example, not a chess-specific replacement for the all-data requirement.

### 10.6 Developer experience and shared action protection

The same field/operator/relation descriptors serve UI rendering, typed API clients and documentation. Native HTTP and applicable MCP/compatibility operations use the same semantic query/effect owners; compatibility protocols expose only their declared appropriate operations. A developer adds one supported type/operation integration rather than separately maintaining a browser version, a source-specific controller and several incompatible endpoint implementations.

Preserve CIEDigital's centralized protected-parameter abstraction, not its historical DES/embedded-key implementation. Selected route/cursor/intent state uses purpose-scoped authenticated protection with managed keys and declared expiry where appropriate; TLS and current per-request authority are separate obligations. Client-posted runtime type names never determine allowed types. Protected state retains exact query/cardinality/context and is reusable through shared link/binding controls, not custom cryptography per page. Public permanent identity links and ordinary SDK requests need not acquire a proprietary encrypted request format. Details and primary references are in [SCHEMA_DRIVEN_INTERFACE_REFERENCES.md](SCHEMA_DRIVEN_INTERFACE_REFERENCES.md).

### 10.7 Feature acceptance additions to existing owners

These refine existing DBR/UX/DATA/AUTH/INT/EVO criteria; they are not implemented tests or a new numbered completion score.

| Feature proof | Existing ownership and acceptance |
|---|---|
| An ordinary supported field added to an authorized source/result schema acquires suitable filter, sort, grid and detail behavior without new screen-specific code; empty and mixed results retain correct schema | #268/#68; DBR-08/30, EVO-01/02 |
| The same select/filter/compare/drill-back journey works for lexical content, a chess cohort, documents/code, a media collection and operational jobs; missing providers remain explicit unfinished acceptance, not a substitute mock | #68 with existing domain owners; UX-01/03, DBR-05/17/18/30, QA-01 |
| A selected set survives changes of viewer and record focus; explicit all-matching/top-N/page/IDs scopes produce the correct plural or batch operation and per-item outcome | #268; DBR-02/06/27, INT-01 |
| Table selection, graph node/edge, curve ordinal, board ply and source/media span synchronize by exact typed occurrence/version, without matching labels as identity | #68/#174/#176; DBR-09/10/13/14/17, DATA-04/05 |
| Two simultaneous rich viewers have independent focused keyboard controls; expand/resize/recenter-display preserves query and does not silently execute a new semantic operation | #68/#176; UX-05..08, DBR-05/28 |
| Dense graph and dual-glome fixtures reproduce the visible overlaps and fail until readable labels, unobscured controls, proper typed views and accessible linked rows work at the reviewed sizes | #172/#174/#176; UX-05..08, DBR-17/28 |
| A selected plural comparison is bounded set-wise work; high-cardinality selections cannot become N per-item SQL/API calls or unbounded pairwise work hidden behind a button | #268/native query owner; DBR-29, QA-05 |
| Schema/field/relationship discovery, protected navigation and action execution apply the same current access scope to every family, including admin and raw views | #64/#268; AUTH-04/06/08, DBR-24/25 |

The delivery condition is usable, interconnected product functionality. Generating a schema, writing these criteria, or displaying a test receipt does not deliver it. The current work remains design review, with application implementation awaiting the user's review of the workflows.
