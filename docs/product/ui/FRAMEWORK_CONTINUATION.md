# Framework continuation and preservation

Checkpoint date: **2026-09-08**. Repository: `SaltyPatron/Laplace-Refactor`. Integration: **PR #272**, branch `docs/68-ui-admin-acceptance-review`. Machine-readable partner: [FRAMEWORK_CHECKPOINT.json](FRAMEWORK_CHECKPOINT.json).

## 1. Exact scope and present state

The user requested completion and preservation of this documentation, issue and framework work. The previous direct request to start framework setup is retained; earlier agent-authored `D4 not granted` text is not a continuing global prohibition on that request. The current checkpoint makes documentation and tracking changes only. It does not register providers, move money, reset/seed a database, change services, deploy software or certify unfinished visual/policy choices.

The existing branch already holds the interface design and research. This pass reconciles remaining source/provider and scope contradictions, preserves original research verbatim, ties the late #64/#141 corrections into the framework entry point, and records the precise continuation boundary. No application implementation is represented by a document or issue count.

The pre-checkpoint head is `a4aeb952a4f30bcbc7faaefa300842bccd9b6c7d`; its tree is `055252b6ff18e1d1a25a726b2e9096edafeed2d0`. PR base is `f02d78730aa6312885784a3a986ebb243810756f`. Final published commit is the commit containing this document and the closing PR preservation comment. A document cannot include its own content-derived Git commit ID; no self-referential placeholder is presented as an actual SHA.

## 2. Preserved work and current entry points

The existing 15-file PR packet contains the architecture/control map, concrete source-data navigation, SpecEditor/CIEDigital reference analysis, UI response/task behavior, auth/protocol detail, acceptance, operator recovery, issue mapping and infrastructure research. No original content is deleted from history.

The original infrastructure research and decision JSON are retained as identical Git blobs at `INFRASTRUCTURE_RESEARCH_BASELINE.md` and `INFRASTRUCTURE_DECISIONS_BASELINE.json`. They are historical records, not a parallel active plan. The current foundation and register correct the broker prerequisite and later user direction while preserving IF-01..IF-20 and F01..F08. All other detailed specifications remain in place, interpreted with the explicit chronology here.

The 23 detailed issues #274..#296 already contain component/control arguments, native/host responsibilities, dependencies, work and acceptance. The earlier 15 expanded issue bodies remain their owners. The later #141 provider-neutral membership refinement is additional, not another billing epic. Native semantic owners and the original invention lineage remain unchanged.

Source/issue provenance is separate from Git reachability. Source files are Git objects. Issue bodies/comments are GitHub issue records, not included in a Git clone automatically. The issue map and the required late corrections are documented here and in the current foundation; this checkpoint is not a claim that every issue comment has been exported or that Git alone backs up the whole account.

## 3. Late corrections that must survive continuation

| Correction | Owning work and implementation consequence |
|---|---|
| Direct Microsoft/Google sign-in, no mandatory B2C or broker | #64/#288/#289/#293. Configure the application's registered providers; users use existing accounts. Optional API/MCP issuer/broker is a separately qualified protocol choice. |
| Accounts, Stripe, billing and users are Laplace data/programs | #64/#141/#62/#63/#145. No opaque User/Subscription service becomes canonical authority. Providers supply authenticated source records and permitted external effects. |
| Shared metadata-driven controls | #274..#277/#290. Schema/type/native operation and result metadata drive reusable fields, filters, references and actions, following the inventor-selected reference mechanisms. |
| Native computation everywhere appropriate | #278/#66/#67. C#/SQL/browser bindings marshal; missing native behavior is implemented there rather than replaced with managed/UI semantics. |
| Local owned substrate, not permanent answer caching | #279/#65/#275/#280. Preserve exact bodies; re-evaluate changing heads/membership and labels; no unrelated device work or automatic sharing. |
| One usable all-data workspace | #68/#176/#281..#296. Chess, LaTeX, DNA, boards, documents, media, conversations, Foundry and operation views compose the same controls and state. |
| Correct scope after the setup request | #68/#268 and all referenced drafts. Earlier issue/draft permission labels cannot override later direct user direction; live effects and undecided policy remain separate. |

## 4. Concrete source-integration contract

Use the existing source/recipe/reference/observation/evidence/authority/effect framework, not a new generic wrapper over unrelated business engines. The following are semantic roles to map to generated contracts, not newly invented exported symbols:

| Input/output role | Required content |
|---|---|
| Verified provider interaction | Trusted configured provider, connection/account context, protocol profile and verification result; transaction/callback binding; protected raw reference when retention is permitted |
| Attributed observation | Exact content/AST, occurrence and source profile; asserting source, acting principal, referenced subject and beneficiary as distinct roles; observed/effective time and context |
| External reference | Authority/release/namespace/local identifier/version, plus provider tenant/account/environment where applicable; no email-as-person or source data in the canonical content hash |
| Native interpretation/mapping | Declared source fields/roles, reference relationships, ambiguity/missing obligations, dependence and history |
| Effective grant/materialization | Actual selected native policy/epoch and assertions; allowed action, audience and resource scope; reason/provenance links |
| Permitted provider effect | Exact target and inputs, actor/payer/beneficiary, idempotency and authority/quote boundary, observed outcome or explicit indeterminate state |

