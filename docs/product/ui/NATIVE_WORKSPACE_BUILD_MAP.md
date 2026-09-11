# Native workspace build map: shared controls, local substrate and domain snap-ins

Status: **consolidated design for review, not application implementation or completed acceptance**. PR #272 remains the integration vehicle. This is the implementation-oriented entry point to the earlier screen, data-browser, response, authentication and source-reference documents. Their unique requirements remain; their repeated examples are not separate implementation queues.

Inspected review baseline: `d772637e61671fd33c76dec64d4afb56ead0f12f`. Native ownership is grounded in `contracts/authority-stack.json`, `contracts/recipe-model.json`, `docs/architecture/BOUNDARIES.md` and `docs/architecture/COGNITION_EXECUTION.md`. Current discussion adds the concrete local-client use case. No live database or application runtime was exercised by this document.

## 1. The combined requirement

It just happens to be the chess page, the LaTeX view, the DNA sequence view, or another task. The reusable product is the same: discover data, construct a query, select one or many objects, inspect/compare/transform them, follow their constituent/source/evidence links, and retain the working context. Domain knowledge changes the applicable records, programs and renderers, not the navigation/query/storage/lifecycle implementation.

The client is not a passive display and is not a worker recruited for unrelated provider jobs. It executes the user's own Laplace operations using that user's device and locally available state: exact deconstruction/reconstruction, inspection, filtering, calculations, editing, useful local indexes and bounded retrieval of missing dependencies. Better latency, less redundant transfer and less repeated server work are consequences of serving that user directly. This design does not add model training for others, idle-compute harvesting, cryptocurrency work, a compute marketplace or mandatory public sharing.

The default local asset is **content-addressed substrate state**, not a frozen answer cache. The user can retain records they inspect and create durable local knowledge of their own. Query results and preferred realizations may change as new evidence, sources, recipes, permissions or context arrive. Reuse the exact underlying objects and re-evaluate the affected current query; do not silently reuse an old answer as current knowledge.

One native C/C++ semantic implementation runs under accepted host providers. C#, SQL, PostgreSQL datum conversion, browser bindings and platform storage/transport adapters marshal, orchestrate and realize its results. A client-side JavaScript, C# or local SQL rewrite of identity, geometry, trust, search or decomposition is not the intended offload.

These three distinctions govern all work below:

- **Generic behavior:** query/filter/order, selection, fields, reference navigation, local presence, batches, sync, jobs, permissions, error/loading states and workspace layout behavior.
- **Reusable specialization:** one shared implementation for a genuinely new value class, grammar/codec, geometry/sequence viewer or typed operation kernel, usable in every applicable collection.
- **Domain configuration:** source/profile mappings, accepted recipes, role labels, task layouts and renderer arguments. No chess, DNA or source-name branch in the generic workspace.

## 2. One execution spine, several hosts

```text
User's action in a reusable control
  -> generated typed request + selection + scope + resource grant
  -> native planner/executor on the user's device
       -> local canonical records / occurrences / accepted indexes
       -> local native deconstruction, reconstruction and eligible calculations
       -> typed missing dependencies or remote operation requirement
  -> host transport marshals bounded requests to an authorized peer/server
       -> C# / SQL orchestration, as applicable
       -> PostgreSQL extension + the same native engine where server access is needed
  -> exact records, dependency manifests, coverage and changed heads returned
  -> local native verification, deposition, reconstruction and completion
  -> typed rows / render data / result state
  -> controls render and retain the user's workspace
```

This is a logical flow, not a promise that every instruction already runs in a browser or that PostgreSQL internals can be linked unchanged into WebAssembly. PostgreSQL-dependent work runs at the PostgreSQL host. Storage-independent kernels and the common executor need a qualified browser/native-client target and host adapters. A missing local capability can use the same remote operation if the selected policy permits; it cannot silently acquire a different meaning. Local-only private operations return a clear missing capability instead of uploading private content without permission.

The C boundary declares ABI/schema versions, opaque session/result handles, exact integer widths, typed vector/buffer layouts, string encoding, ownership/freeing, error identity, cancellation and async dependency-resumption behavior. Pointers never cross the network. A WebAssembly binding handles its own memory address space and growth; platform marshaling retains 128/256-bit IDs, large ordinals and exact numeric contracts. A scalar operation is a one-element batch, not another code path.

The native plan selects work placement from available data, complete operator capability, authority, transfer cost, CPU/memory/I/O limits and the user's device preference. It must not transfer an entire server corpus to save a small server query, or call a server for every character/constituent it already has. Shared semantic planning is native-owned; event loops, network requests, system calls and UI scheduling remain host services.

## 3. Shared contracts: the arguments the components agree on

Names below are proposed API roles, not claims of existing exported symbols. Generate their actual types from the existing registries rather than adding a parallel hand-maintained semantic schema.

