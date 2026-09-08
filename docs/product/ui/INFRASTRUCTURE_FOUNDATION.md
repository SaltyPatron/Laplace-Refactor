# Researched infrastructure foundation

**Research date: 2026-09-08. Status: recommended architecture for inventor review, not approved configuration or implemented capability.** This is the HOW supplement to [the native workspace build map](NATIVE_WORKSPACE_BUILD_MAP.md), [interface issue map](INTERFACE_ISSUE_MAP.md) and draft PR #272. It supplies concrete technology and integration decisions rather than another component inventory. [The decision register](INFRASTRUCTURE_DECISIONS.json) maps the recommendations to existing issues and records remaining qualifications.

Current repository baseline inspected: `d03002a8cbea56243a78c817193b6e1c5f55dd5d`; runtime/source base `f02d78730aa6312885784a3a986ebb243810756f`. Repository declarations were read; the running installation was not inspected. No dependency was installed, application built, database queried/reset/seeded, provider configured, workflow changed or deployment performed.

Upstream facts have numbered primary-source references below. Architectural decisions are recommendations derived from those facts and the inventor's requirements, not claims that a vendor prescribes Laplace's architecture. A published stable version does not prove the selected combination works together. Exact artifact versions/digests, supported hosts and acceptance results remain explicit deliverables, never mutable `latest` dependencies.

## 1. Recommended foundation, with one purpose

Deliver a universal workspace that performs the user's own native operations over locally retained exact substrate, queries remote scope when necessary, and offers usable administration of the same machine. It only happens to display chess, LaTeX, DNA, models or a source run. None receives a second query, identity, job or storage engine.

Recommended baseline:

| Boundary | Proposed technology/profile | Reason and limitation |
|---|---|---|
| Browser interface | Patched React 19.2 family, TypeScript 6, Vite 8.1 static build | Rich client composition without adding another server-side application runtime. React 19.2 is the current documented family; Vite 8.1 is stable. Node 24 LTS is build tooling only. [S03-S06] |
| Controls | React Aria interaction primitives; TanStack Table 9 headless row/column presentation; a single Laplace selection/query adapter | Accessible input/focus mechanics plus dynamic tables, while native operations retain filtering/ranking/aggregation meaning. These libraries do not supply the product's UX by themselves. [S07-S09] |
| Web/API orchestration | ASP.NET Core on .NET 10 LTS; same-origin backend-for-frontend (BFF); Npgsql typed set-wise access | Matches the existing C# orchestration boundary. .NET's current policy lists 10.0.11 and support through 2028-11-14. Do not start new work on .NET 8/9 merely from old templates. [S01,S10,S22] |
| Semantic execution | Existing C/C++ core and PostgreSQL extension; qualified worker-hosted WebAssembly and native-client adapters | One implementation of identity, decomposition, physicality, queries and evidence. No JavaScript/C#/SQL replacement algorithm. |
| Product persistence | Retain the already pinned PostgreSQL 18.6 package and native extension | This is also the current supported upstream 18 minor; PostgreSQL 19 is still beta at the research date. Existing native build/storage requirements govern admission. [S02] |
| Server management persistence | Independently operated PostgreSQL control cluster; separate control and broker databases/roles | Recovery access, job history and sessions cannot reside exclusively in the product postmaster being stopped. It is an operational provider, not another knowledge/person engine. |
| Identity | Keycloak 26.7.3 as the proposed hosted broker for configured OIDC/SAML providers; standard .NET OIDC integration | One issuer/resource-token boundary, with Microsoft personal/organizational and additional provider setup. Preview/experimental features are not assumed production-ready. [S10-S14] |
| Gateway | Maintained Caddy 2 release, exact patch to pin; HTTPS, static assets and route-specific proxy behavior | Small explicit entry point. Streaming cancellation needs careful configuration; do not apply global low-latency overrides blindly. [S16,S17] |
| Browser records | IndexedDB for transactional records, owned edits, outbox and metadata; OPFS initially for reproducible perfcaches/packs | One authoritative local publication transaction; no assumed cross-store ACID and no silent eviction of authored work. [S18-S20] |
| Events/jobs | Native operation lifecycle with durable journal/outbox and resumable application event cursors; SSE as the initial UI delivery mechanism | No new broker or workflow engine is required to obtain durable application work. Database NOTIFY is a wakeup, not event storage. [S23] |
| Delivery | Existing immutable native packages and managed services; first Linux deployment fits the current service estate | A browser deployment does not require Kubernetes. Containers remain an optional physical profile, not a reason to replace the current product package. [S32] |
| Operations/security | OpenTelemetry collector integration, structured protected logs, off-host pgBackRest/WAL recovery, artifact provenance/SBOM and isolated release credentials | Trace usable operations and protect recoverability; do not confuse a signed package or telemetry dashboard with working functionality. [S24-S30] |

This is a hosted-server profile, **not a requirement to install Keycloak, two PostgreSQL clusters and a gateway on every user's device**. A client needs its UI, qualified native core, local provider and optional connection to a service. A private personal client can retain and operate on its own information without becoming a public server.

## 2. What is already constrained by this repository

`contracts/postgresql-cluster.json` currently declares PostgreSQL **18.6**, database `laplace_refactor`, service `laplace-refactor-postgresql.service`, port 55433 with TCP listeners disabled, and the existing native package. It assigns the DEV/BAT estate to `laplace-runner`, uses `laplace_admin` and `laplace_app`, and caps product connections at 24. These are source declarations, not a fresh observation of the live host.

Retain these paths unless a separately reviewed migration changes them:

- immutable releases `/opt/laplace/releases` and activation `/opt/laplace/current`;
- product data `/opt/laplace/pgdata/refactor/data`;
- WAL `/var/lib/pgwal/refactor`, temporary data `/pgtemp/refactor`;
- perfcache `/opt/laplace/pgdata/refactor/perfcache`;
- socket `/opt/laplace/runtime/postgresql/refactor`;
- configuration `/etc/laplace/instances/refactor`;
- logs `/var/log/laplace/postgresql/refactor` and receipts `/opt/laplace/receipts/postgresql/refactor`.

The current contract uses the same operating-system identity for app and administrator access. Do not describe public-service privilege separation as already implemented. A publicly exposed service profile needs an explicit identity/permission migration and must preserve current ownership and other installations while it is introduced.

The product constitution and `BOUNDARIES.md` retain native semantic ownership. Third-party grids, local databases, brokers and protocol libraries are replaceable physical/presentation providers. They cannot silently change canonical content, source interpretation, evidence, authority or completion. No vector store, ORM entity model, hosted LLM or graph library is substituted for the invention.

## 3. Process and trust layout — IF-01, IF-04, IF-05

Use one modular repository/application design with a few deliberate process boundaries. Do not create a network service per control, source or modality.

```text
Browser / personal native client
  UI controls + generated descriptors + native worker + local store
                 |
              HTTPS
                 |
       Caddy entry / static package
          |                   |
     .NET WebHost       Identity broker
   BFF + API adapters       Keycloak
          |                   |
          +------ separate control PostgreSQL cluster
          |        operation journal / sessions / broker DB
          |
  ControlHost / native worker hosts
          |                    |
  product PG socket       typed lifecycle helper
          |               enrolled-target operations only
  product PostgreSQL + native Laplace extension
          |
    canonical product state / native perfcaches

Both clusters -> separate backup policies -> off-host backup destination
Operational metrics/logs -> optional collector/backend; not a runtime dependency
```

Recommended process responsibilities:

| Process/module | Allowed work | Must not possess or decide |
|---|---|---|
| Gateway | TLS, static assets, explicit upstream routes and rate/body policies | Database administrator credential, source parser execution or arbitrary lifecycle commands |
| WebHost | Session/OIDC flow, current scope, generated HTTP/MCP/compatibility mapping, bounded requests and event delivery | Root shell, native cognition loops in managed code, source-file execution or unrestricted cluster ownership |
| ControlHost | Durable admission/leases/checkpoints, native operation coordination, replay reconciliation, bounded worker dispatch | A private scheduler/evidence/authorization law; arbitrary commands received from browser |
| Native worker host | Execute admitted C++ program under the exact capability/resource grant; return typed result/failure | Ambient unrestricted credentials or data scopes not included in its grant |
| Product PG extension | Native operations requiring product indexes/transactions and canonical publication | Pretending remote database work executed on the client |
| Lifecycle helper | Allowlisted initialize/stop/stage/activate/restore effects on registered installations | Generic command strings, arbitrary paths/databases, inference credentials as admin authority |
| Broker | Verify external credential control and issue configured resource credentials | Canonical person identity, native entitlement/evidence or automatic administrator assignment |
| Control store | Physical storage for installation records, native-typed recovery grants/receipts, job state and sessions | An alternative social/knowledge/semantic-trust database |

Management records remain typed native observations, governance/effect references or strictly classified transport state. Current native grant validation must have a qualified installation-scoped provider when the product DB is down. Lack of access to the product database cannot be handled by a hard-coded administrator bypass. Normal knowledge operations can correctly be unavailable during recovery.

A second **database inside the same product cluster is insufficient**: stopping/replacing that PostgreSQL process would stop both. The proposed control cluster has its own service, data, socket, backup and account lifecycles. Keycloak uses a different database/role from the job journal even if both initially share the control cluster. Neither reset scope includes it. Separation on one host survives a product-DB outage, not destruction of the host; whole-host availability requires a separately selected topology.

Before provisioning additional services, reconcile memory/CPU/disk/connection demand against the actual co-resident host. Do not give every component the full existing product resource grant. Proposed process names and control paths are deployment descriptors to review, not permissions to create directories now.

## 4. Frontend composition and state ownership — IF-02, IF-03

Select React as the presentation host, not the semantic runtime. Use Vite to emit static versioned assets served by the gateway/BFF. No production development server. A Next.js/RSC server or server-held Blazor UI circuit is not proposed for this app's primary local-native workspace: it would add another server/UI execution boundary without supplying a required operation. This is a fit decision, not a claim those frameworks are generally unsuitable. A separate public documentation site can have its own presentation profile.

Proposed package boundaries, subject to the existing repository naming conventions:

| Package role | Imports/exports |
|---|---|
| generated contracts | Native registry projections, wire validators, exact reference/value types; no handwritten domain DTO twins |
| runtime adapter | Native worker/session interface, request generations, scoped query/result handles and dependency fulfillment |
| storage adapter | Transactional local bytes/checkpoints and replica presence; logical validation remains native |
| workspace state | One owner for current query, focus, working set, pane layout, navigation and saved-view drafts |
| controls | Descriptor-driven fields, filters, references, grid, forms, selectors and response states |
| renderers | Typed graph/geometry/sequence/board/media/formula adapters emitting Locators/intents |
| task recipes | Declarative bindings/layouts over controls and renderers; no private fetch/query/cache implementation |
| protocol client | Generated native HTTP plus explicitly qualified transport profiles; no API secrets stored in view props |

