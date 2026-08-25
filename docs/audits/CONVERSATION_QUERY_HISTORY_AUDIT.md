# Historical conversation and query-execution audit

## Scope and evidence status

This is a read-only clean-room audit of the current historical repository and its Git
history. It determines which conversation, prompt-compilation, path-search, evidence,
and realization mechanisms existed; what their tests actually prove; and which product
claims remain open. It does not accept historical source, names, tests, constants,
layouts, or control flow as clean implementation input.

The inspected tree was historical commit `1578a113b2e4a9413e5ffcf5b407b33fc87fe16a`.
The principal current files are:

| Historical file | Lines | Blob |
|---|---:|---|
| `extension/laplace_substrate/sql/functions/converse/chat.sql.in` | 469 | `a885058aec0367ec97ba46b70204a41417fac5d2` |
| `extension/laplace_substrate/sql/functions/converse/compile_prompt.sql.in` | 118 | `7bf625c302efdf6205ed1e59d0777356afcf762b` |
| `extension/laplace_substrate/sql/functions/converse/execute_prompt.sql.in` | 98 | `e5af0ea583ff3f50650bbe59e76308256e236a61` |
| `extension/laplace_substrate/sql/functions/converse/infer.sql.in` | 152 | `9da634c92d068c48c662ae55415c575d843db169` |
| `extension/laplace_substrate/src/recall.c` | 1,411 | `8c1f10a1c21313d6844627587e6794a390e74257` |
| `extension/laplace_substrate/src/recall_route.c` | 90 | `4f53d56d1230bb6b60ac767fa57c0bf8d3405c88` |
| `engine/core/src/astar.cpp` | 144 | `c90659823dc2d0ede6cbf2fa9ba4b44111f82aed` |
| `engine/core/include/laplace/core/astar.h` | 50 | `cd4f6925c189fbf2753b5a50bb42d38d89f05e5b` |
| `extension/laplace_substrate/src/astar_path.c` | 313 | `b4ee66eb21f00f9aa3b2f96f69e18ff27966f8ae` |
| `extension/laplace_substrate/tests/sql/converse.sql` | 383 | `cb0a3b2ed8101ba18b58238ed9ffcc2a7c169a29` |
| `extension/laplace_substrate/tests/sql/chat_loop.sql` | 156 | `d3cb699ef9adb27609acf09112121e637ba7215b` |
| `extension/laplace_substrate/tests/sql/prompt_election.sql` | 66 | `d50473e2d4ae0148dae33248e8fc51a3104226eb` |
| `extension/laplace_substrate/tests/sql/walk_richer_forward_pass.sql` | 150 | `0e45ae9b3e539215a10e9585c29fb4a71b27963c` |

## Audit conclusion

The historical product contains several useful partial mechanisms:

- exact trajectory-backed container filtering and ordered sequence verification;
- witnessed frame cues for one natural-language operation;
- prompt-language orientation, topic election, and session topic history;
- Glicko-derived relation standing and indexed consensus reads;
- native least-cost path search over a declared relation-type set;
- bulk label realization after ID-space selection;
- deterministic ordering in several read paths;
- a test demonstrating that changed adjudicated standing can change a later answer.

Those mechanisms do not demonstrate the complete Laplace cognition chain:

```text
multimodal request
  -> typed goal, referents, constraints, evidence and result contract
  -> query-relative state space and neighborhood calculations
  -> indexed bulk frontier search over typed transitions
  -> defects, counterfactuals and act selection
  -> semantic act
  -> modality-native calculated realization
  -> observed consequence
```

The current product surface instead joins a narrow one-operation compiler, a catalog
of caller-selected reads, single-topic election, fixed traversal or fact functions,
and prose scaffolds. “Retrieval was mistaken for cognition” is therefore supported as
a product acceptance finding. It would be inaccurate to claim that no cognition-related
mechanisms were attempted or that no structural prompt compilation exists.

## Recovered execution paths

### Conversational entry point

The current `converse.chat` path is observably:

1. If neither a shape nor explicit topic is supplied, call `execute_prompt` and return
   its rank-one row if present (`chat.sql.in:57-75`).