| Contract | Required contents | Why it is shared |
|---|---|---|
| `RecordRef` | Exact record kind and ID; revision where that kind requires one | An Entity ID, mapping record, physicality and receipt cannot be confused because all look like hashes |
| `Locator` | Record reference plus optional occurrence, ordinal/range/span, coordinate system and version | A selected move, repeated word, DNA range or image region needs more than the parent's ID |
| `ViewContext` | World/audience, source selection, language/realizer, dependent evidence/recipe/geometry versions, read consistency, authority handle | Every pane uses the same meaning and disclosure boundary; a handle is not self-asserted permission |
| `FieldDescriptor` | Stable field identity, exact value type/null states, units, origin, label, cardinality, legal filters/sorts, reference target, renderer/editor policy | One field adapter supplies controls everywhere; discover existing metadata instead of configuring every page |
| `CollectionDescriptor` | Record/result schema, supported relations, query capabilities, cardinality/count policy, compatible viewers and actions | Tables, related lists, grouped results and empty results remain self-describing |
| `RelationDescriptor` | Source/target kinds, roles, direction/arity, traversal operator, source/version constraints and disclosure | Relationship filtering and navigation are generic without pretending FK metadata defines every semantic relation |
| `QuerySpec` | Typed predicate AST, projection, relation quantifiers, grouping/measures, post-group filters, ordered sorts/ties, top-N and pagination | Visual builder, API and any textual query view have one meaning; UI never sends arbitrary executable code |
| `SelectionSpec` | Focus versus working set; explicit refs, ordinal ranges, page, top-N or query selection with exclusions and a read boundary | Multi-selection and batch actions cannot silently change scope |
| `ResultManifest` | Query/program identity, schema, scoped refs or typed blocks, required dependencies, completeness, generation and pending/failed components | Transport need not repeat every reconstructed string or object already local |
| `MeasureDescriptor` | Type/units, aggregation law, group keys, reference population, uncertainty/support meaning and contributor operator | A heatmap number becomes a navigable calculation rather than an unexplained scalar |
| `OperationDescriptor` | Inputs/outputs, accepted cardinality, effect class, preconditions, authority, compatible target/provider, cancellation/progress and result law | One action form/runner supports inspection, transformations and administrative commands without merging their effects |
| `PackageManifest` | Content fingerprint/version, dependencies, schemas, providers/recipes, rendering adapters, layout recipes and qualified execution targets | A new domain can register capabilities without building another application |
| `ReplicaCoverage` | Selected collection/source scope, exact generation/checkpoint, available fragments and completeness boundaries | Local absence cannot masquerade as global absence or a partial local sort as global top-N |

Runtime metadata is typed and validated, not arbitrary reflection over a client-posted runtime type name. It combines authorized installed schema, generated domain annotations and the actual projection. A binary column alone cannot identify its semantics; a schema-derived default plus one type annotation should, however, be enough to produce ordinary controls. Do not turn metadata into a manual configuration burden that defeats the user's SpecEditor/CIEDigital precedent.

## 4. Runtime services: build once, inject into controls

These are modules/interfaces, not a proposal for twelve network microservices. They may live in one package/process, with the native executor and host boundary kept explicit.

| Service | Representative arguments and results | Implementation responsibility |
|---|---|---|
| Descriptor catalog | `describe(collection/result, context, expectedRevision)` -> effective schema and capabilities | Generated/native type identity; host caches revisioned metadata |
| Native session | `submit(program, inputs[], context, resourceGrant)` -> result or resumable dependency needs | Same native engine, error law and batch execution |
| Local record provider | `getMany(refs, scope)`, `presence(refs)`, `putVerified(batch, retentionClass)` | Host storage mechanism; native verification and logical deposition rules |
| Closure resolver | `ensure(refs, depthOrRanges, requiredKinds, context, budget)` -> exact bounded dependency closure or typed missing state | Native decides semantic dependencies; host fulfills batched I/O |
| Native query provider | `query(QuerySpec, coverage, context)` -> manifest/typed results | Local, server or mixed physical plan with one native semantics |
| Change/sync coordinator | `follow(scope, checkpoint)`, `reconcile(manifest)`, `publish(selected local events)` | Native exchange/admission law plus transport, restart and backpressure adapters |
| Realization service | `realizeMany(refs, language/context, recipeVersion)` -> values plus source/provenance and disposition | Native realizer; local reuse of exact label content and versioned mappings |
| Contribution service | `contributors(resultRef, groupKeyOrRegion, stage, context)` -> ordinary queryable collection | Native calculation lineage; direct contributors distinct from normalization/reference population |
| Workspace controller | `open/focus/select/pin/compare/navigate/restore` with typed locators | Presentation state, no semantic graph traversal or calculations |
| Action runner | `prepare(operation, selection, args)` then applicable `execute/observe/cancel` | Host orchestration through native authority/effect law; no private job scheduler per domain |
| Storage/device controller | `usage`, `pin`, `export`, `backup`, `evictReplica`, resource preferences | User ownership, quotas, durable-write status and host capability reporting |
| Target/pack resolver | `resolve(requiredCapabilities, versions, device)` -> exact compatible packages/providers | One manifest/version compatibility and integrity lifecycle, not arbitrary code from a data row |