Use React Aria primitives for input/dialog/focus/keyboard mechanics and a reviewed Laplace token/style layer. Do not introduce a second table/selection data model from another component suite alongside TanStack Table. Complex grid accessibility must still be tested; a primitive library does not certify the assembled workspace. [S09,S31]

TanStack Table 9 became stable on 2026-08-04. Its APIs differ from v8; implementation examples must be pinned to v9, not copied from remembered v8 hooks. Use its manual processing modes for applicable filtering, sorting, grouping, aggregation, expansion and pagination. The native result provider supplies rows and aggregate/coverage metadata. **Manual mode can consume client-native results as well as server results**; it does not mean all work moves back to the server. Table callbacks alter a typed QuerySpec rather than run its JavaScript row algorithms as a second semantic engine. [S07,S08]

One selection owner identifies record kind, occurrence/range and version. Grid-local row indices or shortened labels cannot identify the selection. Pure presentation state—column widths, hover, camera, density—can stay local to the view and must not trigger native semantic work. Shared application state exposes selective subscriptions; rebuilding the entire UI on every worker event is forbidden by the intended response contract.

Do not add a general query-result cache or reactive replica framework as the authority for substrate freshness. A transport-cache library may later be admitted for immutable descriptors or ordinary request deduplication, but it must implement the established dependency/permission rules. Local user data remains a substrate store, not a cache of permanently correct answers.

## 5. Authentication, protected state and keys — IF-06, IF-07

Recommend one hosted Keycloak broker issuing the accepted Laplace-resource tokens. Configure Microsoft personal and organizational account registration/consent and any additional OIDC/SAML connection explicitly. The broker's user identifier is a credential mapping, not the canonical person. Application/workspace grants and their history stay under #64's native authority. Community software maintenance and an enterprise support contract are different things; select any paid support requirement separately.

Use standard ASP.NET Core confidential OIDC code flow with PKCE and server-managed session. Browser access/refresh credentials remain behind the BFF. Browser cookie: Secure, HttpOnly, host-only, explicit path/lifetime, and callback-compatible SameSite handling. Cookie-authenticated mutations require anti-CSRF/origin validation; callback state/nonce and local-only return destinations are validated. Use top-level redirect sign-in as the baseline rather than depending on a popup/opener that conflicts with cross-origin isolation. [S10,S15]

Store session tickets and protected key material outside the product reset boundary. Configure an application-specific Data Protection discriminator, purpose-specific protection, persistence and encryption-at-rest for the key ring. Microsoft documents that choosing a custom key repository can disable the automatic at-rest protection; persisting a file is not enough. Do not claim a particular encryption mode merely because Data Protection is enabled. Selected cursor/intent protection is shared plumbing, not proprietary encryption imposed on every standard SDK call. [S14]

API and MCP credentials use the correct issuer, audience, signature/algorithm, key metadata, time validity and current scope. ID tokens or Graph access tokens are not interchangeable with Laplace resource access. Do not accept client-posted roles or expose a Keycloak admin credential to the browser. Future grant revocation is checked at admission and applicable execution boundaries; refresh-token expiry alone is not the effect-revocation policy.

### Important current broker limitations

Keycloak's current 26.7 release documentation identifies SCIM as preview and Admin API v2 and several other capabilities as experimental. Its MCP documentation also identifies **OAuth Client ID Metadata Documents (CIMD) as experimental**. Do not turn those on invisibly merely to claim any-client interoperability. [S12,S13]

Baseline broker profile uses mature OIDC/SAML and explicit client preregistration or a separately constrained registration policy. #270/#293 must record the exact MCP client registration method. Clients that require CIMD need a deliberate, tested CIMD profile with allowlisted HTTPS metadata, redirect handling and SSRF limits, or another qualified authorization provider. Retain this as an unresolved interoperability qualification; do not market unrestricted MCP client compatibility from successful login by one preregistered client.

Native private local creation remains available under local ownership without requiring the hosted identity stack. Connecting, sharing, or invoking server authority is a different action. Neither local possession nor a copied server token permits access to other users' worlds. Do not promise remote deletion of plaintext already copied by an offline authorized user.

## 6. Edge routing, HTTPS and streaming — IF-08, IF-10

Recommended outward origin structure:

| Route family | Destination / authority |
|---|---|
| `/` and fingerprinted application assets | Static approved application package; HTML revalidates, digest assets are immutable |
| `/auth/*` | BFF login/callback/logout with session/CSRF law |
| `/api/v1/*` | Native generated API with declared cookie-session or machine-token profile |
| `/mcp` | MCP adapter and its protected-resource metadata; no browser-session shortcuts |
| `/v1/*` | Approved OpenAI-compatible profiles with normal scoped machine credentials |
| Identity-provider origin | Hosted broker with dedicated administration access restrictions |

Internal listeners use Unix sockets or loopback according to each qualified host. Product database port/socket is never a browser-facing interface. Restrict forwarded headers to the actual trusted proxy and validate external hosts; a caller-supplied forwarded host cannot change OIDC redirect or credential destinations.

Use trusted HTTPS on public and local/LAN origins. Caddy can automate certificate handling; internal/private certificates still require appropriate client trust installation. A local installer should guide that trust and hostname setup. Do not tell users to disable TLS checks, and do not assume an arbitrary HTTP LAN hostname has localhost's secure-context privileges. [S17-S20]

For a threaded browser profile, host approved core/worker assets with the required COOP/COEP policy and test OIDC top-level return, same-origin assets and any allowed cross-origin resources together. Untrusted documents cannot simply load arbitrary scripts or embeds into the isolated application origin. Maintain a nonthreaded qualified core profile; choosing fewer workers cannot change semantics. [S21]

