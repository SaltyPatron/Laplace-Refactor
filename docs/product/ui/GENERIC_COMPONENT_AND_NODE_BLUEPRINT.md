# Universal components and local-node execution blueprint

Status: **implementation design for review, not implemented functionality**. This is the consolidated engineering entry point for PR #272. It builds on the existing review rather than opening another architecture, semantic engine or implementation branch. Names/signatures below are proposed contract notation, not assertions that those APIs exist. Application implementation still awaits the user's design review.

## 1. What must be built

Build a programmable interface over Laplace, backed by a reusable local-node runtime. A page is a composition of controls over a typed query, selection, result and available operations. Chess, LaTeX, DNA, backgammon, source administration and other domains supply data, meaning and suitable view recipes; they do not each acquire a private application stack.

The user's computation serves their own work: reconstructing their content, browsing, comparing, ingesting or executing their selected operation. Reduced server traffic is a consequence of that local work. This design does not enlist idle devices for unrelated work, automatically publish personal observations, or require a donation/mining/federated-training scheme.

The client retains exact reusable content, IDs, immutable versioned physicalities and selected local information. It is not primarily a store of stale answers. Labels, memberships, current authority, evidence/standing and query results have explicit dependency/update rules. The same native semantic operations deconstruct, resolve, reconstruct and calculate locally or on a permitted remote node. C#, SQL and browser bridges marshal/orchestrate; they do not redefine identity, geometry, cognition or source semantics.

### How to read the consolidated review

This document supplies component arguments, runtime boundaries, dependencies and work packages. Retain the detailed requirements in:

- [DATA_BROWSER_AND_SOURCE_LINKS.md](DATA_BROWSER_AND_SOURCE_LINKS.md): raw/enriched browsing, top-N and source-to-content navigation.
- [SCHEMA_DRIVEN_INTERFACE_REFERENCES.md](SCHEMA_DRIVEN_INTERFACE_REFERENCES.md): inspected SpecEditor/CIEDigital mechanisms and exact-content correction.
- [SCREEN_CONTRACTS.md](SCREEN_CONTRACTS.md): universal scope, rich views, selection and proposed interaction layouts.
- [WORKSPACE_BEHAVIOR_AND_RESPONSE.md](WORKSPACE_BEHAVIOR_AND_RESPONSE.md): task-led composition, aggregate drill-through, loading and label behavior.
- [AUTH_AND_TRANSPORTS.md](AUTH_AND_TRANSPORTS.md): common identity/authority and public transport profiles.
- [Operator recovery](../OPERATOR_RECOVERY_AND_OBSERVABILITY_REVIEW.md): database-independent administration and lifecycle.
- [ACCEPTANCE.md](ACCEPTANCE.md) and [ISSUE_TRACEABILITY.md](ISSUE_TRACEABILITY.md): evidence requirements and existing ownership.

No field, modality, operation or prior approved requirement is removed by these groupings. Earlier generic references to a client cache are refined here: durable canonical/personal storage, derived perfcaches, mutable heads and temporary result views are different lifecycles.

## 2. The shared vocabulary: what every layer passes

Do not pass an arbitrary object and infer meaning from property names. Generate the vocabulary from the existing native/binding contracts and authorized installed schema, then describe result projections with the same types. Schema discovery must do the repetitive work; users should not configure a semantic registry merely to browse an ordinary table.

| Contract | Required arguments/state | Purpose |
| --- | --- | --- |
| RecordRef | Record kind/namespace, full canonical or record key, necessary revision; occurrence/context reference when relevant | Typed address; a label, URL or similarly sized hash is not enough to resolve its target |
| Locator | RecordRef, coordinate-space identity, ordinal/range/path/region, indexing unit and orientation where applicable | Identifies a constituent, board location, source span, media interval or sequence interval without conflating their coordinate systems |
| FieldDescriptor | Stable field ID, value type, cardinality, nullable/missing dispositions, units/precision, label/description, origin/provider, filter/sort operators, authority, semantic annotations | One field definition drives display, input, query validation and documentation |
| RelationDescriptor | Relation ID/version, typed participant roles/endpoints, arity, direction, cardinality, lookup/traverse operation and scope | One relationship can be inspected, followed and filtered from either end; n-ary roles are not erased |
| CollectionDescriptor | Collection/query identity, schema revision, fields, keys, relations, available operations, default projection/order and capabilities | Supplies the list and its controls before or independently of returned rows |
| QuerySpec | Source scope, typed filter AST, relationship binders, projection, grouping/aggregates, post-group predicates, ordered sort/ties, top-N, page/cursor and read boundary | Defines what is being requested; not SQL or an arbitrary executable string |
| ResultDescriptor | Result/continuation reference, output schema, inputs/program/dependency versions, coverage/completeness, lineage/contributor operations, available views | Computed, grouped, empty and heterogeneous results have proper metadata too |
| Selection | Explicit records/locators, an exact snapshot/query selection or ranges; included/excluded items and focused item separately | Preserves one/many/all-matching meaning without downloading or silently selecting the entire database |
| OperationDescriptor | Operation/program ID and version, input/output schemas, cardinality, effects, authority, dependencies, placement/provider profiles, progress/cancel/replay and failure semantics | Supplies forms, actions, API calls, local execution and result inspection |
| ViewRecipe | Compatible shape/roles, component bindings, named layout slots, priority/minimum sizes, responsive transitions, selection mapping and presentation settings | A domain-labelled page is an instance of this composition, not an independent controller |
| NodeCapability | Loaded package/ABI, execution/storage providers, admitted numerical profiles, topology/resource budget and reachable scopes | Allows placement based on real capability, not browser name or optimistic feature flags |
| ResidencyPolicy | Ownership/durability class, namespace, pin/eviction rules, dependency versions, offline/sharing permissions and quota policy | Separates personal data from dispensable acceleration and separates possession from permission |

