# Laplace UI and control-plane design review

Status: **DRAFT FOR INVENTOR REVIEW — NOT APPROVED FOR IMPLEMENTATION**. Discussion date: 2026-09-07. Repository baseline inspected: `f02d78730aa6312885784a3a986ebb243810756f`.

This packet records the requested design work. It does not implement an application, configure an identity provider, activate an endpoint, certify a capability, or authorize deployment. No proposed layout, role name, protocol profile, numeric target, or technology choice becomes inventor-approved merely by appearing here.

## Direct instructions and their consequence

The current discussion supplies these direct instructions:

> Why does Laplace-Refactor not have a UI that grows and implements with the app itself?

> We can set up a UI, api endpoints, the mcp, openai compatable endpoint, etc all with auth that lets anyone with a microsoft or even any SSO account log in and give us JWT/claims/etc so we can start fleshing everything out, starting with the admin panel for ingestions of seed sources, app management, user/data/etc control, etc...

> We're gonna flesh out the UI and acceptance criteria before you dive in so you have ZERO excuse

> gh issues and such for it all and docs too

These establish: design and review before implementation; repository documentation and GitHub tracking now; a UI developed alongside the application; common authenticated UI/API/MCP/OpenAI-compatible access; and an admin-first review of seed ingestion, application management, and user/data control. They do not select a frontend framework, identity-hosting vendor, exact visual palette, or production permission policy.

## Read this packet in order

1. [Screen contracts](SCREEN_CONTRACTS.md): navigation, layouts, user journeys, fields, actions, and error/recovery states.
2. [Authentication and transports](AUTH_AND_TRANSPORTS.md): identity/claims, authorization, proposed endpoints, protocol profiles, and developer experience.
3. [Acceptance matrix](ACCEPTANCE.md): stable review IDs, positive scenarios, deliberate failures, evidence, and release boundaries.

The existing [Constitution](../CONSTITUTION.md), [architecture boundaries](../../architecture/BOUNDARIES.md), [complete capability map](../LAPLACE_COMPLETE_CAPABILITY_AND_FLOW_MAP.md), and [authority stack](../../../contracts/authority-stack.json) remain governing context. This draft does not create another semantic engine or replace their accepted requirements.

## What the repository inspection established

At the inspected baseline, `managed/Laplace.Managed` contains ABI and native transport bindings, not a managed web application. The README names an `orchestrator/` product-service area that is not present at that baseline. These are repository observations, not an assertion about every historical branch or a live server that was not inspected.

There is also a sequencing inconsistency: issue #21 lists all operational engine stages as dependencies, while #23 explicitly says phase order is not permission to defer useful vertical progress until Phase 8. Complete-product closure needs the complete engine; designing and delivering an authenticated administrator journey should depend on that journey's actual operations, not unrelated future capabilities. The issue refinement must preserve this distinction.

## One product, not a dashboard beside Laplace

The UI is an operator and user interface to the same canonical machine. C# handles protocol, credential, session, and service orchestration. Native C/C++ and PostgreSQL-server execution remain the semantic owners. UI code does not recalculate identity, geometry, relevance, entitlement, standing, admission, pricing, or completion.

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
| Primary interaction | Browse, inspect, act; search is an accelerator, not the only entrance | Proposed |
| Home | Operational cockpit showing real readiness and resumable work | Proposed |
| First complete journey | Sign in as an explicitly authorized administrator, inspect sources, select a release, preflight, run admission, inspect durable output and receipt | Proposed |
| Visual direction | Coherent readable blue identity; avoid both unreadable near-black surfaces and a glaring white canvas; exact tokens and visual mockups still required | Proposed, not a final palette |
| Frontend framework | Select after reviewing data-grid, streaming, workbench, accessibility, deployment, and generated-client needs | Not selected |
| Browser authentication | Server-managed OIDC code flow with PKCE and an HttpOnly session cookie | Proposed |
| Identity-provider hosting | Standards-compliant issuer/broker with Microsoft and additional configured providers; no hand-written OAuth server | Vendor/deployment not selected |
| Enrollment | Configurable open sign-in or invitation-only enrollment; first sign-in never implies administrator status | Default not approved |
| Anonymous reads | Only explicitly public worlds/views; no operator credential for public browsing | Exposure policy not approved |
| Workspace data | Private by default, explicit sharing, separately scoped administration | Proposed |
| Owner enrollment and recovery | Explicit installer-bound identity enrollment and audited recovery, never first-login-wins | Exact experience to review |
| API compatibility | Versioned support matrix for Chat Completions, Responses, MCP, and additional families | Exact initial compatibility floor to approve |
| Lifetime/retention/performance | Measurable proposed budgets in acceptance; token/session lifetime and retained-data schedules separately selected | Not approved |

