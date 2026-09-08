# Authentication, authority and public transport design

Status: **draft for review, not deployed configuration**. This document makes the requested SSO/UI/API/MCP/OpenAI-compatible product concrete without choosing a frontend or identity-hosting vendor. See [review decisions](README.md), [screen contracts](SCREEN_CONTRACTS.md) and [acceptance](ACCEPTANCE.md).

## 1. One identity boundary, several transports

Proposed logical flow:

```text
configured Microsoft / other identity provider
 -> standards-based authenticated account assertion
 -> verified external-account mapping
 -> Laplace principal + current scoped grants
 -> validated session or audience-bound resource credential
 -> common operation authority / resource admission
 -> generated native/SQL/C# operation boundary
 -> same canonical execution, durable state and receipt
 -> UI / HTTP / MCP / OpenAI-compatible envelope
```

Credential validation is transport/security plumbing. Referential identity, visibility, entitlement and effect authority retain their existing #64/#62/#63/common-engine owners. No transport may trust an arbitrary user/tenant header, email, display name, browser-supplied role, model prompt or UI route as authority.

## 2. Microsoft and other SSO accounts

The proposed Microsoft configuration supports both personal Microsoft accounts and work/school accounts where the app registration and the organization's consent policy permit them. Microsoft's `common`, `organizations`, `consumers` and tenant-specific authorities have different account audiences; configuration must be deliberate and tested, not inferred from a login-button label [S1]. An organization's own restrictions cannot be promised away by Laplace.

Other providers participate through configured OIDC federation; SAML-only providers require an explicitly selected federation bridge/adapter. “Any SSO” means extensible standards-based provider support, not accepting arbitrary JWT issuers or every SSO protocol without configuration. The provider catalog must show configured, tested, unavailable and unsupported states honestly.

The verified external account key includes issuer and subject; provider-specific immutable tenant/object coordinates may be retained where their documented semantics apply. Names and emails are display/contact assertions, not merge keys. Linking a second provider requires proof of control and current authority; conflicts, recovery and unlinking preserve prior provenance without silently transferring ownership [S2].

## 3. Browser session proposal

Use an ASP.NET Core orchestration host with a server-side OIDC authorization-code flow, PKCE, state/nonce validation and a server-managed browser session. This is consistent with the existing C# boundary; it does not select Blazor versus another frontend. Microsoft's current guidance describes confidential OIDC clients, cookie sessions and the backend-for-frontend pattern [S3].

Proposed controls: Secure/HttpOnly cookies; callback-compatible SameSite behavior; CSRF protection for cookie-authenticated mutations; fixed/validated callback URLs; safe local return paths; session rotation on authentication/privilege change; server-side storage of refresh/access credentials; and logout/revocation. Do not store bearer/refresh credentials in localStorage, query strings, analytics, rendered HTML, error receipts or source-control configuration.

Session idle/absolute lifetime, step-up policy and the recovery experience require approval. A missing provider or cancelled consent must return a usable status without losing an existing valid local session or repeatedly redirecting forever. Sensitive claim display is redacted and permission-scoped.

## 4. Resource tokens, claims and revocation

JWT is a representation, not an authorization decision. Validate signature, approved algorithm, issuer, resource audience, expiry/not-before, signing-key trust and applicable client/tenant binding. Key rotation uses trusted provider metadata with bounded refresh, not an arbitrary `jku`/`x5u` URL from an untrusted token. Claims unknown to the configured profile cannot widen grants.

An ID token proves authentication to its client; it is not a generic API bearer token. A Microsoft Graph access token is not a Laplace API credential. Use an authorization service issuing accepted Laplace-resource tokens or explicitly configured direct resource-token validation, with one documented issuer/audience contract. The issuer/broker product is a review decision; do not implement a bespoke OAuth server merely to avoid that decision [S2, S4].

Map authenticated credentials to principal, actor/service, client, workspace/world, operation scope, policy/grant epoch, audience, resource ceiling and expiry. Claims describe authenticated assertions; the common authority program calculates effective permission. A valid token can still yield a denied operation.

Revocation is not deferred until a long-lived JWT expires. Every new governed mutation rechecks current applicable grants; active streams and long-running effects observe revocation at declared enforcement/checkpoint boundaries. Display any already committed effects and residual cancellation boundary honestly. The maximum read/stream revocation propagation is a proposed acceptance budget, not an instant-revocation claim.

Service accounts and SDK credentials are scoped, expiring/revocable principals, not shared administrator keys. Where compatibility clients need static API keys, those are individually issued, stored only as a verifier/secure reference, shown once, limited by principal/world/action, and revocable. Authentication convergence does not imply every protocol uses the same wire credential.

## 5. Proposed role and scope matrix