Record IDs, byte lengths, wide ordinals and numeric values retain their exact types. Do not route every `proposition_id` to the Entity page or coerce every 64-bit value to JavaScript Number. `King` and `king`, ordered permutations, whitespace and combining-codepoint sequences remain distinct exact content. Case-folded or normalized comparisons are explicit operations, not storage identity rules.

A data cell's present/null/unobserved/not-applicable/withheld/unresolved disposition is separate from the UI resource's loading/refreshing/failed state. A loading field cannot become a numeric zero. An empty result still has a schema. Unknown types remain safely inspectable and explicitly lack unsupported operations rather than disappearing.

### Runtime generics and schema dynamics are complementary

Compile-time generics supply reusable, type-checked native kernels, bindings and component implementations. Runtime descriptors select those implementations for discovered tables, relation paths, projections and operations. A single numerical field control may serve thousands of columns. A new supported numeric column should appear without new page code. A genuinely new semantic value kind needs one shared adapter and operation integration, not repeated implementations in every source/page.

Physical introspection reveals storage shape and declared keys; it does not invent that an arbitrary byte array is a trajectory or that two names identify one person. Native annotations and source/recipe metadata supply those distinctions. Descriptors are versioned and permission-filtered before client delivery; client-posted runtime class names or arbitrary column paths never expand the allowed surface.

## 3. Native execution and targeted data movement

The existing [architecture boundaries](../../architecture/BOUNDARIES.md) place semantic ownership in C/C++ and make SQL and C# peer orchestrators. Preserve that division across device placement.

    UI interaction / API request
        -> typed operation or query + exact selection + scope
        -> common planning/admission and dependency resolution
        -> permitted native execution provider
             local packaged native engine
             browser worker hosting the same C/C++ core compiled to WebAssembly
             PostgreSQL-server native extension
             explicitly selected remote Laplace node
        -> typed result + dependency/coverage information
        -> shared controls and local retained content

These are proposed physical providers, not equivalent availability claims. The browser profile needs actual builds, compatible libraries, storage adapters, numerical parity and UI responsiveness evidence. A WebAssembly target is not a way to run an unchanged PostgreSQL extension in a browser. PostgreSQL-specific access stays in its adapter; common semantic kernels remain shared. A platform lacking a provider reports the exact gap or uses an explicitly permitted remote provider, never a silent JavaScript semantic imitation.

### Normal open/inspect path

1. Resolve the selected RecordRefs through the local native presence/index path.
2. Determine the dependency closure needed for the requested view/operation/range, not every reachable edge in the world.
3. Reuse verified resident atoms, compositions, physicalities, artifacts and compatible perfcache sections.
4. Fetch missing references in typed batches or declared packs, with cancellation, integrity and current disclosure scope.
5. Native validation checks returned identities, framing and applicable manifests before publication into local storage.
6. Execute reconstruction/calculation once through the canonical operation and expose the result to the view.
7. Retain verified reusable data and selected private output under their correct residency classes. A later view reuses it.

Demand discovery may take successive batches when new dependencies become known; independent frontier work can proceed together. Do not create one network/database request per character, vertex, cell or label. A server may return a bounded requested closure or referenced pack to avoid round-trip chains. Sending an entire local content inventory to discover a small missing set is not required and can disclose private interests. A probabilistic presence hint cannot certify that an object is present.

Known IDs support cheap exact retrieval; they do not eliminate the need for semantic discovery. A query for all matching entities must execute against a provider with the relevant scope/index coverage. A locally cached page cannot answer a global top-N as though it were the whole database. Expose local-only/selected-replica scope when that is what was searched.

Hash/integrity verification establishes the received bytes and identity, not the truth of an assertion or the completeness of a search. Current authority, independent evidence, signed provenance where applicable and native completion rules remain separate. A local execution receipt is not automatically a remotely trusted statement of correct execution; remote acceptance uses the existing verification/admission contract.

### Proposed service-facing signatures

