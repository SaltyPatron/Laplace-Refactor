# Direct requirement evidence

Date: 2026-08-24

## Purpose

This audit binds the clean-room requirement set to exact human-message events. It
prevents repository code, agent prose, a commit title, or a later summary from being
mistaken for an invention decision.

The raw records remain in their preserved session files. This repository stores only
message locators, byte counts, and SHA-256 digests. Technical summaries below are
index entries, not replacements for the source messages.

## Indexed records

| Source | Human text records | Manifest | Manifest SHA-256 |
| --- | ---: | --- | --- |
| Current Codex session through 23:02:54 UTC | 94 | `evidence/current-session-human-messages.jsonl` | `be3b3b9dee2ed236e6fb801ab8a23922e62890ab3a796ca8bc6a95bb6ee3bdc2` |
| Preserved top-level Claude project sessions | 596 | `evidence/claude-parent-human-messages.jsonl` | `6b084bdc2e6e38b1821fc2879e7add28c52b8e649de4e6b3f77e0da27131d02b` |

The indexer excludes tool-result blocks, worker notifications, local-command notices,
and interruption markers. It does not treat assistant output as human-authored text.
The extraction is reproducible with `tools/audit/index-human-messages.sh`.

## Evidence precedence

1. Exact direct requirements and corrections establish product intent.
2. Inventor-authored artifacts, measurements, and counterexamples clarify those
   requirements when they do not conflict with a direct correction.
3. Read-only observations establish what the current repository, database, installed
   product, and preserved artifacts actually do.
4. Agent prose and commit claims establish only what an agent claimed or attempted.
5. Current implementation behavior never creates a product requirement by itself.

Conflicting implementation evidence is recorded as damage or divergence. It is not
converted into an architectural choice for the new product.

## Current-session requirement map

Each locator is a `response_item` message ID in the current Codex session manifest.