A native request that needs data returns a bounded set of missing typed inputs and a continuation identity, or an equivalent accepted asynchronous provider mechanism. It must not synchronously block the main UI thread or create one network round-trip per constituent. Completion, cancellation and resource accounting remain native-owned even when the host awaits network/storage I/O.

## 5. Reusable control catalogue and argument sets

### Common control contract

Each data control receives `descriptor`, `binding` (a query/record/result handle, not its own URL), `context`, `selection`, `presentationOptions` and injected services. It emits typed interaction intents such as `select(locator)`, `open(ref)`, `setQuery(patch)`, `compare(selection,basis)`, `requestAction(operation,args)` and `inspectContributors(group)`.

The host routes those intents once. A module cannot smuggle authority through props, run a hidden endpoint, or make opening/expanding a visual invoke an unrelated effect. Layout events include their origin so linked panes do not echo selection events indefinitely. Focused item, hover and multi-selection are distinct states.

### A. Fields and input primitives

| Reusable element | Specific arguments beyond the common contract | Behavior used everywhere |
|---|---|---|
| `ValueCell` / `ValueInspector` | Field, exact value or disposition, format, copy modes | Raw/formatted value, units, origin, filter-by-value and full precision |
| `ValueEditor` | Field, draft value, constraints, read-only/effect policy, validation | Shared exact text/number/date/enum/ref editor; typing a draft does not mutate canonical state |
| `PredicateEditor` | Field, selected operator, typed operands, null/comparison policy | Correct controls for equals/range/contains/flags/reference; never lowercase exact identity implicitly |
| `ReferenceLink` / `ReferencePicker` | Target kinds, refs, label policy, allowed target query | Open/new-tab/copy/select typed records, including unresolved or withheld states |
| `RangeSelector` | Coordinate system, bounds, unit, direction, selected ranges | Base/ordinal/time/image/board location selection without mixing coordinate conventions |
| `StatusValue` / `ValidationMessage` | Machine state, explanation, permitted recovery | Pending, empty, partial, failed, no-evidence and zero remain different |

Adapters for enum/flags, exact integers, decimals, text, dates, vectors, references, sets and ranges are registered by type, not table name. Complex types can supply editors/viewers once. Unsupported types retain safe raw inspection and explicit operator gaps rather than disappearing.

### B. Querying, lists and workspace organization

| Reusable element | Specific arguments | User behavior |
|---|---|---|
| `CollectionPicker` | Catalog, scopes, supported views | Browse immediately without guessing a search term |
| `FilterBuilder` | Query AST, field/relationship descriptors, supported groups | AND/OR/NOT, same-related-record versus independent-existence predicates |
| `FacetPicker` | Field, query scope, self-filter policy, bounded value provider | Suggestions from actual authorized data, not a global unbounded distinct query |
| `ProjectionAndSort` | Columns, order/ties, group/measure choices, top-N | Field selection and real whole-query sorting before pagination |
| `DataGrid` | Result binding, columns, page cursor, selection mode, density | Virtualized/paged list with clickable rows and stable navigation; no per-cell requests |
| `SelectionBar` | Selected refs/query/ranges, operation compatibility | Pin/compare/export/operate on the exact selected set, with eligible and ineligible items shown |
| `RecordInspector` | Focus ref, available facets, initial facet | Composition of applicable detail controls; no giant condition tree keyed on source names |
| `RelatedGrid` | Parent locator, relation, direction/roles, query | The same DataGrid scoped by a typed relation |
| `CompareWorkspace` | Inputs A/B/N, comparison basis, alignment, units/scale | Aligned data and differences, not alternating tabs and mental arithmetic |
| `WorkspaceFrame` | Task-layout recipe, pane bindings, focused pane, size rules | Useful default layout, resize/pin/expand and deliberate narrow-screen transitions |
| `HistoryAndSavedView` | Query/selection/layout references, return cursor | Back restores the working context; saved views do not save extra permissions |

### C. Structure, rich displays and evidence

| Reusable element | Specific arguments | Shared/domain boundary |
|---|---|---|
| `StructureViewer` | Root, child/ancestor operators, role/ordinal/range policy | Trees, ASTs, documents, expressions and n-grams; expansion preserves actual structure |
| `SequenceViewer` | Sequence ref, coordinate convention, alphabet/notation, tracks, selected range | Text, DNA and timed sequences reuse range/navigation; domain interpretation comes from recipes |
| `DocumentOrCodeViewer` | Artifact/span map, language/renderer, annotation tracks | Readable source plus exact span selection, linked structure and governed edits |
| `GraphViewer` | Typed node/edge result, role/direction style, layout, label density | Same selected records, readable pinned selection, linked tabular equivalent; no UI-private semantic expansion |
| `GeometryViewer` | Coordinate class, dimension, projection, trajectories, camera, metric | Packed address, real placement and realized curve are explicit; metric invokes native operation |
| `HeatmapOrPlot` | Dimensions, measure, units, scale policy, selected cells/ranges | Every supported aggregate opens contributors; shared scale for comparable A/B views |
| `BoardViewer` | Topology/locations, pieces/stacks, position, interaction mode, annotations | Chess and backgammon are configurations plus actual rule providers; renderer does not decide legal moves |
| `ReplayControls` | Sequence binding, cursor, timing policy, speed, focused viewer | Moves, events and media stepping; replay does not submit new game actions |
| `MediaViewer` | Artifact/ranges, decoder, tracks, playback and selection | Image/audio/video display with source/time/region links |
| `EvidenceAndContributors` | Result/proposition, lineage/group selector, evidence scope | Sources, dependence, contradictions, uncertainty, direct and normalization contributors |
| `StandingAndHistory` | Arena/participant, recipe/epoch, outcome query | Comparable typed standing, not one universal Entity score |