These are design notation over existing generated native contracts, not new exported ABI symbols:

    describe(scope, revision, requestedCapabilities) -> EffectiveDescriptors
    resolveMany(refs, requiredClosure, scope, budget) -> PresenceAndCoverage
    fetchMissing(refs, manifest, scope, byteBudget, cancel) -> VerifiedBlocks
    execute(programRef, inputRefs, arguments, context, providers, budget) -> ExecutionHandle
    reconstruct(rootRef, locatorRange, representation, context) -> TypedResult
    inspectContributors(resultRef, groupingKey, scope, cursor) -> TypedResult
    observeHeads(dependencies, sinceRevision, scope) -> ChangeStream
    synchronize(selectedRoots, peer, direction, authority, checkpoint) -> ExchangeHandle

The common executor owns semantic dependency reasoning and placement. The host owns I/O and lifecycle required by its adapter. UI components do not choose private numerical algorithms or silently send private input to a remote fallback.

## 4. Resident content is not a cache of permanent answers

| State | Local treatment | Refresh/invalidation |
| --- | --- | --- |
| Exact atoms/compositions/artifacts | Store verified content-addressed objects; reuse shared substructure | Immutable bodies do not change under the same identity; residency may be reclaimed only according to ownership/pins |
| Versioned physicalities/coordinates | Retain exact recipe/geometry version and structural form with the object | New recipe/epoch produces another selected representation; never overwrite or mix versions silently |
| Source label/alias content | Store exact strings and attributed label evidence as content | Which label is currently eligible depends on language, audience, context, evidence and realization recipe |
| Schemas/type/operation descriptors | Retain versioned compiled metadata | Refresh installed/authorized descriptor heads; identify affected controls and invalid filters |
| Evidence, standing, memberships and current heads | Retain exact history where authorized and track selected/current pointers separately | Subscribe/reconcile changes; revoke stale grants and rebuild affected projections under their native rules |
| Derived perfcaches | Retain validated typed modules with dependency/recipe/ABI metadata | Stage and atomically activate compatible generations; existing readers retain legitimate pinned versions |
| Query results | Keep short-lived views or explicit immutable historical snapshots | Dependency changes invalidate 'current' interpretation, not exact old inputs; reuse only with valid scope/coverage |
| User-authored local data/drafts | Durable private working state, with explicit export/backup and native canonicalization when committed | Never evict as ordinary cache; private creation is not automatic synchronization/public testimony |
| UI preferences/selection/layout | Persist user view recipes and locations | Reauthorize on restore; adapt view revisions without losing content or silently changing query meaning |

A label cache is keyed by its actual dependencies, not only Entity ID. A physicality lookup includes selected recipe/geometry where necessary. A projection over shared canonical content also includes disclosure scope. Time-to-live alone cannot determine semantic validity. Avoid one global epoch invalidating every cached object after an unrelated write.

Local indexes record exact coverage: available key ranges/partitions, reference versions, completeness and last reconciliation. Local absence means not present in this store unless the coverage contract establishes something stronger. Offline result and online-global result must not be conflated.

### Storage providers and durability classes

Proposed browser baseline: IndexedDB for asynchronous indexed metadata and local records; OPFS for large binary blocks/perfcache artifacts where supported. IndexedDB is designed for substantial structured browser storage; OPFS supplies origin-private files and worker-only synchronous access facilities [W2,W3]. Both are physical adapters, not private semantic databases.

Persistent packaged nodes can use admitted filesystem/native-store providers and their declared database placement. The provider must demonstrate the durability of each stored class; browser-origin storage must not be advertised as having PostgreSQL's durability guarantees. Browser clearing, eviction, private-session behavior and quota failures require explicit handling. OPFS shares browser storage constraints, and deleting site data removes it [W3]. A sole local copy of irreplaceable personal data needs visible backup/export and storage-status support. Clearing expendable data must not silently discard personal drafts or unsynchronized canonical work.

Retain a portable storage interface: bulk presence/read/write, transaction/checkpoint where supported, range read, manifest publication, pin/unpin, quota/health, verified export/import and recovery. SQLite or another index provider may be admitted when it earns a concrete benefit, but no particular database brand is the product architecture. Do not force SQL Server LocalDB or localStorage as a cross-platform semantic foundation.

Cross-tab ownership, shared in-flight reads and activation must coordinate. Closing one inspector releases its subscription; it must not cancel a fetch still needed by another view. Corrupt/incompatible modules are isolated and refetched/rebuilt, never consumed as valid data. Upgrade can change derived modules independently of retained canonical/private state.

### Initial load and useful prefetch

Load the application shell, safe static control labels, session scope, compatible descriptor head, selected node's capabilities and the starting working set. Reuse installed/native modules and resident public foundational data. Defer unrelated datasets, full graph expansion, global counts and viewer packages not yet needed.

A foundational pack can be an explicitly selected installation asset rather than fetched once per record. For scale, 1,114,112 positions times four eight-byte components is exactly 34 MiB of raw coordinates; adding a 16-byte key per position yields 51 MiB before properties, indexes, witnesses and headers. This is a sizing calculation, not an actual artifact size or permission to preload the full pack on every device. It illustrates useful bulk reuse; the same pack/manifest mechanism applies to other typed reference data.