| Time UTC | Message locator | Technical requirement established |
| --- | --- | --- |
| 17:24:16 | `msg_01a034cd-2bb9-7ab0-83fa-3ad40bb16d08` | Determine the actual repository state and quantify missing work. |
| 17:24:57 | `msg_01a034cd-cb3f-7ae2-8258-463bef9fa997` | Use system Claude logs and JSONL records to reconstruct deletions and damaged work. |
| 17:25:33 | `msg_01a034ce-5727-79d3-95e9-044adccfccca` | Recover the full work history or prove why claimed work has no surviving artifact. |
| 17:33:43 | `msg_01a034d5-d291-7d83-bfb5-b5bebf08b3e6` | Investigate the missing SQL restructuring specifically. |
| 17:34:15 | `msg_01a034d6-4f9c-76a1-aee2-226c78397756` | Remove hidden session settings, make SQL indexable and inlineable, and establish correct namespaces. |
| 17:39:18 | `msg_01a034da-eeb7-7163-8c30-5216341981d8` | Determine exact durable progress after cancellation of a seven-hour chess ingest; do not alter the database. |
| 17:40:28 | `msg_01a034dc-00c3-7781-b6a9-22e599baf0d5` | Treat timeouts as evidence of SQL and execution defects; use parsed atomic bodies and reusable exposed surfaces. |
| 17:46:07 | `msg_01a034e1-2b37-7903-a871-9a687e6972c0` | Tests must execute the implementation rather than restate the concept. |
| 17:46:07 | `msg_01a034e1-2b3a-76c1-bf80-c548a6e85e6b` | Acceptance asserts the actual returned value, not that an abstract equation is true. |
| 17:48:39 | `msg_01a034e3-7d81-73c1-9621-29aa2be8bfef` | Database-tier tests are measured performance contracts, including one GB in at most 30 seconds on the i7-6850K. |
| 17:49:43 | `msg_01a034e4-792a-7a71-8293-fec1a76be66b` | Complete ingest must sustain at least 500,000 records per second; primary paths are genuinely batched. |
| 17:50:34 | `msg_01a034e5-3e5b-76b1-a335-0a9760a59ae5` | Every discussed part remains required and must form one cohesive product. |
| 17:59:35 | `msg_01a034ed-818e-74b0-ac2f-1f39ad1b1699` | Build in a new repository, preserve the current tree as evidence, and design typed generic interfaces from the beginning. |
| 17:59:56 | `msg_01a034ed-d329-7410-b760-eebf4c5b078d` | Make SQL reusable and cohesive while moving engine computation to C/C++ and PostgreSQL server integration. |
| 18:01:03 | `msg_01a034ee-d74e-70b1-8ce5-9406a86c8565` | SQL and C# are both orchestrators. |
| 18:02:12 | `msg_01a034ef-e5b1-7702-8c44-a16f2743f7ae` | C/C++ and PostgreSQL server integration are the engine; orchestrators cannot own private semantics. |
| 18:03:41 | `msg_01a034f1-41fe-7c10-ae43-946aac0c93e7` | When set iteration is required, bounded affected-row loops are preferred to cursors; native bulk kernels remain the engine. |
| 18:08:57 | `msg_01a034f6-1425-7f90-8eec-8fb4889ea9e6` | Establish industrial build, install, script, packaging, and convergent lifecycle infrastructure at the start. |
| 18:09:43 | `msg_01a034f6-c7fe-7b52-b5a1-ae6e81c6c7e8` | PostgreSQL, PostGIS, GDAL, GEOS, PROJ, and related dependencies are custom product builds. |
| 18:09:58 | `msg_01a034f7-02a1-7453-aca0-9d74d32362da` | Keep the product name Laplace and do not copy or migrate the current database. |
| 18:12:14 | `msg_01a034f9-166f-7970-83bf-c72eb1a521b8` | Do not treat the current repository as a complete product to be ported. |
| 18:14:41 | `msg_01a034fb-52f3-77d0-8623-26b8d231d3a8` | Laplace is a SQL-executed transformer reinvention and universal persistent substrate across modalities, languages, and models, with testimony, trust, model ingest, model export, and symmetric materialization. |
| 18:17:40 | `msg_01a034fe-0fae-7d91-af8d-db2373a30291` | The substrate ISA and complete conversational execution are product core, not deferred interfaces. |
| 18:18:43 | `msg_01a034ff-0582-72c0-a591-44fc9e6ac8f3` | Accurate batch and bulk execution is mandatory wherever scale requires it. |
| 18:19:33 | `msg_01a034ff-c938-7100-a873-133b6daed3d4` | Copy no current Laplace implementation into the new repository. |
| 18:19:59 | `msg_01a03500-2cbc-7b72-93c1-702c57a72009` | Carry forward only independently established invariants and facts that cannot vary. |
| 18:20:53 | `msg_01a03501-0243-7671-b0ac-b6c998e9b49e` | Derive the correct product directly; do not route existing implementation through an adoption process. |
| 18:34:33 | `msg_01a0350d-8378-7052-979e-ea5a49c4cf97` | Establish the complete cognition model: persistent relational world state, goal-conditioned trajectories, relation algebra, hypotheses, counterfactuals, time and context, firmware, pattern induction, conversation, model work, and machine cognition experiments. |
| 18:36:34 | `msg_01a0350f-5bd8-78c0-a0e3-f173bf231f29` | Audit `/opt/laplace` and `external/` before designing the dependency supply chain. |
| 18:36:34 | `msg_01a0350f-5bdd-7143-bb77-720dea5677aa` | Audit the whole session, retain the complete scope, and deliver production implementation backed by evidence. |
| 18:37:08 | `msg_01a0350f-e06f-7182-8269-3f2f44166bc9` | Product completion is a talking SQL transformer whose exported artifacts also converse correctly. |
| 18:46:53 | `msg_01a03518-cebb-7a71-a6fa-bd454fddf899` | Infer from Laplace-specific evidence rather than forcing the invention into conventional AI architecture. |
| 18:48:23 | `msg_01a0351a-2f8a-7e82-a859-873775295dba` | Fully understand same-content identity, Unicode/DUCET/S3/Hilbert, modular perfcaches, and current defects before implementation. |
| 19:07:05 | `msg_01a0352b-4ee4-7ad0-a610-16b7d0092733` | Every modality reaches the Unicode codepoint floor; role is outside identity; BLAKE3 SIMD 128-bit identity is fixed; composites use live four-dimensional arithmetic centroids; DUCET covers all 1,114,112 positions with complete semantics; repeated content uses runs, sparse recording, testimony, and Merkle DAG reuse. |
| 19:09:16 | `msg_01a0352d-4c83-7dd1-9882-9e11dfc28a20` | Treat damaged implementation as defect evidence, never design authority. |
| 19:09:48 | `msg_01a0352d-c8bd-7a13-a872-dae6d4f72114` | Infer the complete damage and repair the product state rather than asking the inventor to re-specify established concepts. |
| 19:10:46 | `msg_01a0352e-aca0-7921-b391-27f84e59c16c` | Continue autonomously toward a fully fleshed and stable state while the inventor is away. |
| 20:57:31 | `msg_01a03590-687f-7c21-b1c7-b3d30ce907c9` | Reconstruct complete working logic, feature intricacies, prior failures, and retained methods deeply enough to produce exact structural, interaction, and behavioral specifications. |
| 20:57:31 | `msg_01a03590-6886-76c2-a1c4-4e0107c5cb58` | Classify bypass mechanisms as defect evidence when they mask an architectural failure; do not preserve them as design methodology without implementation proof. |
| 21:21:01 | `msg_01a035a5-ea83-72d3-9bab-da75699a50de` | Reconstruct repeated PostgreSQL client activity and replace repeated inspection or transformation shapes with cohesive server-side views, functions, procedures, types, operators, indexes, and native PostgreSQL integration selected by their actual semantics. |
| 21:26:27 | `msg_01a035aa-e507-7d22-a209-e029a90b6d99` | Include `/vault/AI_Sabotage` and `/vault/.claude` in the evidence audit despite duplicated or disturbing log content. |
| 21:26:27 | `msg_01a035aa-e50b-70e2-92a6-7ddac4f503b3` | Preserve and investigate the current `/tmp/claude-1000` state. |
| 21:26:27 | `msg_01a035aa-e50f-78a1-8a87-d62125ed2255` | Use the existing decomposer, agent-log, repository, and Git-history infrastructure as non-authoritative investigative instruments. |
| 21:26:27 | `msg_01a035aa-e512-7cc1-9ea3-9a031cd59f62` | Query the current Laplace substrate to troubleshoot its functionality, while corroborating its results against independent evidence. |
| 22:00:14 | `msg_01a035c9-d566-7180-9515-23108c2df933` | Calculate relation-derived target operators and complete target tensors from substrate testimony; treat conventional models as witnesses and neural formats as compilation targets rather than native authority. |
| 22:02:49 | `msg_01a035cc-2f36-7963-917a-6ac819c59864` | Preserve the demonstrated June substrate-to-GGUF execution and exact Japanese composition as baseline behavior while distinguishing direct output-matrix testimony lookup from unproven Q/K/V/O semantics. |
| 22:11:32 | `msg_01a035d4-2b95-7450-b7ac-687c577c2d0e` | Harden identity-versus-interpretation, centroid nonidentity, evidence lineage/dependence, model gauge equivalence, derived-state publication, index admission, normalized performance units, temporal epochs, and materialization growth with implementation tests. |
| 22:13:55 | `msg_01a035d6-58e3-7812-a7c9-fa312515850a` | Keep entity as exact content identity, physicality as calculated realization, occurrence as observed use, attestation as attributable claim, consensus as epoch-specific standing, and inference as lineage-bearing derived claim. |
| 22:15:36 | `msg_01a035d7-e38b-7973-b5c0-f2544ac58f8d` | A declared MIME or type is testimony about an entity or physicality; usage belongs to occurrence/context; neither can instantiate or mutate physicality. |
| 22:25:37 | `msg_01a035e1-107a-7bb1-a33d-4ef9fd2bfab3` | Investigate a native evidence-induced Laplace cognition operator, spectral AImap coordinates over the canonical S3 domain, sparse eigensolvers and alignment, relation-operator factorization, logical cognition planning, and modality realization as one generated architecture. |
| 22:28:50 | `msg_01a035e4-02df-76d2-a3ba-21341660cc00` | Separate solver residual from existing-relation compatibility, missing expected structure, boundary defect, innovation, contradiction, and counterfactual defect; use the operator, typed defect calculus, action selection, and realization as distinct stages. |
| 22:28:50 | `msg_01a035e4-02e4-7a42-85d2-f073319682f3` | Laplace calculates and generates from exact typed substrate state; it does not flatten relations into a permanent graph or representation. |
| 22:29:22 | `msg_01a035e4-7f3d-75c1-918e-66bf69d8970f` | Formalize evidence-metrized typed incidence as a generated calculus, retain relation-specific transport and n-ary composition, and treat AImaps, embeddings, heads, rerankers, and target tensors as calculated artifacts rather than the substrate itself. |
| 22:49:53 | `msg_01a035f7-4685-7cc2-9752-72eb4641c679` | Physicality trajectories are bit-perfect trunk-to-leaf Merkle-DAG realizations that directly calculate containment, precedence, occurrence position, tier, ancestry, and exact structure for text, models, and every modality before semantic testimony. |
| 22:52:15 | `msg_01a035f9-741e-7441-bf34-a3ae1ea0f543` | Neighbor is a program-scoped calculation across distinct structural, angular, Fréchet, Hausdorff, Karcher-derived, Glicko-2, trust, tier, containment, relation-type, and contextual-importance channels rather than one universal metric. |
| 22:56:11 | `msg_01a035fd-0d6c-74e1-ad5d-40a708d3931b` | Retain exact structural constraints separately from epistemic constraints; use recovered spectral type erasure, direction loss, invalid post-processing, and target-rank imitation as deliberate-defect tests. |
| 22:58:29 | `msg_01a035ff-2910-7ee2-83b2-e08714e9b7b3` | Replace universal KNN relevance with query-compiled A-star and indexed bulk expansion over typed state; metric-nearest operations can generate candidates but cannot substitute for cognition, goal, or completion. |
| 23:02:25 | `msg_01a03602-c08a-7652-91da-2104eeab4f87` | Separate ordinary cognition, evidence learning, and Gödel discovery: use persistent explanatory failure to generate and independently test relation, rule, operator, firmware, and cognition-program extensions through an OODA feedback cycle. |
| 23:02:38 | `msg_01a03602-f5b7-7633-9781-928701be10d1` | Treat incompleteness as a typed discovery signal; ask what explanatory structure is missing rather than reducing observed error to an anonymous parameter update. |
| 23:02:54 | `msg_01a03603-33ad-75c3-9fd7-2b4663d9286b` | A frayed edge is a constrained vacancy whose surrounding topology predicts an occupant signature; the predicted occupant remains a derived hypothesis and the slot stays unwitnessed until reality supplies evidence. |
| 23:02:54 | `msg_01a03603-33b1-75a1-9b5f-a903fa0e7e9f` | Unify existing mechanisms as consequences of one substrate, organize structure so absences become predictive, and generate problem-relative measurements and target artifacts without treating a coordinate system as intrinsic reality. |