## Existing GitHub ownership, not another backlog

The following owners already exist. Amend their local acceptance and cross-link this packet; do not duplicate their semantic work into a new epic.

| Owner | Responsibility in this packet | Acceptance IDs |
|---|---|---|
| [#68](https://github.com/SaltyPatron/Laplace-Refactor/issues/68) | UI review coordination, shell, navigation, capability coverage, public surfaces | UX-01..08, INT-01..09, EVO-01..03 |
| [#64](https://github.com/SaltyPatron/Laplace-Refactor/issues/64) | Login, provider identity, claims, sessions, authority and isolation | AUTH-01..10, DATA-01..03 |
| [#21](https://github.com/SaltyPatron/Laplace-Refactor/issues/21) | Application/control-plane management and operation-level sequencing | OPS-01..06 |
| [#53](https://github.com/SaltyPatron/Laplace-Refactor/issues/53), [#195](https://github.com/SaltyPatron/Laplace-Refactor/issues/195) | Shared source admission and the configured estate; UI consumes those owners | ING-01..08 |
| [#112](https://github.com/SaltyPatron/Laplace-Refactor/issues/112), [#115](https://github.com/SaltyPatron/Laplace-Refactor/issues/115) | Source discovery, qualification and recipe/profile review | ING-01..03 |
| [#5](https://github.com/SaltyPatron/Laplace-Refactor/issues/5), [#10](https://github.com/SaltyPatron/Laplace-Refactor/issues/10), [#58](https://github.com/SaltyPatron/Laplace-Refactor/issues/58) | Generated contracts and the common lifecycle | INT-01..09, EVO-01..03 |
| [#62](https://github.com/SaltyPatron/Laplace-Refactor/issues/62) | Entity worlds, audience-authorized materializations and user/data views | DATA-01..06 |
| [#145](https://github.com/SaltyPatron/Laplace-Refactor/issues/145) | Preflight/actual-cost and entitlement visibility | COST-01..03 |
| [#172](https://github.com/SaltyPatron/Laplace-Refactor/issues/172), [#174](https://github.com/SaltyPatron/Laplace-Refactor/issues/174), [#176](https://github.com/SaltyPatron/Laplace-Refactor/issues/176) | Bounded panels, correct geometry, context-preserving inspection | UX-05..08, DATA-05 |
| [#22](https://github.com/SaltyPatron/Laplace-Refactor/issues/22), [#54](https://github.com/SaltyPatron/Laplace-Refactor/issues/54) | Installed acceptance and change-sensitive evidence | QA-01..05 |

Historical counterexamples remain evidence, not source templates: old Laplace [#1012](https://github.com/SaltyPatron/Laplace/issues/1012) (free-text tenant and missing account/operator controls), [#609](https://github.com/SaltyPatron/Laplace/issues/609) (one inspection plus retries saturating other views), and [#660](https://github.com/SaltyPatron/Laplace/issues/660) (missing structural document count). Preserve clean-room boundaries and avoid copying the old implementation.

Branch `docs/68-ui-admin-acceptance-review` is the explicit documentation/reconciliation vehicle for #68/#21. Its scope is this review packet and issue linkage, not application implementation. A draft PR is its route to main; merging documentation does not grant D4.
