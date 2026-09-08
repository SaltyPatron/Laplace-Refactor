# Laplace interface and framework: current entry point

**Checkpoint date: 2026-09-08. Repository work is consolidated in PR #272, branch `docs/68-ui-admin-acceptance-review`.**

Start with [framework continuation](FRAMEWORK_CONTINUATION.md) and its [machine-readable checkpoint](FRAMEWORK_CHECKPOINT.json). They separate what is committed, what remains to implement, current user direction, prior recommendations, and live deployment authority. The PR remains a documentation/review vehicle; it is not proof of a running framework.

## Current direction and precedence

The user first required design before implementation, then requested starting framework setup, direct Microsoft/Google sign-in without B2C, and integrations expressed through Laplace itself. The latest instruction is:

> Finish documenting, gh issues, and whatever other framework stuff you were doing and make sure that work isnt lost

This checkpoint finishes and preserves documentation and tracking. Earlier `D4 not granted`, `implementation_authorized: false`, and equivalent statements in dated drafts describe that earlier phase; they are **not a new global prohibition on the subsequently requested framework setup**. Starting setup does not approve every proposed library, policy, visual layout or provider, and does not authorize a live database reset, payment, deployment or registration. These remain distinct from the current documentation-only changes.

Direct inventor requirements and the existing invention/ISA contracts govern. Read the [authority stack](../../../contracts/authority-stack.json), [Constitution](../CONSTITUTION.md), [Invention Model](../INVENTION_MODEL.md), [original invention lineage](../../../contracts/original-laplace-invention-lineage.json), [native execution](../../architecture/COGNITION_EXECUTION.md) and [boundaries](../../architecture/BOUNDARIES.md) before adopting a framework pattern. A later agent summary cannot silently replace the invention.

## Requirements carried forward

Laplace is the native compositional/evidence/calculation machine, not an AI endpoint with a separate SaaS application attached. C/C++ owns semantic operations; SQL, C#, browser/native bindings and PostgreSQL integration marshal and orchestrate. Protocol libraries can validate authentication and signatures; those checks cannot be overridden by a standing score.

The same exact content has the same canonical identity, while `King` and `king` differ. Source, person, role, tier, location and policy do not enter content identity. Occurrences, typed references, source statements, interpretations and evidence retain their own context. People, accounts, billing, interfaces and federation follow those same laws.

All data and operations use the shared workspace: browse, filter/sort/top-N, select one or many, inspect/compare/act, follow constituents/contributors/source/evidence and return without losing context. Graph/glome/board/sequence/document/media representations remain rich controls, not domain-private application stacks. It only happens to be the chess page.

The client performs that user's useful native work. It retains exact substrate and durable personal information, reconstructs locally, and requests bounded missing IDs/ranges or remote operations. Current query membership and preferred labels can change with the substrate; a cached answer is not timeless knowledge. Owned data is not disposable cache and private work is not automatically shared.

Direct Microsoft/Google sign-in does **not** require B2C, a customer directory, or a mandatory Keycloak deployment. Provider-issued application registration and protected credentials are deployment configuration. OAuth/OIDC, Stripe and other providers enter as authenticated source interactions and permitted effects; they do not own native person, grant or billing semantics.

The admin panel initializes/recreates/backs up/restores the selected database, triggers exact seeds, follows durable work/logs and recovers failures. Its narrowly authorized recovery/history must survive the product database's absence. This does not promise survival after total host loss.

## Read the packet by purpose

