# Schema-driven interfaces: SpecEditor and CIEDigital references

Status: **design refinement in PR #272; no application implementation or runtime test result**. This document extends [the entity-first data browser](DATA_BROWSER_AND_SOURCE_LINKS.md), [screen contracts](SCREEN_CONTRACTS.md), and [authentication/transport review](AUTH_AND_TRANSPORTS.md). Owners remain #68 for the interface, #268 for shared query/metadata contracts, #64 for authority and protected state, and #265 for source inspection.

## 1. The inventor-selected precedents

Anthony explicitly supplied his public `AHartTN/SpecEditor` and `AHartTN/CIEDigital` repositories as demonstrations of generics, reusable controls, dynamic schema-driven querying and encrypted endpoint parameters. These are positive engineering references selected by the user, not merely historical Laplace failure examples. Their design patterns must inform this review; replacing that evidence with a generic dashboard description would miss the instruction.

Read-only inspection used these exact revisions:

- SpecEditor: `68b8bed0160aab0bc2358b88fd3f006d4836dd7c`.
- CIEDigital: `5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8`.

The inspected source establishes concrete mechanisms. Neither older application was built or launched in this task, and no full security or runtime-conformance audit is claimed. The code descriptions below are limited to the named files and paths. Both reference repositories remain unchanged. Their SQL Server/Entity Framework execution mechanisms are precedents, not a reason to move Laplace-native semantic calculations into a new managed engine.

## 2. What the source actually does

| Observed implementation | Specific mechanism | Consequence for Laplace |
|---|---|---|
| SpecEditor `SqlHelper.cs`, `RefreshDatabase`, metadata retrieval constants | Enumerates servers, then databases, schemas, tables and columns; reads SQL type identifiers, length, precision, scale, nullability and FK-related import locking; maps SQL/CLR/.NET types | Derive available fields and technical controls from the actual authorized installed schema, rather than maintaining a parallel table list by hand |
| SpecEditor `frm_Main.cs`, selection-change handlers | Selecting server/database/schema/table populates dependent selectors, column grids and mapping controls; selection is coordinated across controls | Context changes drive related panels through shared metadata and selected identities |
| SpecEditor binding models and `SqlHelper.cs` | Represents the discovered hierarchy with linked records and first/last-observed metadata | Distinguish observed schema inventory from source knowledge and from newly inferred semantics |
| CIEDigital `SearchExtensions.GetDefaultSearchCriteria(Type)` | Reflects readable/writable properties; chooses a search type for supported strings, enums, booleans, dates and numerical types; unwraps collection element types | One type-to-control registry supplies filters for every supported field of every collection |
| CIEDigital `AbstractSearch` | Common property path and target type; display label from metadata; null checks; nested member access; collection predicates using `Queryable.Any`; typed expression passed to `Where` | Relationship-aware filtering is a shared query facility, not a special filter implementation on each screen |
| CIEDigital `BaseController<T>.Index` | Uses default criteria when absent and composes `GetQuery<T>(true).ApplySearchCriteria(...).GetPagedResult(...)` into a generic view model | Shared browse orchestration with filtering before result materialization; replaceable query provider, not per-entity controllers with private behavior |
| CIEDigital `PagingExtensions` | Builds order expressions for selected and nested properties, then applies `Skip`/`Take` | Ordering and pagination execute as part of the data query, not just on visible browser rows |
| CIEDigital `Views/Combines/Index.cshtml` and `EditorTemplates/IntegerSearch.cshtml` | One `EditorFor(m => m.SearchCriteria)` invocation renders polymorphic filter templates; integer template supplies comparator and value controls, including second operand for range-style comparisons | Reuse the control family; a page should not hand-code a new number/string/date filter for each field |
| CIEDigital `AbstractSearchModelBinder` | Reconstructs the posted concrete search type and obtains its property descriptors | Preserve typed query round-tripping, with server-approved descriptor IDs instead of client-selected arbitrary runtime types |
| CIEDigital `SearchExtensions_Irony` | Parses terms, phrases, boolean operators and parentheses into expression trees over supplied text properties | A compact query-language view can share query meaning with the visual builder; parsing is not arbitrary code execution |
| CIEDigital `HtmlExtensions.EncryptedActionLink` and `EncryptedActionParameterAttribute` | Helper serializes route values into an encrypted `q` payload; action filter decrypts and supplies controller parameters | Protected route/query state is a reusable cross-cutting mechanism, not encryption logic repeated inside each feature |

The concrete CIEDigital list path is:

    selected model type
      -> reflected supported properties
      -> typed AbstractSearch instances
      -> shared editor templates
      -> posted typed criteria
      -> generic expression construction
      -> IQueryable filtering, ordering and paging
      -> shared paged view model

