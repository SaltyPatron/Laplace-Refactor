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

## 2. CIEDigital — June 2016

**Classification:** IMPLEMENTATION EVIDENCE. It is **not** the beginning of the generic/reuse lineage and is **not** UX authority for Laplace.

### Dated evidence

Repository created `2016-06-19T21:03:30Z`. Repository description states it was a technical assessment for CIE Digital written while the Hartstone technical assessment for Blizzard was being written.

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

## 3. GISParser — December 2016

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

This is a second, independent 2016 example of **inspect the type/structure and let reusable machinery adapt** rather than hard-code every record shape.

### Bulk/set-oriented evidence

The repository's stated motivation is also relevant to later Laplace execution economics: avoid tedious record-at-a-time processing, handle whole file estates and bulk-deposit data.

This is not proof that modern vector/set execution directly descends from GISParser, but it is strong evidence that bulk/set-oriented thinking predates Hartonomous by many years.

## 4. GISSchemaGenerator and adjacent spatial/schema tooling

**Classification:** IMPLEMENTATION EVIDENCE. Deep chronology still pending.

Located `GISSchemaGenerator` code shows a pipeline that:

- walks directories and zip archives;
- reads DBF fields;
- accumulates table/column definitions;
- resolves data types;
- detects geometry presence;
- generates class/schema artifacts from discovered source structure.

This combines spatial source handling with schema discovery and generation. It must be researched alongside GISParser, Census-Data-Parser, Shape2Sql/Shp2Sql and other AHartTN spatial/database repositories before any claim is made about the origin of later geometry/schema ideas.

## 5. D&D / TTRPG autonomous-system research — July/August 2025

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

### Superseded implementation ideas

The historical D&D material often uses conventional LLM agents, Neo4j/vector stores or other then-current architecture. Those choices are historical evidence, not modern Laplace authority.

## 6. Early Hartonomous transition — August 2025 onward

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

## 7. Current synthesis — recurring practice before Laplace

The evidence already supports one bounded synthesis:

> Long before Laplace, the engineering record repeatedly favors making shared mechanics generic, typed, metadata/schema-driven and reusable, while retaining domain-specific structure outside those mechanics. It also repeatedly favors bulk/set processing over tedious record-at-a-time execution and explicit state/operations/control in game/autonomous-system work.

This is **not yet** equivalent to:

> “Laplace directly derives from Census/GIS/D&D code.”

That stronger statement requires explicit bridging evidence and will not be asserted merely because the design tendencies rhyme.

## 8. Required next ancestry research

P0 backlog:

1. enumerate older AHartTN repositories preceding Dec 2015 and inspect relevant code rather than assuming Census-Data-Parser is the beginning;
2. date and inspect Hartstone and other board/game repositories;
3. date GISSchemaGenerator, Shape2Sql/Shp2Sql, SQL_Scripts and spatial tooling;
4. reconstruct D&D document/revision ordering, not just file creation dates;
5. map D&D operation/composite/orchestration/control/self-improvement vocabulary into later Hartonomous documents with explicit first-appearance dates;
6. trace Sep–Dec 2025 Hartonomous transition from agent-factory architecture toward universal knowledge/substrate architecture;
7. only then map 2026 Hartonomous repository mechanisms to original Laplace and current Refactor.