Prefetch only relevant dependencies/adjacent inspected ranges within the user's selected resource policy. Surface memory/storage/network use and allow cancellation or tighter limits. Viewers share prefetch and presence services rather than each requesting duplicate objects. Do not call unrelated background work 'prefetch.'

## 5. Shared runtime services

| Service | Inputs | Shared responsibility | Not its responsibility |
| --- | --- | --- | --- |
| Descriptor service | Scope, package/schema heads, selected projection | Effective type/field/relation/operation catalogue and change notices | Inventing domain meaning from names |
| Content resolver | Typed refs, closure/range request, versions, budget | Local presence, batch missing-data acquisition, exact validation and coverage | Global search from an incomplete local subset |
| Native execution host | Program, typed input buffers/refs, arguments, context, providers | Generated bridge, worker lifetime, cancellation and canonical execution outputs | Another managed/JavaScript engine |
| Residency manager | Ownership class, manifests, pins, budget and storage provider | Durable versus expendable data, quotas, staging/recovery and versions | Deleting user work to improve cache-hit figures |
| Query coordinator | QuerySpec, source/provider scope, request generation | One logical query, bounded result delivery, stale-response rejection | Per-panel private ranking or source-specific SQL |
| Realization service | Visible typed refs, language/context/versions | Batched eligible labels and content realization | Renaming/reminting entities or silently changing case |
| Dependency observer | Current/pinned head references and scope | Mark affected current projections stale; reconcile updates | Recalculating every view on any global change |
| Selection/navigation store | Query/result, focused ref, selected refs/ranges, locators, view recipe | Cross-view selection, history, pins, deep links and return state | Treating a selected row as effect authorization |
| Operation dispatcher | Descriptor, bound args/selection, authority/impact approval | Applicable actions, exact invocation, request/job ownership and recovery | Performing a mutation when someone merely opens a view |
| Synchronization adapter | Explicit local roots/peer/scope/checkpoint | Existing native federation and private/shared/public exchange | Automatic publication or independent sync semantics |
| Measurement service | Common request/execution/storage events | End-to-end timing, transfers avoided, CPU/memory/I/O, first useful result and diagnostics | Synthetic percentage/completion or telemetry containing credentials |

These services are assembled once for a client/node and injected into controls. A long-lived session context is a handle to governed state, not dozens of prop copies or a global mutable object whose implicit settings change query meaning.

## 6. Reusable control catalogue and arguments

The following is the implementation breakdown, not a requirement to ship one class per row or use a particular frontend framework. Components may share lower-level implementations. Common inputs are `context`, `descriptor`, `resource`, `selectionChannel`, `viewState` and supported event callbacks. Common lifecycle supplies loading/error/refreshing status, permission/readiness, focus, cancellation, exact-value copy, sizing and diagnostics. A control does not acquire its own database connection, account model, cache or scheduler.

### 6.1 Values, fields and query construction

| Control | Main arguments | Reused behavior |
| --- | --- | --- |
| ValueDisplay | value, FieldDescriptor, displayFormat, optional RecordRef | Correct text/unit/precision/disposition; exact copy and field origin |
| ValueInput | FieldDescriptor, value, constraints, onChange | Type-aware editing, parse errors, nullable states and units; no identity-changing implicit trim |
| EnumFlagsInput | enum/flag descriptor, selection, allowed operators | Names/options and bit tests from metadata |
| ReferencePicker | target descriptor, scope, current refs, cardinality | Bounded target browse/search and exact selection |
| FieldPicker | result schema, selected fields, allowed purpose | Select display/filter/sort/group fields with origins and availability |
| OperatorInput | field type, selected operator, operand schema/values | Correct arity and widgets for equality, ranges, membership, null and typed operations |
| FilterBuilder | descriptor, filter AST, relation binders, onChange | AND/OR/NOT groups, same-related-record grouping, explicit quantifiers and comparison semantics |
| SortGroupControls | schema, ordered clauses, aggregates, top-N | Stable sorting/tie rules, pre/post-group distinction and result limits |
| ScopePicker | permitted worlds/sources/releases/time, current scope | Make scope visible and selectable without widening permission |
| SavedViewControl | QuerySpec, ViewRecipe, owner and revision | Save query/layout, not access rights or an eternally current answer |

Query conditions use stable field/operator identifiers with typed operands. Text input and visual builder lower into the same admitted query program. Missing/null and ANY/ALL/NONE semantics are explicit. A filter on two attributes of the same occurrence must not accidentally match two different occurrences. Filtering/ordering occurs before global top-N; page-local hiding remains a separately labelled view action.

### 6.2 Collections and navigation

