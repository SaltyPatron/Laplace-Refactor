# Context-preserving inspection work surfaces

Status: inventor-direct product clarification, 2026-09-02. This is additive to the existing conversation/query/cognition, product-surface, geometry, billing/measurement, and acceptance work. It does not replace any prior issue, document, route, or implementation.

## 1. Why this is part of cognition/query correctness, not unrelated UI polish

Laplace's product surfaces are instruments for inspecting the machine that answers a query. Chat accuracy, query accuracy, search performance, evidence selection, geometry, standing, and physical-plan behavior are difficult to validate when the user can only see a small clipped card or an aggregated prose result.

A dense surface such as the glome/trajectory view is therefore part of the observability contract for the forward pass. The product should let a user inspect, at useful scale, the exact state that caused a result:

```text
whole observation / discourse
-> interpretation hypotheses
-> selected operands + obligations
-> structural providers
-> semantic/evidence providers
-> standing / contradiction / epoch
-> candidate frontier / search path
-> physical plan / measured work
-> fold / completion state
-> semantic act / result
-> receipts + ancestry
```

This does not mean every chat answer must dump all of that by default. It means the user must be able to drill from the compact answer into the machine state without losing the query that produced it.

## 2. Dense surfaces need an in-place expand/fullscreen mode

The old entity `Glome` tab currently places two interactive canvases side-by-side inside the ordinary Explore content column:

```text
Packed
  hash/address trajectory display

Placement
  real physicality.coord + realized curve display
```

That is enough for a thumbnail/overview, but not enough for serious inspection of dense trajectories, non-coplanar depth, labels, neighbors, evidence overlays, or cross-pane correspondence.

The required interaction is **context-preserving expansion**, not a redirect to another page.

A user must be able to expand a dense instrument to the viewport while retaining the exact state already being inspected. Acceptable physical implementations include an in-app top-layer/dialog/workbench or the browser Fullscreen API where supported. Navigation to a new route that reconstructs or re-queries state is not the primary mechanism.

## 3. No-redirect state preservation law

Entering or leaving expanded mode must not change the logical query/program state.

For the glome example, expansion retains at least:

- canonical entity/reference identity;
- selected tab and structural/semantic overlay mode;
- packed versus placement pane identities;
- loaded neighbor/result set and pinned query/geometry/evidence epochs;
- selected ordinal and RLE correspondence;
- 4D rotation values;
- camera/orbit/zoom state where practical;
- highlighted walk/path/receipt identities;
- filters and visibility selections;
- result/receipt IDs needed to prove the displayed data is the same calculation.

Closing expanded mode returns to the same embedded surface and state. `Escape`, explicit close, browser back behavior, focus restoration, and accessibility semantics are defined rather than emerging accidentally from routing.

A fullscreen action must not silently issue a new semantic query, choose a new neighbor frontier, re-rank evidence, or move to a new consensus/geometry epoch merely because the viewport changed.

## 4. Expand the instrument, not only the canvas pixels

Fullscreen is not just `canvas { width:100vw; height:100vh }`.

A serious inspection work surface needs enough surrounding instrumentation to explain what the user is looking at. Depending on the selected surface, expanded mode may expose:

- the main visualization at useful size;
- view/projection identity and loss contract;
- packed versus real-coordinate legend;
- synchronized ordinal/trajectory scrubber;
- structural versus semantic overlay controls;
- selected node/edge/relation details;
- provenance/evidence/standing drill-through;
- query/search/provider receipt summary;
- candidate counts, visited work, timings, database crossings and selected physical plan;
- exact WHY_NOT/partial/completion state when the query is incomplete.

The compact embedded view may hide some of these until requested. Expanded mode is where the machine can become a real inspection/workbench surface rather than a decorative graph.

## 5. Connection to chat accuracy

When chat misinterprets a request, selects the wrong referent, retrieves the wrong relation family, or stops before semantic completion, the user needs to inspect the calculation that led there.

A future conversation answer can therefore expose compact affordances such as:

```text
inspect interpretation
inspect evidence
inspect search
inspect geometry
inspect standing
inspect receipt
```

Those inspections can open the same generic expanded work-surface framework over the pinned conversation result. The surface must show the actual whole-observation/joint-interpretation state from #170 rather than inventing an after-the-fact explanation.

