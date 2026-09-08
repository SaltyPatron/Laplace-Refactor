# Laplace UI and control-plane design review

Status: **DRAFT FOR INVENTOR REVIEW — NOT APPROVED FOR IMPLEMENTATION**. Discussion began: 2026-09-07. Repository baseline inspected: `f02d78730aa6312885784a3a986ebb243810756f`.

This packet records the requested design work. It does not implement an application, configure an identity provider, activate an endpoint, certify a capability, or authorize deployment. No proposed layout, role name, protocol profile, numeric target, or technology choice becomes inventor-approved merely by appearing here.

## Research-backed HOW — 2026-09-08

[INFRASTRUCTURE_FOUNDATION.md](INFRASTRUCTURE_FOUNDATION.md) supplies the researched implementation profile: current stable release choices, native/presentation boundaries, process and trust layout, product-versus-control persistence, BFF/SSO, proxy and protocol behavior, exact wire contracts, native browser execution, local transactions, journal ordering, resource pools, backups, updates, security and observability. Its primary-source ledger distinguishes upstream facts from Laplace recommendations and records failed lookups and remaining qualification.

[INFRASTRUCTURE_DECISIONS.json](INFRASTRUCTURE_DECISIONS.json) gives IF-01..IF-20 recommendations, existing issue owners, rejected shortcuts, F01..F08 implementation-specification packets and explicit outstanding decisions. It is a review register, **not a dependency lock, runtime configuration, approved policy or executed test**.

Recommended profiles now include React/TypeScript/Vite, .NET 10 LTS, the existing PostgreSQL 18.6 product package, a separately operated control PostgreSQL cluster, Keycloak, Caddy, native/Wasm worker execution, and IndexedDB/OPFS under separate ownership/publication rules. They remain recommendations pending review; exact artifact and compatibility locks are not invented. Existing requirements and the native engine are not replaced. No new application stack is required on every personal device.

## Start here: consolidated native workspace build map

[NATIVE_WORKSPACE_BUILD_MAP.md](NATIVE_WORKSPACE_BUILD_MAP.md) combines the discussion into an implementation-oriented breakdown: shared contracts; runtime services; reusable controls and arguments; native client/server placement; targeted ID/dependency exchange; durable user-owned local data versus replicas and perfcaches; changing knowledge; domain-package integration; and granular work packages under existing GitHub owners.

The latest direct corrections are part of that map:

- A task only happens to be chess, LaTeX, DNA or another domain. All applicable data and operations share the same workspace and execution machinery.
- The user's device performs legitimate operations for that user through the same native C/C++ engine. C#, SQL, PostgreSQL and browser/native bindings marshal and orchestrate; no private managed or UI semantic engine is introduced.
- Retain exact acquired substrate objects and the user's own authored state locally. Current answers change with the substrate: stale query-response caching is not the primary design.
- Resolve local presence and reconstruct locally; ask servers/peers for bounded missing IDs, ranges, newer heads or operations requiring remote data, rather than repeatedly asking for whole reconstructed objects.
- Client hardware is not recruited for unrelated provider work. Local execution benefits the user directly; reduced service load follows from eliminating redundant work.

The build map consolidates rather than deletes the detailed requirements below. Implementation authorization remains ungranted.

## Direct instructions and their consequence

The current discussion supplies these direct instructions:

> Why does Laplace-Refactor not have a UI that grows and implements with the app itself?

> We can set up a UI, api endpoints, the mcp, openai compatable endpoint, etc all with auth that lets anyone with a microsoft or even any SSO account log in and give us JWT/claims/etc so we can start fleshing everything out, starting with the admin panel for ingestions of seed sources, app management, and user/data control, etc...

> We're gonna flesh out the UI and acceptance criteria before you dive in so you have ZERO excuse

> gh issues and such for it all and docs too

These establish: design and review before implementation; repository documentation and GitHub tracking now; a UI developed alongside the application; common authenticated UI/API/MCP/OpenAI-compatible access; and an admin-first review of seed ingestion, application management, and user/data control. They do not select a frontend framework, identity-hosting vendor, exact visual palette, or production permission policy.

### Granular data-browser correction