### A concrete cancellation trap found in current documentation

Caddy's `flush_interval -1` is documented as low-latency flushing **and as preventing cancellation of the backend request when the downstream client disconnects**. Its ordinary proxy already flushes recognized `text/event-stream` responses immediately. Therefore do not set negative flush intervals globally as a streaming fix. Qualify route-specific behavior from client through proxy to ASP.NET cancellation and native request termination. [S16]

This matters because the pinned MCP 2026-07-28 Streamable HTTP profile treats response-stream disconnect as cancellation of that request. It has no protocol-level sessions/GET stream endpoint or Last-Event-ID stream resumption; subscriptions use that profile's explicit request mechanism. Durable Laplace jobs and application event cursors are separate. Older MCP profiles remain separate compatibility entries. [S33]

Proxy retries must never replay an ambiguously completed mutation. API request timeout, browser abort, durable-job cancel and effect rollback are different outcomes. Request-stream compression/buffering/timeouts are configured per route and measured, not copied globally from an ordinary JSON example.

## 7. Wire contracts and endpoint mechanics — IF-09

Use the existing native registry as the authoring authority. Generate C ABI, managed/SQL bindings, browser types, runtime validators and API descriptions from it; ASP.NET's emitted document is a checked projection, not a new schema inferred independently from a second DTO set.

Recommend OpenAPI **3.1** plus JSON Schema **2020-12** for the initial .NET 10 interface profile. OpenAPI 3.2 is published, but current ASP.NET documentation identifies 3.1 as .NET 10's default. Adding 3.2 is a generator/client-compatibility decision, not a reason to require a preview server release. [S34,S35]

Proposed concrete route roles, refining rather than replacing #268:

| Operation | Inputs | Returns / persistence |
|---|---|---|
| GET capabilities/descriptors | Current principal, installed generation, requested descriptor revision | Authorized metadata and compatible host/provider profile; conditional fetch by public or scope-safe revision |
| POST queries | QuerySpec, ViewContext, top-N/page, request generation and resource ceiling | Ordinary bounded result: 200 + typed rows/refs/labels and actual coverage/cursor. Explicitly asynchronous work: durable job only after admission |
| POST records:batch / records:ranges | Typed RecordRefs or exact range locators, generation and requested closure bounds | Bounded missing bodies/manifests; no per-constituent waterfall or unbounded graph traversal |
| POST operations:prepare | Operation ID, exact selected inputs, typed arguments and expected versions | Native plan, admissibility, effects/impact and resource/price disposition |
| POST operations:execute | Prepared plan, required approval, current grant and idempotency identity | Actual synchronous result or durable job identity; changed payload under key is conflict |
| GET jobs/{id}; POST supported job actions | Authorized job reference, cursor or action and expected state | Current native-backed lifecycle/receipt; no request-owned background job |
| GET application events | Authorized subscription set and per-stream resume cursors | Multiplexed application SSE, explicit retained-history gap and snapshot recovery |
| POST selected publication | Local roots/events, destination scope and expected head | Native admission/convergence receipt, not automatic public upload |

Final route spellings and generated OpenAPI must be ratified together before code. These are design signatures, not live endpoints. Identifiers have typed kind plus full fixed-width representation; 64-bit ordinals outside JavaScript's exact integer range use documented decimal strings or the generated binary representation. Floats have an exact inspection representation separate from rounded display. Source content is not silently trimmed or case-folded.

Use small JSON envelopes for request/control metadata and a versioned typed bulk-content envelope for records/ranges. Its fields must include encoding/schema, record kind, exact length, content identity, relevant recipe/version, declared compression and completeness. The exact bulk framing must be generated from/compatible with the existing canonical codec before qualification; this review does not invent a second protobuf/Arrow representation or serialize process-local C structs/pointers across the wire. Limit decompressed size before allocating.

Map HTTP errors to RFC 9457 problem details with native condition identifiers and safe extensions. Preserve denied, unavailable, invalid, conflicting, partial, exhausted and indeterminate meanings; a protocol error envelope cannot convert them into empty success. Expose correlation, not secrets or confidential source names. [S36]

## 8. Durable work and event ordering — IF-10, IF-14

The common native lifecycle defines jobs, attempts, supported checkpoints, cancellation and effect reconciliation. ControlHost executes that lifecycle with a durable journal/outbox provider; it does not introduce a separate general-purpose workflow engine with competing retries or state semantics.

Admission writes operation identity, exact request digest, grant/target/plan, initial state and its outbound event atomically in the applicable journal transaction. Native effect publication has its own transaction and receipt. When those stores differ, they are **not one distributed ACID transaction**: reconcile the native effect receipt into the control journal after failure or lost acknowledgement. Do not claim generic exactly-once execution. Never create a second effect merely because the orchestration response was lost.

Use a durable per-stream sequence/publication head whose commit order is guaranteed by the selected native storage law. A simple `MAX(sequence-id)` cursor is not sufficient when IDs can be allocated before transactions commit: a late lower ID can otherwise be skipped. Choose transactional stream-head serialization or an existing proven commit/publication frontier, and include a deliberate out-of-order-commit fixture.

PostgreSQL NOTIFY may wake journal listeners, but it only signals active listeners and its transactional/coalescing behavior is not a retained log. Consumers always catch up from durable storage; event gaps or disconnected listeners must not lose admitted work. [S23]