This is particularly important for the inventor-direct rule that the engine cannot begin with a preselected `topic = lightning`. An inspection surface should make it possible to see which hypotheses were considered, what evidence discriminated them, which were pruned, what obligations remained, and why a particular semantic program was finally bound.

## 6. Connection to query accuracy and semantic/structural separation

The expanded workbench must preserve distinct typed channels rather than flatten them for display convenience.

For example:

```text
physicality.coord
    real four-component structural coordinate

physicality.trajectory
    packed BLAKE3/address + ordinal/RLE/metadata manifest

realized curve
    ordered child physicality.coord sequence

semantic web
    senses / referents / relations / testimony / standing / context / discourse
```

The visualizer may place several of these in one workbench, but it must label which coordinate/relation plane each visible element belongs to. A line between two screen points is not automatically a semantic relation; geometric nearness is not meaning; packed XYZ is not real S3 placement.

Fullscreen is useful here because the distinctions become inspectable instead of being compressed into one tiny graph where labels, depth, edges, and overlays visually collapse.

## 7. Connection to performance and physical-plan debugging

Expanding a surface should normally reuse the already calculated/pinned result. Resizing the viewport is presentation work, not authorization to repeat a costly world query.

The workbench should make performance visible when relevant:

- logical program/receipt ID;
- selected provider/index/perfcache generation;
- estimated versus actual candidates/rows/records touched;
- frontier sizes and pruning/filter counts;
- CPU/memory/I/O/database crossings;
- elapsed time by major stage;
- cache/perfcache/index hit/miss state;
- whether an accelerator miss fell back to an exact provider;
- why additional work was or was not scheduled.

This connects directly to the billing/measurement/Gödel work: the same plan-vs-actual telemetry used for cost estimation and optimization can help explain slow or over-broad queries. The UI must not invent a second measurement system.

## 8. Generic work-surface contract

The expand/fullscreen capability is reusable across dense product instruments, not a one-off Glome button.

Initial consumers should include at least:

- Explore Glome/trajectory/mesh/constellation views;
- query/search frontier and path inspection;
- conversation interpretation/evidence/receipt inspection;
- Chess Lab board/transcript/analysis instrumentation where density warrants it;
- operator/ingest execution traces and receipts;
- benchmark/physical-plan inspection.

Each surface supplies typed state and controls to one generic context-preserving workbench shell. The shell owns viewport expansion, focus/keyboard behavior, restoration, layout bounds, and common receipt/status affordances. Feature surfaces retain their own domain-specific rendering without acquiring private semantics.

## 9. Acceptance

- [ ] A Glome workbench expands to use the viewport without route navigation.
- [ ] Expansion preserves entity, selected ordinal, overlay mode, loaded neighbors, rotation and pinned result/epoch identity.
- [ ] Closing restores the embedded view with the same state.
- [ ] Expanding does not trigger an unrequested semantic re-query or change result ranking/standing.
- [ ] One-pane maximize and two-pane synchronized inspection are possible for Packed/Placement comparison.
- [ ] Dense non-coplanar fixtures can be inspected at a scale where node depth, labels and correspondence are usable.
- [ ] Conversation/query result inspection can use the same shell while displaying actual interpretation/provider/search receipts.
- [ ] Physical-plan telemetry shown in the workbench comes from the same execution receipts used by runtime measurement/billing/Gödel analysis.
- [ ] Browser accessibility covers focus trap/restore, Escape/close, keyboard operation and screen-reader labeling.
- [ ] A deliberate implementation that redirects to a new route and reconstructs the state fails the preservation fixture.
- [ ] A deliberate implementation that re-runs/re-ranks the query solely because the view expanded fails result-identity acceptance.

## 10. Relationship to other work

This clarification is intentionally additive:

- #17/#60/#132 own cognition, typed search, and provider execution;
- #169/#170 own learned cognition habits and whole-observation interpretation before focus;
- #168 owns structural geometry/semantic-web separation and metric providers;
- #172 owns generic content-driven sizing/bounded containers;
- #174 owns actual glome depth/color/projection rendering acceptance;
- #21 owns complete product surfaces;
- billing/benchmark/measurement issues own shared plan-vs-actual resource telemetry.

Expanded inspection is the product lens over those systems. It does not replace any of them and cannot manufacture evidence that their underlying execution did not produce.
