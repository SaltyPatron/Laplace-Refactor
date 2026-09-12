# Pre-Hartonomous design ancestry

Status: partial but source-grounded. This file records **earlier engineering practice** that must be understood before interpreting Hartonomous/Laplace. It deliberately separates recurring practice from explicit causal lineage.

## 1. Census-Data-Parser — 2015/2016

**Classification:** IMPLEMENTATION EVIDENCE. Recurring-practice synthesis is permitted; direct causal lineage to Laplace is not yet established.

### Dated evidence

- Repository `AHartTN/Census-Data-Parser` created `2015-12-26T19:31:54Z`.
- `CensusDataParser/Helpers/SSISHelper.cs` source header: `Authored: 01/31/2016 3:35 PM`.
- `CensusDataParser/Extensions/EntityTypeConfigurationExtensions.cs` source header: `Authored: 01/31/2016 11:18 PM`.

### Generic/schema-driven mechanisms already present

`EntityTypeConfigurationExtensions` defines generic methods over `EntityTypeConfiguration<T> where T : class` to introspect EF configuration and recover:

- column/property name;
- database column type;
- column order;
- maximum length;
- schema name;
- table name;
- fully-qualified table name.

The point is not merely that C# generic syntax appears. The pipeline extracts behavior/configuration from the **type/model metadata** so one mechanism can operate across many Census entity shapes.

`SSISHelper` includes a generic `ProcessFlatFile<T>(..., EntityTypeConfiguration<T> map, ...) where T : class`, connecting typed model configuration to generated/assembled ETL behavior instead of hand-writing a distinct loader for every Census table.

The repository also contains large generated binding estates for Census summary-file segments. That is evidence of a pattern:

```text
external structured estate
    -> inspect/encode metadata
    -> generate typed bindings/configuration
    -> drive reusable execution machinery
```

### Relevance to later reconstruction

Supported recurring-practice synthesis:

- prefer reusable typed machinery over table-by-table implementations;
- let metadata/schema drive behavior;
- generate repetitive structural bindings;
- distinguish reusable execution machinery from data-specific definitions.

Not yet justified as direct causal lineage:

- current generated ISA contracts;
- universal AST;
- Laplace identity law;
- current modality system.

Those require explicit bridging evidence.

## 2. Shape2Sql — February 2016

**Classification:** IMPLEMENTATION EVIDENCE.

Repository created `2016-02-28T11:35:28Z`, before Hartstone and CIEDigital. The repository description states that the console application creates a batch file for processing **multiple shape files into Microsoft SQL Server**.

This matters to chronology because spatial/database processing was not a late GISParser-only interest. A distinct shapefile-to-database automation stage already existed in February 2016.

Supported recurring-practice evidence:

- automate repeated external-file processing rather than handle each source manually;
- bridge formal spatial file structures into relational storage;
- treat source-estate processing as a repeatable pipeline.

Not established from metadata alone:

- generic/reflection details inside Shape2Sql;
- exact geometry semantics;
- any direct causal connection to modern GeometryZM.

Those require source/commit inspection.

## 3. Hartstone — March 2016

**Classification:** IMPLEMENTATION EVIDENCE. Board/game-state ancestry is now code-evidenced rather than an inferred side note.

### Dated evidence

Repository `AHartTN/Hartstone` created `2016-03-02T08:24:22Z`; repository description is `Technical assessment for Blizzard`.

### Explicit game/world state

`HartStone/Classes/Game.cs` carries state such as:

- `IsRunning`;
- `CurrentTurn` / `MaxTurns`;
- the participating `Players`;
- `PlayerOne` / `PlayerTwo`;
- `Winner`;
- per-player hands and decks;
- health, mana and fatigue mutated during play.

The class implements explicit phases/transitions:

```text
Setup
 -> InitialDraws / mulligan state
 -> ProcessTurn
 -> TakeTurn(player, opponent, hand, deck)
 -> legal-action selection
 -> PlayCard
 -> state mutation / damage / draw / heal / death
 -> winner / stop condition
```