Required sign-in path: configured handler challenge -> validated callback -> native account/reference/linking/grant program -> bounded session -> inspectable source/grant links. Cryptographic checks are mandatory, not voting. Do not retain reusable bearer/refresh tokens as replicated knowledge. Ordinary observation and verification metadata do not automatically become arbitrary semantic attestations.

Required billing path: verified provider event -> durable intake -> typed source event/attempt/reference/occurrence -> native plan/grant calculation -> effective permission and history. A payment effect has a separate plan/result boundary. Retried deliveries, distinct event IDs for one transition, late events, company payer with another beneficiary, refunds and ambiguous effect responses must retain correct attribution without duplicate benefits or charges. A redirect is not settlement.

The relevant concrete existing code at the pre-checkpoint head is `managed/Laplace.Managed/Transport.cs`, `managed/Laplace.Managed/Abi.cs` and `contracts/isa.json`: generated `ExecuteBatch` marshaling through `laplace_isa_execute`; Highway/reference candidates/mappings; AST, source/world admission and evidence families. Registered primitives do not prove completed authentication or entitlement programs. Before adding code, map each required stage to its actual implementation, required output capacity/ownership and whole-program result. No pretend native function may replace that inventory.

## 5. Work remaining, with existing owners

| Next implementation boundary | Existing owners | Completion is a product outcome |
|---|---|---|
| Generated descriptors/ABI/typed query + common fields/list | #274..#277/#268/#5/#10 | Real unrelated collections browse/filter/top-N/inspect/drill-back with exact values and usable labels |
| Protocol edge -> actual native source/reference/authority program | #64/#141/#287/#288/#289/#293 | Direct configured sign-in and provider events have inspectable attribution and correct native grants, without a private user/billing engine |
| Native local host/storage/targeted exchange | #278/#279/#65/#66/#67 | Local reconstruct/edit/reopen, bounded missing fetch, meaningful change discovery and actual host parity |
| Task layouts and rich result controls | #172/#174/#176/#280..#286/#296 | Fast useful labelled results, plural comparisons, contributor/source navigation and preserved focus/selection |
| Operations and recovery | #264..#266/#287/#289 | Real reset/seed/restore/job/log journeys with independently durable limited management |
| Domain/public-interface integration | #290..#294/#270/#271 | Actual native conversation/Foundry/domain/SDK journeys; no canned results or separate per-domain plumbing |
| Installed proof alongside each change | #295/#22/#54 | Exact fixture/result/effect, deliberate failure detection and measured usability, not issue/check counts |

F01..F08 retain the precise lock/ABI/deployment/client/control/provider/event/recovery/promotion decisions in `INFRASTRUCTURE_DECISIONS.json`. Scope-specific unresolved choices must be resolved where used; they are not a new global approval waterfall. Missing live provider registration blocks the live-provider exercise, not generic code that does not require it.

No exact release lock, complete runtime compatibility matrix, approved final visual design, live provider credentials, measured resource/RPO/RTO target or complete native account/entitlement workflow is asserted as finished. These are named remaining work, not hidden conditions.

## 6. Safe continuation and preservation verification

Read this checkpoint, current #68/#64/#141 and the current PR head before writing. Reconcile any new commits/issue changes rather than force-push over them. Preserve the existing integration lineage; do not open a duplicate implementation queue simply because a session ended.

Use the **published commit in the PR preservation comment**, not an ephemeral local checkout, as the resume anchor. Retrieve it and inspect its parent/diff. The expected checkpoint change is documentation only: current README/foundation/decision register, this continuation and checkpoint, and exact archived research blobs. No native/app/SQL/workflow/service file belongs in this finalization diff.

Compare the pre-checkpoint packet path list with the resulting tree. Verify archived research SHA identities above, unchanged detailed-specification blobs, complete IF-01..IF-20 and F01..F08 identifiers, #274..#296 coverage, direct-provider/no-B2C correction and #141 membership linkage. Local syntax/inventory checks are documentation checks; they cannot be reported as runtime acceptance.

Do not mark an implementation issue completed or merge/deploy solely because the packet is now preserved. PR draft state means review remains open, not that the user failed to request framework setup.

## 7. Observation boundary

GitHub source and issue records were inspected through the connected tool. The local attempt to retrieve public source for a full working copy failed DNS; no full clone, native build or runtime test resulted. The assistant workspace exposed the conversation's images, not a pre-existing local framework worktree. This record does not assert that an uninspected `hart-server` worktree has no uncommitted work.

No live database, secret store, IdP registration, payment destination, service configuration, deployment, external backup or issue-status completion was modified by this finalization. GitHub issue documentation and the owning PR may be updated as described. Git reachability and file-identity verification prove preservation of the named work, not complete product delivery.