| Control | Main arguments | Reused behavior |
| --- | --- | --- |
| CollectionBrowser | CollectionDescriptor, QuerySpec, selection, layout recipe | Coordinates field/filter/sort, results, detail and actions |
| ResultGrid | ResultDescriptor, row provider, columns, order, selection | Virtualized/paged accessible grid; linked cells; zero-row schema; no per-cell fetch |
| RecordLink | typed RecordRef, optional realized label, navigation intent | Open/peek/pin/new-tab/copy with coherent scope and typed resolution |
| RecordInspector | ref, selected schema/versions, visible sections | Shared field/structure/relation/provenance/raw inspection |
| RelatedCollection | source refs, RelationDescriptor, role/direction, query | Same browser over incoming/outgoing/n-ary participants and related results |
| StructureViewer | root refs, grammar/structural projection, ordinal/range, expansion | Ordered/shared nodes, repeated runs, exact child/parent navigation and bounded expansion |
| SelectionTray | explicit/query/range selection, focused locator, actions | Distinguish focus from membership and one/page/top-N/all matching |
| ComparisonWorkbench | result/ref sets, comparison basis, matched keys, scale policy | A/B or plural comparison with compatible units, common scope and linked source records |

Default inspection should show useful content, not demand that a user read a protocol receipt. Exact identity and provenance stay one interaction away. Opening another record adds to navigation history but does not silently replace the working cohort. Resize/expand restores focus and the same result, not another query.

### 6.3 Projections and interactive surfaces

| Control | Main arguments | Applications without a private page implementation |
| --- | --- | --- |
| GraphSurface | typed node/edge/role result, layout settings, label budget, locators | Semantic/evidence/dependency/source graphs; decluttering is presentation, not unreported data deletion |
| GeometrySurface | physicality refs, coordinate class, projection, camera, overlays | S3 placement, packed-address visualization and realized curves kept explicitly distinct |
| OrderedSequenceView | sequence refs, coordinate-space/unit, range, annotations, orientation | Text constituents, DNA residues, move trajectories, source/token streams |
| AlignmentView | ordered inputs, admitted alignment result, correspondence locators | Sequence, translation, source/AST and structural comparison; UI does not invent alignment |
| DocumentSourceView | content refs, source map, grammar, selected spans, render mode | Prose, code, LaTeX source, source artifacts and their structural links |
| FormulaSurface | mathematical representation, renderer profile, source map, selected node | Typeset formula plus source/AST navigation; untrusted typesetting effects remain constrained |
| BoardSurface | topology/layout, cells/locations, piece/stack layer, state ref, legal-action descriptors, orientation | Chess, backgammon and other board/track games without hardcoded 8x8 or one-piece-per-cell assumptions |
| MediaSurface | artifact/ranges, playback timeline, annotations, source locators | Image regions, audio/video and synchronized transcript/structure |
| HeatmapPlot | grouped result, row/column keys, measures/units, scale, missing-state policy, contributor operation | Piece-square tables, source coverage, sequence/geometry/operational measures |
| TimelineReplay | event/state sequence, clocks, selected index, transport settings | Game replay, execution stages, edits, media annotations and state histories |

Rendering styles and pure display transforms may run in the frontend. Semantic transformations, game transitions, interpretation, geometry metrics and aggregate calculations use the native operation owner. A board drag emits a proposed typed action; it does not implement legal moves privately. A replay step changes the inspected occurrence, not the live game. Backgammon dice/cube/match rules are operation state rather than UI randomness. Source and parsed meaning remain distinct for formula or sequence data.

Controls synchronize by typed locators, including occurrence/range and selected version. A repeated word or repeated position cannot make all occurrences the same selection. Keyboard events belong to the focused surface, not every open board or graph. Numeric overlays retain exact values, units and dependencies even when presentation is rounded.

### 6.4 Operations, explanation and administration

| Control | Main arguments | Reused behavior |
| --- | --- | --- |
| OperationForm | OperationDescriptor, bound inputs/selection, argument values | Schema-driven input validation and grouped basic/advanced controls |
| ActionBar | applicable operation descriptors, selection, readiness/current authority | Display actions relevant to this input with actual single/plural/batch capability |
| EffectReview | admitted plan reference, exact target/revision, impact, recovery/backup | Confirm only the reviewed mutation; not every ordinary read |
| RunMonitor | execution/job ref, stages, progress/cancel/checkpoint capabilities | One job/status component for ingestion, analysis, export, restore or a calculation |
| ContributorInspector | aggregate result, cell/group locator, contribution and reference-population queries | Follow displayed value -> real inputs -> exact occurrence/source; separate direct contributors from normalization population |
| ProvenanceStandingPanel | selected assertions/results, evidence/arena/recipe versions | Typed support, dependence, disagreement, rating/RD/volatility when applicable, not a global score |
| LogEventViewer | authorized event source, cursor/range, filters, follow mode | Bounded tail/search/export, preserved scroll and explicit retention gaps |
| StorageNodeInspector | node/storage descriptors, owned/pinned/derived sets, resource stats | Local data residency, package/provider status, sync choice, backup/export and recovery |

