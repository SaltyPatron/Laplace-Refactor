# Product model: faceted information worlds, not implementation workspaces

Status: reconstruction dossier. Direct inventor corrections supersede the narrower master/detail interpretation of the historical “league site, not a lab” note.

## 1. Product law being reconstructed

Laplace is not intended to make a user understand substrate tables, source adapters, graph tools, SQL workspaces or provider-specific ingestion flows before they can explore a subject.

The ordinary product model is a **domain-native, faceted information world** backed by common substrate/query/evidence semantics.

A world can be entered and reorganized through peer dimensions such as:

- domain/world;
- topic;
- type/class/noun/entity family;
- canonical or referential entity;
- relation family/direction/role;
- source/provider/release;
- standing/consensus arena;
- ranking/leaderboard/measure;
- occurrence/event/game/time/season/epoch;
- structural altitude/tier;
- evidence/provenance/dependence/contradiction;
- modality/realization where relevant.

This list is not yet declared exhaustive. It is the currently evidenced/corrected facet set.

## 2. What the sports-site analogy actually means

The historical product note says “a league site, not a lab” and illustrates one hierarchy:

```text
league -> division/conference -> team -> position -> player -> roster -> schedule
```

That hierarchy remains a useful sports-domain projection.

The incorrect reconstruction was to promote it into the whole UX architecture as master/detail CRUD.

The direct correction is broader:

> MLB/NBA/NFL-style product behavior means a dense information world where users can enter through many familiar subjects and continuously pivot among related topics, entities, relations, rankings, records, sources, events and evidence without falling out into unrelated technical tools.

A sports site does not require a user to think “which storage collection owns this?” before moving from player → team → game → opponent → standings → season → statistic → source.

## 3. Reusable semantics do not imply identical screens

The reusable product layer should share semantics/mechanics such as:

```text
resolve/search
filter/scope
group/facet
rank/sort/page
compare
follow/recenter
inspect relation/evidence/provenance
change epoch/time/world
request applicable acquisition
run applicable operation
materialize/realize in requested modality
```

The renderer then reflects the domain.

Representative presentations:

| World/subject | Natural domain-native materializations |
|---|---|
| NFL/NBA/MLB team | standings, roster, schedule, games, players, record, statistics, division/conference, seasons, sources |
| sports player | identity/profile evidence, team history, games, opponents, statistics, rankings, seasons, records |
| chess player | provider/reference identities, career, games, rivals, openings, ratings/standing, game boards, sources |
| chess game | board, move sequence, clocks/timeline, players, event, opening, source/provenance |
| language concept | surfaces/lemmas/senses, taxonomy, frames/classes/roles, translations, facts, sources, standing |
| document/book | rendered content, structure, sections/spans, entities/topics, sources/provenance, citations/relations |
| place/GIS entity | map/boundary/contained places, linked entities, sources, time/version, statistics |
| image/audio/video | native media rendering plus structural regions/timeline/metadata/relations/evidence |
| code/repository | source tree, AST/composition, symbols/relations, commits/occurrences, tests/evidence, generated artifacts |
| TTRPG/campaign | characters, party/factions, places, quests, events, rules, state transitions, reputation, chronology |
| source estate | release/artifact/coverage/provenance/admission/job state — especially for operators |

The universal substrate is not permission to render all of these as the same record table.

## 4. Old product evidence that points toward the richer model

### TopicView

Historical `web/src/topic/TopicView.tsx` describes the topic page as:

> “ask once, see EVERYTHING. No shape dropdown, no mode picking”

It simultaneously requests definition, taxonomy, strongest facts, translations, mesh position and verdict/record state.

**Interpretation:** positive evidence for a topic-centered information world; negative evidence against requiring users to select internal query shape first.

### Chess player index/page

Historical chess surfaces include:

- player search and alphabetical browse;
- rank/standing/game-count sorting;
- external/provider profiles and aliases;
- career record and color splits;
- chronological games;
- opponents/head-to-head;
- source-tagged rating distributions;
- domain board/game realization;
- pivot to the generic substrate entity.

**Interpretation:** positive evidence that domain-native pages can aggregate many substrate/evidence facets while still permitting universal pivots.

### Browse/Highway/Mesh/Warehouse/etc.

Historical screens contain useful views but fragment dimensions into named tools.

**Interpretation:** evidence that the capabilities existed separately; **counterexample** to making users navigate internal feature islands to combine them.

## 5. Common information-world context

**SYNTHESIS pending explicit contract formalization**

A reusable world materialization needs an addressable context resembling:

```text
subject / current center
+ domain/world scope
+ entity/type/structural scopes
+ relation/provider families
+ source/evidence/dependence boundary
+ standing/ranking measure
+ time/epoch
+ authority/audience
+ selected realization/modality/language
+ navigation/pivot state
```

A pivot changes one or more coordinates while preserving the rest when still applicable.

Example:

```text
NFL -> AFC -> Chiefs -> Mahomes -> 2024 games -> Bills opponent
```

and:

```text
passing leaders -> Mahomes -> Chiefs -> AFC West standings
```

are two routes through the same domain world, not unrelated applications.

Likewise:

```text
Magnus Carlsen -> game -> opening -> other players using line -> book/source
```

should remain navigable through common entity/relation/evidence semantics.

## 6. Entity/task-first acquisition

**DIRECT INVENTOR CORRECTION**