### D. Operations and local data management

| Reusable element | Specific arguments | Behavior |
|---|---|---|
| `ActionForm` | Operation descriptor, selection, input schema, draft/plan | Auto-fields and curated steps; read, transform and destructive effects remain distinct |
| `PlanAndImpact` | Exact prepared plan, target/revision, effects and recovery | Human review only when the operation warrants it; routine browsing does not require approval dialogs |
| `JobMonitor` / `LogView` | Operation/job, stages/events/cursors, filters | Durable progress, readable failure and restart; no feature-owned polling/retry loop |
| `DeviceDataPanel` | Owned/replica/index inventory, storage class, pins, sync policy | Show what is local, unsynced, reconstructable or backed up; export and remove exact scope |
| `ResourceAndConnection` | User policy, current useful operation, bytes/CPU/memory and sync state | Explain device work and keep controls responsive; no unrelated background work |

This catalogue describes reusable behavior, not a mandate for one source file per row or a UI of thirty unrelated cards. Reuse existing primitive libraries where qualified. The application needs a small number of coherent modules and deliberately composed task layouts.

## 6. Queries, labels and exact identity as the substrate changes

The exact content `King` differs from `king`. Case folding, normalization, canonical LaTeX rendering or domain interpretation is an explicit transformation/relationship, never an implicit identity rewrite. Equally, the same content can have many occurrences and interpretations without duplicating its canonical structure.

Filtering occurs over the declared authorized candidate set before top-N and paging. Multi-valued sorts name their selector/aggregate. Predicates on input contributors and predicates on a finished aggregate are different stages. Original high-precision values survive marshaling and display. S3/Fréchet/Karcher calculations are typed operations, not aliases for content equality or semantic truth.

A client may retain an old result for history or continuity, but current answers are not memoized by query string alone. The data plan accounts for evidence/source/recipe/authority changes and whether new matching entities have appeared. Tracking only already-returned IDs misses newly eligible records; maintain scoped collection/change checkpoints or repeat the bounded authoritative discovery query as appropriate.

Labels have two layers: exact label content is stable under its content identity; the eligible/preferred label for a referent depends on language, source evidence, recipe and context. Retain the content and versioned mapping; refresh the selection when its dependencies change. Geometry likewise retains each exact physicality under its recipe/epoch, while the current active geometry selection may change. A human may inspect prior state without calling it current.

Local-only queries explicitly say which local world and coverage they examined. A complete local range/collection can support an exact local result; having only yesterday's visited records cannot support a global absence claim, a current global rank or an exhaustive set of incoming relations. Such operations need an authoritative indexed server/peer stage or explicit partial disposition.

## 7. Local storage is user state plus reusable replicas, not one disposable cache

| Class | Examples | Retention/update rule |
|---|---|---|
| Application packages and descriptors | UI shell, compatible native/Wasm binary, field/operator metadata, renderers, ordinary interface labels | Content-versioned assets; atomic compatible updates, no mixed package generations |
| Exact acquired substrate records | Entities, ordered structures, occurrence records, physicalities, source spans, label content and pinned recipe records | Verify typed identity; retain by exact revision; automatic eviction only for eligible unpinned replicas |
| Versioned heads and eligibility | Active evidence/geometry/recipe references, current label selection, visibility policy, collection coverage | Synchronize deltas/checkpoints; never treat a cached head as permanently current |
| Local perfcaches | Presence/constituent indexes, label lookup, range indexes, reconstructed working sets | Derived, dependency-versioned, disposable/rebuildable; same native verifier/invalidation law |
| User-owned durable state | Authored documents/sequences/notes, imported private data, observations, edits, local world versions | Not cache garbage; transactional save, recovery/export, explicit sharing; preserve unreplicated data |
| Workspace preferences and drafts | View/layout, focus, selected ranges, unsent edits and saved queries | Recoverable local state; exact drafts cannot be silently trimmed/normalized; distinguish draft and committed local content |
| Pending synchronization | Selected publication events, acknowledged checkpoints, conflicts and tombstones | Durable outbox; retries reconcile identity and scope; no automatic replay of stale privileged actions |

A local database is a physical storage/index provider, not a second semantic implementation. SQLite or an IndexedDB store does not replace the native identity, AST, evidence or query laws. It also does not become a pretend local PostgreSQL installation.