`TakeAction` computes legal card choices from current hand and available mana before permitting execution; the game checks terminal conditions such as death/max turns and records a winner.

`IPlayer.cs` separately abstracts the player-facing contract (`CurrentClass`, `CurrentDeck`, `Name`).

### What this establishes

It establishes that **explicit persistent game state, actors, turn progression, legal action filtering, action execution and consequences** were implemented before the 2025 D&D/Hartonomous research and before CIEDigital.

This is useful ancestry for understanding why later game/world systems repeatedly separate state, actor, operation and consequence.

### What it does not establish

Do not overclaim:

- Hartstone is not proof of modern OODA;
- it does not contain the modern Laplace ISA;
- a turn loop is not automatically a cognition loop;
- `IPlayer` is not evidence that all later provider interfaces directly descend from it.

Those are possible recurring-practice/lineage questions, not facts established by this source.

## 4. TMS — May 2016

**Classification:** IMPLEMENTATION EVIDENCE; relevance still under review.

Repository `AHartTN/TMS` was created `2016-05-07T22:22:27Z`, before CIEDigital. The current tree contains a substantial ASP.NET application with administration areas, attributes, configuration and other shared application infrastructure.

No stronger Laplace-lineage claim is made yet. It is recorded because a chronology that jumps from Hartstone directly to CIEDigital without reviewing contemporaneous framework/application work would repeat the same convenient-example failure.

## 5. CIEDigital — June 2016

**Classification:** IMPLEMENTATION EVIDENCE. It is **not** the beginning of the generic/reuse lineage and is **not** UX authority for Laplace.

### Dated evidence

Repository created `2016-06-19T21:03:30Z`. Repository description states it was a technical assessment for CIE Digital written while the Hartstone technical assessment for Blizzard was being written.

That description is itself useful chronology: CIEDigital and Hartstone are explicitly related in work context, but Hartstone's repository predates CIEDigital by more than three months.

### Reusable mechanisms

Examples include:

- `BaseController<T>` implementing common typed browse/search/paging behavior;
- polymorphic/abstract search criteria;
- reusable model metadata and UI helpers;
- generic query filtering and ordering;
- shared route/encrypted-parameter behavior;
- domain controllers such as `TeamsController : BaseController<Team>` inheriting common behavior instead of rebuilding it.

### Sports-domain evidence

The model estate includes teams, players, positions, plays, games/results and related sports state. `Team` connects players, offensive/defensive plays, current/previous names, home/away games and weather.

This matters because later sports/reference-world product metaphors are not appearing in a history devoid of sports-domain modeling. However, the 2016 CRUD/master-detail presentation is **not** evidence that Laplace should use CRUD/master-detail as its final UX.

### Relevance to later reconstruction

Supported recurring-practice synthesis:

```text
central reusable mechanics
+ typed specialization
+ domain model relationships
```

The UX lesson is **not** “copy the old MVC screens.” The engineering lesson is that common mechanics should be implemented once and specialized behavior kept thin.

## 6. Shp2Sql — June 2016

**Classification:** IMPLEMENTATION EVIDENCE.

Repository created `2016-06-19T22:22:17Z`. Its description states that it is a C# interpretation of **manually parsing raw `.shp` files and inserting them into a database**.

Together with Shape2Sql, this shows two distinct 2016 spatial-ingestion approaches before GISParser:

```text
Shape2Sql
  automate multi-file processing into SQL

Shp2Sql
  manually interpret raw shapefile structure in C# and deposit to DB
```

This is relevant to source/format decomposition and spatial-data history. It still does not authorize projecting modern GeometryZM meaning back into these projects.

## 7. GISParser — December 2016

**Classification:** IMPLEMENTATION EVIDENCE.

### Dated evidence

Repository created `2016-12-03T01:27:38Z`.

The README explicitly contrasts the implementation with an earlier OGR workflow that processed one file at a time and committed per record. GISParser instead processes a directory-of-directories, handles large files and uses bulk insertion.

### Generic/reflection-driven mechanisms

`GISParser/Helpers/DataHelper.cs` includes:

```csharp
CreateDataTable<T>(IEnumerable<T> entities)
```