| Document | Purpose and status |
|---|---|
| [Framework continuation](FRAMEWORK_CONTINUATION.md) | Current direction, integration contracts, actual native entry points, unresolved work and preservation procedure |
| [Infrastructure foundation](INFRASTRUCTURE_FOUNDATION.md) | Current physical recommendations with direct-sign-in and native-application corrections |
| [Infrastructure decisions](INFRASTRUCTURE_DECISIONS.json) | IF-01..IF-20, F01..F08, owners and explicit qualification gaps |
| [Native workspace build map](NATIVE_WORKSPACE_BUILD_MAP.md) | W01..W11, reusable contracts/services/controls and arguments, local runtime and domain packages |
| [Interface issue map](INTERFACE_ISSUE_MAP.md) | 23 previously created detailed issues #274..#296 and the earlier 15 expanded owners; #141's later integration refinement is linked below |
| [Issue traceability](ISSUE_TRACEABILITY.md) | Original criterion namespaces and reconciliation, read with this current checkpoint |
| [Screen contracts](SCREEN_CONTRACTS.md) | Task navigation, layouts, all-data selection and rich views |
| [Data browser/source links](DATA_BROWSER_AND_SOURCE_LINKS.md) | Actual table versus enriched collection, correct queries, exact values, source-native mapping and drilldown |
| [SpecEditor/CIEDigital references](SCHEMA_DRIVEN_INTERFACE_REFERENCES.md) | Inventor-selected mechanisms for schema discovery, generated filters, expressions and protected parameters |
| [Workspace behavior/response](WORKSPACE_BEHAVIOR_AND_RESPONSE.md) | Useful first result, labels, contributor inspection, task layout and failure isolation |
| [Authentication/transports](AUTH_AND_TRANSPORTS.md) | Protocol/authority detail; provider choice is refined by current IF-06 and #64 |
| [Acceptance](ACCEPTANCE.md) | Proposed UX/AUTH/ING/OPS/DATA/INT/COST/EVO/QA criteria and measurement fixtures, not executed proof |
| [Operator recovery](../OPERATOR_RECOVERY_AND_OBSERVABILITY_REVIEW.md) and [cases](../OPERATOR_RECOVERY_REVIEW_CASES.json) | Retained-state, database-independent management, seed/readback, logs/alerts and OC cases |
| [Preserved infrastructure research](INFRASTRUCTURE_RESEARCH_BASELINE.md) | Byte-identical original research and S01..S37 ledger; historical broker/approval assumptions are superseded, other detailed mechanisms remain reference material |
| [Preserved decision baseline](INFRASTRUCTURE_DECISIONS_BASELINE.json) | Byte-identical earlier JSON, retained for provenance, **not active configuration** |
| [Component reconciliation](GENERIC_COMPONENT_AND_NODE_BLUEPRINT.md) | Preserves the prior G blueprint and maps it into the one W map; no parallel implementation queue |

## Existing ownership

[#68](https://github.com/SaltyPatron/Laplace-Refactor/issues/68) coordinates the whole interface. #268/#274/#275 own common public descriptors/query integration; #276/#277 reusable fields and collections; #278/#279 native client and local state; #65/#66/#67 exchange/targets/placement; #280 useful response; #172/#174/#176/#296 sizing/geometry/workspace/design; #281..#286 rich views and evidence; #287 actions; #288/#289 user/data/node management; #290 domain packages; #291/#292 conversation/Foundry; #293 developer access; #294 source connections; #295 installed interface evidence. #264/#265/#266 retain database/source/operations workflows, #270/#271 protocol adapters, #22/#54 installed/change-sensitive evidence.

[#64](https://github.com/SaltyPatron/Laplace-Refactor/issues/64) owns authenticated external-account attribution, native authority and direct sign-in. [#141](https://github.com/SaltyPatron/Laplace-Refactor/issues/141), under #140 and alongside #62/#63/#142/#143/#145, owns provider-neutral membership calculation; it now specifies exact Stripe/source-event, payer/beneficiary, replay and settlement boundaries. These are not new application exceptions or duplicate billing/authentication engines.

Each component issue already states its user outcome, arguments, native/host responsibility, dependencies, failure behavior and acceptance. Existing issue counts describe tracking, not completion. No implementation checkbox is checked by this preservation pass.

## Original direct instructions retained

> Why does Laplace-Refactor not have a UI that grows and implements with the app itself?

> We can set up a UI, api endpoints, the mcp, openai compatable endpoint, etc all with auth that lets anyone with a microsoft or even any SSO account log in and give us JWT/claims/etc so we can start fleshing everything out, starting with the admin panel for ingestions of seed sources, app management, user/data/etc control, etc...

> We're gonna flesh out the UI and acceptance criteria before you dive in so you have ZERO excuse

> gh issues and such for it all and docs too

The earlier complete index remains in Git history at `a4aeb952a4f30bcbc7faaefa300842bccd9b6c7d`. This current entry point resolves chronological scope and provider assumptions; it does not erase those original instructions or unfinished visual, native, integration and installed-product requirements.