Roles are editable permission bundles over explicit resources, not hard-coded global truth. The table is a review baseline; an operation is admitted only when all applicable grants and native authority conditions hold.

| Role | Normal scope | Proposed allowed work | Not implied |
|---|---|---|---|
| Installation owner | Instance governance | Enroll/recover administrators, manage trust configuration and delegate roles | Universal disclosure of private user content |
| Instance administrator | Instance operations | Inspect runtime, manage services/configuration/providers and resource policy | Workspace content access without an explicit data grant |
| Source operator | Selected source/world | Discover, inspect, preflight, ingest, pause/cancel/retry where supported | Change identity-provider trust, grant own roles or ingest into other worlds |
| Workspace administrator | Named workspace | Manage membership and permitted data lifecycle in that workspace | Instance control or another workspace |
| Member | Own/shared authorized state | Converse, query, inspect, create permitted content and manage own sessions | Global seed ingestion or management operations |
| Viewer/auditor | Explicit read/audit scope | Browse permitted worlds, jobs or redacted audit records | Mutations, secret reads or unrelated private records |
| Service principal | Declared operation/world scopes | Automation under delegated grant and resource ceiling | Interactive identity or unrestricted operator authority |
| Anonymous | Explicitly public materialization | Browse published public information where deployment permits | Private counts, management state or privileged writes |

No first-login-wins owner assignment. Initial ownership is an explicit installer-bound enrollment tied to a proven external identity or an approved auditable recovery procedure. Credential recovery and role delegation cannot silently mutate person identity or historical evidence. Open sign-in and usable public browsing do not require open administration.

## 6. Proposed HTTP surface

These paths are design proposals, not claims that endpoints currently exist. Final names and schemas must be generated/versioned under the existing binding owner. UI routes and API routes are distinct.

| Surface | Proposed resource/operation families | Authority/lifecycle |
|---|---|---|
| Session | `/auth/login/{provider}`, callback/logout; `/api/v1/me` | Standards login, safe return, current grants, CSRF-protected logout where cookie-authenticated |
| Capabilities | `/api/v1/capabilities` | Caller-scoped installed availability/readiness and versioned supported operations |
| Sources | `/api/v1/sources`, `/{id}/releases`, `/{id}/artifacts`, `/{id}/profiles` | Manifest/profile reads, scoped discovery/validation commands |
| Planning | `/api/v1/operations/preflight` | Exact input/world/version/effect/resource and cost envelope; no hidden full execution |
| Ingestion | `/api/v1/ingestions` | Submit only the authorized exact plan; durable job reference and idempotency |
| Jobs | `/api/v1/jobs/{id}`, `/{id}/events`, `/{id}/actions` | Authorized status/progress; typed supported pause/resume/cancel/retry; per-attempt receipts |
| Receipts | `/api/v1/receipts/{id}` | Exact receipt readback with authority-scoped redaction/provenance |
| Application | `/api/v1/instance/status`, `/configuration`, `/operations` | Observed runtime, revision-checked config diff, preflight/apply/verify management |
| Access | `/api/v1/principals`, `/workspaces`, `/grants`, `/sessions`, `/clients` | Scoped lifecycle, grant provenance, recovery/revocation and concurrency control |
| Data | `/api/v1/worlds`, `/datasets`, `/artifacts`, `/data-operations` | Authorized browse/share/export/withdraw/delete-impact/restore where admitted |
| Audit/cost | `/api/v1/audit`, `/entitlements`, `/quotes`, `/usage` | Scoped immutable history and common preflight/reconciliation state |
| Exploration/cognition | `/api/v1/entities`, `/queries`, `/conversations` | Same semantic owners and exact observations/results/receipts as other transports |
| Compatibility | `/v1/models`, `/v1/chat/completions`, `/v1/responses`, `/mcp` | Explicit protocol profiles below; no private reasoning or auth bypass |

Reads use bounded paging/filtering and stable continuation tokens; private resources never leak through aggregate counts. Mutations declare input schema, idempotency policy, expected revision/epoch, scope, accepted/denied outcome and receipt. No state-changing GET and no generic arbitrary shell/SQL execution endpoint hidden behind an admin UI.

An asynchronous acceptance response binds a durable job; transport success does not mean semantic completion. Errors preserve invalid input, unauthenticated, unauthorized, stale/conflicting state, throttled/resource-limited, unavailable dependency, unknown, unsupported, partial and failed distinctions. HTTP and protocol-specific mappings must be documented, including failures after streaming headers have been sent.

## 7. MCP interoperability contract

The reviewed external reference is MCP revision **2026-07-28**, not an unversioned moving `latest` or draft [S5, S6]. The implementation review must pin the exact supported protocol revisions and real client versions; earlier-client compatibility is a separate tested profile.

