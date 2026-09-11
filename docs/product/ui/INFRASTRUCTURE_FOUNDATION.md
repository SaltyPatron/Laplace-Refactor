# Infrastructure foundation — current reconciled profile

**Updated 2026-09-08. Read [framework continuation](FRAMEWORK_CONTINUATION.md) for current direction and incomplete implementation.** This is the current interpretation of IF-01..IF-20, not a runtime configuration or dependency lock. The user requested framework setup after the earlier design-only phase; the present change finishes documentation and preservation, not runtime work.

## 1. Preserve the research without preserving a wrong dependency

The complete previous infrastructure text and S01..S37 primary-source ledger are preserved **byte for byte** in [INFRASTRUCTURE_RESEARCH_BASELINE.md](INFRASTRUCTURE_RESEARCH_BASELINE.md). Its original Git blob is `702bf886a8b23c6751627e6041f7faf52ef7c387`, from commit `a4aeb952a4f30bcbc7faaefa300842bccd9b6c7d`. The former decision JSON is likewise retained in [INFRASTRUCTURE_DECISIONS_BASELINE.json](INFRASTRUCTURE_DECISIONS_BASELINE.json), blob `5a034c5951910431e8160af308fa9113c717b406`.

These are historical research records, not another implementation queue. Their detailed UI-library adapters, process, ABI, event, storage, recovery, telemetry, security and supply-chain analysis remain available. Their broker-first recommendation and categorical implementation-approval hold are superseded by the user's later direct corrections. The current [decision register](INFRASTRUCTURE_DECISIONS.json) states that difference explicitly.

All previously researched release numbers are dated candidate observations, not fresh installed-version findings, complete transitive locks or approved production choices. This finalization rechecked direct Microsoft/Google sign-in and Stripe intake documentation; it did not requalify the entire technology stack or build anything.

## 2. Laplace itself remains the application machine

The original invention lineage, Constitution, Invention Model and native execution contracts govern. Content/AST/physicality/occurrences/references/testimony/history remain knowledge; recipes and governance calculate over them. A protocol provider cannot redefine person identity, entitlement, source trust or content identity.

C/C++ owns the semantic program. C#/SQL orchestrate and marshal. Libraries perform mandatory protocol/cryptographic and physical-I/O functions. The UI realizes selected native state through common descriptors and controls. Neither a collection of separate business services behind generic interfaces nor opaque provider JSON plus `user_id` satisfies this boundary.

The literal content `active` is reusable content. Its appearance as one subscription's status is a distinct source-context assertion from its use about an account or document. Likewise, `King` differs from `king`; case folding and normalization are explicit transformations, not hash pre-processing. Attributable source/actor/subject/payer/beneficiary roles remain outside canonical content identity.

## 3. Current recommended physical profile

| Boundary | Current recommendation | What remains to qualify |
|---|---|---|
| Native engine | Existing C/C++/PostgreSQL semantic owner, generated ABI, set-wise operations | Actual whole-program capabilities and host/library/numerical parity |
| Web orchestration | .NET 10 BFF and generated API adapters | Locked SDK/runtime/libraries, exact session/effect/storage adapters |
| Direct sign-in | Configured Microsoft and Google handlers/approved OIDC profiles | Application registrations, redirect/audience policy, protected credentials and live fixtures; **no B2C or mandatory broker** |
| Optional authorization/broker provider | A separately selected standards implementation for a required resource-token/client profile | Useful for selected enterprise/MCP profiles, not required for ordinary direct browser sign-in; no home-grown OAuth server |
| Browser composition | Retain researched React/TypeScript/Vite, React Aria and headless Table candidates | Exact patches/licenses/toolchain and descriptor-driven behavior; table library does not own semantic filtering/ranking |
| Product database | Retain the repository's PostgreSQL18/native extension contract | Live installed state and provider changes must be measured, not inferred from documentation |
| Management durability | Independently operable narrowly scoped operational storage, with the researched control PostgreSQL cluster as a candidate | Product-postmaster independence, keys/revocations/jobs/history, actual host capacity; no compulsory broker database |
| Client | Same core in a qualified worker/native host, generated buffers/handles and native dependency resolution | Runtime lifecycle, actual supported instructions, numerical profile and cancellation |
| Local data | IndexedDB transactional records/owned state; OPFS initially derived/recoverable packs | Save/flush/publication, multi-tab fencing, quotas, offline ownership and verified export/restore |
| Jobs/exchange | Existing native lifecycle with journal/outbox, bounded batches and scoped checkpoints | Cross-store reconciliation, true publication order, new collection membership and request/job lifetime |
| Gateway/operations | Retain researched Caddy, optional OpenTelemetry and backup/provenance candidates | Exact routing, trust, resources, cancellation, restore and release profiles |