This is stronger evidence than the phrase 'use reusable components': it specifies what is reused and where new fields enter the system.

Important scope distinctions: CIEDigital derives its default filters from CLR/model metadata; SpecEditor discovers physical SQL metadata. They demonstrate complementary parts of the requested pattern. The inspected CIEDigital index still names its display columns and uses ordinary action links, so this inspection does not claim that every legacy grid or endpoint was automatically generated or encrypted.

## 3. One metadata-to-interface path for Laplace

The design must combine:

    authorized installed storage schema
    + existing generated Laplace record/operation/type metadata
    + current query-result projection
      -> one versioned effective collection/field/relation descriptor
      -> shared filter, sort, grid, detail and link controls
      -> typed query request
      -> common admitted query/ISA execution
      -> typed result descriptor and rows
      -> the same controls again, at the next level

Schema inspection supplies names, types, nullability, keys, cardinality where declared, and technical constraints. Existing Laplace metadata supplies domain meaning, units, exact identity kind, coordinate class, permitted operators, authority and realization. A raw binary column alone cannot say whether it is an Entity key, a receipt, a hash, a trajectory or arbitrary bytes; annotations augment discovery rather than replace discovery with hand-maintained per-screen code.

For an authorized ordinary field of a supported type, discovery must be sufficient to make it selectable, inspectable, sortable/filterable where supported, and exportable according to its contract. It must not require a new page, route, controller, handwritten DTO and acceptance branch. A genuinely new semantic type requires one shared adapter/control registration and its native operation contract, not one adapter per table or seed source.

The effective metadata also follows the result being presented. A joined projection, grouped result, related-record grid, or calculated distance result must describe its own fields. Users should not see only the base table's filter vocabulary when inspecting a richer result. Where a predicate acts before grouping and another acts on an aggregate result, the interface must label that distinction and compile the corresponding typed stages.

### Type-aware controls

| Effective field type | Shared behavior |
|---|---|
| Exact text/content | Exact equals, membership, prefix/substring where supported; explicit opt-in case-folded or normalized comparison with its rule shown |
| Integer/ordinal/count | Exact numeric entry, range/comparison, radix where useful; no floating-point coercion of large ordinals |
| Decimal or measured scalar | Numeric controls retaining units, precision and missing-value meaning |
| Boolean | True/false plus separate unset/not-applicable handling when the field permits it |
| Enum or flags | Labels generated from the installed type; enum choices or named bit tests, not an unexplained integer input |
| Date/time | Time-aware range controls and visible timezone/clock meaning |
| Typed reference | Search/select within authorized targets, ordinary record link, inspect relation, optional nested target filters |
| Collection/relation | Related-record grid plus explicit existence/quantifier and same-child predicate grouping |
| Geometry/trajectory | Coordinate components and type-aware metric controls, plus range/ordinal and structure inspection; not a generic array of floats |
| Unknown/new type | Inspectable safe raw representation and explicit unavailable operators until one shared adapter is provided; never silently drop the field |

Finite enums come from metadata. Data-derived suggestions or facets come from an authorized bounded query over the selected scope, with their current filters and count/completeness meaning. Do not enumerate a private/global distinct-value set, scan the whole estate on page load, or infer a closed enum from a small sample. Distinguish narrowing current results from removing the current field's predicate to show alternative facet choices.

Nested relationship filters must preserve the same-child versus independent-existence distinction already specified in DBR-07. Missing values, inaccessible targets and empty collections need their declared semantics; copying an old default null policy indiscriminately is not sufficient.

## 4. A concrete interaction, not another settings exercise

Open Entities. The registered metadata offers the real Entity columns and supported enrichments. Select Physicalities as a related collection: the shared related-grid control receives its descriptor and automatically offers its keys, recipe, geometry epoch, numeric components and structural quantities. Add a numeric comparison by choosing an offered field and operator; the browser submits a typed query rather than inventing SQL.

Open a source mapping from the result. The same reference control resolves its correctly typed left/right endpoints; the same filter builder exposes that mapping result's fields; the same detail pane renders the selected source/release and available linked records. Return to Entities without losing the original selection, columns or result boundary.

The requirement is not to make the user configure a new registry for each page. Registry population derives from existing schema/contracts. User choices configure a view; they do not define the storage semantics or give permission to query arbitrary private columns.

## 5. Exact identity and the Unicode/geometry correction

The direct correction preceding the repository references is retained:

    King : U+004B U+0069 U+006E U+0067
    king : U+006B U+0069 U+006E U+0067