Application SSE carries authorized events from that journal with bounded buffers and resume per declared stream. Multiplex selected topics rather than opening a connection per card. A slow subscriber gets a bounded gap/snapshot recovery, not unlimited memory. Browser disconnect can stop monitoring without cancelling an independently admitted server job. MCP request SSE obeys its separate current profile.

Work scheduling respects the existing native dependency graph and resource grant. Lease expiry fences the previous worker; retry is only at a legal boundary. Reserve finite capacity for login/status/cancel/recovery so an ingestion cannot consume every connection or worker. Operational logs remain accessible if an optional metrics backend fails.

## 9. Native client host and local transactions — IF-11, IF-12, IF-13

Compile the admitted storage-independent C/C++ core to WebAssembly in a worker. Use the generated ABI for batch buffers, opaque handles, exact values, errors and resumable data dependencies. PostgreSQL-specific code remains in its host adapter. Record actual native libraries/compiler/numeric profile; an Emscripten file is not proof every core library ports unchanged.

Maintain distinct qualified threaded/nonthreaded bundles as documented by Emscripten. Pin the emsdk and toolchain artifact versions explicitly; the online documentation can show a development version and must not be used as the release lock. Restrict optional SIMD/library choices to profiles that meet the same native output contract. [S21]

The native session returns a bounded set of missing typed inputs and a continuation. Host I/O retrieves those inputs in batches, validates through native code, and resumes. Move bulk byte buffers between worker/main thread using qualified transfer/ownership handling; do not promise zero-copy over unrelated memory or network boundaries. Live JS views must be refreshed when the Wasm memory grows. The main UI receives only the data required to render the current bounded view.

### Initial physical local-store decision

**IndexedDB is the transactional authority for the first browser record provider.** Store verified encoded records, owned drafts/commits, manifest references, current checkpoints and the selected-publication outbox in its declared schema. These are native-typed bytes and indexes, not a JavaScript model of Laplace semantics. Commit user-authored content and its outbox/reference state in one transaction where required. Native calculation runs outside a long held transaction; publication verifies expected revisions before committing. [S18]

**OPFS initially holds rebuildable derived perfcaches and recoverable immutable pack copies.** Do not make an unflushed OPFS file the sole acknowledged copy of irreplaceable authored data. A later large-owned-artifact profile requires an explicit flush/stage/publish/recovery protocol. IndexedDB and OPFS do not share an assumed ACID transaction. The initial simple ownership rule avoids pretending they do. [S19,S20]

For pack/index publication: write a staged file, verify its length/digest/native header and flush under the qualified backend; atomically publish the active manifest pointer in the metadata transaction; retire prior files only after readers release pins. On crash, unreferenced stages are recoverable/collectable; a referenced missing or corrupt file is never silently consumed. Native perfcache dependency/generation validation still governs activation.

Coordinate same-origin storage mutation using a Web Lock and one active owner with fencing/checkpoint recovery. Other tabs communicate with the owner or wait; message delivery alone is not durable storage. If the necessary lock/profile is absent, allow an explicit safe single-writer mode rather than concurrent unsafe writes. Closed or suspended tabs can disappear; no background browser process is assumed immortal. [S37]

Request persistent storage and display the actual result, quota and export/backup status. Browser site clearing, eviction or private-mode behavior can remove data despite application-level ownership classes. User-owned state is never voluntarily evicted as cache by Laplace. Distinguish local save from backup or replication, and offer verified export/restore. [S20]

SQLite-Wasm remains a considered provider, **not a selected dependency in this profile**. The official persistence documentation returned an error during this research pass, so its latest backend/concurrency details were not revalidated. Do not add it merely to get a familiar SQL API when IndexedDB plus native indexes meets the defined responsibility. Its future admission would need a concrete measured benefit and cross-tab/durability tests.

### Reuse and change propagation

Retain immutable content, selected physicalities, exact label bodies, source spans and compatible recipes by identity. Keep active label/evidence/geometry/recipe references and collection-coverage checkpoints separate. Refresh only affected current projections. Newly matching records require collection-scope discovery, not just watching known IDs. Local absence is not global absence.

The first startup loads shell, current permitted descriptors, compatible core profile and the user's starting working set. Domain renderers/source packs are on demand or pinned. No mandatory corpus/model download. Reopening an unchanged complete local object should avoid body transfer and remote reconstruction; any necessary current authority/freshness request is recorded separately. A private local operation cannot silently upload its inputs to a remote fallback.

## 10. Database access, storage and resource plan — IF-14, IF-16

Use NpgsqlDataSource-based bounded pooling and parameterized/set-wise native bindings, not a new ORM domain model for canonical entities and evidence. UI query schemas are not a reason to generate arbitrary SQL over hidden columns or per-row native calls. SQL restriction/routing and native operators remain the existing owners. [S22]

The product contract caps connections at 24. Pool sizes must be **summed across processes and replicas**, including event listeners, migrations, background work and recovery reserve. Do not accept default per-process limits that each assume the whole budget. Reserve control capacity first, then divide available browse/ingest work from the native topology grant. Exact counts depend on an actual host/load fixture and are a tracked decision, not invented here.

No PgBouncer is required in the first colocated profile. Introducing pooling later must qualify prepared statements, session state, transaction ownership and LISTEN connections. No Redis, Kafka, service mesh, GraphQL gateway or separate vector database is part of the baseline. Add a component only for an explicit requirement or measured bottleneck and record its lifecycle, failure and semantic-boundary cost.