Names above select physical/presentation candidates, not new semantic owners. No new vector database, ORM canonical user model, per-modality microservices or unrelated client compute is required.

## 4. Direct authentication without a customer directory

Microsoft app registration supports the organizational/personal audiences relevant here. Google documents obtaining an OAuth client and configuring its redirects. ASP.NET Core documents multiple external provider handlers. These establish the direct integration route without requiring a B2C customer directory or mandatory Keycloak layer [A01-A03].

The installer/operator configures the application's own registrations and protected credentials once. End users use their existing accounts; they do not register applications or build identity tenants. Missing live credentials do not block implementing or locally exercising the generic framework, but a controlled test provider does not prove live Microsoft/Google compatibility.

Select the handler/profile for the actual account audience. Do not assume a Microsoft-personal-account helper alone proves multitenant organizational OIDC support. Use configured trusted discovery/issuer validation, audiences, state/nonce/PKCE and callback binding; no issuer validation disabled to make `common` appear to work.

The validated result supplies an authenticated source occurrence to a qualified native source/recipe path. Resolve the provider-scoped account reference and allowed linking/enrollment, calculate current native grants, then issue the bounded session at the proper admission boundary. No email-as-person join, C# `isAdmin` rule or private canonical User table.

Retain provider/client/subject/time and necessary claim/verification evidence without replicating bearer/refresh tokens or client secrets as ordinary knowledge. Use protected credential storage with explicit retention. Source validity and current authority are separate; cryptographic failure is never repaired by an evidence rating.

Direct browser login and OAuth authorization for third-party API/MCP clients are different profiles. Removing a mandatory broker does not turn a Google ID token into a Laplace API credential. #64/#270 must select a qualified authorization-server/resource-token integration for advertised clients, independently of the default direct-login experience. Earlier broker feature research remains conditional evidence only.

## 5. Billing and other integrations are source programs and effects

Use #141's provider-neutral membership/entitlement program, #64 identity/authority and #287 effect lifecycle; #145 displays the actual quote/resource/settlement state. Stripe-specific code owns protocol verification, field/schema interpretation and permitted external interaction, not another entitlement engine.

The common path is:

```text
provider exchange or event
 -> mandatory protocol validation and protected durable intake
 -> exact content/AST plus source occurrence and typed reference context
 -> declared source interpretation and mapping
 -> native person/account/plan/grant calculation as applicable
 -> audience-authorized records and shared controls
 -> separately requested native effect plan -> provider -> observed outcome
```

Keep payload content, source event identity, delivery attempt and business effect distinct. An identical delivery neither adds independent support nor grants another allowance. A different event ID is not automatically a distinct economic effect. Preserve provider account/environment/schema version, observed/effective time and payer/beneficiary/actor/subject roles.

Stripe documents signature verification over the original request body, duplicate deliveries and non-guaranteed delivery ordering [A04]. Durable acknowledgement, retry and reconciliation must respect those facts. A browser success redirect is not settlement; an ambiguous response after a charge needs reconciliation before any further effect. This specification performs no financial action.

Shared configuration forms, event grids, reference links, evidence inspectors and effective-permission views consume the same descriptors used by other data. A future provider supplies its real protocol and source/operation delta; it does not require another query, person, job, storage or UI framework.

## 6. Existing native integration points, not invented APIs