Initialization/recreation/seed orchestration composes these same operations/forms/monitors while retaining its separately durable management boundary. Basic recovery must not depend entirely on the product database being destroyed. Destructive targets are enrolled/scoped; an operation form is not a browser SQL shell.

## 7. A concrete composition contract

Illustrative notation; names are not shipped APIs:

    Workspace(
      subject = queryOrRecordSet,
      context = activeScopeAndNode,
      descriptor = effectiveResultSchema,
      selection = sharedSelection,
      recipe = taskLayout,
      services = clientRuntime
    )

    HeatmapPlot(
      result = selectedAggregate,
      axes = [rowField, columnField],
      measure = measureField,
      scale = comparableScale,
      selectionChannel = sharedSelection,
      onInspect = openContributorInspector
    )

    BoardSurface(
      state = focusedStateRef,
      topology = declaredBoardTopology,
      layers = declaredPieceAndAnnotationLayers,
      actions = applicableNativeActions,
      selectionChannel = sharedSelection,
      mode = inspectReplayOrInteract
    )

A supplied recipe binds fields/roles into existing components. It does not duplicate the components or execute arbitrary code downloaded from a source corpus. Role compatibility determines whether a result supports a board, matrix, graph or formula. A source filename, English label or `if domain == chess` block does not choose semantic behavior.

The recipe contains layout intent: primary workspace, adjacent primary actions, optional inspector, compact status and an expandable secondary panel. Specify minimum usable sizes, resizing priority and a deliberate narrow-screen single-pane transition. Do not append another full-width card whenever another operation appears. Discoverability comes from a task-oriented action menu/command palette, not displaying every opcode at once. Users can customize and save useful defaults without first becoming dashboard designers.

## 8. What a new modality/provider supplies

A module contributes **only what cannot already be expressed by existing types, recipes, operations and renderers**:

| Contribution | Required description | Inherited instead of rebuilt |
| --- | --- | --- |
| Source/grammar profile | Exact source/container grammar, loss/reconstruction, canonical lowering, semantics/provenance | Hash identity, batching, presence, persistence, retry, source jobs |
| Type/role annotations | Typed value/participant roles, units, coordinate-space and declared reference meanings | Primitive field controls, links, schema discovery and raw inspection |
| Native operations | New irreducible rules/calculation kernels or compositions of existing ISA operations | Execution spine, allocation/resource policy, cancellation, errors, receipts and authority |
| Representation adapter | Typed scene/document/board/formula/sequence projection; source/selection mapping | View host, common controls, selection, layout, accessible navigation and history |
| View recipes | Compatible components, field bindings and task defaults | Separate frontend routes/controllers/forms for every dataset |
| Package/conformance declaration | Dependency/ABI/profile pins, positive and negative fixtures, installed integration boundary | Private installer, auth system, cache, local database, or synchronization implementation |

Example integration sketches:

- **LaTeX:** exact source and include/macro dependencies -> parser/typesetting provider -> syntax and rendered-region correspondence -> source/formula/structure viewers. Changing a macro can affect multiple regions; cache dependency scope must reflect the actual provider. Rendering is not mathematical proof, and untrusted TeX must not acquire arbitrary filesystem/process/network access.
- **DNA:** exact source artifact -> declared sequence/alphabet/reference conventions -> sequence/annotation/alignment results -> existing range/sequence/alignment/table viewers. Store whether coordinates are base-zero/base-one, inclusive/half-open, strand/orientation and reference version. Biological normalization or reverse-complement is an explicit operation and cannot rewrite original exact source identity.
- **Backgammon:** declared board locations, piece stacks, turns, dice, cube and match state -> native rule/transition/evaluation operations -> the shared board/replay/operation controls. No reuse of chess's move rules merely because both render pieces.
- **Chess:** those same service/control families bind player/game/position/move/clock/analysis descriptors; piece-square results are inspectable measures, not a private heatmap database.

### Making 'one-day snap-in' an engineering target

For an extension whose necessary native primitives/providers already exist, the integration target is an installed usable vertical path in one working day: registered data/profile -> generated browse/filter -> appropriate composed view -> one meaningful operation -> exact result/contributor inspection -> reload/reuse. Record the starting dependencies and actual elapsed integration effort. No successful timing is claimed here.

The strong test is a held-out new source/domain introduced without editing shared auth, query coordination, persistence, selection, navigation, caches or per-screen filter code. One new semantic type may add one shared adapter; subsequent users of that type inherit it. A substantial new algorithm/rule system still requires implementation; it does not justify rebuilding the interface infrastructure. Merely opening a raw file or adding a menu entry does not satisfy the snap-in target.

## 9. Responsiveness and visible correctness

A first useful result includes the labels, units and controls needed to use it. Static labels come from local metadata. Resolve visible record labels with the bounded result or in a shared batch; no label-per-cell request waterfall. Existing compatible local content should render without refetching. Missing eligible labels stay a visible implementation/data gap, not a blank success.