The subsequent direct request requires starting from Entity/other data collections, selecting top-N with filters/sorting, clicking master-list records and inspecting their interconnected data through reusable controls. This is first-class **product exploration**, not an admin-only test/receipt dashboard. The detailed [entity-first data browser and source-link contract](DATA_BROWSER_AND_SOURCE_LINKS.md) defines raw versus enriched views, actual schema distinctions, master-detail navigation, reusable inspectors, Unicode/physicality/trajectory readback, source-release mappings, unresolved connections and local DBR-01..30 acceptance cases. It accounts for the supplied `/vault/Data` and `.refresh-20260903` listing without claiming those directories have been admitted to the database. The requested feature is required; specific layout and mechanism proposals remain reviewable.

## Review map and detailed references

1. [Consolidated native workspace build map](NATIVE_WORKSPACE_BUILD_MAP.md): common types, service boundaries, controls/arguments, local substrate, targeted exchange, domain snap-ins and implementation ownership.
2. [Screen contracts](SCREEN_CONTRACTS.md): navigation, task layouts, all-data collection/selection/compare, rich viewers and administrator journeys.
3. [Entity-first data browser and cross-source inspection](DATA_BROWSER_AND_SOURCE_LINKS.md): filter/sort/top-N, clickable records, shared detail controls and source-to-substrate connections.
4. [SpecEditor and CIEDigital engineering references](SCHEMA_DRIVEN_INTERFACE_REFERENCES.md): inspected schema discovery, generic filters/templates/query expressions and protected parameter handling.
5. [Workspace behavior and response](WORKSPACE_BEHAVIOR_AND_RESPONSE.md): useful task composition, aggregate-to-contributor navigation, labels, first useful result and delay/failure behavior.
6. [Authentication and transports](AUTH_AND_TRANSPORTS.md): identity/claims, authorization, proposed endpoints, protocol profiles and developer experience.
7. [Operator recovery and observability](../OPERATOR_RECOVERY_AND_OBSERVABILITY_REVIEW.md): database-independent administration, exact recreation/seed/restore, durable work, logs and alerts; [focused case specifications](../OPERATOR_RECOVERY_REVIEW_CASES.json).
8. [Acceptance matrix](ACCEPTANCE.md) and [earlier issue traceability](ISSUE_TRACEABILITY.md): stable review IDs, deliberate failures, evidence and release boundaries. The other documents refine these existing criteria, not add passed tests or independent completion scores.
9. [Researched infrastructure foundation](INFRASTRUCTURE_FOUNDATION.md) and [decision register](INFRASTRUCTURE_DECISIONS.json): current technology recommendations, concrete integration/failure boundaries, source ledger and remaining HOW decisions. [Interface issue map](INTERFACE_ISSUE_MAP.md) links all granular owners.

The existing [Constitution](../CONSTITUTION.md), [architecture boundaries](../../architecture/BOUNDARIES.md), [complete capability map](../LAPLACE_COMPLETE_CAPABILITY_AND_FLOW_MAP.md), and [authority stack](../../../contracts/authority-stack.json) remain governing context. This draft does not create another semantic engine or replace their accepted requirements.

## What the repository inspection established

At the inspected baseline, `managed/Laplace.Managed` contains ABI and native transport bindings, not a managed web application. The README names an `orchestrator/` product-service area that is not present at that baseline. These are repository observations, not an assertion about every historical branch or a live server that was not inspected.

The initial inspection also found a sequencing inconsistency: issue #21 listed all operational engine stages as dependencies, while #23 explicitly said phase order is not permission to defer useful vertical progress until Phase 8. The review's #21 refinement now distinguishes complete-product closure from individual workflow prerequisites. Complete-product closure needs the complete engine; designing and delivering an authenticated administrator journey depends on that journey's actual operations, not unrelated future capabilities.

## One product, not a dashboard beside Laplace

The UI is an operator and user interface to the same canonical machine. C# handles protocol, credential, session, and service orchestration. Native C/C++ and PostgreSQL-server execution remain the semantic owners. UI code does not recalculate identity, geometry, relevance, entitlement, standing, admission, pricing, or completion. Local clients invoke that same native implementation through their accepted host bindings, not a separate browser or managed algorithm.