The method:

- reflects the properties of `T`;
- excludes `NotMapped` / inappropriate association members;
- unwraps `Nullable<>`;
- creates a `DataTable` schema dynamically;
- projects arbitrary typed entities into rows.

This is another independent example of **inspect the type/structure and let reusable machinery adapt** rather than hard-code every record shape.

### Bulk/set-oriented evidence

The repository's stated motivation is also relevant to later Laplace execution economics: avoid tedious record-at-a-time processing, handle whole file estates and bulk-deposit data.

This is not proof that modern vector/set execution directly descends from GISParser, but it is strong evidence that bulk/set-oriented thinking predates Hartonomous by many years.

## 8. GISSchemaGenerator — December 2016 repository, later revisions

**Classification:** IMPLEMENTATION EVIDENCE. Date provenance matters because the default-branch source contains later edits.

Repository created `2016-12-04T00:03:24Z`, one day after GISParser. Current code walks directories/zip archives, reads DBF fields, accumulates table/column definitions, resolves data types, detects geometry presence and generates class/schema artifacts.

However, the repository was pushed/updated years later as well. Therefore the current code must not be described as entirely December-2016 behavior without commit/file-specific dates.

This combines spatial source handling with schema discovery/generation and remains important ancestry evidence after date separation.

## 9. SQL_Scripts — repository created 2017, contents described as older accumulated work

**Classification:** IMPLEMENTATION EVIDENCE with ambiguous artifact-age semantics.

Repository created `2017-10-08T00:03:02Z`. Its description says it contains “various SQL scripts I have written over the years.” Therefore repository creation is **not** a trustworthy first-authorship date for every script inside.

This repo must be dated at file/commit level before using it to establish pre-2017 chronology.

## 10. D&D / TTRPG autonomous-system research — July/August 2025

**Classification:** HISTORICAL INVENTOR EVIDENCE. Some implementation choices are historical/superseded; mechanism ancestry is still being mapped.

### Dated evidence

Current Drive metadata establishes at least:

- `D&D AI Ecosystem Research` — created `2025-07-26T22:37:17Z`;
- self-building/autonomous D&D ecosystem compendium copies — from `2025-07-27T00:17:31Z` onward;
- `DNDAI Research Plan Development` — `2025-08-02T07:35:07Z`;
- `D&D: Dungeon Master Research` — `2025-08-02T09:25:53Z`;
- `Project Chimera` R&D plan — `2025-08-02T10:47:53Z`;
- `AI Dungeon Master Development Plan` — `2025-08-02T12:02:30Z`;
- `Neo4j, SQL Server TTRPG Ecosystem` — `2025-08-06T05:24:37Z`.

These precede the currently located Aug 9 Hartonomous master-blueprint objects.

### Primitive operation vs larger control

The D&D research explicitly defines atomic game operations with formal contracts, including examples such as:

- `RollDice(notation, modifiers, target) -> result, success/fail`;
- `CheckSave(entityID, DC, saveType) -> pass/fail`;
- `UpdateEntityStat(entityID, stat, delta, reason)`;
- `MoveCharacter(characterID, coordinates, speed)`;
- `ApplyDamage(entityID, amount, type, source)`.

The same research distinguishes primitive tasks from composite/macro tasks and separately discusses OODA, BIPA and HTN-style decision/control loops.

This matters because modern Laplace must not collapse:

```text
operation
program/composite task
orchestration
control/decision loop
feedback
meta-learning
```

into one generic “agent loop.” There is clear pre-Hartonomous evidence that these concerns were already being decomposed.

### Persistent/living world evidence

The TTRPG corpus repeatedly treats the game as persistent state that changes because actions have consequences. It models/considers:

- actors/players/party/NPCs/factions;
- goals and motivations;
- rules and adjudication;
- encounters as state machines;
- time/turn progression;
- reputation and cascading consequences;
- persistent world state;
- content/tool generation;
- self-monitoring/reflection and adaptive control.

Modern Laplace's world/effect/occurrence/receipt architecture may have conceptual continuity with these concerns, but the exact causal mapping is currently **SYNTHESIS**, not direct law.