Independent panels load independently, but values from incompatible epochs cannot be combined as one calculation. Retain an old result during refresh only when unmistakably labelled as the old scope/version. Late requests never overwrite the current filter/selection. Resizing, rotating, stepping through a resident replay or changing label density cannot rerun semantic work merely because a widget changed size.

Heavy semantic work runs in an appropriate worker/native host, not the browser UI thread. A frame has no need to re-decode all retained structure. Shared reads are deduplicated; cancellations are subscriber-aware. Transferable/columnar buffers and explicit ownership can reduce copies where the admitted bridge supports them; measure retained and transferred bytes rather than promising zero-copy everywhere.

The existing proposed budgets remain one review envelope: 200 ms p95 immediate feedback and one-second p95 bounded useful reads on the declared fixture, including necessary labels; cold/warm, first install, active ingestion and client capability are reported separately. Do not invent easier per-module budgets or treat a 30-second initial browse as acceptable because it has a spinner.

Trace interaction -> local presence -> dependency fetch -> queue/admission -> native/database work -> realization -> transfer -> first useful render. Also measure local CPU/memory, storage bytes, cache/module hit coverage, actual requests/crossings and cancelled superseded work. A high cache-hit ratio is not success if local main-thread work freezes the UI.

## 10. Granular work packages under existing issues

These are execution units within existing ownership, not parallel epics. Acceptance refs point to existing UX/DBR/AUTH/INT/EVO/QA criteria; all remain unrun. Each package includes its generated contract, applicable bindings/API, actual controls and documentation together after design approval.

| Package / owner | Build work | Required dependency | User-visible exit |
| --- | --- | --- | --- |
| G01 Effective descriptors — #268 / #5 | Schema introspection bridge; native annotations; result projections; field/operator/relation/operation metadata; generated client bindings; version/authority filtering | Existing native/binding law | Newly added supported field appears in all relevant controls, including empty results, without page edits |
| G02 Native client execution — #10 / #21 with #268 | Packaged native and proposed worker/WASM adapters; buffer ownership; async I/O continuation; capabilities/numeric profile; common errors/cancel | G01 interfaces and native providers | Same selected operation/identity/reconstruction runs on admitted local and server providers; actual gaps disclosed |
| G03 Local canonical store and presence — #15 / #14 with #65 | Exact blocks/refs/manifests; batched presence; typed indexes; private durable state; quotas/pins/recovery; storage adapters | G02 codec/persistence contracts | Reload opens verified resident content and own work without unnecessary re-download; cache clearing preserves personal state |
| G04 Targeted exchange and heads — #65 / #64 with #268 | Requested dependency closure, missing-ID batches/packs, integrity and disclosure; version/head reconciliation; explicit private/shared/public sync | G03 and authority | Changed view retrieves only needed missing data; historical/current and local/global scopes remain distinct |
| G05 Query/result coordination — #268 / #17 / #60 | Typed filters and relation binders; ordered top-N/page; grouping/post-group filters; coherent read boundaries; result lineage; supersession/cancellation | G01, G02; G03/G04 enrich locality | Correct filtered/sorted result beyond page one, no join duplicates, no cached-subset masquerading as whole scope |
| G06 Primitive controls and data grid — #68 / #268 | Value/input/operator/field/reference controls, filter builder, grid, selection tray, local metadata labels and reusable request states | G01 and G05 | Browse and manipulate unrelated collections with the same controls; exact wide values survive copying/export |
| G07 Workspace/layout/selection — #68 / #172 / #176 | Task recipes, named slots, resizable panes, shared locators, history, focus/shortcuts, saved layouts and compare state | G06 and stable refs/results | No stacked-card hunting; select/open/compare/expand/back retains exact working context |
| G08 Rich surfaces and contributors — #68 / #174 / #176 | Graph, typed geometry, board topology, sequence, document/formula, media, heatmap/replay adapters; contributor inspector | G07 and actual native providers | Cell/node/span/move -> real contributors -> related data -> back; no dead-end visualization |
| G09 Actions, jobs and recovery — #268 / #264 / #265 / #266 | Operation forms, applicability, preflight/impact, approval binding, durable jobs/logs and DB-independent recovery | G01, G06, existing operation owners | Same mechanics run ingestion, calculation, export and restore; inspect never mutates implicitly |
| G10 Local privacy and synchronization experience — #64 / #65 / #62 | Local owner namespace; sharing selection; account/link/session policy; storage status; backups/export; revocation/conflict UX | G03/G04 and authority | Private work remains local until selected for exchange; stale offline state never silently authorizes server effects |
| G11 Module packaging and extension DX — #5 / #10 / #68 / #268 | One manifest/profile path, descriptor docs, compatible renderer registration, sandboxing, exact ABI/provider pins and extension example | G01..G09 incrementally | Held-out extension supplies only new semantic/profile material and inherits the rest |
| G12 Installed user journeys/performance — #22 / #54 with all owners | Real client+provider fixtures, local/server parity, whole-task timing, missing-ID byte/read counts, failure/permission cases | Each preceding vertical slice | User can actually work through connected real data; docs/control stubs do not close implementation |