These are different exact compositions. Equal canonical content uses the same content hash and reuses its structure; capitalization, order, whitespace and combining-codepoint differences must not be erased by a generic text widget, database collation, ingestion trim, or serializer default. Case folding and normalization are explicit transformations/comparison relations, not silent identity rules. A hash collision must never be treated as proof of content equality; existing full identity verification remains applicable.

For these two compositions, the different first atom and shared exact suffix must be navigable. A comparison view highlights the changed constituent and the reused content, then separately displays their stored physicalities, realized curves and selected metric outputs. This does not assert fresh live database results or invent hash/coordinate values.

Unicode's codespace has 1,114,112 positions from U+0000 through U+10FFFF [U1]. A finite alphabet supports arbitrarily long finite sequences: at length n the unrestricted positional alphabet has K^n sequences. Thus the alphabet size is not a fixed vocabulary of words or n-grams. Laplace's projection supplies one common geometric domain for composing and comparing them; a new document or n-gram does not require a new atomic alphabet entry.

The user's approximate '300k' occupancy statement is not adopted as a measured census of all human use. Unicode 17.0 documents 159,801 encoded characters [U2], while its codespace also distinguishes controls, private use, surrogates, noncharacters and reserved positions [U1]. Those counts and categories must be labelled rather than folded into one 'humanity used' number. The browser should use the selected UCD release's actual classifications. This numerical clarification does not change the requested full-codespace substrate.

Bounded geometric extent is separate from storage capacity. Exact compositions and their ordered trajectories carry distinctions that a finite-precision point or summary alone cannot preserve. This is why geometry and the Merkle/identity structure must remain jointly inspectable, not why geometric calculations should be hidden or excluded.

The existing `PHYSICALITY_COORD_TRAJECTORY_REALIZATION.md` and `STRUCTURAL_TRAJECTORY_METRICS.md` remain the applicable geometry definitions:

- Angular/geodesic comparison accepts the declared real-coordinate class and reports its units; do not collapse antipodal S3 points as though every point were a rotation quaternion.
- Frechet curve distance operates on realized ordered coordinates after exact constituent resolution, with the selected continuous/discrete and underlying point-metric variant visible.
- Karcher/intrinsic mean controls report selected samples, weights, manifold, solver/convergence and ambiguity. They do not silently overwrite the existing arithmetic composition centroid. A Frechet mean and Frechet curve distance are different operations.
- Exact constituent order/multiplicity stays available beside every summary. A zero geometric distance or equal mean alone cannot merge distinct canonical compositions.

These are first-class inspection/calculation controls selected by their typed operation metadata. They are not a UI-private mathematics implementation and are not described as already operational merely because the control is specified.

## 6. Reusable protected endpoint state

The inspected CIEDigital mechanism protects route values through a shared link helper and shared MVC action filter. It is evidence for transparent parameter handling; it is not evidence that every HTTP request body, response, route or network connection was encrypted. The inspected implementation uses DES with embedded key/IV material. No credential values are reproduced here and those historical choices are not a deployment specification.

For Laplace, preserve the centralized abstraction with a currently supported authenticated protection mechanism and managed keys. ASP.NET Core Data Protection offers purpose-separated protect/unprotect APIs and key management; time-limited protectors add expiry [S1-S3]. Exact hosting/key storage remains an explicit review choice, not a new vendor dependency selected here.

Distinguish three contracts:

1. TLS protects transport between communicating endpoints.
2. An opaque, integrity-protected envelope protects selected query/route/cursor state passing through a client. Bind its intended purpose/operation, schema version, target/read scope and expiry; principal binding is selected according to whether the link is private or intentionally shareable.
3. Authentication and authorization still decide what the caller may read or change. Decryption alone cannot grant data access or approve an effect.

Generic link helpers and request binding must invoke that service rather than each page implementing cryptography. Invalid, tampered, expired, wrong-purpose or wrong-scope envelopes fail explicitly before execution. Privileged single-use intent additionally needs replay/idempotency enforcement; encrypted read links do not automatically become single-use actions.

Ordinary public content links and SDK/API requests must remain usable through documented identifiers and authentication; do not require nonstandard encrypted payloads in every third-party client merely because protected UI navigation exists. An encrypted cursor is not the permanent identity of a record. Intentionally shareable links must reauthorize the recipient; revocation and source-scope changes remain effective. Large or sensitive filter state may use a server-held opaque reference rather than leak into a GET URL. Tokens and private plaintext must not enter diagnostics.

## 7. Refine existing feature acceptance, not another test-count goal

All cases below are proposed and unrun. They extend existing DBR/UX/AUTH/INT/EVO obligations in this same review:

- **DBR-08/30, EVO-01/02:** Add an authorized nullable numeric field of an already supported type. On descriptor refresh it appears with the right control, operators, formatting and documentation in the master list, related grid and detail field chooser, without new page-specific source.
- **DBR-09/18/30:** Add an admitted typed relation. Existing record-link, nested-filter and related-grid controls navigate it and back under the correct identity kinds and scopes. No source-family conditional is needed.
- **DBR-02/03/06/07:** Visual criteria and the supported textual query representation produce equivalent typed predicates/results, preserving case, null policy, witness grouping, ordering and top-N-before-paging boundaries. The same relation with two predicates must not accidentally use two different children.
- **DBR-10/11/14:** `King` and `king` remain separate exact content; shared constituents are reusable; opt-in case-folded matching can return both without merging them. Coordinate/trajectory/mean/curve comparisons retain distinct typed inputs and outputs.
- **DBR-24/25, AUTH-04/08:** Metadata, facets, hidden fields and nested targets are authorization-filtered before disclosure. A posted runtime type name or arbitrary column/operation is not accepted as a substitute for the installed approved descriptor.
- **AUTH-06/08, INT-01:** Protected route/cursor state round-trips through one reusable service; tampering, expiry, cross-purpose reuse and unauthorized recipient fail. Valid decryption does not bypass current grants, and ordinary SDK calls do not need custom route encryption.
- **DBR-29, QA-05:** Schema refresh, facet loading, related summaries and computed-field inspection remain bounded under the existing reviewed performance fixture. Do not copy unconditional count-all/eager relation loading into large-estate browsing.
- **UX-03/05/07, DBR-05/28:** Shared controls preserve focus, keyboard behavior, selection and return context across descriptor changes and nested navigation; invalidated filters are identified rather than silently discarded.

The user-visible success is dynamically useful browsing and correct linked data. The checks substantiate that result; they are not its replacement. This document neither authorizes application implementation nor declares these features complete.

## 8. Inspected primary-source locators

All repository links below pin the inspected revisions. Source references establish the described code, not a successful current deployment.

### SpecEditor

- [Schema discovery, SQL-to-runtime type mappings and refresh flow](https://github.com/AHartTN/SpecEditor/blob/68b8bed0160aab0bc2358b88fd3f006d4836dd7c/SpecEditor/SqlHelper.cs)
- [Dependent selectors, column/mapping grids and selection behavior](https://github.com/AHartTN/SpecEditor/blob/68b8bed0160aab0bc2358b88fd3f006d4836dd7c/SpecEditor/frm_Main.cs)
- [Discovered server model and relationships](https://github.com/AHartTN/SpecEditor/blob/68b8bed0160aab0bc2358b88fd3f006d4836dd7c/SpecEditor/Models/Binding/Server.cs)

### CIEDigital

- [Reflected filter creation](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigitalLib/Extensions/SearchExtensions.cs)
- [Nested and collection-aware query expressions](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigitalLib/Search/AbstractSearch.cs)
- [Generic list controller](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigitalLib/Controllers/BaseController.cs)
- [Sort and page expressions](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigitalLib/Extensions/PagingExtensions.cs)
- [View invoking shared search templates](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigital/Views/Combines/Index.cshtml)
- [Integer search editor](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigital/Views/Shared/EditorTemplates/IntegerSearch.cshtml)
- [Polymorphic request binder](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigitalLib/Binders/AbstractSearchModelBinder.cs)
- [Irony query expression construction](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigitalLib/Extensions/SearchExtensions_Irony.cs)
- [Protected link and metadata helpers](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigitalLib/Extensions/HtmlExtensions.cs)
- [Encrypted action parameter handling](https://github.com/AHartTN/CIEDigital/blob/5b6662d0151bb0dbfa5cacc72fbbe0981d7359d8/CIEDigitalLib/Attributes/EncryptedActionParameterAttribute.cs)

### Standards and existing Laplace geometry

- U1: [Unicode 17 core, code points and character classes](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-2/)
- U2: [Unicode 17.0 repertoire count](https://www.unicode.org/versions/Unicode17.0.0/)
- S1: [ASP.NET Core Data Protection](https://learn.microsoft.com/en-us/aspnet/core/security/data-protection/introduction?view=aspnetcore-10.0)
- S2: [Purpose-separated protection](https://learn.microsoft.com/en-us/aspnet/core/security/data-protection/using-data-protection?view=aspnetcore-10.0)
- S3: [Time-limited payloads](https://learn.microsoft.com/en-us/aspnet/core/security/data-protection/consumer-apis/limited-lifetime-payloads?view=aspnetcore-10.0)
- [Existing coordinate/trajectory/realized-curve law](../../architecture/PHYSICALITY_COORD_TRAJECTORY_REALIZATION.md)
- [Existing structural metric definitions](../../research/STRUCTURAL_TRAJECTORY_METRICS.md)
