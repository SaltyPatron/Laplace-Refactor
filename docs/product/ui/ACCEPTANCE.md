# UI, administration and transport acceptance matrix

Status: **PROPOSED CRITERIA — NO SCENARIO IN THIS DOCUMENT IS CLAIMED TO HAVE RUN**.

This is review material under [#68 and the design gate](README.md), not an implementation test suite or a replacement for whole-product acceptance. IDs below are local review IDs, not newly approved `LP-*` product requirements. After review they must map to generated contracts, owning tests and durable evidence without inventing a second semantic implementation.

## Evidence required for each accepted scenario

Record the exact source commit, installed package/UI/engine/extension identities, database/schema and world/authority/recipe epochs, fixture/input identity, principal/grants, browser or SDK/protocol version, test command, actual observable result and durable receipt. Retain browser trace/DOM/layout evidence and screenshots where relevant, network/protocol results, and persisted readback. Redact credentials and protect private evidence.

A screenshot cannot prove authority, durable execution or correctness. A unit test cannot alone prove the installed user journey. Synthetic fixtures may prove controls, but actual configured-source and installed-provider acceptance must use declared real boundaries. A mock or test identity provider never counts as a completed Microsoft/provider integration.

Every critical control below has a deliberate broken variant. Show that the variant fails the owning test. A timeout, skipped test, unsupported provider or unavailable fixture remains a missing/failing acceptance result, not a pass.

## A. Shell, navigation and inspection

| ID | Positive observable acceptance | Deliberate defect / failing condition |
|---|---|---|
| UX-01 | A newly authorized operator reaches the source catalog, runtime state and current jobs by ordinary navigation without SQL, a guessed ID or a search term | Search-only entrance, undocumented route, required operator token pasted into UI |
| UX-02 | Home distinguishes service, database, seed, capability, permission and evidence readiness with observed timestamp/generation | Healthy heartbeat paints unavailable capabilities green; missing values shown as zero |
| UX-03 | Source selection, filters, selected job and safe return destination survive refresh/back/login as specified without cross-user state leakage | Lost selection, duplicate submit, open redirect, another user's saved state restored |
| UX-04 | Empty/loading/denied/failed/stale states have accurate copy, valid recovery and accessible controls; private object existence is not exposed | Perpetual spinner, fake sample metric, confidential name in permission error |
| UX-05 | One-line and very long logs have a visible bounded viewport and internal scrolling in real computed layout; narrow/wide and enlarged-text views remain usable | Title-only panel, page-wide overflow or unbounded DOM/log accumulation; consume #172 |
| UX-06 | Expand/collapse preserves result, entity, epoch, receipt, selection, rotation/camera and focus; no semantic query runs solely because the viewport changed | Redirect/reconstruct, rerank/requery, new epoch, lost selected ordinal; consume #176 |
| UX-07 | Keyboard-only operation covers browsing, multiselect, forms, dialogs, confirmations and escape/focus restoration; manual and automated WCAG 2.2 AA review covers supported journeys | Keyboard trap, unlabeled control, color-only status, obscured focus, inaccessible virtualized data |
| UX-08 | Combining characters, supplementary Unicode, RTL content, long translated labels/IDs and hostile log strings display safely without truncating underlying values | Broken Unicode, HTML/script execution, silent normalization of exact content, invisible controls |

## B. Authentication and authority

| ID | Positive observable acceptance | Deliberate defect / failing condition |
|---|---|---|
| AUTH-01 | Real Microsoft personal and work/school login succeeds for permitted configured accounts; a second configured OIDC provider reaches the same account/grant model | Microsoft-only hard-coded identity semantics; mock provider claimed as live integration |
| AUTH-02 | Equal email/name and equal subject strings from different issuers do not auto-merge identities; explicit verified linking/unlinking preserves history | Email-as-person or issuer-free subject collision |
| AUTH-03 | Invalid signature/algorithm, issuer/audience, expired/future token and wrong-resource token are rejected before protected operations; trusted-key rotation works | Decode-only validation, Graph/ID token accepted as Laplace API bearer, arbitrary key URL trusted |
| AUTH-04 | Granted and denied roles produce the same world/data boundary through UI, direct HTTP, MCP and compatibility routes | Hidden button is the only gate; free-text tenant/header or tool argument widens scope |
| AUTH-05 | Role/session/key revocation blocks the next governed mutation and invalidates streams/reads within the reviewed bound; committed effects remain accurately reported | Unexpired token bypasses revoked grant; stale cache or reconnect restores access |
| AUTH-06 | Login state/nonce/PKCE, callback, cookie and CSRF controls withstand replay, cross-site mutation and return-URL substitution | Login CSRF, forged callback, unsafe redirect or cookie-authenticated destructive GET |
| AUTH-07 | Owner enrollment requires the explicit approved bootstrap proof; concurrent first logins cannot acquire owner rights; audited recovery works | First-login-wins, guessed enrollment secret, recover-by-email without required proof |
| AUTH-08 | Keys/tokens/secrets do not appear in URLs, browser storage, rendered HTML, telemetry, logs, export or support bundle; session/key rotation is testable | One seeded canary credential appears in any unauthorized output |
| AUTH-09 | Provider/source URL tests reject untrusted metadata/SSRF targets, path traversal and unsafe archive expansion; legitimate allowed providers/artifacts still work | Internal network/metadata access via user URL, root escape, unsafe symlink/archive member |
| AUTH-10 | Provider outage, cancelled consent and configuration failure show bounded recovery; ordinary users need no shell/config hacks | Redirect/retry loop, unrelated valid session destroyed, default admin bypass used to recover |

## C. Sources and ingestion

| ID | Positive observable acceptance | Deliberate defect / failing condition |
|---|---|---|
| ING-01 | Catalog enumerates the exact configured source manifest and dispositions; artifact detail preserves mandatory sidecars/dependencies | Directory scan becomes configured seed; omitted source or missing sidecar appears complete |
| ING-02 | Discovery, artifact verification, profile qualification, staging, world admission, activation and configured-seed closure remain distinct | Download/parser/row count automatically marks a source or the whole seed admitted |
| ING-03 | Candidate profile changes display field/role/loss/ambiguity diffs and need applicable authority before activation | Auto-activate inferred recipe, discard unmatched field, silently replace selected release |
| ING-04 | User selects an exact boundary across pages, reviews world/version/resource/cost, submits once and receives the same durable job on an idempotent retry | Double-click duplicates work, selection broadens, changed plan runs under stale confirmation |
| ING-05 | Progress reconciles distinct byte/artifact/record/field/content/occurrence/testimony/deposition/readback/activation units; unknown denominator stays unknown | Parser at 100% shown as complete world admission; aggregate hides missing fields |
| ING-06 | Refresh, network loss, process interruption and supported pause/resume/cancel/retry preserve durable boundary and exact attempt history | Job owned by browser; cancellation loses committed output; retry duplicates canonical effects |
| ING-07 | Completed admission exposes exact persisted/reconstructable output and receipt; repeat admission preserves canonical identity and does not double-vote equivalent packaging | Log line or accepted HTTP request is sole proof; repeated ingest multiplies independent evidence |
| ING-08 | At least two structurally unrelated real selected profiles use the same UI/job/admission lifecycle; source-specific interpretation remains profile/provider-owned | Source-name-specific UI engine or one fixture claimed as generic/whole-seed completion |

## D. Operations, users and data

| ID | Positive observable acceptance | Deliberate defect / failing condition |
|---|---|---|
| OPS-01 | Runtime view reports installed/loaded object identity and observed subsystem states; setup/authorized management works before semantic seed completion | CI success copied as live state, fabricated metrics, unseeded system blocks all admin setup |
| OPS-02 | Resource/worker/queue observations use the common grant and receipt units; viewing a log does not trigger expensive semantic maintenance | UI-owned scheduler/resource estimate, inspection initiates hidden repair/vacuum/cache rebuild |
| OPS-03 | Typed supported management actions retain plan, actor, impact, execution and verification receipts across service restart/API disconnect | Arbitrary shell endpoint, browser holds superuser credential, lost/false success after API restart |
| OPS-04 | Configuration preview and apply bind expected revision; concurrent change conflicts rather than overwrites; effective values and secret references are clear | Last-writer-wins overwrites another admin; secret value returned in readback |
| OPS-05 | Upgrade/rollback/repair shows compatibility and affected jobs/data; restart preserves accepted state and disclosed recovery boundaries | Installation claimed from files alone; upgrade deletes canonical state or abandons jobs |
| OPS-06 | Audit ties every allowed/denied management mutation to principal, target, policy, before/after reference and receipt with authorized redaction | Unattributed action, editable audit history, secret/private-data leakage in audit |
| DATA-01 | Instance administration and workspace-content grants are separate; two workspaces with equal canonical content remain disclosure-isolated | Instance role or shared hash discloses another user's private conversation/artifact |
| DATA-02 | Grant changes take effect across lists, search, direct IDs, counts, suggestions, logs, streams, receipts, caches and exports | One alternate read surface leaks denied material or its existence |
| DATA-03 | Account linking, workspace membership, data sharing and credential revocation are independently operable and preserve provenance | Unlinking destroys identity/history or revoking a token deletes unrelated data |
| DATA-04 | Result drill-down reaches exact content, occurrence/source, evidence/dependence, typed standing, selected recipe/epoch and receipt | UI invents a summary/standing score or substitutes current epoch for the executed one |
| DATA-05 | Unicode inspection exposes actual exact coordinates/identity; packed trajectory, placement and realized curves are distinct; #174's depth/color/projection controls pass | Packed address payload shown as S3 coordinate, invented Z jitter, rounded view used as exact evidence |
| DATA-06 | Export/withdraw/delete/restore preview states exact scope, references, retention and recovery; scoped deletion cannot remove another authority's shared state | Delete source button erases shared canonical content; UI promises erasure not actually performed |

## E. API, MCP, compatibility and cost

| ID | Positive observable acceptance | Deliberate defect / failing condition |
|---|---|---|
| INT-01 | UI/HTTP/MCP/managed/native routes invoking an applicable equivalent operation return equivalent canonical results, authority disposition and receipt meaning | Route-private semantic implementation or lost provenance |
| INT-02 | Installed OpenAPI/operation descriptors, generated client types and UI controls agree on versioned inputs, effects, failures and readiness | Stale handwritten form/client, unversioned schema drift, advertised operation absent |
| INT-03 | Real MCP client completes the pinned revision's discovery, registration/auth, permitted read and denied write; cached tools do not bypass current grants | Arbitrary bearer acceptance, missing resource metadata, method list claimed as interoperability |
| INT-04 | MCP progress/cancellation and result receipts obey its selected revision and the common job/effect semantics, including older-client profiles only when tested | Old session assumptions silently reused; transport disconnect falsely claims durable job cancelled |
| INT-05 | Pinned official Python and JavaScript SDKs use base URL and ordinary credential for approved model-list and Chat Completions nonstream/stream profiles | Special debug headers, monkey patches, canned completion or ignored full conversation |
| INT-06 | Approved Responses profile has independently tested event/input/output/history/storage behavior and caller isolation | Chat Completions envelope relabeled as Responses; response ID crosses user boundary |
| INT-07 | Supported tools/structured output/parameters honor the published contract; unsupported requests fail visibly with stable mapping | Ignored tool/schema/temperature/logprob request, fabricated usage or finish reason |
| INT-08 | Rate/resource denial, dependency failure, validation error and midstream failure preserve appropriate protocol errors and canonical disposition | Every error becomes empty success or a plausible canned answer |
| INT-09 | Capability and compatibility matrices enumerate retained families and actual per-user readiness; installed documentation/examples match endpoint generation | Boolean OpenAI-compatible badge conceals unsupported families; private capability names leak |
| COST-01 | Preflight exposes exact authorized scope, included/queued/denied/charged disposition, estimate and hard ceiling before expensive work | UI-local credit table, full ingest secretly performed as preflight, hidden extra charge |
| COST-02 | Concurrent/retried submissions cannot double-reserve or double-charge; actual usage links to the same quote/job/receipt | Quote ignored by alternate route, replay creates another economic effect |
| COST-03 | Failure/cancellation reconciles reservation, consumed/released amount and already committed output under the same authoritative contract | Cancel shown as refund without ledger reconciliation; actual cost fabricated from elapsed time |

## F. Co-development and installed acceptance

| ID | Positive observable acceptance | Deliberate defect / failing condition |
|---|---|---|
| EVO-01 | An operation change updates its UI workflow/management visibility, auth matrix, applicable transports, docs and tests in the same owning change or an explicitly reviewed headless disposition | Backend feature closes while ordinary users cannot reach or inspect it |
| EVO-02 | A UI-visible capability's version/readiness comes from the shared registry and actual accepted state; missing dependency is distinct from denied access | Hand-maintained green tiles and hidden unsupported flags |
| EVO-03 | Every approved screen/action/field maps to an owner and acceptance ID; existing capability inventory is preserved through redesign | A feature disappears because it did not fit the new navigation |
| QA-01 | Clean installed environment completes login -> source selection -> preflight -> ingestion -> readback/receipt -> restart -> same durable result | Browser-only fixture, database seeded by hidden test SQL, direct private helper bypass |
| QA-02 | Critical deliberate defects above fail; forbidden physical plans fail even when returned values look correct | Source-string assertions or screenshot-only tests count as behavioral proof |
| QA-03 | Guided administrator setup and ordinary authenticated use require no undocumented env variables, manual SQL, operator token or post-install shell repair | README omits indispensable manual intervention or special browser path |
| QA-04 | Change-sensitive CI selects exact UI/auth/protocol/runtime evidence; documentation-only validation does not claim runtime acceptance | Skipped/unavailable test counted green; docs PR initiates unrelated deployment |
| QA-05 | Reviewed performance envelope passes under representative ingestion load with retained measurements and no cross-screen starvation | Small empty fixture extrapolated to corpus scale; retries saturate the connection pool |

## Detailed first administrator journey

```gherkin
Scenario: Review, ingest and verify a selected seed boundary
  Given a clean installed product with an explicitly enrolled administrator
  And the selected source manifest, providers and target-world prerequisites are identified
  When the administrator signs in using a configured real SSO provider
  And browses Sources without entering SQL, a guessed ID or an operator token
  And inspects every required artifact and profile obligation for a selected release
  And reviews and authorizes the exact plan and resource ceiling
  And submits that plan twice with the same idempotency identity
  Then there is one effective admitted job
  And measured progress preserves acquisition, parsing, deposition and verification distinctions
  When the browser disconnects and later reopens the job
  Then its durable attempt and progress history remain available
  When the declared admission and readback obligations complete
  Then the UI links exact output, source coverage and an independently readable receipt
  And only the acceptance state actually proven is marked complete
  When the product restarts
  Then the same accepted output and receipt remain addressable
```

The fixture must identify what bootstrap state is allowed before the selected ingest. It cannot quietly pre-ingest the target source to make the UI path succeed. Run this journey for unrelated selected source structures; whole configured-seed acceptance still needs the complete selected estate and its existing #53/#195 closure law.

```gherkin
Scenario: A read-only identity cannot mutate through another transport
  Given a viewer in workspace A and private content in workspace B
  When that viewer calls a write through a direct HTTP path, MCP tool or compatibility tool call
  Or supplies a forged tenant, role, principal or world field
  Then the common authority rejects the action
  And no durable effect, extra resource grant or private disclosure occurs
  And the scoped denial can be audited without exposing B's private content
```

```gherkin
Scenario: A reviewed plan cannot change under the confirmation button
  Given a preflight bound to exact source, profile, world, authority and pricing revisions
  When any bound prerequisite changes before execution admission
  Then the original confirmation cannot authorize a different operation
  And the UI presents the conflict and the changed plan for review
  And no unapproved execution or charge has occurred
```

## Proposed measurable performance and usability envelope

These numbers are **design proposals, not measured performance or approved commitments**. D3 must approve or replace them with explicit values; implementation cannot choose an easier threshold after seeing failures. Record hardware, browser, network, selected corpus, resource policy, warm/cold state, concurrency, timing boundary and percentile calculation.

| Boundary | Proposed target and fixture |
|---|---|
| Immediate interaction | Selection/open/cancel-request acknowledgement paints within 200 ms at p95, excluding completion of remote work; at least 100 repetitions |
| Bounded management reads | First 100 source/job rows or runtime summary within 1 s at p95 on the declared local deployment with 10 concurrent browser users and an active admitted ingest; cold start reported separately |
| Live progress | Committed job-event visibility within 2 s at p95 on the declared network; stale/disconnected status appears within 5 s of detected stream loss |
| Read/stream revocation | No new protected mutation after grant revocation; existing read/stream authority refreshed or closed within a proposed 60 s maximum, with exact in-flight effect boundaries disclosed |
| Source/job estate | 10,000 catalog rows and 100,000 job-history rows remain server-paged/filterable; browser never downloads the full estate merely to open a list |
| Long logs | A 100,000-event job has a bounded browser window, accessible historical paging/download and no steadily unbounded DOM growth |
| Reconnect | At least 20 disconnect/reconnect cycles retain event ordering, stable job identity and no duplicate effective mutation |
| Contention | Long ingestion plus one deliberately slow entity inspection does not starve home/status/cancellation; bounded retries/backpressure cannot multiply the work into a pool-wide outage |
| Layout | Review at 360, 768, 1280 and 1920 CSS-pixel widths, 200% text enlargement and applicable WCAG reflow conditions; real DOM bounds and manual usability checks |

Readiness, availability and performance failures remain visible; hiding a slow operation or lowering the backend correctness contract is not a performance fix. The historic old-Laplace #609 failure is a counterexample for the contention fixture, not a current measurement.

## Completion and sign-off record

No approval is recorded yet. Before implementation, record the approved packet revision, exact screen scope/visuals, identity-provider setup, role matrix, compatibility floor, budgets, fixtures and explicit deferred decisions. Before delivery, fill each applicable acceptance ID with its actual test and receipt; unrun IDs stay unrun.

Admin-first completion does not establish full conversation, arbitrary multimodal cognition, every model format, all federation targets or release. Conversely, pending downstream cognition does not justify deferring the design of usable administration or returning to unauthenticated manual operator hacks. Complete capability and whole-product closure remain with their existing owners.
