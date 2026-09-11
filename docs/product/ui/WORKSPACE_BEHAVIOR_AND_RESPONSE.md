# Working with data: layout, aggregate drill-through and response behavior

**Status: design refinement, not application implementation or executed acceptance.** Part of PR #272 under #68/#268; this extends [SCREEN_CONTRACTS.md section 10](SCREEN_CONTRACTS.md#10-universal-collection-workspace-every-data-type-and-capability), [the data browser](DATA_BROWSER_AND_SOURCE_LINKS.md) and [schema-driven interface references](SCHEMA_DRIVEN_INTERFACE_REFERENCES.md). It does not create another semantic engine, separate chess application or implementation queue.

## Direct correction and evidence boundary

The inventor objects to how they work with the data: views stacked without UX consideration, missing labels, and an initial display followed by another display after a reported 20-30 seconds. Chess is not an exception to Laplace. The earlier assistant wording suggesting an exception was misleading. The board, graph, glome and piece-square heatmaps are useful representations that need connected interaction, not removal or replacement by a table-only interface.

The earlier supplied play screenshot shows substantial unused main-page space while controls, move scores, exploration and a partly off-screen piece-square view compete in a narrow vertical column. The six piece-square screenshots show selectable piece categories and numerical heatmaps. Those images are visible evidence of presentation, not proof of what every underlying value measures. The later wait and missing-label description is a user report: no browser timing, request trace or live database measurement was captured by this design work. Do not attribute its exact cause to a database, label resolver, framework or particular endpoint without tracing the actual route and deployed version. The latest interruption images were not returned by the conversation file listing during this inspection; their pixels are not used to invent additional visual observations. No screenshot or browser chrome is republished.

## 1. Task-led composition, not append-another-card

The layout must answer three questions without hunting: what am I working on, what can I do to it, and what changed because of my last action? A widget count is not an information architecture. A collapsible stack of the same unrelated cards does not resolve the problem.

Use curated starting layouts over the same retained workspace state. Layout names below are proposals, not new navigation silos:

| Task focus | Primary work area | Closely coupled tools | Secondary material |
|---|---|---|---|
| Play or replay | Board, current position and selected move | Turn/clock, move list and game/replay controls adjacent to the board | Detailed corpus/engine settings, calculation history and full piece-square research open on request |
| Analyze selected data | Heatmap, graph, geometry, document, media or table at useful size | Current selection, source/cohort filters, measure and comparison controls | Selected-record/contribution inspector beside the active view |
| Compare | Aligned A/B or multi-item views and a shared difference table | Visible cohort, units, versions, orientation and scale policy | Supporting records and per-result calculation details |
| Operate | Selected source/job/installation and its actionable status | Its plan/stage/status controls and immediate failure details | Logs, configuration and dependency inspection in context |

Switching focus retains selected objects, exact position/ordinal/span, query/cohort, revisions, filters and comparison. A user can pin an analysis alongside a board; task defaults are not a ban on simultaneous work. Do not require everyone to build a dashboard before they can operate the product. Optional resizing and saved layout extend useful defaults, not compensate for the absence of them.

On desktop, the primary view and its immediate controls share one visual working area. A contributor inspector opens beside it or replaces a secondary pane, rather than appending another long card at the page bottom. When width is insufficient, choose a deliberate single-pane transition with a clear return to the preserved context; do not compress all panes into unreadable columns. Large lists have their own clear bounded scrolling, not ambiguous nested scrollbars around everything. At the reviewed wide-screen size, routine play/replay controls must not require scrolling down a research sidebar.

Group controls by effect: selection/view changes, analysis requests and application mutations are visibly different. Inspecting a heatmap, filtering evidence or replaying a game must not silently update the active playing engine, submit a move or regenerate canonical data. Applying a selected evaluator/configuration is a separate explicit action.

## 2. A heatmap is an entrance to data

The reusable interaction is:

    select a displayed value or region
    -> inspect what it represents
    -> open its contributing records
    -> filter/group those records
    -> pin and compare selections
    -> open an exact contributor in a compatible viewer
    -> return to the same analysis

This applies equally to piece-square cells, source-coverage matrices, geometric comparisons, corpus statistics and operational charts. Merely enlarging a chart or adding tooltips is not this feature.

### Concrete piece-square journey