The inspected baseline contains `managed/Laplace.Managed/Transport.cs`: `ILaplaceIsaTransport.ExecuteBatch<TOperation,TInput,TOutput>` calls generated value/instruction descriptors through `laplace_isa_execute`. `Abi.cs` supplies exact IDs, Highway keys and reference/evidence structures; `contracts/isa.json` declares AST packets, source/world admission, reference topology/mapping and evidence families.

These are concrete seams to extend under the existing owners, not proof of complete login, provider intake, authority or entitlement programs. Map each adapter input to the actual generated type and accepted native program; implement missing native operations rather than concealing them with managed claims or billing state. The generic transport's present fixed input/output capacity is not a universal variable-sized result solution; buffer ownership/capacity/continuation still needs its declared ABI contract.

The sign-in UI must permit session -> account reference -> assertion occurrence -> linking evidence -> effective grant inspection. Billing must permit event -> typed source fields -> payer/beneficiary -> plan/grant calculation -> permitted task -> actual outcome. Both use the same record, source, field, relationship and action controls.

## 7. Detailed HOW retained from the researched baseline

The following original sections remain mechanism specifications to qualify; no unique material is discarded by the corrected entry point:

| Retained section | Required interpretation now |
|---|---|
| 2–3: repository constraints/processes | Keep existing product paths/resources; independently durable recovery is required. Optional broker is not an obligatory process or database. |
| 4: frontend composition | Native results drive common controls; supported fields generate controls from metadata; no per-page semantic algorithms. |
| 5–6: security/gateway | Apply session/keys/CSRF/issuer and routing/cancellation requirements to direct providers; broker-specific details only for a selected broker. |
| 7–8: wire/journal | Exact generated types, bounded batches, real event ordering, cross-store effect reconciliation and distinct request/job lifetimes. |
| 9–10: client/storage/resources | Owned state differs from replica/perfcache; IDB/OPFS have no implicit joint transaction; native local reuse and current discovery coexist. Sum pool allocations across processes. |
| 11–13: operation/recovery/security/telemetry | Immutable artifacts, tested restore boundaries, actual execution attribution, scoped logs and supply-chain separation; no rollout is authorized by a documentation commit. |
| 14–15: domain/platform/packets | W01..W11 and F01..F08 keep named work and prerequisites. New native rules are real work; reusable infrastructure removes duplicated application plumbing. |
| 16: dated source ledger | Historical primary references and failed retrievals remain visible. Recheck actual release/artifact compatibility when selecting dependencies. |

The original source is [here](INFRASTRUCTURE_RESEARCH_BASELINE.md); its exact prior commit is pinned in [the checkpoint](FRAMEWORK_CHECKPOINT.json). #295's installed behavior remains required, not replaced by checking document syntax.

## 8. Remaining implementation decisions

Framework setup has been requested; do not use earlier draft approval labels as a blanket stall gate. Resolve decisions at the boundary they actually affect: package locks and generated ABI before dependent implementation, actual IdP registration before live provider tests, resource/RPO/RTO policy before live provisioning, exact protocol/client profiles before compatibility claims, and annotated layouts before claiming UX acceptance. Work that does not depend on those live settings need not wait for them.

No default is inferred for real hostnames, secrets, payer accounts, charges, public exposure or destructive targets. No native capability is inferred merely from a registered opcode. F01..F08 and their existing issue owners remain in the decision register and current continuation document.

## 16. Primary-source research ledger

S01..S37 are preserved in [the byte-identical research baseline](INFRASTRUCTURE_RESEARCH_BASELINE.md#16-primary-source-research-ledger). The following focused checks supplement that research on 2026-09-08:

- A01: Microsoft application audiences/registration — https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app
- A02: ASP.NET Core external provider handlers — https://learn.microsoft.com/en-us/aspnet/core/security/authentication/social/?view=aspnetcore-10.0
- A03: Google OIDC setup and subject/claim validation — https://developers.google.com/identity/openid-connect/openid-connect
- A04: Stripe webhook verification and delivery behavior — https://docs.stripe.com/webhooks

These checks support protocol mechanics only. Stock ASP.NET Identity persistence is not Laplace's person model, and source descriptions do not prove deployed integration. No native/application build, live database inspection, credentials provisioning, runtime acceptance or deployment was performed during this finalization.