Protected remote MCP access must use authorization-server/resource metadata, audience-bound access tokens, appropriate client registration/discovery and scoped consent. Do not treat a browser cookie or a decoded Microsoft ID token as a universal MCP credential. Do not pass a client's token onward to unrelated resources [S5].

Tools/resources expose the same allowed operation descriptors and receipts as the UI/API. Tool discovery is scope-filtered, and execution revalidates authority even when a client cached a tool description. Tool arguments and prompts cannot self-grant roles. A write tool needs the same plan/approval/effect boundary as its UI counterpart.

Transport framing, request metadata, progress, cancellation and older-version behavior must follow the selected revision [S6]. Do not hard-code earlier connection-session assumptions into the shared application model. Durable Laplace conversations/jobs are application state, distinct from protocol request/connection state. Disconnecting an interactive request and cancelling an independently admitted background job require explicitly different operation semantics.

Acceptance must use a real protocol client and prove discovery, auth, a permitted operation, denied operation, progress/result handling, cancellation and receipt parity. Listing a JSON-RPC method or returning a canned result is not MCP acceptance.

## 8. OpenAI-compatible interoperability contract

Compatibility is a published, versioned matrix, not one boolean. Chat Completions and Responses have different message/event structures and must not share a hand-waved envelope [S7, S8].

| Family | Proposed requirement |
|---|---|
| Models | List invocable authorized Laplace execution profiles with truthful IDs/capabilities; never advertise a missing engine as available |
| Chat Completions | Standard requests, complete supplied conversation, Unicode content, nonstream and streaming responses; role/tool/error/usage behavior explicitly mapped |
| Responses | Separately tested input/output and streaming-event profile; history/retrieval/cancellation/storage features individually declared |
| Tool calls | Canonical authorized tool/effect operations with validated arguments and correlated results; no tool name bypasses permissions |
| Structured output | Supported schemas/validation/error behavior explicitly declared and tested rather than accepting and ignoring constraints |
| Embeddings, image, audio, files and batch | Retained product families with explicit coverage/owners; unavailable entries stay unsupported, not silently simulated or dropped from the inventory |

A compatibility execution profile may identify native Laplace firmware/recipe behavior rather than a conventional trained model. Document that mapping. Compatibility does not prove an external model is used or that native reasoning has become transformer semantics.

The ordinary SDK path requires only documented base URL, normal credential and supported request fields. Chat Completions must work when a client supplies its conversation history without a Laplace-only session header. Any optional durable-session association is documented, caller-scoped and must not merge unrelated conversations merely because text matches. Responses history features require separate declared storage and ownership semantics.

Unknown or unsupported parameters must fail explicitly or follow the published schema's allowed behavior; never accept `temperature`, `logprobs`, seed, modalities, tools or output constraints and silently pretend their requested semantics were honored. Do not invent logits, probabilities, token usage, billing usage or a finish reason for an incomplete result. Any required compatibility usage fields need a defined measured projection; optional unavailable fields are omitted/null only where the exact schema allows it. Native resource costing remains separate.

Test the pinned official Python and JavaScript/TypeScript SDK versions against the exact installed endpoint for the approved profiles. Preserve native result/evidence meaning and provide a documented receipt lookup/correlation mechanism without forcing ordinary clients to parse nonstandard hidden fields.

## 9. Standards references

References were inspected on 2026-09-07. They constrain protocol/security/accessibility behavior, not Laplace semantic identity or the inventor's approval. Select exact dependency versions during implementation review.

- S1: Microsoft OIDC/account audiences — https://learn.microsoft.com/en-us/entra/identity-platform/v2-protocols-oidc
- S2: OpenID Connect Core 1.0, errata set 2 — https://openid.net/specs/openid-connect-core-1_0.html
- S3: ASP.NET Core OIDC, confidential code flow and BFF — https://learn.microsoft.com/en-us/aspnet/core/security/authentication/configure-oidc-web-authentication?view=aspnetcore-10.0
- S4: Microsoft access-token resource/claims guidance — https://learn.microsoft.com/en-us/entra/identity-platform/access-tokens
- S5: MCP 2026-07-28 authorization — https://modelcontextprotocol.io/specification/2026-07-28/basic/authorization
- S6: MCP 2026-07-28 transports — https://modelcontextprotocol.io/specification/2026-07-28/basic/transports
- S7: OpenAI Chat Completions reference — https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create/
- S8: OpenAI Responses migration and streaming — https://developers.openai.com/api/docs/guides/migrate-to-responses and https://developers.openai.com/api/docs/guides/streaming-responses

These references do not settle identity-provider hosting, final framework choice, session/token lifetime, production public-data policy or the approved initial compatibility floor. Those decisions remain visible in the review register.