## Earlier direct technical evidence

These locators are UUIDs in the preserved Claude parent-session manifest.

| Time UTC | Message locator | Technical requirement established |
| --- | --- | --- |
| 2026-08-22 21:03:20 | `bdabe62c-c200-405a-90d2-a7fa3451d52a` | One generic centralized AI-agent session decomposer must cover every discovered provider, preserve data, and execute in parallel batches. |
| 2026-08-23 17:03:10 | `2a8ab0c1-1fcc-4b83-af58-64268e4c1097` | The system speaks Unicode. |
| 2026-08-23 17:03:10 | `782ddb4e-fe18-40ef-9ab1-042da3304700` | Valid inference traverses the substrate graph and topology. |
| 2026-08-23 17:06:19 | `aac5ebba-fbcb-41cd-a796-7da93c85d826` | Hilbert values support indexing, lookup, and gap detection. |
| 2026-08-23 18:07:21 | `5db65a1c-aa69-4596-97e1-93da928268e5` | Distinguish entities, geometry, trajectories, paths, packed metadata, coordinates, centroids, Fréchet operations, relations, testimony, witnessing, and consensus. |
| 2026-08-23 18:08:42 | `736d28a8-0e0a-4836-9b52-1a80ab3ed3d9` | Replace conventional QK, KV, VO, and related transformer operations through SQL-executed substrate mechanics. |
| 2026-08-23 18:12:54 | `888a7cf8-b803-49a3-a2c1-c133ada23c31` | Use C/C++ and PostgreSQL server integration for recursion and scalable engine work rather than forcing computation into SQL text. |
| 2026-08-23 18:24:13 | `07d94750-8c81-4754-b269-0e33f0ccba4b` | UAX29 is text-edge behavior; the universal engine cannot encode Latin punctuation or language-specific assumptions. |
| 2026-08-23 18:26:47 | `99076602-1729-4e58-90eb-e32a7e10df4c` | Tier is a structural floor; a lower-tier entity can participate above its floor. |
| 2026-08-23 18:26:47 | `5d6301a9-cf52-44f8-93f2-c57df402095f` | A higher-tier structure cannot be coerced below its proven floor. |
| 2026-08-23 18:38:48 | `b4a546e6-50a3-4033-914e-f775721a1305` | Do not persist derivable sequence relations when native trajectory operations already recover them at required scale. |
| 2026-08-23 18:47:43 | `a75a7a09-ccd2-4a96-9689-151e5817cfda` | `physicality.trajectory` is the sequence and each packed point retains its ordinal metadata. |
| 2026-08-23 20:40:40 | `f185d5a4-c6b8-4a58-9017-de700426bd20` | Testimony is witnessed assertion; consensus is aggregate standing; deduplication, scoring, matchups, and geometry coverage must reconcile. |
| 2026-08-23 23:56:46 | `c4ce9685-b914-4978-a284-cd1540f478a0` | The canonical UCD source is under `/vault/Data/UCD/Public/UCD/latest`. |
| 2026-08-24 04:05:07 | `dfefff97-8ec1-430f-a463-aaa74f13d302` | Tier must never re-enter content identity. |
| 2026-08-24 05:04:28 | `e151ed36-9f4d-4526-9b13-1162b312e5ee` | Product activation must use the complete custom PostgreSQL/PostGIS and geospatial dependency stack. |
| 2026-08-24 08:26:00 | `d2ac6cd4-f2ea-4aa5-ab57-b783a3937967` | Conversation acceptance requires a full connected discussion of Earth's water cycle. |
| 2026-08-24 08:27:53 | `f6424d6b-9e8e-47ed-ba50-461ffbaa6cb1` | Isolated lookups are not conversation, and language-specific pattern handling violates the universal substrate. |

## Result

The direct record supports the complete scope already represented in
`requirements/alignment.yaml`. The repository and installed-product audits identify
implementation contradictions, missing integration, false completion signals, and
artifact identity failures. None of those observations reduce the product contract.