A shared operation description is proposed to supply operation/version identity, typed input/output schemas, effect class, required authority, readiness, resource/preflight contract, job/progress/cancellation behavior, receipt schema, and supported transports. It can generate clients, schema validation, capability catalogs, API reference, and basic form controls. It does **not** automatically generate good navigation or operator workflows; those require the reviewed screen contracts.

After approval, a new or changed user-facing operation is not complete until its ordinary user journey, management visibility, permission behavior, API/protocol disposition, diagnostics, documentation, and acceptance evidence evolve in the same owning change. A deliberately headless operation needs an explicit reviewed disposition and consumer; silence is not a disposition.

A deployed capability must distinguish implementation availability, installed-provider readiness, selected-source readiness, caller authorization, resource admission, and acceptance evidence. An unavailable button may explain a real missing dependency, but an indefinitely disabled shell does not close an implementation issue.

## Review gates

| Gate | Required review material | What it does not imply |
|---|---|---|
| D0: scope inventory | Every existing product capability maps to a route/view or an explicitly retained later review owner | That later capabilities have been removed |
| D1: interaction review | Screen contracts and visual designs for normal, empty, loading, denied, failed, stale, and recovery states | That textual wireframes are final visual approval |
| D2: authority and protocol review | Role/action/world matrix, identity setup, public endpoint coverage, protocol/client versions, destructive-action rules | That login grants arbitrary rights |
| D3: acceptance review | Exact scenarios, deliberate defects, real fixtures, performance budgets, and evidence requirements | That scenario prose is an executed test |
| D4: implementation authorization | Inventor explicitly approves the recorded revision/scope and unresolved decisions are resolved or expressly deferred | That unrelated scope or deployment is authorized |

Current status: D0-D3 are proposed review material; D4 is **not granted**. Do not use this packet to start application implementation while the user is still fleshing out the design.

## Decisions to settle during review

| Decision | Proposed starting point | Status |
|---|---|---|
| Primary interaction | Browse, inspect, act; search is an accelerator, not the only entrance | Required table-first browsing clarified above; detailed interaction proposed |
| Home | Task-led workspace with actual readiness and resumable work; operator cockpit when that is the selected task | Detailed defaults proposed |
| First complete journey | Sign in, browse real data, retain/inspect locally; operate selected sources through exact plan and durable readback | Detailed slices in the build map |
| Visual direction | Coherent readable blue identity; avoid both unreadable near-black surfaces and a glaring white canvas; exact tokens and visual mockups still required | Proposed, not a final palette |
| Frontend framework | React/TypeScript/Vite static client, Aria primitives and Table9 native-result adapter (IF-02/03) | Researched recommendation; not approved or artifact-locked |
| Native client target/storage | Qualified native/Wasm worker; IndexedDB transactional ownership and initially derived/recoverable OPFS packs (IF-11/12/13) | Direct behavior required; proposed provider profile not yet qualified |
| Browser authentication | Server-managed OIDC code flow with PKCE and an HttpOnly session cookie | Proposed; researched BFF/key integration in IF-07 |
| Identity-provider hosting | Keycloak proposed broker with configured Microsoft and other OIDC/SAML providers (IF-06) | Recommended, not approved; experimental CIMD client support remains explicit qualification |
| Enrollment | Configurable open sign-in or invitation-only enrollment; first sign-in never implies administrator status | Default not approved |
| Anonymous reads | Only explicitly public worlds/views; no operator credential for public browsing | Exposure policy not approved |
| Workspace data | Private local authorship by default, explicit publication/sharing and separately scoped administration | User-owned local state required; detailed recovery/sync policy proposed |
| Owner enrollment and recovery | Explicit installer-bound identity enrollment and audited recovery, never first-login-wins | Exact experience to review; independent control-store proposal in IF-05 |
| API compatibility | Versioned support matrix for Chat Completions, Responses, MCP and additional families | Exact initial compatibility floor to approve; researched wire/gateway profiles in IF-08/09/10 |
| Lifetime/retention/performance | Measurable proposed budgets in acceptance; token/session lifetime, local durability, resource and retained-data schedules separately selected | Not approved |

## Existing GitHub ownership, not another backlog