Keep source originals and large immutable artifacts outside reset scope. Use the existing native content/manifest provider, qualified filesystem permissions and range readers; an object-storage adapter can be added without changing identity. Do not select a particular S3-compatible server or license from habit. Current object-store credentials and allowed egress are separate from user content authority.

## 11. Deployment, upgrade and recovery — IF-16, IF-17

Primary first-server delivery remains the current native/systemd package model. Release bundles bind UI assets, .NET binaries, native engine, extension, generated descriptors and required provider versions. Install read-only versioned artifacts, validate compatibility, stage state migrations, then activate explicitly. Preserve previous compatible artifacts and restart/cutover semantics. Do not replace `/opt/laplace/current` or change service ownership during this design task.

A Compose profile can serve isolated developer or explicitly chosen single-host deployments; Docker documents that pattern. It is not by itself high availability, and it must not cause an unqualified stock PostgreSQL image to replace the native extension package. Kubernetes belongs to a separately justified multi-node scheduling/availability profile, not the definition of enterprise quality. [S32]

For a later multi-instance WebHost profile, use shared appropriate session/protection state and independently accounted pools. The durable control store supplies journal/lease state; native effects remain fenced. Control PostgreSQL and product PostgreSQL each need their own HA/backup policy. User clients never become replicas for other people's jobs by default.

Recommend pgBackRest with WAL archiving/PITR for admitted PostgreSQL backup profiles and an off-host repository. Backups include separate recovery instructions for broker/control state, product state, compatible native binaries/extensions, source/artifact manifests and protected key material. A restored old product cannot restore superseded grants if current revocation survived outside it. [S24]

Define distinct recovery cases: process restart; failed update; lost product database; corrupt/missing perfcache; lost control store; lost entire host; lost user device. RPO/RTO depend on retained bytes, bandwidth, replica topology and backup schedule. They must be selected and measured for the actual estate, not asserted from the existence of an archive. Restore testing targets an isolated new generation and verifies real records/native capability before cutover.

Browser application updates are staged too. New UI, worker and descriptor/native ABI revisions must be compatible as a set. Do not force activation/reload while the only copy of an edit is unsaved. Preserve old readers or require a visible save/reload handoff; service-worker installation alone is not application activation. A pack update cannot erase owned local content.

## 12. Security and supply-chain operation — IF-15, IF-19

Adopt OWASP ASVS 5.0 Level 2 as the proposed general application requirement set, with applicable stronger controls for destructive administration and key management. This is a requirements mapping, not a certification. NIST zero-trust guidance supports validating access rather than treating local network position or a previously authenticated device as permanent authority. [S27,S28]

Minimum design boundaries:

- Public/BFF processes are non-admin. Native parsing/render providers run in restricted workers, with bounded memory/time and explicitly permitted filesystem/network imports.
- No mounted container-control socket, arbitrary web shell, unrestricted SQL, source-provided executable upload or user-chosen native library path.
- One selected trusted proxy supplies forwarded information; CSRF/Origin/Host and SSRF controls cover login, MCP, source acquisition, metadata fetching and callbacks.
- No secrets or private identifiers/content in unrestricted diagnostic payloads, URLs, metrics dimensions or copied examples.
- Hash validation verifies content integrity, not truth, authorization, independent testimony or complete search. Locally produced receipts do not prove an untrusted device executed honestly.
- Local storage encryption needs a distinct key/durability/backup design; a web key cannot protect unlocked plaintext against malicious same-origin code. Provider keys cannot be bundled with browser assets. Re-encryption does not remint canonical plaintext identity.

Trusted application code and domain data have different supply-chain classes. Data/view recipes are validated declarations; they cannot inject arbitrary same-origin JavaScript. A new renderer/kernel is an explicitly reviewed versioned code artifact. WebAssembly still needs constrained host imports, finite resources and native validation. A native library loaded into a process is not automatically sandboxed because its descriptor is signed.

Release pipeline proposal: locked dependency graph and checksums; license inventory and SBOM; immutable artifact; source/build provenance; isolated build identity; independent release approval; verify exact artifact before activation; retain rollback and revocation metadata. Pin third-party Actions to immutable full commit SHAs, minimize workflow permissions and prevent untrusted pull requests from receiving deployment credentials or access to the running data host. Long-lived self-hosted runners require explicit isolation; ephemeral untrusted-build workers are preferable. [S29,S30]

SLSA 1.2 supplies the current provenance framework. GitHub's attestation documentation expressly notes that attestations do not guarantee artifact security. A signed compromised build is still compromised. Do not claim a SLSA level until its actual builder/isolation/provenance requirements are met. Documentation-only work must not trigger an unrequested production deployment. [S29,S30]

## 13. Observability that serves the workspace — IF-18

Instrument interaction -> local presence -> network admission -> connection wait -> native operation/PG stage -> realized labels -> transfer -> first useful render. Use one correlation chain with exact executing host and operation identities. Native measurements/receipts remain authoritative; .NET/HTTP telemetry must not invent native resource numbers.

Use OpenTelemetry protocols and a collector integration so backends are replaceable. A colocated collector is sufficient initially; agents/gateway or separate backend topology is a scale/operations choice. Pin permitted collector components, restrict listeners and redact before export. Backend metrics/traces can be sampled; required effect audit and durable job outcomes cannot vanish because sampling omitted them. [S25,S26]

Avoid high-cardinality raw user/entity/source IDs in metric labels. Sensitive event details belong in protected, bounded traces/logs or native receipts with access control. Browser instrumentation is user-visible and scope-limited, not covert capture of document contents, keys or every movement. No fabricated savings counters.

