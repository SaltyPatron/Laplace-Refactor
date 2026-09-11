# Entity-first data browser and cross-source inspection

Status: **design review; no application implementation or runtime acceptance claimed**. This is part of PR #272, not another implementation queue. The user's requested feature is required: browse Entity/other data collections with top-N, filters, sorting, clickable lists and rich reusable detail controls. Navigation labels and mechanisms below are review proposals. Verification supports those features; it is not the primary product experience.

Owners: #68 product browsing/materialization; #268 typed public queries and descriptors; #265 source-to-admitted-data navigation; #195 selected source estate. Existing #7/#13/#15 own canonical/Unicode/structural state, #16/#110 evidence and standing, #17/#60 semantic selection, #62 referential worlds, #64 access and #172/#174/#176 reusable inspection. No duplicate semantic owner is introduced. [Packet index](README.md), [screen contracts](SCREEN_CONTRACTS.md), [acceptance matrix](ACCEPTANCE.md), [traceability](ISSUE_TRACEABILITY.md).

## 1. Required experience and observation boundary

The direct correction is a persistent data workbench: start with the Entity table or another collection, ask for a filtered/sorted top-N, open a row, follow its typed links, inspect details at any structural altitude and return to the original list with context intact. This is neither a search-box-only product nor a forced graph visualization nor a page of test receipts.

A one-off query returning several letters' S3 coordinates does not satisfy this experience. Those results need navigable identities, field provenance, related physicalities, source versions, composition/occurrence drilldown and a way to issue the next ordinary inspection without an agent composing SQL.

Schema observations below refer to source commit `f02d78730aa6312885784a3a986ebb243810756f`, inherited by review base `c005a770ca793445d9cf5c3e04aed2c935f81572`. They describe declarations, not proof that a specific live installation has that schema or populated data. This design task did not query the live database. The `/vault/Data` and `.refresh-20260903` directory names are from the user's supplied terminal listing; file contents, digests and present database admission were not inspected here.

## 2. Two entrances to the same data

**Explore -> Data browser** is the ordinary entry, with collections for Entities, Physicalities, Occurrences, Relations/mappings, Evidence, Standing, Source releases, Reference coordinates, Programs/recipes and Results. They are typed views over the same substrate, not new parallel stores. A user can enter through any accessible collection rather than finding a word first.

**Storage view** exposes the actual installed schema/table/view names, columns, types, keys and authorized rows for technical inspection. Friendly `Entities` names the collection; the observed physical table is singular `entity`. The page always identifies the actual schema and installation. Registered raw readers must enforce row/column/world access before returning data; an app role never obtains an unrestricted database connection. Raw view is not an editor for immutable canonical rows. Changes use existing typed operations with explicit consequences.

Show a catalog of known collections and their installed/readiness state. A required but unavailable collection has its missing operation identified to an authorized operator; it is not replaced with made-up data or silently removed. Internally registered record families must have a browse/detail disposition and the reason for any intentionally nonpublic family.

### Actual storage distinctions that constrain the UI

Source: `integrations/postgresql/extension/laplace--version.sql.in`, especially entity/physicality declarations and reference/evidence sections.

| Actual declared object | What its reader must show | What must not be invented |
|---|---|---|
| `entity` | `entity_id` (16-byte content key), `identity_witness` (32-byte full identity witness); the first 16 witness bytes equal the key | An intrinsic source, English name, language, created-at timestamp or universal semantic type column |
| `physicality` | 32-byte physicality key; linked entity; physicality/vertex/form kinds; recipe version/fingerprint; geometry epoch; four centroid components; radius; logical/vertex counts; trajectory fingerprint and bytes | One physicality per entity, normalization of every composition to the unit sphere, or packed payload as S3 placement |
| `attestation` | Typed record kind, entity, optional physicality, source/context fingerprints and source ordinal | Every physical occurrence being a semantic proposition or independent evidence |
| `evidence_node`, `evidence_dependence`, `evidence_root_projection`, `evidence_testimony` | Occurrence/proposition links, source/profile, epistemic kind, dependence parents/roots, source-specific testimony and uncertainty | A flat count of records as independent support |
| `consensus` and typed standing records | Selected proposition/arena, evidence boundary, recipe, epoch, disposition; rating/RD/volatility where the standing contract supplies them | One global confidence/relevance/rating attached to every Entity |
| `source_profile` and profile receipt/member records | Exact authority/release/namespace/version, artifact/mapping fingerprints, declared coverage/dispositions and readback lineage | Directory existence as proof of admitted source data |
| `reference_coordinate`, `reference_occurrence` | Authority + release + namespace + local identifier + version; exact source row/field/value and occurrence | Same local identifier across sources/versions being the same referent |
| `reference_mapping_proposition`, `reference_mapping_occurrence` | Left/right reference coordinates, relation kind/version, mapping assertion versus its source occurrences, field identities and endpoint dispositions | String equality being a source mapping, or every mapping asserting exact equivalence |
| `execution_receipt` and specialized receipts | Exact program/input/output references and observed execution; reveal on demand from the relevant data/action | A receipt list substituting for the content browser |