2. Otherwise elect one topic from prompt coherence, or use caller-supplied identity
   (`:90-114`). If election produces no topic, inspect recent session topics and select
   one ranked candidate (`:115-170`).
3. Infer a second topic only for two named read shapes (`:180-189`).
4. Branch across band listing, relation summary, caller-named read, walk composition,
   or default descriptive read (`:278-458`).
5. Record the prompt and resolved topic for session carry (`:462-465`).

The function's own header accurately says that the caller names the read and that
phrasing did not originally determine it (`:1-14`). A compiler was added later in
commit `61a596e1` and placed before that older path.

### Natural request compiler

`compile_prompt` performs real structural work:

- resolve prompt words once;
- expand witnessed case variants;
- follow witnessed frame-evocation relations;
- require both request and binder roles;
- select one operation by summed standing;
- retain request, binder, and ordered operand identity arrays
  (`compile_prompt.sql.in:31-117`).

However, its route table contains four rows for one operation only
(`operation_frame_routes.sql.in:14-22`), and `execute_prompt` rejects every compiled
shape other than `what_is` (`execute_prompt.sql.in:29-36`). The executor then finds
containers that contain the operand and binder identities, unpacks exact trajectories,
checks a declarative prefix and continuation, excludes the question composition, and
renders the shortest observed closures (`:38-97`). This is a useful exact structural
query. It is not a general goal, discourse, planning, inference, or act compiler.

The regression suite proves route-role extraction using synthetic words connected to
the required frames and proves abstention without a route
(`tests/sql/converse.sql:164-176`). No test in the historical test tree directly calls
`execute_prompt`; the chat-loop fixture also lacks the compiler's route testimony and
therefore exercises the older descriptive path. The executor's complete behavior was
not pinned by a direct acceptance fixture.

### Fixed read dispatcher

`recall_intent` requires the caller to supply a known read name and resolved topic,
then dispatches without consulting question text (`recall.c:1220-1269`). The current
catalog contains 14 fixed read shapes (`recall_route.c:62-89`). `recall_session`
chooses one built-in default, resolves one topic, records it, and passes it to the same
responder (`recall.c:1313-1410`).

These are legitimate explicit query operations. Treating their combined output as
general conversation would make the caller, not Laplace, responsible for compiling
intent, references, constraints, completion, and act selection.

### One-step predictor

`converse.infer` elects one topic, reads its outbound consensus distribution, lets all
other token senses contribute one-hop intersection counts, selects a fixed number of
rows, and renders once (`infer.sql.in:39-152`). The source explicitly records its
limits as forward-only, one-hop bias, and single-step emission (`:19-26`). This is one
useful computational read. It is not a native conversation program, multi-step typed
inference, completion proof, or modality realization.

### Historical path-search surface

The historical tree does contain a reusable native path-search core and PostgreSQL
surface. The wrapper expands either directed or bidirectional consensus neighbors for
an explicit relation-type array and converts Glicko standing into nonnegative additive
cost using negative log probability (`astar_path.c:23-27`, `101-178`). This is direct
evidence that indexed graph search was part of the implementation, not merely prose.

The current implementation remains narrower than the required query-compiled search:

1. The priority queue orders accumulated `g` first and consults `hint` only on an
   exact `g` tie (`astar.cpp:27-39`). It is uniform-cost search with a tie hint, not
   an A-star priority of `g + h`. The header says the same (`astar.h:30-37`).
2. The `k_paths` argument is ignored (`astar.cpp:51-57`), so the API advertises a
   capability the implementation does not execute.
3. Best cost and predecessor are keyed solely by entity identity (`:66-68`, `83-107`).
   Search depth and other query state are absent from the key. Under a depth bound, a
   cheaper deeper arrival can suppress a more expensive shallower arrival that still
   has enough remaining depth to reach the goal.
4. The PostgreSQL expansion performs one SPI adjacency query per expanded entity
   (`astar_path.c:120-178`), rather than issuing a set-oriented frontier expansion
   over the current batch.
5. A state contains only entity, accumulated cost, depth, and hint. It cannot represent
   discourse bindings, container position, occurrence, relation role, time, evidence
   boundary, counterfactual state, remaining obligations, or act completion.