The following owners already exist. Amend their local acceptance and cross-link this packet; do not duplicate their semantic work into a new epic. Work packages W01-W11 in the build map make the implementation breakdown more granular without creating parallel owners.

| Owner | Responsibility in this packet | Acceptance linkage |
|---|---|---|
| [#68](https://github.com/SaltyPatron/Laplace-Refactor/issues/68) | UI review, shell, reusable controls, task layouts, capability coverage and public surfaces | UX/DBR/EVO criteria; build-map W06-W10 |
| [#268](https://github.com/SaltyPatron/Laplace-Refactor/issues/268) | Typed public descriptors, query/result orchestration and generated client integration | INT/DBR/EVO criteria; W01/W05/W11 |
| [#64](https://github.com/SaltyPatron/Laplace-Refactor/issues/64) | Login, provider identity, scoped grants, protected state and local/remote disclosure | AUTH/DATA/DBR criteria; local ownership and offline policy in build map |
| [#65](https://github.com/SaltyPatron/Laplace-Refactor/issues/65) | Content-addressed exchange, scoped changes/checkpoints, publication and convergence | W04; current knowledge and exact-state exchange, not frozen answer caching |
| [#66](https://github.com/SaltyPatron/Laplace-Refactor/issues/66), [#67](https://github.com/SaltyPatron/Laplace-Refactor/issues/67) | Qualified native client targets, local storage/perfcache and execution placement | W02/W03/W11; same semantics and explicit actual execution host |
| [#21](https://github.com/SaltyPatron/Laplace-Refactor/issues/21) | Application delivery and operation-level sequencing | OPS criteria; #264/#265/#266 specific operator consumers |
| [#53](https://github.com/SaltyPatron/Laplace-Refactor/issues/53), [#195](https://github.com/SaltyPatron/Laplace-Refactor/issues/195) | Shared source admission and configured estate | ING criteria and DBR-20 bridge coverage |
| [#112](https://github.com/SaltyPatron/Laplace-Refactor/issues/112), [#115](https://github.com/SaltyPatron/Laplace-Refactor/issues/115) | Source discovery, qualification and recipe/profile preparation | ING-01..03 |
| [#5](https://github.com/SaltyPatron/Laplace-Refactor/issues/5), [#10](https://github.com/SaltyPatron/Laplace-Refactor/issues/10), [#58](https://github.com/SaltyPatron/Laplace-Refactor/issues/58) | Generated contracts and common recipe/provider lifecycle | Shared bindings and domain packages; no UI/managed semantic engine |
| [#62](https://github.com/SaltyPatron/Laplace-Refactor/issues/62) | Entity worlds and audience-authorized materializations | DATA-01..06 |
| [#145](https://github.com/SaltyPatron/Laplace-Refactor/issues/145) | Preflight/actual-cost and entitlement visibility | COST-01..03 |
| [#172](https://github.com/SaltyPatron/Laplace-Refactor/issues/172), [#174](https://github.com/SaltyPatron/Laplace-Refactor/issues/174), [#176](https://github.com/SaltyPatron/Laplace-Refactor/issues/176) | Bounded panels, typed geometry and context-preserving inspection | UX/DATA/DBR; W07 |
| [#22](https://github.com/SaltyPatron/Laplace-Refactor/issues/22), [#54](https://github.com/SaltyPatron/Laplace-Refactor/issues/54) | Installed acceptance and change-sensitive evidence | QA criteria and actual user journeys in build map |

Historical counterexamples remain evidence, not source templates: old Laplace [#1012](https://github.com/SaltyPatron/Laplace/issues/1012) (free-text tenant and missing account/operator controls), [#609](https://github.com/SaltyPatron/Laplace/issues/609) (one inspection plus retries saturating other views), and [#660](https://github.com/SaltyPatron/Laplace/issues/660) (missing structural document count). The inventor-selected SpecEditor/CIEDigital mechanisms and liked rich views are positive design references as described in the packet. Preserve clean-room semantic ownership without discarding the user's requested behavior.

Branch `docs/68-ui-admin-acceptance-review` is the explicit documentation/reconciliation vehicle for #68/#21. Its scope is this review packet and issue linkage, not application implementation. A draft PR is its route to main; merging documentation does not grant D4.
