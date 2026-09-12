# Laplace invention reconstruction ledger

Status: active research dossier. This directory is not product law. It records the source-grounded reconstruction of the invention, its chronology, corrections, unresolved conflicts, and current implementation counterexamples without allowing current implementation state to redefine the invention.

## Purpose

The previous reconstruction work repeatedly failed by stopping once a convenient example had been found, treating current code as intent, omitting dates, flattening historical evolution, and turning direct corrections into prose instead of durable constraints. This directory exists to make those failure modes mechanically visible.

Every material claim in this reconstruction must be classified as one of:

1. **DIRECT CURRENT LAW** — inventor-direct correction/requirement that governs derived material.
2. **CURRENT PRODUCT LAW** — current stable architecture/product contract consistent with direct law.
3. **HISTORICAL INVENTOR EVIDENCE — UNSUPERSEDED** — older inventor material that remains authoritative because no later direct/current law supersedes it.
4. **HISTORICAL / SUPERSEDED** — genuine historical design evidence that explains evolution but is no longer current law.
5. **IMPLEMENTATION EVIDENCE** — code, schema, runtime, UI or test behavior proving what an implementation does or did.
6. **COUNTEREXAMPLE** — implementation/history demonstrating a behavior current law rejects.
7. **SYNTHESIS** — a supported reconstruction/inference connecting primary evidence; never silently promoted to inventor intent.
8. **UNRESOLVED** — evidence is incomplete, conflicting, undated, or insufficient to choose between interpretations.

## Date discipline

Every chronology entry must identify the date basis. These are not interchangeable:

- repository `created_at`;
- commit author/committer date;
- authored timestamp inside a source file;
- Google Drive file creation/modification time;
- document text claiming a historical date;
- issue/PR/comment creation time;
- observed deployment/runtime date.

A repository creation date does **not** prove every file was authored then. A later push date does **not** make the design newer. A document discussing older work does not prove the discussed mechanism existed on the document's creation date unless additional evidence supports it.

## Priority order

Research and documentation proceed in this order so the reconstruction cannot be dominated by the current Refactor repository:

| Priority | Workstream | Why first |
|---|---|---|
| P0 | Authority + direct corrections | Prevents lower-authority code/docs from rewriting intent. |
| P0 | Chronology with date provenance | Prevents 2015–2016 work, 2025 D&D/Hartonomous work, original Laplace and current Refactor from being flattened into one timeless design. |
| P0 | Pre-Hartonomous design ancestry | Establishes recurring engineering principles from Census-Data-Parser, CIEDigital, GISParser/GISSchemaGenerator, games/board work and other older repositories before drawing lineage conclusions. |
| P0 | Hartonomous evolution | Separates temporary architectures (agents/vector/Neo4j/SQL Server/etc.) from mechanisms that survive into Laplace. |
| P0 | Original Laplace invention lineage | Recovers invention behavior before Refactor projections and implementation damage. |
| P0 | Current direct/current law reconciliation | Establishes the present machine after historical evolution is understood. |
| P1 | Product/UX information-world reconstruction | Reconstructs domain-native exploration, sports/reference-world behavior, entity-first acquisition and reusable UX without mistaking admin shells for product law. |
| P1 | O(tier), Merkle bidirectionality and perfcache | Restores the execution-economics/memory architecture that previous summaries buried. |
| P1 | Operation/program/orchestration/OODA/feedback/Gödel | Keeps execution, control, learning and meta-discovery mutation rates distinct. |
| P1 | Modality/physicality/GeometryZM | Preserves inventor-direct modality-derived physicality and the full GeometryZM carrier family. |
| P2 | Models/AImaps/target compilation | Reconstructs model-as-witness and model-as-target symmetry after substrate/cognition laws are fixed. |
| P2 | Current implementation audit | Used as conformance/counterexample evidence only, never as invention authority. |

## Files

### Governance / evidence

- `00_AUTHORITY_AND_METHOD.md` — authority stack, evidence classes, research rules, anti-hallucination rules.
- `01_CHRONOLOGY.md` — dated evidence ledger; no undated mechanism is allowed to silently establish sequence.
- `02_DESIGN_ANCESTRY.md` — pre-Hartonomous repositories and design principles, with inference separated from explicit evidence.
- `03_CORRECTIONS_LEDGER.md` — direct corrections to the reconstruction itself; superseded descriptions remain visible.
- `04_OPEN_QUESTIONS_AND_CONFLICTS.md` — unresolved research rather than invented answers.
- `05_CURRENT_IMPLEMENTATION_COUNTEREXAMPLES.md` — damaged/current behavior that must not be treated as product authority.

### Priority mechanism / evolution dossiers

- `06_O_TIER_AND_PERFCACHE.md` — leaf→trunk/trunk→leaf addressability, Merkle reuse, O(tier) execution economics and modular typed perfcache/ROM planes.
- `07_EXECUTION_CONTROL_GODEL_OODA.md` — operation vs program vs orchestration vs OODA vs feedback vs evidence learning vs Gödel, including historical terminology evolution.
- `08_PRODUCT_INFORMATION_WORLDS.md` — faceted sports/reference-style information worlds, domain-native realization, entity-first acquisition and operator/admin separation.
- `09_PHYSICALITY_MODALITY_GEOMETRYZM.md` — modality-derived physicality, full GeometryZM carrier family, live `coord` versus packed trajectory/address payload and typed structural metrics.
- `10_HARTONOMOUS_EVOLUTION_2025.md` — dated transition from D&D/agent factory through deterministic substrate, database-centric unified knowledge fabric, spatial/geometric turn and Dec-13 PostgreSQL/PostGIS convergence; superseded historical claims remain explicitly marked.

Next dossiers are blocked on explicit primary-source passes rather than being filled from inference: original-Laplace invention chronology; machine ontology/state classes; evidence/standing; whole-observation cognition; realization/conversation; model/AImap/target compilation; full product acceptance; exact current implementation conformance.

## Non-negotiable reconstruction rules

- Current implementation never creates product law.
- A convenient example never closes a lineage search.
- One repository is never treated as the invention boundary merely because it is easiest to search.
- Historical code may prove a recurring engineering practice; it does not automatically prove direct causal ancestry to Laplace.
- Direct corrections supersede derived prose where they conflict.
- `GeometryZM` must not be reduced to `LINESTRINGZM`; modality/form/tier/recipe determine the physical structure and the carrier family remains polymorphic.
- `physicality.coord` and packed trajectory/address payload are different state.
- Sports/reference-site UX must not be reduced to CRUD/master-detail hierarchy.
- Entity/task-first acquisition and source-estate administration are different workflows.
- `O(tier)`, leaf→trunk composition, trunk→leaf reconstruction/addressability and perfcache are architectural mechanisms, not implementation footnotes.
- Operation, program, orchestration, OODA, feedback, evidence learning and Gödel discovery are separate mechanisms/mutation authorities.
- Unknowns remain `UNRESOLVED`; they are not completed from generic AI priors.