6. The edge handed to the core contains only target and scalar cost
   (`astar.h:19-22`). Result rows contain only step, entity, and cumulative cost
   (`astar_path.c:298-309`). Relation type, direction, structural or epistemic layer,
   evidence roots, source standing, index source, and transition receipt are lost.
7. Geometry is a normalized angular tie value, deliberately not a cost bound
   (`astar_path.c:181-220`). No Fréchet, Hausdorff, Karcher-derived, Hilbert,
   trajectory, containment, occurrence, source, or program-conditioned channel is
   compiled into the state space.
8. Conversation does not call this path surface. Historical call sites are the
   explicit client query and graph-cascade path reconstruction; the natural request
   compiler and `chat` branch tree do not lower a cognition program to it.

The eager `astar_open` calculation completes while SPI remains connected; SPI closes
only after the path has already been constructed (`astar_path.c:291-300`). Closing SPI
before iteration is therefore not a defect in this version.

## Test-evidence findings

### 1. The conversation-loop test proves adjudicated read-through, not cognition

The fixture constructs fact rows, renders them through `converse.about`, and verifies
that `converse.chat('what is a dog?')` emits the same text
(`tests/expected/chat_loop.out:136-151`). It then changes one consensus row and proves
that the descriptive answer changes (`:177-218`).

That is useful evidence for read-after-adjudication behavior. It does not test goal
compilation, reference resolution, query-relative search, counterfactual selection,
semantic-act formation, or generated language realization. A changed ranking that
changes a selected fact is a learning-loop property; by itself it does not establish
the fast cognition loop or the discovery loop.

### 2. Topic-election tests isolate ranking but not full prompt behavior

The prompt-election fixture tests a temporary candidate table and the ordering
expression directly (`tests/sql/prompt_election.sql:15-64`). Its own comment says it
does not call the actual prompt-coherence function (`:11-13`). The test proves the
chosen scalar ordering on controlled rows. It does not prove end-to-end reference,
intent, discourse, or act correctness.

### 3. Conversation routing gates primarily inspect source topology

Managed gates verify that frontends call the designated chat function before the
default read, that a common turn-closing helper is referenced, and that fixed branch
names stay within the published catalog. These are useful integration and parity
checks. They cannot establish the semantic behavior of the invoked path.

### 4. Path-search tests leave central contracts open

The core unit tests cover small least-cost graphs, multiple goals, high-degree
adjacency, no-path behavior, start-as-goal, and a basic depth bound
(`engine/core/tests/test_astar.cpp:49-145`). They do not test:

- `g + h` priority or admissible-heuristic efficiency;
- path-count behavior;
- depth as part of bounded-search state;
- typed or contextual state;
- transition receipts;
- deterministic equivalence across native, SPI, SQL, and C# routes.

The PostgreSQL regression only proves that omitting the geometry flag equals explicit
false and that missing goal coordinates do not change the result
(`tests/sql/walk_richer_forward_pass.sql:117-145`). It does not exercise an active
geometric ordering difference or a query-compiled multi-channel search.

## Historical change sequence

| Commit | Time | Observed change | Audit implication |
|---|---|---|---|
| `709ab812` | 2026-07-12 06:26 UTC | Introduced the SQL conversation, walk, and chat family. | Conversation began as topic orientation plus fixed retrieval and rendering functions. |
| `1af68167` | 2026-07-18 04:12 UTC | Added richer path scoring and an opt-in geometric search hint. | Native path search existed but remained a fixed consensus-graph operation. |
| `c38ca551` | 2026-07-18 21:23 UTC | Added the measured-answer-change loop and shared deposit lane. | Read-after-adjudication was labeled as complete conversational closure without testing the cognition chain. |
| `194b6422` | 2026-07-21 22:55 UTC | Replaced the English surface router with caller-named structural reads. | Language-specific routing was removed, but intent compilation shifted to callers. |
| `cb4438fe` | 2026-08-01 23:53 UTC | Installed the one-step inference read. | A ranked one-hop read was promoted as a forward pass while its own source retained narrow limits. |
| `61a596e1` | 2026-08-17 10:19 UTC | Added the witnessed-frame prompt compiler and exact structural executor for one operation. | This is the closest historical path to a logical compiler, but it was added late and never generalized. |
| `11c8262a` | 2026-08-20 04:34 UTC | Derived path cost from Glicko state. | Traversal cost gained an epistemic basis but still collapsed each transition to one scalar. |
| `26902201` | 2026-08-20 04:54 UTC | Removed invented ranking scale parameters from multiple read paths. | Query scoring remained under active repair at the end of the preserved history. |