Deleting cache, signing out, switching accounts, uninstalling a pack and deleting user data are different operations. Explain consequences and recovery. User-authored data must not be automatically evicted because a thumbnail budget was exceeded. Browser eviction remains a platform risk even when the application separates these classes: request persistence where appropriate, show whether it was granted, and provide verified export/backup/restore. Never promise that browser-local storage is an indestructible vault [P3,P4].

Local creation runs the same native decomposition/identity/observation programs. Offline work commits to the user's local world and can be synchronized later if the user chooses. It does not require service approval merely to keep private data locally. Publishing to a shared/server world has that destination's current authority and admission rules. A network retry cannot mint a second independent observation or testimony for the same event.

## 8. Targeted exchange: reuse here, ask only for the missing work there

A conceptual protocol family under #65/#268 is `describe`, `query`, `have/need`, `fetch-records/ranges`, `changes`, and `publish`. These are semantic roles, not final endpoint names or implemented APIs.

1. Native planning resolves the user's request and the exact current scope. For global discovery, it invokes a bounded indexed server/peer query rather than guessing from a partial local cache.
2. The response identifies selected records, relevant generation/coverage and required dependency references. It can include a bounded useful first batch of labels/data; forcing a second round trip for every display name would defeat responsiveness.
3. The client's native presence check identifies which exact records, ranges and versions already exist locally. Presence means validated usable content, not merely a saved key string.
4. Fetch missing inputs set-wise, with authorization, byte/record limits, declared requested closure and continuation. Repeated shared subtrees are sent once per needed scope. Avoid per-node request waterfalls; allow bounded dependency bundles without recursively sending the whole world.
5. Native code verifies/deposits inputs, constructs the necessary indexes/working set, reconstructs structures and performs the eligible user calculation locally. A complete subtree can be reused without reconstructing all of its descendants merely to display a stored summary.
6. A server-only data/transaction stage executes on its owner, returning typed outputs that the client can then inspect/reconstruct. Do not misreport remote PostgreSQL work as client compute.
7. Change notifications update relevant heads and collection scopes; only affected calculations/views rerun. A missed notification resumes from a durable checkpoint or obtains a new bounded snapshot, not blind trust in an uninterrupted stream.

For repeated inspection of an unchanged, fully present immutable object, there should be no repeat transfer or server-side reconstruction of that object; any separate freshness/access check is measured explicitly. Opening a newly returned object should transfer only the needed missing records/ranges, plus declared protocol overhead. Cache-eviction races during transfer trigger exact missing recovery, not a corrupt reconstruction.

Content hashes verify content integrity; they do not prove source truth, completeness of a query, authorization or that an untrusted client executed a program honestly. A receiving node applies its native admission/evidence rules. Existing signatures and source lineage remain attributable; a client-generated descendant is not independent evidence of its ancestor. Do not broadcast a user's entire possession inventory or private content hashes to peers. Scoped have/need exchange is authenticated, bounded and limited to the requested task.

Changes in distributed knowledge cannot become instant global knowledge. Expose synchronization freshness and examined coverage. Peers can be intermittent; exact content merges idempotently, while competing statements/edits remain versioned history with explicit conflict/supersession law, not last-writer-wins over truth.

## 9. What to load initially and where it lives

Start with a small versioned bootstrap: application controls, effective schema/operator metadata, native kernel entry points, the last workspace, its retained records/labels and relevant change checkpoints. Fetch or activate additional grammar/codec/calculus/renderer modules when the user's task needs them. Prefetch may cover a bounded likely next page or selected object's needed closure under the user's policy; it is not a crawl of their whole world.

Optional pinned substrate packs can include Unicode/type tables or repeatedly used domain data. Their manifest states byte size, dependencies, exact version and coverage. Do not force every user to download every standard, model and corpus before seeing their first usable screen. A full four-component binary64 coordinate array for 1,114,112 positions alone would occupy 35,651,584 bytes (34 MiB), before IDs/properties/indexes; this is a size calculation, not a measured Laplace package. Decide initial versus on-demand placement from actual manifests and target-device measurements, not the word 'cache'.

| Client host | Proposed physical implementation | Required limitation |
|---|---|---|
| Browser | Native C/C++ compiled to Wasm in a worker; generated host bridge; IndexedDB for transactional records/metadata and/or OPFS files/qualified SQLite-Wasm provider; Cache API for versioned app assets | Capability-test chosen APIs and memory/threading, handle quota/eviction and multi-tab ownership; no SQL-server connection exposed to browser |
| Desktop native | Same core via generated C ABI; host UI/C# orchestration; qualified local files/database/index provider | Same exact semantics and export/sync format; use platform credential storage |
| Mobile/native shell | Same core compiled for the admitted target, platform storage/UI adapters | Battery/thermal/background lifecycle and storage limits; checkpoint user work |
| Server/personal node | Native engine with PostgreSQL extension where selected, service/API host and admitted durable storage | Same content/exchange/recipe law; wider data availability, explicit authority and durable service operations |