### Delivery order without another bottom-up waterfall

**Slice A — usable generic browser:** G01/G05/G06 with the necessary G02 provider and G07 state. Start with multiple materially different existing record families, not a special Unicode page. Show filters/order/top-N, labels, detail, related rows and return navigation on real data.

**Slice B — local retained working set:** add G03/G04 and the relevant G10 ownership controls to the same journey. Prove second-open reuse, one changed dependency, batched missing acquisition, exact reconstruction and private local creation. Unsupported browser/native providers remain explicit work, not a reason to remove local-node goals.

**Slice C — useful analysis and operations:** add G08/G09 to the same selected data. Demonstrate heatmap -> contributors -> source/game/structure; plural comparison; one configured ingestion and actual result browsing; database recovery preserves its management view.

**Slice D — extension and cross-platform evidence:** complete G11/G12 with at least two unrelated snap-ins and a held-out extension, across the agreed packaged/browser/remote provider matrix. New native algorithms are separate named work, not hidden behind a success badge.

Work on independent contracts, primitives, viewer integrations and storage adapters can proceed concurrently once their interfaces are reviewed. Integrate into the one owning change chain; do not leave a native branch, a UI branch and a cache branch each claiming its own finish line.

## 11. Feature demonstrations that establish the design works

The following demonstrations refine existing acceptance instead of creating a test-count goal:

1. Add a nullable numeric field and a typed relation to a fixture collection. Refresh metadata: useful filters, exact display, sort, links and related-grid controls appear without new source-specific UI code.
2. Open a shared structure, close the app and reopen. Verified local bodies/coordinates are reused; missing children alone are requested in batches; one altered/corrupt body is rejected. A current source/head change invalidates the relevant view, not all immutable content.
3. Apply two conditions to the same related occurrence; compare with separate-existence predicates. Correct rows appear before top-N, independent of paging. A result computed over an incomplete local subset cannot claim global completion.
4. Select multiple records, open geometry/graph/board/sequence views, focus one occurrence, expand and return. Exact selection/version and view state survive; no semantic execution is caused solely by resize.
5. Select a heatmap cell, inspect the measure and reference population, filter contributors, pin two cohorts, open a contributor at its exact location and return. Missing support is not zero; colour scale and units are declared.
6. Keep own local data while signed out/offline under the approved local-owner policy. No automatic public upload. Reconnect with a revoked remote grant: no restored stale permission or hidden submission of queued privileged effects. Clearing caches cannot erase unsynchronized owned work.
7. Run one typed operation locally and on a permitted server provider with exact input/program/dependency identities. Exact outputs meet bitwise contracts; floating outputs meet their declared numerical contract; no untested blanket equivalence claim.
8. Delay a secondary panel, fail a label batch, rapidly change filters and fill a storage quota. The primary task remains understandable and responsive; no late-result overwrite, unbounded retry, silent data loss or blank-label completion.
9. Integrate a held-out modality without modifying generic orchestration, auth, persistence, filters or navigation. Demonstrate a meaningful operation and contributor/source readback, not only a rendered example.

## 12. Decisions needed to authorize implementation

Settle the concrete choices, not whether these product behaviors are optional: the initial frontend host/framework; first native/browser execution profiles and their actual dependency gaps; storage/durability and local identity policy; initial type/operator/renderer catalogue; task-layout visual prototypes; exact source/cohort fixtures; and one numerical/latency/resource envelope. Use the same reviewed descriptor and permission set across clients.

The full architecture cannot be proven future-proof by declaring interfaces. The intended evidence is that unrelated and held-out domains use the same machinery without core rewrites, while native semantics and usable interaction remain intact. 'One-day integration' is a measurable DX target with declared prerequisites, not an unsupported promise that all novel rules or algorithms already exist.

## References and observation boundary

Repository source baseline inspected: `f02d78730aa6312885784a3a986ebb243810756f`; prior review head: `d772637e61671fd33c76dec64d4afb56ead0f12f`. [BOUNDARIES.md](../../architecture/BOUNDARIES.md) establishes semantic ownership, generated ABI and common native execution/perfcache lifecycle. #14 already owns typed perfcache modules and coherent handoff. #65 already owns authorized content-addressed node exchange; this blueprint does not replace either. The pinned SpecEditor/CIEDigital source observations remain in the linked reference review.

- W1: Emscripten, C/C++ and JavaScript interaction: https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html
- W2: MDN, IndexedDB: https://developer.mozilla.org/en-US/docs/Web/API/IndexedDB_API
- W3: MDN, origin-private file system, worker access and storage limits: https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system

These references support proposed physical mechanisms, not proof of Laplace's present target support. No application was built/deployed, live database queried, source ingested, credentials configured, or performance measured by writing this blueprint. Existing runtime files and source repositories remain unchanged.