`identity_witness` is an identity/collision-checking field; its name does not make it human testimony. Record IDs are typed: for example, `reference_mapping_proposition.proposition_id` is a 32-byte record ID while `evidence_node.proposition_id` references a 16-byte entity key. An ID-shaped string is not sufficient to choose a route or join.

Friendly columns such as display label, source coverage, language, structural tier, first-observed time and relation counts are **declared projections** with named providers/scopes. Their column info shows origin, derivation, selected versions, cardinality, missing-value meanings and sort/filter support. Raw `entity` remains inspectable without these enrichments. No schema change is required merely to show a useful joined column.

## 3. Master list: top-N means the requested result

Proposed toolbar: collection/table; field chooser; typed filter builder; ordered sort clauses; result limit; page size; saved view; snapshot/live mode; compare; export. A compact scope bar retains installation, world/audience, source/release selection, evidence/geometry versions, realization language and observation time.

For Entities, raw mode starts with the actual two identity fields. Enriched mode can add readable preview, selected physicality kind/count, logical size, language/sense/source summaries and other registered projections. All are optional; opening a 100-row identity list must not calculate every relation count or full label graph in the database.

The logical contract is:

    accessible collection at a declared read boundary
    -> typed predicates and scoped relationship predicates
    -> declared total ordering, including stable tie breaker
    -> top-N result boundary
    -> pages of that result
    -> bounded presentation enrichments

Filtering or sorting only the downloaded page is a defect. N is a result limit, not semantic relevance, graph fanout or page size. Default sorting is proposed as stable canonical key order; a request for 'top by standing' must choose a specific arena/recipe/epoch and sorting field. There is no universal 'best entities' score.

Filter operators depend on field types: equals/in/range for IDs and integers, numeric comparison for measured quantities, explicit substring/prefix for human text, null/missing/disposition, and grouped AND/OR. Exact content search is distinct from normalized/case-folded text or human-label search. Unicode codepoint order, DUCET placement order and locale collation are distinct sort choices. Layout preference never changes underlying identity.

Relationship filters specify their quantifier and witness scope. 'Has an English WordNet sense' differs from 'has an English occurrence and has some WordNet occurrence.' A same-occurrence grouping must bind both predicates to one occurrence when requested. ANY, ALL and NONE have documented empty-set behavior. Filtering by two sources uses an existence/semijoin or its equivalent and preserves one Entity row unless the user explicitly chooses an occurrences view.

Sorting a multi-valued field needs a selected physicality/sense/arena or an explicit aggregate such as min/max over a named scope. Do not pick an arbitrary joined row. Null, unknown, not applicable and denied cannot all become zero. Tie breaking and null order must be deterministic and visible.

Pagination uses a bounded, server-defined result/cursor identity tied to the query, sort, schema, authorization and read boundary. A semantic epoch alone does not freeze concurrently inserted rows or every derived column. Snapshot mode needs an actual coherent read mechanism, such as a bounded retained result under pinned dependent versions; it must not keep a database transaction open indefinitely while a person browses. Live mode explicitly says results can change and offers a refresh/new-results action. Expired snapshots/cursors have visible recovery, never silent mixing of generations.

A total count is optional and independently exact/estimated/unknown with its boundary. Display '100 returned; more available' when that is all proven. A large exact count or non-indexed derived sort may require a cancellable background query through the same job system; never silently sort a sample and call it global top-N. Saved views store the query/presentation recipe, not grants or an immortal result; reopening reauthorizes.

Selection distinguishes this page, selected IDs, top-N result and all matching records. Exports preserve this distinction, source/snapshot scope, exact IDs and field types. CSV exports neutralize spreadsheet formula injection without changing the canonical content or exact JSON/binary export. Full expensive export is a separately visible bounded operation.