Emscripten supplies a C/C++-to-Wasm path, but it does not automatically port every platform/third-party kernel or prove Laplace parity [P1]. Native/Wasm results must follow the operation's bit-exact or declared numeric comparison contract. Where a required provider cannot run locally yet, expose that capability gap rather than a reduced private algorithm.

IndexedDB is asynchronous transactional object storage; OPFS is origin-private file storage with worker-only synchronous access handles [P2,P3]. SQLite's Wasm distribution documents distinct persistence/concurrency backends; it is a provider candidate, not a selected product dependency [P5]. Multi-tab writes need one coordinated owner or a proven lock/transaction strategy with fencing and recovery; disabling corruption warnings is not a solution. Memory-only fallback must say it is not durable.

A service worker can help versioned asset/offline fetch behavior; it is not an always-running compute daemon [P6]. User jobs need recoverable native state and host lifecycle handling. Threaded Wasm requires appropriate shared-memory/isolation deployment; Emscripten describes separate threaded and nonthreaded builds [P7]. Choose a qualified profile, not a requirement for users to edit browser flags. Same meaning under fewer workers is a resource difference, not a different engine.

## 10. User-benefiting compute and privacy controls

Every local program has a purpose tied to the user's action or an explicitly enabled maintenance/sync task for their data. Defaults perform useful local work automatically within sensible declared limits; the user does not approve every reconstruction. Device preferences bound CPU workers, memory, disk and network, and determine whether resource-heavy local work or permitted remote work is preferred. Do not assume a browser can accurately measure all battery/thermal hardware state; use available capabilities and user choices.

Expose a compact local-data status, not telemetry clutter on every task: 'available locally', 'fetching missing structure', 'saved on this device', 'sync pending', or a precise failure. Detailed usage can identify the current operation and its measured calls/bytes/CPU; it must not imply one global synchronization state or a made-up savings figure.

Fetching data for the user's current task does not opt them into serving other users. Public sharing, peer service, backup and account synchronization are distinct explicit choices. Private authored content remains local by default unless the user selects a destination. Local-first use does not require downloading a conventional model or running unrelated training.

External grants still govern remote disclosure. A server can revoke future access and a cooperating client can purge the revoked replica scope; it cannot guarantee erasure of plaintext an authorized recipient already copied, particularly while offline. Define offline access/retention policy honestly. Store keys through a qualified host mechanism; authenticated local encryption can protect stored bytes under that threat model but cannot protect plaintext from malicious same-origin code executing while the application is unlocked. Key rotation or re-encryption must not change canonical plaintext content identity. Browser-origin migration and device loss need an explicit export/restore path.

## 11. Domain packages: how a page happens to be chess

A package registers the minimum new domain information through existing typed contracts:

```text
identity/version/dependencies
source formats + grammar/codec provider + exact lowering/recomposition recipe
field/role/unit/coordinate annotations over canonical structure
native operator recipes and only irreducible new kernels
compatible renderer/editor adapters and their argument schemas
task-layout recipes using common slots and bindings
conformance fixtures, exact supported feature boundaries and target capability metadata
```

It does not register a private account system, event bus, query language, job runner, persistence engine, cache strategy or source-specific controller. Names and dependency versions identify the package, not a new content-identity law. Registering one supported type automatically enables applicable ordinary fields, filters, sorts, references and docs in every collection. Activating a domain pack adds task presets; it does not insert another stack of widgets into every page.

| Example | Reused immediately | Actual domain delta |
|---|---|---|
| Chess | Collection/selection/compare, reference grids, board topology renderer, sequence/replay, source/evidence, heatmaps, local presence and native execution | Position/rule/notation programs, piece/topology arguments, game-specific measures and source recipes |
| Backgammon | Same board/location/stack and state-sequence controls, actions, comparisons, events and provenance | Board topology/art, dice/chance/turn/rules/settlement semantics supplied by the admitted domain program; no chess rules reused by assumption |
| LaTeX | Exact source/structure, span navigation, diff/editor, local decomposition, operations, artifact dependency and result viewers | Selected grammar/macro environment, math renderer and round-trip/source mapping; rendering is not proof checking or universal TeX support |
| DNA sequences | Exact sequence/range viewer, annotations/tracks, filtering, comparison, source/provenance and bounded local processing | Alphabet/ambiguity conventions, coordinate/strand/reference annotations and each requested alignment/transformation kernel; shared letters alone do not supply biology |

A small data-only manifest should suffice where all needed types, operations and renderers already exist. A new renderer or kernel is integrated once when genuinely required. A mathematics parser, alignment algorithm, or game rule program is real domain work; generics should eliminate repeated application plumbing, not claim those semantics appear from a file extension.

The 'one-day snap-in' is a concrete integration target: with prerequisite native operators and adapters already available, a materially new package can be registered and exercised in one working day without changing the generic workspace, transport, local store, query builder, job machinery or authority implementation. Measure changed core files, handwritten repeated scaffolding, integration time and working user journeys. This is not a promise to implement an arbitrary new science/game engine in one day, nor an excuse to omit the domain delta.