### Relation to the much older Hartstone evidence

The 2025 D&D research is not the first game-state work in the located estate. Hartstone in 2016 already implements explicit turn/player/action/consequence state. The D&D corpus is materially more ambitious: persistent social/world state, formal primitive operation contracts, composite tasks, autonomous control strategies and self-improvement.

This gives the chronology a more defensible progression without claiming a single straight causal chain:

```text
2016 Hartstone
  explicit board/card-game state + legal actions + consequences

2025 D&D/TTRPG research
  persistent living worlds + formal primitive/composite operations
  + adjudication + multi-scale decision/control + self-building

2025+ Hartonomous
  general autonomous software/knowledge architecture
```

### Superseded implementation ideas

The historical D&D material often uses conventional LLM agents, Neo4j/vector stores or other then-current architecture. Those choices are historical evidence, not modern Laplace authority.

## 11. Early Hartonomous transition — August 2025 onward

**Classification:** HISTORICAL INVENTOR EVIDENCE; many concrete stack choices later superseded.

Drive metadata places several Hartonomous blueprint objects on `2025-08-09`, after the July/August D&D research stream.

The early Master Blueprint includes:

- natural-language project intake;
- specialized agent swarm;
- task DAG orchestration;
- SQL Server as transactional system of record;
- Neo4j/Milvus read-specialization;
- Reflexion/error recovery;
- inter-run strategy learning;
- reusable architectural-template extraction.

The importance for reconstruction is not to preserve that stack. It is to trace which problems survive while implementations change:

- persistent state;
- explicit task/program structure;
- operation/tool contracts;
- feedback/error correction;
- self-improvement/reusable procedure discovery;
- schema/data ownership;
- source/world integration.

## 12. Current synthesis — recurring practice before Laplace

The evidence already supports one bounded synthesis:

> Long before Laplace, the engineering record repeatedly favors making shared mechanics generic, typed, metadata/schema-driven and reusable; automating repeated source-estate work; preserving explicit domain/world state; and using bulk/set processing rather than tedious record-at-a-time execution where possible.

The chronology now supports this with multiple distinct examples rather than one convenient repository:

```text
2015/Jan-2016 Census
  metadata/schema-driven generic ETL + generated bindings

Feb-Jun 2016 Shape2Sql/Shp2Sql
  automated/manual spatial format -> database processing

Mar 2016 Hartstone
  explicit game state, players, turns, legal actions, consequences

Jun 2016 CIEDigital
  reusable generic application/query machinery + sports domain model

Dec 2016 GISParser/GISSchemaGenerator
  bulk spatial estate processing + reflection/schema generation

Jul-Aug 2025 D&D
  persistent living worlds + primitive/composite operations + control loops
```

This is **not yet** equivalent to:

> “Laplace directly derives from Census/GIS/Hartstone/D&D code.”

That stronger statement requires explicit bridging evidence and will not be asserted merely because the design tendencies rhyme.

## 13. Required next ancestry research

P0 backlog:

1. enumerate older AHartTN repositories preceding/overlapping Dec 2015 and inspect source/commit dates rather than assuming Census-Data-Parser is the beginning;
2. inspect Hartstone commit history/technical-assessment material and separate implementation convenience from reusable game-state principles;
3. inspect Shape2Sql/Shp2Sql/GISParser/GISSchemaGenerator commit history and source evolution rather than relying on repository dates;
4. review TMS and other 2016 generic/application-framework repositories for relevant reusable mechanisms without promoting unrelated work;
5. date file-level SQL_Scripts content because the repository explicitly aggregates scripts written “over the years”;
6. reconstruct D&D document/revision ordering, not just file creation dates;
7. map D&D operation/composite/orchestration/control/self-improvement vocabulary into later Hartonomous documents with explicit first-appearance dates;
8. trace Sep–Dec 2025 Hartonomous transition from agent-factory architecture toward universal knowledge/substrate architecture;
9. only then map 2026 Hartonomous repository mechanisms to original Laplace and current Refactor.