The application must still show job status, errors, logs and recovery when external dashboards are unavailable. Optional Prometheus/Grafana/trace/log backends are deployment integrations, not prerequisites for a person to see why their seed run stopped.

Existing proposed response budgets remain: 200 ms p95 immediate interaction feedback and one second p95 bounded first useful labelled read on a declared local/LAN fixture. Record cold/warm/install/load conditions, client and server CPU/memory/I/O, calls/bytes and sample counts. These are review targets, not measured results or universal WAN promises. A spinner before a 30-second ordinary read is not success.

## 14. Domain extension and platform fit — IF-20

A package supplies exact source/grammar dependencies, typed annotations, admitted native operations, optional renderer adapter and ViewRecipe bindings. It inherits the chosen workspace, query controller, storage, session, jobs and exchange. No source-name branch selects another identity or evidence implementation.

Hold the one-day snap-in target to a useful installed task when native prerequisites already exist. Use LaTeX and DNA plus a held-out package to expose missing abstractions. A source parser, alignment algorithm or game rule not yet implemented remains named native work; it must not be confused with hours spent rebuilding standard forms and storage around it.

Initial client profile is browser/PWA plus a qualified native personal-node adapter, not a promise of automatic native UI support on every OS. Desktop/mobile can reuse the same view host where appropriate or add a platform presentation adapter. The exact desktop/mobile UI toolkit remains open; choosing it now without measuring native library/worker/storage/UX fit would be premature. ARM/Pi, browser engines and device storage profiles retain actual qualification under #66/#278/#279.

Playwright projects can organize Chromium, Firefox and WebKit workflow tests. Browser/device emulation does not substitute for real mobile/ARM lifecycle, storage and performance measurements. Keyboard/assistive/zoom/manual interaction review accompanies automation. [S31]

## 15. Implementation packets — no architecture decisions hidden in tickets

These packets refine the existing issues. They are not a new backlog or approval to implement. Each packet must bind the referenced decision revision, exact inputs/outputs, failure behavior, allowed dependencies and user-visible completion before code is called done.

| Packet | Concrete next design artifacts | Existing issues |
|---|---|---|
| F01 Release/profile matrix | Exact source/artifact/runtime versions, support dates, license/SBOM status, native/browser library capabilities, patch-update process | #274 #278 #289 #290 #295 |
| F02 Host/deployment specification | Process users, sockets/ports, directory/ownership map, control-cluster isolation, worker/helper allowed operations, resource pools and recovery boundaries | #64 #67 #264 #266 #289 |
| F03 Wire/ABI contract | Generated descriptors, exact JSON/bulk value mapping, route schemas, errors, cancellation, idempotency and compatibility profiles | #274 #275 #268 #270 #271 #293 |
| F04 Client provider specification | Worker lifecycle, native batch/need/resume, IDB schema/transactions, OPFS publication, multi-tab fencing, update/eviction/recovery | #278 #279 #65 #280 |
| F05 Screen/control assembly | React Aria/Table9 adaptation, shared selection and query binding, approved layout tokens, first-frame/partial/failure behavior, unrelated-domain fixtures | #276 #277 #176 #280 #281-#286 #296 |
| F06 Identity integration | Provider registrations, audience/claim mapping, administrator enrollment, session/key lifecycle, MCP registration/CIMD decision, offline ownership policy | #64 #288 #270 #293 |
| F07 Operations and recovery | Native job/journal/outbox schema, commit-ordered cursor, capacity isolation, backup matrices, isolated restore/readback and cutover plans | #264 #265 #266 #287 #289 |
| F08 Supply chain and installed evidence | Immutable promotion manifest, workflow permissions/runner boundaries, evidence locations, concrete browser/provider fixtures, observability/redaction | #3 #22 #54 #289 #290 #295 |

Interface declarations can be designed concurrently. Implement later in complete usable increments: first native-backed labelled browse/inspect path; the same path with local reuse and change discovery; local authored state/recovery; rich contributor and domain operations; full admin/protocol paths. Do not finish all platform infrastructure while leaving users with no controls, or finish a grid backed by invented responses.

### Remaining decisions, explicitly not hidden defaults

1. Approve or replace the proposed React/.NET/Keycloak/control-store/gateway profile. The research recommends it; user approval is not inferred.
2. Pin exact React/Vite/Table9/Aria/Npgsql/Caddy/emsdk/native dependency artifacts and their compatible generated profiles. Latest family observation is not a lock file. Recheck security patches at the actual implementation date.
3. Select the installation's public/local hostname and trust setup, actual identity tenants/providers, deployment placement and per-process accounts. Required fields must be configured, not guessed from names.
4. Ratify memory/CPU/connection budgets using actual host resources, plus backup RPO/RTO, offline retention and local key-recovery policy. Current max_connections=24 cannot be multiplied by adding services.
5. Choose the exact MCP client registration/support matrix, particularly clients requiring experimental CIMD, and qualify the 2026-07-28 adapter with the proxy. Do not silently omit a required client or enable experimental code.
6. Finish annotated visual layouts and the native dependency gap inventory. This research did not render mockups, build Wasm, prove composite numerical parity or measure the reported delay.

A departure from an approved decision should identify the changed requirement, rationale, affected interfaces and evidence. That creates an objective implementation record; documentation by itself establishes neither working functionality nor anyone's motive for a failure.

## 16. Primary-source research ledger

Sources checked 2026-09-08. Version observations are dated, not ongoing monitoring. Only primary vendor/project/specification sources are used for technical recommendations. No source's benchmark is presented as a Laplace measurement.

