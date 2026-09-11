# Interface implementation issue map

Published: 2026-09-08. **Issue specifications and design tracking, not implemented functionality or executed acceptance.** This is part of [draft PR #272](https://github.com/SaltyPatron/Laplace-Refactor/pull/272), branch `docs/68-ui-admin-acceptance-review`.

The user requested all interface issues to be fleshed out. This pass created **23 granular issues, #274–#296**, and expanded **15 existing issue bodies**: #64, #65, #66, #67, #68, #145, #172, #174, #176, #264, #265, #266, #268, #270 and #271. Requirements are in the main issue bodies, not only scattered comments. Counts describe tracking work; they are not a product-completion metric. No issue or acceptance checkbox was closed as implemented by this pass.

Start with [#68 — universal interface delivery](https://github.com/SaltyPatron/Laplace-Refactor/issues/68). Shared contract/query/API ownership is [#268](https://github.com/SaltyPatron/Laplace-Refactor/issues/268). The architectural entry remains [NATIVE_WORKSPACE_BUILD_MAP.md](NATIVE_WORKSPACE_BUILD_MAP.md); this document maps that design to executable work ownership rather than introducing another blueprint.

## 1. What every issue now specifies

Each new issue names its user-visible outcome, reusable controls or service inputs/results, single native semantic owner versus replaceable host/UI responsibilities, implementation work, prerequisites/consumers, positive acceptance, deliberate failure cases and existing review-criterion links. An ordinary supported field must gain appropriate controls without page-specific code. Domain pages are compositions over the same machinery, not private query/storage/account engines.

The issue bodies retain native C/C++ execution, C#/SQL/browser/PostgreSQL marshaling, exact content identity, one/many selection, source/occurrence/version context, local owned data versus replicas/perfcaches, targeted missing-input exchange and evolving current knowledge. The client works for its user; no unrelated provider compute or automatic private sharing is introduced.

All issue relationships in this pass are **linked task lists and explicit ownership/dependency sections**. This record does not claim that separate GitHub sub-issue or blocked-by metadata was populated. An issue reference may identify an owner, consumer or integration contract; do not interpret every reference as a requirement to finish that entire issue first. In particular, a child never waits for its parent epic to be completely closed.

## 2. Granular new issues

| Issue | Concrete implementation boundary | Main integration |
|---|---|---|
| [#274](https://github.com/SaltyPatron/Laplace-Refactor/issues/274) | Effective schema/result/field/relation/measure/operation descriptors, typed references and exact marshaling | #268; native #5/#10 and #64 |
| [#275](https://github.com/SaltyPatron/Laplace-Refactor/issues/275) | Typed predicates, grouping, filter/order before top-N, coherent cursors and changing local/global coverage | #268; #274, native #17/#60, exchange #65 |
| [#276](https://github.com/SaltyPatron/Laplace-Refactor/issues/276) | Exact value/editor/predicate/reference/range/facet controls selected by metadata | #68; #274/#275 |
| [#277](https://github.com/SaltyPatron/Laplace-Refactor/issues/277) | Collection/master-detail/related grids, plural selection, comparison, saved queries and exact exports | #68; #274/#275/#276, workspace #176 |
| [#278](https://github.com/SaltyPatron/Laplace-Refactor/issues/278) | Same native core in browser-worker and packaged-client hosts, async dependencies, resource/cancellation and ABI fidelity | #66/#67; #274 and existing native build/framework |
| [#279](https://github.com/SaltyPatron/Laplace-Refactor/issues/279) | Local owned data/replicas/perfcaches, verified presence, multi-tab durability, quotas, export and recovery | #67; native #14/#15, host #278, authority #64 |
| [#280](https://github.com/SaltyPatron/Laplace-Refactor/issues/280) | First useful labelled result, shared batched realization, independent pane states and stale-response prevention | #68/#268; native #18, query #275, residency #279 |
| [#281](https://github.com/SaltyPatron/Laplace-Refactor/issues/281) | Generic typed graph, readable labels, relationship/role selection and source/evidence drill-through | #68/#62; #277/#280 and native query owners |
| [#282](https://github.com/SaltyPatron/Laplace-Refactor/issues/282) | AST/source/sequence/formula/alignment/edit controls, exact ranges and recoverable local drafts | #68; #274/#277/#278/#279, native recipes |
| [#283](https://github.com/SaltyPatron/Laplace-Refactor/issues/283) | Image regions, audio/video timelines/tracks, annotations and geographic coordinate views | #68; #277/#282, actual native modality/codec providers |
| [#284](https://github.com/SaltyPatron/Laplace-Refactor/issues/284) | Topology-driven boards, replay/clocks, plural game analysis and exact position/occurrence links | #68; common selection and actual native game programs |
| [#285](https://github.com/SaltyPatron/Laplace-Refactor/issues/285) | Heatmaps/plots, measure semantics, direct/reference contributors and compatible A/B comparisons | #68/#268; #274/#275/#277, native calculations |
| [#286](https://github.com/SaltyPatron/Laplace-Refactor/issues/286) | Evidence/dependence, source spans, typed standing, referents, contradictions and as-of history | #68/#62; native #16/#110, shared graphs and related grids |
| [#287](https://github.com/SaltyPatron/Laplace-Refactor/issues/287) | Shared action forms/applicability, exact bound plans, effects, batch outcomes and durable invocation | #268/#68; #274/#276/#277, #64/#145/#266 |
| [#288](https://github.com/SaltyPatron/Laplace-Refactor/issues/288) | Account/session/grant/world controls, local ownership, publication, sharing and deletion-impact UX | #68/#64; #62/#63, #279/#287 |
| [#289](https://github.com/SaltyPatron/Laplace-Refactor/issues/289) | Application/node setup, provider configuration diffs, desired/installed/loaded state and placement controls | #68/#21; #264/#266/#64/#65/#66/#67 |
| [#290](https://github.com/SaltyPatron/Laplace-Refactor/issues/290) | Domain package SDK, type/kernel/renderer/ViewRecipe integration and unfamiliar snap-in demonstrations | #68/#10/#58/#5; applicable shared controls and actual native providers |
| [#291](https://github.com/SaltyPatron/Laplace-Refactor/issues/291) | Durable conversation UI, exact drafts/observations, multimodal results and shared inspection | #68; native #18, #277/#280/#286/#287 |
| [#292](https://github.com/SaltyPatron/Laplace-Refactor/issues/292) | Model/firmware/recipe/Foundry inspection, editing, comparison, compilation and artifact readback | #68; native #20/#71/#129/#223/#58 and shared action/result controls |
| [#293](https://github.com/SaltyPatron/Laplace-Refactor/issues/293) | Installed capability/API explorer, scoped credentials, normal client setup and generated documentation | #268/#68; #274/#287/#64/#270/#271 |
| [#294](https://github.com/SaltyPatron/Laplace-Refactor/issues/294) | Source release/artifact/record/span to canonical data and back; typed mappings and unresolved coverage | #265/#68; source #195/#53/#112/#115/#223, shared inspectors |
| [#295](https://github.com/SaltyPatron/Laplace-Refactor/issues/295) | Installed cross-interface journeys, native/local/server parity, response, safety, recovery and accessibility evidence | #22/#54/#68; evidence accompanies each vertical delivery |
| [#296](https://github.com/SaltyPatron/Laplace-Refactor/issues/296) | Shared presentation tokens and annotated task layouts across first-frame/normal/empty/error/recovery states | #68; sizing #172, workspace #176, fields #276, response #280 |

## 3. Expanded existing owners

| Existing issue | Main-body expansion and retained responsibility |
|---|---|
| [#68](https://github.com/SaltyPatron/Laplace-Refactor/issues/68) | Root checklist covering every new issue; retains entity-world and realization law, all-data scope and coordinated delivery |
| [#268](https://github.com/SaltyPatron/Laplace-Refactor/issues/268) | Shared HTTP/contracts umbrella with #274/#275/#287/#293 breakdown, exact IDs/queries/authority and local-client integration |
| [#64](https://github.com/SaltyPatron/Laplace-Refactor/issues/64) | Real SSO profiles, account mapping, sessions/keys, common native authority, protected intents, local ownership/offline limits and recovery revocations |
| [#65](https://github.com/SaltyPatron/Laplace-Refactor/issues/65) | Bounded ID/range/dependency exchange, version/collection changes, local private publication, gap recovery and exact convergence |
| [#66](https://github.com/SaltyPatron/Laplace-Refactor/issues/66) | Actual ARM/Pi requirements preserved alongside browser-worker/packaged-client qualification; no untested platform declared supported |
| [#67](https://github.com/SaltyPatron/Laplace-Refactor/issues/67) | Native physical execution and storage placement, local host/store work, actual PG location, durability, finite resources and transfer accounting |
| [#145](https://github.com/SaltyPatron/Laplace-Refactor/issues/145) | Included/queued/denied/charged task UX, exact quote/ceiling/reservation, measured use and settlement; no invented pricing for user hardware |
| [#172](https://github.com/SaltyPatron/Laplace-Refactor/issues/172) | Common composed sizing, intrinsic/fill propagation, bounded scrolling, unobscured controls and real DOM/viewport acceptance |
| [#174](https://github.com/SaltyPatron/Laplace-Refactor/issues/174) | Exact packed/placement/realized geometry, 4D depth/color, native metrics, multi-selection and source/ordinal inspection |
| [#176](https://github.com/SaltyPatron/Laplace-Refactor/issues/176) | Common task/pane/focus/selection runtime, useful layout slots and retained no-redirect/no-requery expansion law |
| [#264](https://github.com/SaltyPatron/Laplace-Refactor/issues/264) | Distinct DB lifecycle actions, generation-bound recreation/seed/restore, surviving management state and crash/backup/revocation recovery |
| [#265](https://github.com/SaltyPatron/Laplace-Refactor/issues/265) | Exact catalog/manifest/profile selection, preparation, run/schedule/checkpoint recovery, typed progress and admitted readback; #294 handles content drill-through |
| [#266](https://github.com/SaltyPatron/Laplace-Refactor/issues/266) | Durable operation/attempt/event model, useful-progress counters, correlated bounded logs, dependency health and alert lifecycle |
| [#270](https://github.com/SaltyPatron/Laplace-Refactor/issues/270) | Explicit MCP profile/discovery/authority, native tool integration, request-versus-job lifetime, independent-client evidence |
| [#271](https://github.com/SaltyPatron/Laplace-Refactor/issues/271) | Endpoint/field/client compatibility matrix, exact discourse/attachments, correct streams/errors/usage and no private/external replacement engine |

Underlying #62/#63 world/entitlement semantics, native binding/state/cognition/domain owners, #21 delivery and #22/#54 verification retain their original responsibilities. These specifications do not replace them or lower their completion boundaries. #267/#269 remain previously reconciled historical duplicates, not new completed work.

## 4. W01–W11 to issues

| Build-map package | Concrete issue work |
|---|---|
| W01 Contracts/marshaling | #274 under #268/#5/#10/#64 |
| W02 Native local host | #278 under #66/#67 |
| W03 Local data/perfcache | #279 under #67/#14/#15, surfaced through #288/#289 |
| W04 Targeted exchange/heads | Expanded #65 with #274/#275/#278/#279 |
| W05 Query/result coordination | #275/#280 under #268, native #17/#60/#18 |
| W06 Fields/collections | #276/#277 plus #296 presentation and #172 sizing |
| W07 Workspace/rich views | #176/#174/#281–#284/#286; #291/#292/#294 compose them |
| W08 Measures/contributors | #285 with native calculation owners and #275 |
| W09 Actions/administration | #287/#288/#289 with #264/#265/#266/#64/#145 |
| W10 Domain packages | #290 using existing native recipe/providers and reusable controls |
| W11 Public/platform delivery | #268/#270/#271/#293, target/placement #66/#67/#278 |

#295 gathers integration evidence for every package; #296 records visual/interaction approval. Neither is an excuse to defer user workflows or testing until the end.

## 5. Coverage and non-duplication checks

The published new range has 23 unique issues, each listed once in section 2. Shared field/query/list/action/response/workspace services have explicit owners; a domain feature consumes those controls instead of creating another controller, auth system, local store, scheduler or query algorithm.

The coverage includes all discussed families: Unicode and arbitrary compositions, geometry/Highway/typed metrics, lexical/frame/reference mappings, source estates and exact spans, documents/code/formula/sequence editing, images/audio/video/geography, game cohorts/boards/replay, evidence/standing/referents, conversation, model/firmware/Foundry, jobs/logs/alerts, accounts/worlds/permissions/usage, instances/providers/storage/federation and developer/protocol surfaces.

Controls name their arguments and return intents; services name their inputs/results and native owner. Per-field missing state is distinct from pending/error UI state. Known labels load with bounded useful results, not per-cell waterfalls. First-useful result includes the actual labels/controls needed to operate it; a spinner or skeleton is not a performance repair.

The client retains exact inspected objects and durable authored information, not permanent cached answers. Head and collection changes discover new matching records; local absence and partial replica coverage do not certify global absence/top-N. Local native work is attributable to that user's operation or enabled maintenance, not unrelated provider tasks.

There is no new fixed modality list that limits future packages. One-day snap-in is measured integration with existing native prerequisites, not an assertion that unknown algorithms exist. #290's held-out extension demonstrates whether the abstractions actually remove repeated application work.

## 6. Implementation handoff and acceptance

Every issue remains open with unexecuted acceptance. The current instruction authorizes issue/document refinement, not application implementation. #296 and the packet decision register must resolve final visual layouts, framework/initial hosts, local provider/durability/offline policies, permission presets, protocol/client pins and shared performance fixtures before affected implementation is represented as approved.

After approval, deliver complete paths rather than an all-infrastructure waterfall:

1. Descriptors + native query/read + reusable controls + useful labels + retained workspace on unrelated real record families.
2. Local native reconstruction, durable user-owned data and bounded missing-dependency exchange in that same path.
3. New source/collection/head updates change current results while immutable retained content and historical versions remain intact.
4. Rich view -> selected contributor -> exact source/occurrence -> comparison -> return; actual admin plan/run/recovery and protocol paths use the same machinery.
5. Unfamiliar domain and admitted host integrations demonstrate reuse without rewriting shared infrastructure.

Parent/consumer references are not a machine-generated topological dependency graph. Implementers must identify the specific required interface/provider at the chosen slice; loops such as 'finish the whole UI before its own query service' are forbidden. All applicable native product obligations remain, and a partial slice is not whole-product closure.

Map broad UX/AUTH/ING/OPS/DATA/INT/COST/EVO/QA, browser DBR and focused OC cases to issue-owned executable evidence via #295. Keep namespaces and positive/deliberately broken fixtures. Record actual native/UI/package/schema/recipe/evidence/authority versions, installed host/client, input/source fixture, exact test command, readback/effect and timings. Skipped/unavailable/mocked cases are not passing product acceptance; documentation counts are not implementation progress.

The shared UI performance envelope remains proposed, not measured or newly approved: 200 ms p95 interaction feedback and one-second p95 bounded first-useful labelled read on the declared fixture, with cold/warm/load/device/network boundaries separated. Security, correctness, local durability and realistic usability remain required even when a timing target passes.