Open the piece-square analysis from Play, Explore, a query result or saved analysis. It should occupy a useful working area, not remain a small widget at the bottom of a sidebar. Piece selectors, square coordinates/orientation, selected measure, units, source/cohort scope, version and legend are visible together. Select several pieces for aligned comparison, including all six when useful; do not require the user to memorize each panel while switching one piece at a time.

Select a knight square by pointer or keyboard. The selected cell remains outlined and identified by square/piece while its inspector opens. The inspector states the actual measure, exact value, support definition, applicable baseline/transformation and selected calculation boundary. Empty/uncovered cells remain inspectable and explain whether they are unobserved, unsupported, not applicable, prior-only or genuinely measured zero.

'Contributing moves' opens an ordinary sortable/filterable collection of the actual contributing moves and their weights/values. 'Games and occurrences' follows the next declared relationship. Filtering by source, player, date, event, outcome, side or time control is available only where the data/provider supports that dimension. A missing supported product operation is tracked as unfinished implementation; the UI cannot pretend a filter was applied to historical contributors when it only filtered the collapsed cells.

Pin the selected cohort as A; create B using another source/time/player scope; compare the same cell or multiple pieces. Use one visible color scale when units are comparable, with optional clearly labelled independent scales. A missing B value is not a numeric zero and does not support a fabricated difference. Selecting a contributing game opens the board at the relevant move; returning preserves A/B, selected squares, filters, scale, measure and scroll.

The reverse direction also works: choose/group data in a table, then open an appropriate heatmap/graph/timeline from the same result. These are views of the same query and selection, not copied data with unrelated filters.

### Narrow legacy source observation, not a replacement calculation

Inspected legacy revision: `4336f709606c4d4517db4eb81ac6c3b495bde0d5`.