| ID | Primary source | Fact or constraint used |
|---|---|---|
| S01 | https://dotnet.microsoft.com/en-us/platform/support/policy | .NET 10 LTS, current listed 10.0.11; .NET 8/9 near support end |
| S02 | https://www.postgresql.org/support/versioning/ | PG18.6 supported; PG19 beta; major/minor upgrade and support policy |
| S03 | https://react.dev/versions | Current React 19.2 family; patch-level pin still required |
| S04 | https://vite.dev/blog/announcing-vite8-1 | Vite 8.1 release |
| S05 | https://www.typescriptlang.org/docs/handbook/release-notes/typescript-6-0.html | TypeScript 6 published release notes |
| S06 | https://github.com/nodejs/release | Node24 LTS versus Node26 Current lifecycle |
| S07 | https://tanstack.com/blog/announcing-tanstack-table-v9 | Table9 stable Aug4; new composable API, not v8 examples |
| S08 | https://tanstack.com/table/latest/docs/guide/client-side-vs-server-side | Manual row-processing modes and supplied-result responsibility |
| S09 | https://react-aria.adobe.com/ | Style-independent accessible interaction primitives |
| S10 | https://learn.microsoft.com/en-us/aspnet/core/security/authentication/configure-oidc-web-authentication?view=aspnetcore-10.0 | Confidential OIDC and BFF/session architecture |
| S11 | https://www.keycloak.org/downloads and https://www.keycloak.org/server/db | Keycloak26.7.3 release; database/provider setup |
| S12 | https://www.keycloak.org/2026/07/keycloak-2670-released | Preview/experimental status of new broker features |
| S13 | https://www.keycloak.org/securing-apps/mcp-authz-server | CIMD experimental; registration and redirect/metadata constraints |
| S14 | https://learn.microsoft.com/en-us/aspnet/core/security/data-protection/configuration/overview?view=aspnetcore-10.0 | Persistent/shared key protection and custom-storage at-rest caveat |
| S15 | https://www.rfc-editor.org/rfc/rfc9700 | OAuth2 security best current practice |
| S16 | https://caddyserver.com/docs/caddyfile/directives/reverse_proxy | SSE flushing and negative flush_interval cancellation consequence |
| S17 | https://caddyserver.com/docs/automatic-https | HTTPS automation and internal CA trust responsibility |
| S18 | https://developer.mozilla.org/en-US/docs/Web/API/IndexedDB_API | Transactional asynchronous browser storage |
| S19 | https://developer.mozilla.org/en-US/docs/Web/API/File_System_API/Origin_private_file_system | OPFS workers, private storage and file access boundary |
| S20 | https://developer.mozilla.org/en-US/docs/Web/API/Storage_API/Storage_quotas_and_eviction_criteria and https://developer.mozilla.org/en-US/docs/Web/API/StorageManager/persist | Quota/eviction and persistence requests are not indefinite durability guarantees |
| S21 | https://emscripten.org/docs/porting/pthreads.html and https://emscripten.org/docs/tools_reference/emsdk.html | Worker/threading isolation, distinct builds, exact toolchain selection |
| S22 | https://www.npgsql.org/doc/basic-usage.html | Data sources, pooling, parameterized set-wise access |
| S23 | https://www.postgresql.org/docs/18/sql-notify.html | Transactional asynchronous notification, not a retained journal |
| S24 | https://pgbackrest.org/user-guide.html | Physical backup/WAL/PITR and recovery mechanics |
| S25 | https://opentelemetry.io/docs/collector/deploy/ | Collector deployment patterns |
| S26 | https://opentelemetry.io/docs/security/config-best-practices/ | Restricted collector components/endpoints and sensitive telemetry handling |
| S27 | https://owasp.org/www-project-application-security-verification-standard/ | ASVS5.0 release and verifiable application security requirements |
| S28 | https://www.nist.gov/publications/zero-trust-architecture | No implicit trust from network location or device ownership |
| S29 | https://slsa.dev/spec/v1.2/ | Current build provenance framework |
| S30 | https://docs.github.com/en/actions/reference/security/secure-use and https://docs.github.com/en/actions/concepts/security/artifact-attestations | Workflow/runner/token protection and limits of attestations |
| S31 | https://playwright.dev/docs/test-projects and https://www.w3.org/TR/WCAG22/ | Browser project matrix and accessibility requirements; not a substitute for real-device/manual evidence |
| S32 | https://docs.docker.com/compose/how-tos/production/ | Single-host deployment profile; not automatic HA |
| S33 | https://modelcontextprotocol.io/specification/2026-07-28/basic/transports/streamable-http | Current request/stream/subscription lifecycle |
| S34 | https://spec.openapis.org/oas/v3.2.0.html and https://learn.microsoft.com/en-us/aspnet/core/fundamentals/openapi/aspnetcore-openapi?view=aspnetcore-10.0 | Published OpenAPI3.2 versus .NET10's default3.1 generated profile |
| S35 | https://json-schema.org/draft/2020-12 | Selected JSON Schema dialect |
| S36 | https://www.rfc-editor.org/rfc/rfc9457.html | HTTP problem details envelope |
| S37 | https://developer.mozilla.org/en-US/docs/Web/API/Web_Locks_API | Same-origin lock coordination |

Fetch limitations: the SQLite-Wasm persistence guide returned service errors at both official URL spellings; its current detailed backend behavior is not asserted. The systemd execution man page also failed retrieval; specific hardening flags are not claimed verified here. Existing repository service contracts supply the deployment baseline. Exact complete transitive package versions, license compatibility and practical host support were not established by these document reads.