Ordinary users should not need to decide “FIDE or Chess.com or Lichess?” before asking for a player.

The user goal is the thing/task:

```text
find Magnus Carlsen
```

The machine should be able to expose:

```text
resolved/candidate person/player state
+ already admitted profiles/sources
+ external identities where witnessed
+ missing/eligible evidence
+ candidate acquisition providers
+ ambiguity/conflict
```

and from that same context perform an authorized acquisition workflow.

Representative generic program:

```text
user search / current entity
    -> candidate referential resolution
    -> inspect admitted provider/source testimony
    -> identify missing/eligible acquisition paths
    -> preflight authority/cost/source recipe
    -> acquire
    -> deposit source occurrence/testimony with provenance
    -> reconcile referential candidate under evidence law
    -> refresh same information world
```

### Identity safeguard

Entity-first acquisition is **not** fuzzy automatic fusion.

```text
provider search hit
name similarity
shared display name
    !=
referential identity proof
```

The acquisition layer can discover candidates while the evidence/referential layer decides whether correspondence is justified.

## 7. Source-first workflow still exists — for operators

The product needs a separate operator/admin source-estate experience for tasks such as:

- releases/manifests;
- credentials;
- source recipes;
- bulk seeding;
- schedules;
- failure/retry/recovery;
- provenance/readback;
- coverage/health;
- storage/resources.

The mistake is exposing this as the ordinary user's required path for learning about an entity.

## 8. Exploration facets are semantic projections, not top-level app silos

A relation view, leaderboard, source view, graph, timeline, structural hierarchy or consensus view is a projection over the current information world.

Therefore when relevant it should be possible to move, for example:

```text
entity
 -> its ranked relations
 -> one relation's evidence
 -> source
 -> another entity in that source
 -> compare standing
 -> open timeline
```

without changing to an unrelated semantic engine.

A dedicated expert route may expose the same projection with additional controls; that does not make the expert tool the product ontology.

## 9. Rankings and consensus must name their measure

A sports-like product naturally contains standings and leaderboards, but Laplace must not hide typed measurements behind a universal “importance” score.

At minimum, presentations must distinguish:

- arena-scoped `rating`;
- `RD` / uncertainty;
- conservative/effective standing such as `rating - 2*RD` where that recipe applies;
- eligible witness count;
- source/independent-root count where relevant;
- relation-specific measure;
- domain-native externally witnessed measures such as Elo when explicitly sourced/calculated as Elo.

Generic `witnesses` are not automatically “games.”

## 10. URL/addressability and reuse

The information world should preserve shareable state for applicable coordinates such as:

- subject/entity/topic;
- selected facet(s);
- rank/sort/filter;
- domain/world/source/time/evidence scope;
- epoch;
- cursor/page;
- realization/language/modality;
- comparison target.

This is compatible with old reusable web-engineering instincts—typed common query/paging/navigation machinery—without inheriting old MVC CRUD presentation as product law.

## 11. CIEDigital's actual relevance

**IMPLEMENTATION EVIDENCE / RECURRING PRACTICE**

CIEDigital contributes evidence for:

- generic `BaseController<T>` behavior;
- reusable search abstractions;
- common paging/filtering/order machinery;
- metadata/reflection-driven helpers;
- inheritance/interfaces for shared mechanics;
- central cross-cutting route concerns.

It does **not** establish:

- master/detail as Laplace UX;
- table-per-domain product architecture;
- CRUD screens as reference-world design.

Census-Data-Parser and GISParser show that the same generic/schema-driven engineering tendency predates CIEDigital.

## 12. Product acceptance implications

A whole-product UX proof must include at least:

1. **sports/reference world** — enter through league/team/player/ranking/game/topic and pivot laterally without implementation-workspace knowledge;
2. **chess world** — person/player search, games/opponents/openings/sources/rankings, entity-first acquisition and generic entity-world pivots;
3. **language/topic world** — topic/type/noun/relation/source/consensus/translation/structure pivots without requiring query-shape knowledge;
4. **non-text modality** — native image/audio/video or another modality presentation while preserving universal identity/evidence navigation;
5. **source/evidence drill-through** — every friendly presentation can expose provenance/standing/receipts without turning raw provenance into the default UX;
6. **operator separation** — source-estate administration remains available without becoming the ordinary information-navigation model;
7. **deep-link replay** — pinned state reproduces the selected world/facets/epoch/ranking;
8. **cross-domain reuse** — same generic semantic/query contracts with domain-specific presentation, not private domain engines.

## 13. Counterexamples

Reject:

- “Browse starts from a word” as the universal UX definition;
- master/detail as the complete sports-site metaphor;
- separate pages/tools as proof that facets are separate product domains;
- requiring provider/source selection before entity discovery;
- generic JSON/receipt panels as ordinary entity realization;
- one universal ranking/importance score;
- hard-coded chess-private graph/ranking semantics;
- source/provider identity fused from display-name convenience;
- generic back-office UI being called product completeness.

## 14. Research still required

- exact historical UI/product chronology before/through old Laplace;
- additional sports-page evidence for NFL/NBA/MLB, teams/divisions/conferences/players/stats/rankings;
- noun/POS/type-specific exploration evidence;
- relation/source/consensus/ranking entry-point requirements;
- generic component/contract design for facets and domain renderers;
- current installed-browser conformance audit against this model.