Use two unfamiliar packages, not only the well-rehearsed chess case, to expose missing abstractions. Complete a LaTeX-source/structure/preview journey and a DNA-range/annotation/comparison journey using the same query, selection, storage and host APIs. Retain browser and native-client verification, dependency failures and exact scope. Demonstrations are not yet implemented by this document.

## 12. What needs to be built, under existing owners

The following are work packages inside existing issues, not new product requirements or a count of completed tasks. Current state is **specified; implementation and runtime proof not established by this review**. Audit existing usable core APIs before writing replacements.

| Work package | Concrete deliverables | Owner / immediate dependencies | User-visible completion |
|---|---|---|---|
| W01 Effective descriptors and exact marshaling | Generate record/field/relation/result/operation metadata; typed refs, numeric fidelity, versions, target capabilities, allowed fields | #268 with #5/#10/#64 | Ordinary supported fields and relation links appear automatically across views |
| W02 Native local host | Package storage-independent core, worker/native host bridge, batch input/output, resumable dependencies, cancellation and resource accounting | #66/#67 with #3/#4/#5 | The user's exact decomposition/reconstruction runs locally without UI/managed semantic copies |
| W03 Local data and perfcache providers | Verified object/occurrence storage, owned versus replica retention, native indexes, transactional save, multi-tab recovery, export/restore | #67 with #14/#15/#64 | Restart retains inspected and authored state; cache cleanup does not erase unreplicated work |
| W04 Targeted exchange and evolving heads | Bulk have/need/fetch, bounded closure/ranges, generation manifests, collection deltas, checkpoint recovery, publication and conflicts | #65 with #268/#15/#16 | Reopen avoids repeat object transfer; new source knowledge changes current queries without whole-cache deletion |
| W05 Shared query and result controller | Typed QuerySpec, server/local/mixed plan, coverage, projection/grouping, top-N/cursors, batched labels, stale-response rejection | #268 with #17/#60 and W01-W04 | Correct labelled results at usable speed, including new matching entities and incomplete-local disclosure |
| W06 Field and collection controls | Value/reference/predicate adapters, query builder, grid, selection, related lists and saved views | #68 with W01/W05 | One generic browse -> filter -> select -> inspect -> back route works for unrelated data |
| W07 Workspace and rich-control integration | Curated task layouts, focus/history, pane binding, tree/sequence/graph/geometry/board/media contracts | #68 with #172/#174/#176 and W06 | No arbitrary stacked sidebar; rich views keep identity/selection and useful space |
| W08 Measures and contributors | Native measure schema, scoped aggregate query, support/units, reference populations, contributor operator and comparison | #68/#268 with native calculation owners | Any supported chart cell opens the actual contributing records and correct recalculation |
| W09 Actions and administration | Shared form/plan/action runner, actual DB recovery, sources, jobs/logs, access/configuration and local device-data panels | #264/#265/#266/#64 using W01/W05-W07 | Operate the app and personal data without ad hoc SQL/SSH; exact recovery survives target DB replacement |
| W10 Domain/package integration | Manifest/version resolver, grammar/kernel/renderer adapters, layout recipes, install/update/remove and compatibility | #10/#58/#68 with qualified domain owners | LaTeX/DNA/backgammon-style additions reuse the workspace rather than fork it |
| W11 Public and platform delivery | Same typed HTTP operations, appropriate MCP/OpenAI mappings, native desktop/mobile host adapters and installed documentation | #268/#270/#271/#66/#67 | Same user operation across admitted hosts/routes without private semantics or proprietary SDK hacks |

These are coordinated work packages, not an infrastructure waterfall. W01-W06 form the first thin complete user journey; layout/control work can proceed against approved generated contracts while native-host/storage work progresses. Every implementation change carries its applicable controls and user behavior. Do not finish a whole cache subsystem without a working record view, or every renderer before a source can be inspected.

### Vertical delivery checkpoints after design approval

1. **Actual data, repeatable inspection:** first useful labelled Entity/physicality and unrelated source/record collection; filter/top-N/detail/back; local native reconstruction; inspect server traffic on cold versus repeated access.
2. **The user's own local work:** import/create/edit an exact local structure; reopen offline; restart/recover; export/restore; publish selected scope without duplicate content or automatic public disclosure.
3. **The substrate evolves:** admit new relevant source state; synchronize changed heads/records and discover newly matching IDs; update current labels/query while a pinned historical view remains intelligible.
4. **Rich operations on data:** a real aggregate -> contributors -> exact occurrence -> compatible viewer -> comparison -> return; show a board plus unrelated sequence/document using the same controls.
5. **Pack and host portability:** attach new domain packages with no generic-page rewrite; execute the same eligible program on browser/native/server targets with equal semantics and measured placement.
6. **Complete operating surface:** retained admin lifecycle, seed orchestration, authority, jobs, recovery, protocol profiles and full required product behavior close under their actual owners. Earlier slices are usable progress, not a declaration of whole-product completion.

## 13. Evidence that this architecture is working for the user

These extend the existing DBR/UX/INT/AUTH/EVO and node/federation criteria, all currently unrun. They are observable feature outcomes, not a new test-score goal.