- [PstGrid.tsx](https://github.com/SaltyPatron/Laplace/blob/4336f709606c4d4517db4eb81ac6c3b495bde0d5/web/src/chess/play/PstGrid.tsx) consumes `piece`, `file`, `rank`, `devPoints`, and `witness`. It supplies refresh, piece selection and cell tooltips. The cell rendering does not bind a selection or contributor-opening action. It normalizes color intensity separately for the selected piece. This is a source observation, not a fresh check of the user's deployed component.
- [LearnedPst.cs](https://github.com/SaltyPatron/Laplace/blob/4336f709606c4d4517db4eb81ac6c3b495bde0d5/app/Laplace.Chess/Service/LearnedPst.cs) projects move results onto arrival squares with black-to-white orientation mapping; its primary and fallback paths calculate differently. `BuildTables` further applies shrink/scale/centering/clamping. A displayed source statistic is not automatically the final blended evaluator value, a centipawn estimate or a count of unique games. Historical comments about measured speed/counts were not independently reproduced here.

These observations expose a product contract gap, not a reason to copy the old implementation. Five aggregate fields alone do not supply provenance, cohort identity or a drill-through operation. The common result description must include grouping keys, measure/units, recipe/version, read boundary, contributor query and reference population/dependencies. An exact reproducible query or admitted indexed lineage can provide this without persisting a duplicate membership table for every cell.

For an aggregate with centering or normalization, the reference population may include rows outside the selected cell. Explain both direct contributors and that wider dependency. Filtering contributors before recalculating and filtering the finished aggregate are different operations. Historical result inspection must not substitute present-day contributors for unavailable historical membership.

## 3. The loading sequence is part of the designed screen

Design the first frame, first useful result, refreshing state, partial state and failed state, not only the eventual screenshot. Proposed behavior:

| State | What the user sees | What remains possible |
|---|---|---|
| Opening | Stable task layout, title/context and known metadata labels; placeholders only where values are actually pending | Navigate, change selection, inspect already known metadata or cancel pending reads |
| First useful data | Bounded rows/selected object with usable labels, units, identity and links; dependent panes have their own status | Browse or act on the complete portion; do not wait for unrelated totals/graphs |
| Refreshing same scope | Previous result retained and clearly marked with its version/time while new data is pending | Continue inspecting that result; no interpretation of old values as the newly requested result |
| Changing scope | New requested scope identified; old data is either removed or unmistakably marked as the previous scope | Cancel/revise the new query; late old responses cannot replace the new selection |
| Partial | Name the incomplete component: labels, records, evidence detail, aggregate or provider; keep successful siblings usable | Retry the affected portion, narrow work or inspect its exact status |
| Delayed or failed | State the pending/failed operation and elapsed time; offer bounded retry/cancel or a real background job when supported | No invisible waiting, infinite retry loop or fake success; a known complete old result remains distinguishable |

A spinner, skeleton or optimistic paint that leaves the same routine 20-30-second wait is not a performance repair. Nor is loading only a sample and silently labelling it global top-N. Ordinary bounded reads need an implementation capable of returning their correct useful result within the reviewed interactive budget.

### Labels are part of usefulness

Static controls, table headings, axis names, square coordinates, metric units and legends come from application/type metadata and should not wait on an expensive data query. Safe navigation context and an already known selected label should be reused while the new read is pending.

Record realizations have separate states: pending, successfully resolved, no eligible label in the selected language/context, denied, unsupported and failed. Request visible rows' labels in bounded batches or deliver them with the bounded result; do not perform one independent lookup for every node/cell. Preserve exact identity even when realization is pending. A shortened technical ID can be an explicit fallback with full ID inspectable; it cannot impersonate a finished human-readable view. An empty label is not an acceptable final state.

Fallbacks do not replace work on real realization coverage. When a known label exists and is authorized, making the user wait through unrelated computation or decode a hash is a defect. A label arriving later may update its presentation without reordering the result, changing identity, stealing focus or resetting graph/board state. Permission checks apply to cached names as well as records.

## 4. Response architecture is part of UX, not a later optimization

The read contract separates bounded initial content from optional aggregates, expansive graphs, full counts and detailed evidence. Independent panes can load independently while sharing the declared query/read boundary. Do not gate the first useful record behind the slowest unrelated panel. Conversely, independently returned values from incompatible versions must not be presented as one coherent calculation.

Cancellation, bounded concurrency and stale-response rejection are generic workspace behavior. Repeated refresh or fast filter changes must not build an uncontrolled backlog. Use declared current-authority and query/version keys for any cached results; stale data is labelled and must never bypass revocation. Heavy calculations use the common durable job facility when appropriate, but labelling every ordinary browse as a job does not satisfy interactive usability.

Do not diagnose the observed 20-30-second delay from screenshots alone. Before calling it repaired, trace the actual deployed route from interaction to first meaningful labelled result: client request scheduling, API admission/queue, data/provider calls, label resolution, payload size and rendering. Record cold and warm behavior under the same declared data/load fixture. This locates the cause; it does not authorize unrelated legacy patches while design review is pending.

## 5. Acceptance tied to the user's task

Refine existing #68/#268, UX-01/03/04/05/07, DBR-02/05/17/18/24/25/29/30 and QA-01/05, rather than create another numerical completion score:

- A first-time user can play/replay with the board, current turn and routine controls together, without scrolling through unrelated research panels. Opening analysis keeps the exact game/position and returning restores the task.
- A heatmap cell opens real contributors; a supported filter changes the contributor query and resulting aggregate; A/B comparison uses declared units/scope; a contributing game opens at the relevant move; returning retains the analysis. Repeat this control/query behavior on an unrelated result family.
- Delay one secondary pane by 30 seconds in a controlled fixture. Initial bounded data and independent controls remain usable. Pending/error status identifies that pane; completion does not steal focus or reset the layout. This is a failure-isolation test, not acceptance of a 30-second ordinary initial read.
- Delay, omit and fail a label batch separately. The UI distinguishes those states from a genuinely unavailable realization; known metadata labels remain visible and existing authorized record labels remain usable. Ordinary success requires readable results, not just empty boxes.
- Deliver responses out of order during rapid filter/record changes. Only the active request generation updates the current result; linked views preserve the same occurrence/version; old results cannot silently become the new cohort.
- Reproduce the crowded-side-column layout at the reviewed desktop/narrow/zoom sizes. Routine controls stay discoverable and adjacent to their object; an expanded analysis has useful space; comparison does not require manual transcription between hidden panels.
- Use the existing proposed performance envelope in [ACCEPTANCE.md](ACCEPTANCE.md#proposed-measurable-performance-and-usability-envelope): 200 ms p95 feedback and 1 second p95 bounded management reads. Proposed extension for review: apply the bounded-read target to the first ordinary browse result **including the authorized labels and controls needed to use it**, with cold/warm/load and transport/render boundaries reported separately. This extension is not an approved or measured commitment; no delayed routine route passes merely because its shell appeared quickly.

All scenarios remain unrun design criteria. No database query/reset/seed, live engine change, service restart, application implementation or screenshot publication was performed by this document. User-visible completion means working with correctly labelled, connected data at usable speed—not merely reaching an eventual arrangement of widgets.