## Deliberate-defect acceptance obligations

| New evidence ID | Deliberately broken implementation | Required failure |
|---|---|---|
| `LP-TEST-CONVERSATION-FIXED-READ-NONCOGNITION` | Route every request sharing one topic through the same fixed read. | Distinct goals, constraints, referents, and completion predicates incorrectly produce one plan and act. |
| `LP-TEST-CONVERSATION-ONE-OP-COMPILER` | Implement only a definitional operation and claim general prompt compilation. | Explanation, correction, comparison, counterfactual, research, calculation, and clarification fixtures fail. |
| `LP-TEST-CONVERSATION-RANK1-TOPIC-NONCOLLAPSE` | Collapse a multi-entity relational request to one elected topic before planning. | Relation, negation, role, and discourse constraints disappear from the logical program. |
| `LP-TEST-CONVERSATION-READ-REALIZATION-SEPARATION` | Concatenate selected fact rows with prose scaffolds and claim semantic realization. | Cross-language generation, novel composition receipts, discourse ordering, and unsupported-proposition handling fail. |
| `LP-TEST-CONVERSATION-DISCOURSE-STATE` | Retain only the most recent topic identities. | Pronouns, ellipsis, unresolved questions, active propositions, corrections, and cross-modal referents resolve incorrectly. |
| `LP-TEST-CONVERSATION-LOOP-SEPARATION` | Treat an answer changing after a rating update as proof of cognition and discovery. | Fast cognition, learning, and calculus-extension traces cannot be distinguished. |
| `LP-TEST-SEARCH-ASTAR-PRIORITY-SEMANTICS` | Name a `g`-primary uniform-cost queue A-star while using `h` only for ties. | The declared algorithm and execution receipt disagree, and the heuristic provides no A-star frontier ordering. |
| `LP-TEST-SEARCH-DEPTH-STATE-COMPLETENESS` | Key bounded search only by entity identity. | A cheaper deep arrival suppresses the shallower state required to reach the goal within the depth contract. |
| `LP-TEST-SEARCH-PATH-COUNT` | Ignore the requested number of paths. | A request for several distinct accepted paths returns only one and fails its result contract. |
| `LP-TEST-SEARCH-BULK-FRONTIER` | Execute one database query per expanded entity. | Crossing count and throughput fail the vector-first search boundary. |
| `LP-TEST-SEARCH-TYPED-TRANSITION-RECEIPT` | Reduce every transition to target plus scalar cost. | Type, layer, direction, source, evidence, context, index, and cost-component reconstruction fails. |
| `LP-TEST-SEARCH-COGNITION-LOWERING` | Leave path search as a separate explicit endpoint. | Natural and multimodal requests cannot prove lowering from one logical cognition program to the same search ISA. |

## Disposition

The historical work supplies valuable evidence, not a clean implementation template:

- exact structural container queries are worth retaining as ISA behavior;
- witnessed semantic cues are useful compiler inputs;
- indexed relation traversal and Glicko-derived cost are valid query ingredients;
- batch realization after ID-space selection is a necessary performance pattern;
- deterministic least-cost search remains one declared search program;
- observed answer changes provide one learning-loop acceptance component.

The clean implementation must begin from the full typed logical cognition contract,
generate the state space and measurements for the current goal, perform indexed bulk
search with accurately named and certified semantics, retain typed transition receipts,
select a semantic act, calculate modality-native content, and observe the result. No
fixed read catalog, one-operation compiler, fact renderer, one-hop predictor, or
standalone entity-path query can certify that complete product behavior.