- Open an unchanged fully local object twice. Exact content/structure/labels can be reconstructed without re-downloading their bodies; count server calls/bytes and separate access/freshness traffic. Do not claim latency from a warm fixture alone.
- Remove one required local subtree. The client fetches bounded missing structure, not the entire previously acquired collection; tampered/wrong-kind content fails native validation.
- Admit an entity newly satisfying an existing query and change one preferred label/evidence head. The current view discovers the new match and updates correct dependencies; cached current results cannot remain frozen by query string or existing IDs.
- Disconnect after a local edit, restart and reconnect. User-owned state survives and repeats do not multiply source occurrence or independent testimony. A stale privileged operation in an outbox cannot bypass new authority.
- Run the same exact program/input/recipe with browser native/Wasm, desktop native and server native providers where qualified. Exact identity/structure agrees; numerical operations meet their declared reproducibility law; receipts identify actual execution locations.
- Delay a remote secondary dependency without blocking local work or unrelated panels. A 30-second routine initial read is still a defect; a spinner is not the fix. Use the reviewed first-useful-result and interaction budgets from ACCEPTANCE.md, including labels.
- Add an ordinary supported field, then an unfamiliar relation and a materially different domain pack. Shared filters/details/links update without source-specific controllers or duplicate query semantics. Capture actual integration effort against the one-day prerequisite-bound target.
- Observe device activity while idle, inspecting, editing, syncing and closing. Every job is attributable to the user's request or enabled maintenance of their data; no unrelated work is scheduled. View changes never trigger remote writes or engine-setting changes implicitly.
- Exercise low quota, denied persistence, process termination, competing tabs, cache eviction, revoked remote access and missed change events. Preserve owned data or report its actual durability loss; never silently fall back to memory and claim saved.
- Compare local-only and global requested query scopes. Partial local coverage cannot certify global absence, exhaustive incoming references or a current global rank. Late/out-of-order responses cannot overwrite a new selection.

Performance accounting reports client CPU/memory, server CPU/rows, marshaling copies, network request count, transferred bytes and actual saved/reused state for the same user task. A client plan that drains a phone or downloads a whole corpus to save a cheap server query is not an optimization. Resource policy should choose the appropriate accepted plan, not pursue client work for its own sake.

## 14. Review decisions and consolidation

The direct behavior is fixed by this conversation: universal generics, native client work for the user, targeted server queries, local substrate retention/ownership and no semantics duplicated in marshaling layers. The exact frontend, local storage provider, first admitted browser/native targets, automatic retention/resource defaults, offline grant policy, package format/version policy and performance fixtures still require one explicit decision set.

No new feature branch, unrelated implementation issue, database run or UI prototype is authorized here. This map is under PR #272 and links existing ownership. The earlier documents remain detailed sources:

- [Screen contracts and task layouts](SCREEN_CONTRACTS.md)
- [Data browser and source connections](DATA_BROWSER_AND_SOURCE_LINKS.md)
- [SpecEditor and CIEDigital mechanisms](SCHEMA_DRIVEN_INTERFACE_REFERENCES.md)
- [Working with data, contributors and response behavior](WORKSPACE_BEHAVIOR_AND_RESPONSE.md)
- [Authentication/transports](AUTH_AND_TRANSPORTS.md)
- [Acceptance and proposed budgets](ACCEPTANCE.md)
- [Operator recovery](../OPERATOR_RECOVERY_AND_OBSERVABILITY_REVIEW.md)
- [Earlier issue traceability](ISSUE_TRACEABILITY.md)

Where earlier prose calls every local artifact a 'cache' or implies query-response caching as the primary mechanism, the user correction and sections 6-8 here refine it: exact replicas, user-owned durable data, derived perfcaches, active heads and transient results have separate laws. Future-platform language does not demote native client execution to a cosmetic frontend feature.

## 15. Primary platform references checked for this review

References establish available mechanisms and constraints, not implemented Laplace targets, measured speed or a selected dependency version. They do not change the native ownership law.

- P1: Emscripten, [Building to WebAssembly](https://emscripten.org/docs/compiling/WebAssembly.html).
- P2: MDN, [IndexedDB API](https://developer.mozilla.org/en-US/docs/Web/API/IndexedDB_API).
- P3: MDN, [Origin private file system](https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system).
- P4: MDN, [Storage quotas and eviction](https://developer.mozilla.org/en-US/docs/Web/API/Storage_API/Storage_quotas_and_eviction_criteria), and [requesting persistence](https://developer.mozilla.org/en-US/docs/Web/API/StorageManager/persist).
- P5: SQLite, [Wasm persistent storage options](https://www.sqlite.org/wasm/doc/trunk/persistence.md).
- P6: MDN, [Service Worker API](https://developer.mozilla.org/en-US/docs/Web/API/Service_Worker_API).
- P7: Emscripten, [pthreads support and browser threading deployment](https://emscripten.org/docs/porting/pthreads.html).

This design does not make a historical priority claim about 'first decentralized AI'. It specifies the user's distinct useful architecture and the evidence required to demonstrate it.