## 4. Master-detail navigation and reusable controls

Clicking a row selects a split-pane inspector. Its ordinary link also supports full detail, open-in-new-tab, copy link and keyboard navigation. Users can pin two records for comparison. Back restores collection, filters, column order, result boundary, cursor, scroll, selection and expanded sections. Following a link adds a navigation breadcrumb; the breadcrumb is a browsing path, not a semantic proof.

The detail header shows readable realization plus exact typed identity, selected world/version, and current disclosure state. Primary content appears before technical receipts. Suggested tabs: Overview; Structure; Used in; Relations & mappings; Evidence & provenance; Geometry & Highway; History; Raw. Applicable tabs share a common shell and report their own loading/error/partial state; one slow tab must not block the entire record.

| Reusable control | Required function |
|---|---|
| Typed record link | Names target record kind, full key, optional realization, versions and allowed actions; no route inferred just from hash length or field suffix |
| Field inspector | Exact value/type/unit, origin, missing-state, copy raw/formatted, filter by this value, show definition |
| Related-record grid | Incoming/outgoing, role/type, source/time/world filters, scoped counts, cursor paging, open target or open relationship itself |
| Ordered structure browser | Constituents, ordinal/range, run length, gaps, structural witness roles and links up/down the composition hierarchy |
| Occurrence/context viewer | Where exact content occurred; container and source span; neighboring content; document/sentence/game/code context |
| Relation/role inspector | Typed law/direction/arity and participant roles; source assertion versus derived calculation; mapping status and proof path |
| Evidence/dependence inspector | Original source passages, repeated/mirrored copies, independent roots, contradiction and derived ancestry |
| Standing inspector | Arena, participant/opponent/event, rating/RD/volatility, prior and selected epoch; no unlabeled global score |
| Source/artifact locator | Release -> artifact/member -> native source record/field/span -> admitted occurrence; reverse path to ingestion and exact recipe |
| Geometry/Highway inspector | Exact four-component physicality, recipe/epoch, decoded trajectory versus placement, typed coordinate/mask namespaces |
| Comparison pane | Same fields, scopes and versions side by side; distinguish content differences from new evidence, labels or physicalities |
| Context-preserving workbench | Expand dense grids, structure, media or graph without semantic rerun or loss of selection (#176) |

Foreign keys can generate storage navigation. Semantic links additionally require declared typed providers; equal bytes and similarly named columns do not create a relation. A provider descriptor declares source/target record kinds, identity resolution, traversal operation, direction/roles/cardinality, applicable epochs, authority, supported predicates/order, result schema, units, completion and renderer. Reusable controls consume this; source-specific code supplies only irreducible parsing/semantic profile interpretation under existing owners.

Graph is an optional view of the same selected typed records, not the only way to browse or a separate UI traversal algorithm. N-ary events/frames retain their participant roles and shared event identity. A pairwise projection is labelled with its loss and links back to the complete relation. Structure links derived from trajectories remain calculations, not newly invented PRECEDES/CONTAINS testimony rows.

## 5. Concrete Unicode and composition journey

Start: Data browser -> Entities -> Unicode preset. Filter a codepoint range or property; sort by codepoint, placement rank, or selected coordinate; choose N. Those fields come from the root/physicality projections, not nonexistent entity columns. `engine/include/laplace/unicode_root.h` declares codepoint position, placement rank, position class, LUP representation, content ID, coordinate, Hilbert key, geometry epoch, physicality ID and typed Unicode fields.

Opening `A` (illustrative operand, not fresh live readback) must expose:

- raw entity key and full identity witness;
- codepoint/name/properties and selected UCD release; script is not silently inferred to be language;
- physicality key and real x/y/z/m values, radius, vertex class, structural form, recipe and geometry epoch;
- round-trip-safe decimal plus raw IEEE-754 bytes; any norm/error computation names its precision/recipe;
- root/source receipt and actual source field provenance;
- ordered containment/usage lists: compositions containing the atom, precise ordinals/run ranges and distinct source occurrences;
- codepoint/DUCET/normalization/case relationships with declared source/algorithm, including unresolved or unavailable detail;
- typed Highway coordinates/masks and decoded components where the relevant contract supplies them, separately from S3 and display projection.

Position classes include assigned, unassigned/reserved, private use, noncharacter and surrogate-address classes. Not every codepoint position renders a Unicode scalar glyph; expose a safe explicit notation rather than forcing an invalid character through UTF-8. Normalization/case transforms create or reference their declared output content; they do not silently merge unequal canonical input bytes. A single composed character and a combining sequence may have a declared normalization relationship without sharing exact-content identity.

For a composition, show stored vertex count and expanded logical count separately. Repeated content may occupy one encoded run and many logical positions. Go-to-ordinal/range must be bounded; the UI cannot expand a billion-element run just to inspect one constituent. Different compositions may share a centroid; never use coordinates alone to identify content or its exact path. Multiple physicalities retain their recipe/epoch selection; the panel cannot arbitrarily pick the first.

Copying machine values must preserve 16/32-byte IDs, 64-bit integers, `numeric(20,0)` ordinals and floating-point bits. JavaScript Number roundoff, shortened hashes and pretty-printed decimals cannot become the readback authority. Frontend projection/rotation is presentation only; it must never mutate stored coordinates.

## 6. How the data interconnects

There are several linked structures, not one undifferentiated graph:

**Exact content/structure:** shared atoms and compositions; immutable identity; physicalities; ordered constituents/containment/repeated trajectories. This supports descending to exact parts and rising to every eligible use.

**Occurrences and source structure:** a particular appearance in a sentence, book, frame definition, source row, code file, game or media item. Same content can occur many times, with different offsets, contexts and source history.

**References and semantic mappings:** source-versioned identifiers point to lexical entries, senses, synsets, frames, roles, documents, people, places and other referents. A relation mapping connects typed endpoints; it is not equality of the labels attached to them.

**Evidence, calculations and standing:** source assertions, derived facts, deterministic calculations, dependence roots, temporal applicability and scoped standing are independently inspectable. One can disagree without erasing the others.

**Execution and product state:** programs, recipes, acquisitions, ingestion attempts, publication epochs, queries and outputs explain how state became accessible. They are reachable from content rather than replacing it as the opening screen.

### Three navigation paths that must compose

1. **Bottom-up:** codepoint -> composition -> containing occurrence -> source document/record -> its source-backed sense/referent -> typed connections.
2. **Top-down:** source release -> artifact/member -> source-native record -> field/span -> admitted occurrence -> canonical Entity -> other eligible uses of the same content.
3. **Across:** selected sense/reference -> explicit mapping with source/version/role -> target sense/frame/reference -> examples/occurrences and their canonical constituents.

Every transition keeps world/audience, source/evidence scope and relevant version pinned unless the user explicitly changes scope. An outside-scope destination is labelled as such when disclosure permits. Following an inspect link must not trigger ingestion, recompute consensus, mutate the selected world or grant access.

### Example: a lexical form is not a sense

A prospective `bank` inspection starts from the exact surface composition, then shows its observed lexical entries and senses separately. Financial and river senses must not merge merely because they share spelling. A chosen WordNet synset may have an ILI mapping to another lexicon's synset; follow that source-backed mapping to its OMW lexicalizations. The CILI coordinate is a bridge, not a reason to erase the source synsets or assert that every translation is interchangeable in every context. WordNet-LMF explicitly separates lexical entries, senses and synsets and permits ILI links [R2].

A selected occurrence can have competing sense hypotheses. Lexical metadata describes what senses exist; it does not prove the intended sense in every UD sentence, subtitle or book that contains the word.

### Example: predicate and argument roles

From a selected predicate sense, inspect available VerbNet classes, FrameNet frames and PropBank rolesets through mappings such as SemLink. Follow the mapping row and its actual release provenance, then inspect role correspondences and annotated examples. SemLink explicitly relates these lexical resources [R3]. `ARG0`, `Agent`, a frame element and a syntactic subject are not universally identical: any alignment is bound to the relevant roleset/frame/mapping and context.

FrameBase adds a declared frame/synset-linked representation [R4]; VerbAtlas supplies its own predicate/frame/role mappings [R5]. Predicate Matrix and historical MapNet/WordFrameNet mappings retain the method and lineage of their derived alignments rather than being counted as fresh independent corroboration [R6]. Not every predicate has a complete mapping chain.

### Example: cross-domain inspection without a word shortcut

For `fork`, a lexical sense, an occurrence in a chess explanation, and a calculated board motif are different linked objects. A valid inspector may traverse a grounded explanation to its game/position, the calculated motif and player occurrences. It must show the actual grounding calculation/mapping. A string match in a book does not create that bridge. The same occurrence/source/role/geometry/evidence controls must also inspect an unrelated sentence or code structure, rather than make chess a private data model.

## 7. Source estate map for the user's listing

Below, `root/` means `/vault/Data/` and `refresh/` means `/vault/Data/.refresh-20260903/`. The paths are observed names from the supplied listing, **not independently verified file inventories or admitted-state claims**. #195 names the selected releases; discovery/readback must reconcile the physical copies. Each row describes required inspection, not a bespoke importer or automatic mapping promise.

| Supplied locations | Required navigable structure and connection | Boundary to preserve |
|---|---|---|
| `root/UCD`, `root/ISO639` | Unicode properties/order/normalization -> canonical atoms; language registry -> declared language coordinates used by lexicons and corpora | Script, language, character, exact content and semantic sense are different; UCD source data is not the Laplace placement algorithm [R1] |
| `root/Wordnet`, `refresh/OpenEnglishWordNet-2025-plus`, `root/OMW`, `root/omw`, `refresh/OMW-2.0`, `root/CILI`, `refresh/CILI` | Lexicon/release -> entry/form -> sense -> synset -> explicit ILI/mapping -> another lexicon -> definitions/examples/relations | Keep PWN/OEWN/OMW release coordinates; #195 supersedes legacy OMW/omw; lowercase/uppercase paths remain distinct physical occurrences until verified [R2] |
| `root/Wiktionary`, `refresh/Wiktionary` | Entry/language/POS/etymology -> individual senses, forms, translations, examples and citations -> exact content and any explicitly resolved external references | Same headword or English gloss is not automatic WordNet sense equivalence; preserve ambiguous and missing alignment |
| `root/FrameNet`, `root/VerbNet`, `refresh/VerbNet`, `root/PropBank`, `refresh/PropBank`, `root/SemLink`, `refresh/SemLink` | Frames/LUs/frame elements, verb classes/thematic roles/restrictions, rolesets/arguments, mappings and annotated spans | Frame roles, predicate roles and syntax retain their own types and applicable releases [R3] |
| `refresh/FrameBase-2.0`, `refresh/VerbAtlas-1.1` | Declared frame/schema/microframe/synset links; predicate/frame/role correspondences; instance or example links only when present | Integrated schema and its source lineage are not new independent world observations [R4,R5] |
| `root/MapNet-0.1`, `root/PredicateMatrix.v1.3`, `root/WordFrameNet` | Historical/derived mapping records -> exact endpoint versions -> mapped/unmapped roles and disagreements | Retain unique mappings and provenance until explicit reconciliation; no quiet deletion or double voting of inherited mappings (#195) [R6] |
| `root/ConceptNet`, `root/Atomic2020`, `refresh/Atomic` | Typed assertions/events -> role-bearing endpoints and conditions -> source utterances/provenance; literal content reused with other corpora | ConceptNet terms are not automatically WordNet senses; ATOMIC participant variables are roles, not named people; resolve actual refresh artifacts before classifying human versus machine lineage |
| `root/UD-Treebanks`, `refresh/UD-Treebanks` | Treebank/document/sentence -> surface spans, tokens/words, lemmas/features and dependency edges -> exact composition/occurrence | Multiword tokens, syntactic words, empty nodes and enhanced dependencies are distinct; lemmas/POS do not by themselves assign semantic senses [R7] |
| `root/Tatoeba`, `refresh/Tatoeba`, `root/OpenSubtitles` | Sentence/caption content and occurrences -> translation/alignment records, language and declared audio/timing links -> structural constituents | Translation links are not content identity; preserve one-to-many alignments, caption timing, sentence IDs and audio availability separately; #195 requires all selected Tatoeba sidecars |
| `root/ProjectGutenberg`, `refresh/project-gutenberg-relocation.tsv` | Work/edition/artifact -> chapter/page/paragraph/sentence/span where recoverable -> repeated canonical content, names and grounded references | A relocation manifest is location evidence, not another edition or independent witness; page absence in an edition remains explicit |
| `refresh/GeoNames`, `refresh/NaturalEarth` | Place reference -> names/languages, feature codes, hierarchy, geographic coordinates; boundary/geometry feature -> place only by declared identifier or qualified matching | Same place name is not same place; CRS/latitude/longitude and containment calculations are not S3 placement; do not auto-unify datasets by name [R8] |
| `root/Games`, `refresh/Games`, `refresh/TWIC`, `refresh/LichessOpenings` | Dataset/event/game occurrence -> player references -> ordered moves/positions -> opening references and separately grounded documents | Position identity is separate from game/player/event; tablebase/calculation presence requires actual manifests, not the directory name |
| `root/TreeSitter`, `root/code-authority` | Inventor-selected grammar/code authority -> file/revision -> CST/AST field/span/reference -> exact shared composition -> diagnostics where admitted | Verify actual directory contents; symbol equality and matching identifier spelling are not universal referential equality |
| `refresh/Safety` | Dataset/edition/example -> annotation dimensions, annotator/method, contrast/context and dependent generated lineage -> exact utterance | Preserve each source's own labels and uncertainty; one global score must not erase context or distinct annotations (#195) |
| `root/LaplaceAssets`, `root/test-data` | Discover artifact roles, byte identity, fixture/asset provenance and declared admission choices | Contents uninspected; names alone do not establish a modality or production-seed role; fixtures cannot masquerade as independent corpus evidence |
| `root/Unicode.BAD-DONOTUSE` | Show explicit excluded/superseded state and its replacement when authorized | #195 forbids using it as the selected Unicode witness; do not delete it or ingest it based only on this design |
| `refresh/DOWNLOADS.sha256`, `refresh/DOWNLOADS.local.sha256`, `refresh/STAGING_MANIFEST.tsv`, `refresh/STAGING_LOCAL.tsv`, `refresh/REFRESH_RECEIPT.tsv` | Acquisition/integrity/location records -> precise selected artifacts -> profiles -> ingestion jobs and actual admitted outputs | Inspect exact manifest semantics first; SHA-256 integrity receipts are not canonical BLAKE3 identity or world-admission proof |

`/vault/models` is separately tracked by #223; it was not in this terminal listing and is not newly inventoried here. Model-source inspection later reuses these controls for artifact/shard/tensor/recipe structure rather than becoming another disconnected browser.

## 8. Link completeness and unresolved references

The source browser needs an interconnection matrix, not merely one ingest percentage per folder. Each declared bridge has source/target family and release, mapping provider/recipe, supported relation/role kinds, witness lineage and actual endpoint coverage. A cell opens the same filtered mapping grid. Example rows: PWN-to-CILI, OMW-to-CILI, SemLink predicate/role maps, FrameBase schema links, source-declared translation/alignment, geography references and grounded book/game links. There is no expectation that every pair of sources has a direct mapping.

Use separate denominators for eligible source references, resolved references and distinct canonical targets. Mapping dispositions include resolved, unresolved, ambiguous, conflicting, retired, out-of-scope, unsupported and not examined, with precise unavailable/denied presentation. Public counts cannot reveal unauthorized missing targets. 'No results in the examined scope' is not 'no relationship anywhere.'

An unresolved row retains its source span, target namespace/version, candidates where permitted, missing prerequisite and dependency. A later target-source admission can resolve it under a new evidence/publication boundary. Prior snapshots remain replayable. Correction or acceptance of a proposed alignment is an explicit existing authority operation and preserves earlier evidence; expanding a table cannot implicitly approve it.

No links are invented from: equal display names; adjacent rows; nearby S3 points; numeric IDs equal in different namespaces; model-generated gloss similarity; repeated mirrors; or receipt IDs sharing byte width. Candidate similarities may be inspected as candidates with their calculation, never styled as established mappings.

## 9. Read contracts, performance and evolution

Extend #268's common operation descriptors with collection/field/relation descriptors. Public browse, inspect, related-records, structure-range, source-locator and compare operations all use generated typed inputs and native semantic providers. SQL can perform approved relational restriction/order/page transport, but must not become another cognition/geometry/evidence engine. No handwritten dynamic per-row traversal or arbitrary SQL submitted by the browser.

A browse request needs dataset/record kind, typed filters and grouped relationship conditions, fields, ordered sort/tie rule, top-N/page size, scope, dependent versions, read-boundary mode and cursor. A response needs rows with typed links, query/result identity, schema/descriptor revision, observed/pinned boundaries, per-field availability, continuation, coverage/completeness, optional scoped count and correlation/receipt. Descriptor publication must be bound to the installed schema/provider set; a stale client receives an explicit refresh/conflict, not reinterpreted columns.

Batch-fetch visible labels and related summaries. Each panel has independent paging/cancellation and a bounded request/resource budget. Do not execute one SQL/API lookup per table cell, materialize the whole evidence graph for a badge, or use unlimited client retries. Index/perfcache shortcuts retain their completeness class and are not truth authority. Unsupported required indexes/providers are implementation work, not an excuse to return an arbitrary sample as the requested result.

Do not add another competing performance envelope here. This journey consumes QA-05 and the reviewed budgets in [ACCEPTANCE.md](ACCEPTANCE.md); extend the fixture to high-cardinality shared atoms/forms, repeated trajectories and concurrent ingestion. Pin list/projection/related-grid work, measure first usable results and interaction separately, and preserve control responsiveness. A deliberately slow detail tab cannot starve the master list or cancellation.

Adding a record or relation family must update its browse fields, links, shared controls, authority, source provenance and acceptance in the same owning change. Introspection supplies schema mechanics, not domain meaning. Each non-UI family has an explicit reason and a useful indirect inspection path.

## 10. Feature acceptance — proposed executable cases, all NOT RUN

These local `DBR-*` review IDs refine existing owners. They are not new approved `LP-*` requirements, implemented tests, or a separate milestone. Keep the user feature visible: list -> detail -> useful connected data. Each case must eventually pair installed-browser behavior with the real native/storage result; test existence alone does not close it.

| ID / owner | Required observable behavior | Deliberate defect that must fail |
|---|---|---|
| DBR-01 / #68 | Open Entities/raw `entity`, choose N and inspect a row without a search term, SQL or an agent; both identity fields visible | Search-only or graph-only entrance |
| DBR-02 / #268 | Fixture puts qualifying rows beyond page one; filtering then sorting returns the actual top-N | Page-local filter/sort or limit-before-filter |
| DBR-03 / #268 | Tied sort values, nulls and large IDs page in declared total order without missing/duplicate rows | No stable tie breaker or numeric-text sort |
| DBR-04 / #268 | Concurrent ingestion respects a real pinned snapshot; live mode labels change and refreshes explicitly | Epoch label falsely presented as a database snapshot |
| DBR-05 / #68 | Opening/expanding/back/new-tab restores list, cursor, columns, scroll, selection and record versions | Detail navigation reconstructs a different result |
| DBR-06 / #268 | One entity with multiple physicalities/occurrences remains one Entity row under source filters; multi-value sorting declares its selector | Fanout join duplicates rows or arbitrary first physicality |
| DBR-07 / #268 | Same-occurrence versus separate-existence filter fixtures produce intentionally different results | Mixing witnesses from different occurrences to satisfy one grouped condition |
| DBR-08 / #68 | Every shown field identifies stored/joined/calculated/realized origin; raw entity has only actual columns | Fabricated intrinsic source/name/date/rating column |
| DBR-09 / #68 | Follow and inspect both directions of typed record links, including a 256-bit mapping record and a 128-bit entity | Treat every ID/proposition field as the same entity route |
| DBR-10 / #68 | Unicode row -> actual physicality -> exact components/raw bits -> source/root -> containing structures | Placeholder coordinates, packed payload as placement or rounded-only readback |
| DBR-11 / #68 | Supplementary, combining, unassigned/noncharacter and surrogate-address cases render exact supported notation and retain their classes | Invalid UTF-8 glyph, normalization merge or script-as-language |
| DBR-12 / #68 | Large logical ordinals/counts and identity bytes round-trip through copy/export without loss | JavaScript Number rounds an ordinal above 2^53 or truncates a key |
| DBR-13 / #68 | Inspect a distant logical position inside a compressed repeated run with correct multiplicity and bounded work | Expand every vertex/occurrence or equate vertex count with logical count |
| DBR-14 / #68 | Same-coordinate different-content and changed-recipe physicalities stay distinct | Centroid equality merges entities or every composition is renormalized to S3 |
| DBR-15 / #68 | A multi-sense lexical form retains separate senses; an explicit CILI bridge reaches a selected lexicon's entry with provenance | Same spelling, gloss or local numeric ID becomes identity/equivalence |
| DBR-16 / #68 | Real selected predicate and role maps retain source/target versions, roles and unresolved candidates | Universal ARG0=Agent or a many-role frame flattened into unexplained binary edges |
| DBR-17 / #68 | Table, graph and expanded workbench render the same bounded typed selection and evidence | UI-private graph traversal/ranking or expand-causes-query rerun |
| DBR-18 / #265 | Source -> artifact/member -> row/field/span -> canonical entity -> other source occurrences is traversable both ways | Source detail ends at a job log or an inaccessible hash |
| DBR-19 / #265 | Original/root and refreshed copies retain locations, exact release, selected/superseded/fixture state | OMW/omw path collision, BAD Unicode selected, SHA-256 used as canonical ID |
| DBR-20 / #195 | Bridge matrix reports actual scoped reference coverage; missing target later resolves in a new boundary without erasing history | Treat acquisition success as connected data or remove unresolved rows |
| DBR-21 / #68 | Copied/derived mappings show dependence and distinct root evidence; standings expose their arena/epoch | Mirrors count as independent corroboration or a single global Entity rating |
| DBR-22 / #68 | UD surface/MWT/empty-node cases, translation/audio links and a document span use shared structures with typed overlays | Lemma assigned as sense, translation as byte equality or zero testimony treated as no data |
| DBR-23 / #68 | Geographic place and boundary inspection preserves CRS, names and mapped references, separate from S3 | Equal place name auto-merges records or latitude displayed as structural coordinate |
| DBR-24 / #64 | Private deduplicated content is protected through list/detail/counts/links/search/export/graph/raw view | Shared hash, disabled button or cache reveals another world's data |
| DBR-25 / #268 | Revocation/stale schema/snapshot expiry reauthorizes or returns a typed state; saved view grants no extra access | Old cursor or saved result bypasses current permission |
| DBR-26 / #68 | Unknown/missing/denied/not-examined/partial states remain distinct; totals are optional and correctly qualified | Missing values become zero or unknown absence becomes no relationship |
| DBR-27 / #268 | Large derived sort/export is explicit bounded work, not a silent sample; output matches selected page/top-N/all scope | User selects all matching but receives only currently rendered rows |
| DBR-28 / #68 | Grid, inspector and reused controls handle keyboard, focus, Unicode and safe untrusted content under existing UX criteria | Click-only links, unsafe HTML/CSV payload or lost focus on resize |
| DBR-29 / #268 | High-fanout entity, active ingestion and a slow detail provider preserve reviewed latency/resource budget and bounded requests | Per-cell N+1 SQL/API calls, full-world scans for badges or retry storm |
| DBR-30 / #68 | New declared relation family appears through the common linked-grid/inspector contract with tested semantics and documentation | One hard-coded browser per source or a backend capability delivered without inspection |

For cross-source positive fixtures, choose real rows that actually carry the required mapping in the pinned selected releases; do not promise a particular named word has a mapping everywhere. Synthetic ambiguity/broken-link fixtures supplement, not replace, real source-backed navigation. Missing required source or provider keeps the relevant product acceptance open. One positive lexical case does not certify the entire estate.

## 11. Primary references and repository evidence

Repository declarations establish current field/record distinctions, not live deployment or completed UI: `integrations/postgresql/extension/laplace--version.sql.in`; `engine/include/laplace/unicode_root.h`; `contracts/authority-stack.json`; source estate #195; source/mapping/standing owners listed above. The user-provided directory listing supplies path occurrence evidence only.

External references explain source data, not Laplace semantics or current local release contents:

- R1: Unicode Character Database specification, https://www.unicode.org/reports/tr44/
- R2: Global WordNet Formats, https://globalwordnet.github.io/schemas/
- R3: SemLink, https://verbs.colorado.edu/semlink/
- R4: FrameBase schema, https://www.framebase.org/schema
- R5: VerbAtlas predicate/frame/role mappings, https://verbatlas.org/api-documentation
- R6: Predicate Matrix project, https://adimen.ehu.eus/web/PredicateMatrix and its version-1.3 publication summary, https://www.lrec-conf.org/proceedings/lrec2016/summaries/950.html
- R7: Universal Dependencies CoNLL-U format, https://universaldependencies.org/format.html
- R8: GeoNames dump field/relationship documentation, https://download.geonames.org/export/dump/

The source matrix also consumes #195's exact selected-source obligations; it does not infer archive contents, semantic admission, complete mappings or source license compatibility from public descriptions. No fixture is marked passed by this document.
