# Laplace machine roadmap

This document is the **current execution projection** of the stable product law and the
machine-readable capability graph in `contracts/operation-model.json`. It is not a
substitute for implementation, acceptance, an installed product, seeded world state,
or release. Stable meaning lives in the Constitution, Invention Model, architecture
contracts, requirements, and owning issues. This roadmap reports the currently observed
execution boundary and must move forward when that boundary moves.

GitHub issues and executable receipts are the live work-status system. Historical
failures remain evidence, but a prerequisite that has been repaired, merged, and
verified does not remain a current blocker merely because an older run failed there.

## Reading the roadmap honestly

Laplace uses the following proof states. They are intentionally not synonyms.

| State | Exact meaning |
| --- | --- |
| Requirement encoded | Product law and acceptance obligation exist. No mechanism is implied. |
| Unimplemented | The required canonical mechanism does not exist in this clean product. |
| Partial | A narrower implementation boundary exists and its omissions are named. |
| Integration proven | The mechanism executed under a controlled integration boundary. |
| Product activated | The accepted package and state are installed and active in the selected product environment. |
| World admitted | An exact source profile completed admission into that activated product with closure receipts. |
| Foundational seed complete | The configured heterogeneous world boundary closed across profiles, references, evidence, and derived publication. |
| Accepted | Required complete-product tests and deliberate defects passed on the declared product boundary. |
| Released | The exact accepted package, receipts, manual, and provenance were published. |

An issue, document, schema, Gherkin scenario, green authority test, or populated roadmap
does not advance a mechanism to an implementation proof state. Conversely, a proven
implementation boundary must not continue to be described as absent because an older
roadmap snapshot predates it.

## Present boundary — 2026-09-04

Observation boundary:

```text
main = 145a226be471c9a8c3b406e8fbb790e772a1d295
       Fix setup-host command discovery on main

product-path run      = 33929292743
product-activation run = 33930237685
```

### Delivery progress that is already cleared

The current delivery path has advanced materially beyond the prior bootstrap and
integration-branch state:

- the one-time DEV/BAT host bootstrap succeeds on the real host;
- `/opt/laplace/receipts/bootstrap/host.json` exists;
- recurring product delivery belongs to CI as `laplace-runner`;
- the administrator boundary is narrowed to the declared systemd service-control
  capability rather than whole-product root execution;
- exact-main hosted requirements/authority proof passes;
- exact-main native `linux-dev` passes;
- exact-main native `linux-sanitize` passes;
- exact-main selected custom-stack PostgreSQL-native proof passes;
- aggregate `product-path` passes;
- persistent DEV/BAT deployment dispatch is reached;
- the persistent activation workflow passes `runner-authority` and `compose-product`.

None of those are current roadblocks unless later evidence proves an actual regression.

### Current failed delivery acceptance

The persistent activation run currently stops in `activate-product` at:

```text
Verify protected deployment context and reconcile installed gateway generation
```

Actual persistent activation steps after that check were not reached in that run.
Therefore the active delivery tail is now:

```text
reconcile installed gateway generation / privilege boundary
-> activate persistent product as laplace-runner
-> PostgreSQL initialize / repair / exact readback
-> Unicode activation
-> Highway activation
-> durable product receipts
-> service restart / later-host-boot readback
-> repeat verification
```

The owning implementation path is PR #215 with #12/#120. This is the real current
execution boundary; `setup-host`, PR #128, and the old disposable custom-stack failures
are historical evidence, not prerequisites still waiting to happen.

### Current repository-integrity state

PR #128 is merged. The original 124-branch / five-open-PR / 13-live-tip audit is a
historical baseline that motivated #183; it is not the current branch-estate count.
The current open-PR observation at this boundary is:

- #215 — persistent DEV/BAT delivery / service ownership;
- #219 — task-scoped habits, safety, and audience-admissibility documentation.

#183 still owns reconciliation of every **actually unique required behavior** to
`main`, and #22 still requires zero unaccounted branch-only required behavior before
release. That integrity work proceeds in parallel with dependency-unblocked product
implementation. Archive/preserve refs and stale historical branch names are not, by
themselves, runtime blockers.

## Current capability snapshot

| Capability | Current fact |
| --- | --- |
| Clean product authority and executable requirement topology | On `main`; #117 post-merge authority/traceability integration verified and closed completed |
| Finish-line program / anti-substitution governance | Implemented on `main`: contract, explanatory document, BDD and validator exist; #180 remains open for live branch-ledger/mutable-status reconciliation |
| Host prerequisite/bootstrap boundary | **Completed on the real DEV/BAT host** for `main@145a226`; recurring delivery handed to `laplace-runner` |
| Change-sensitive product-path execution | Exact-main hosted, both native lanes, selected custom-stack PostgreSQL-native proof, and aggregate product-path **passed**; #54 remains open for required-check authority/durable receipt tail |
| Persistent PostgreSQL product activation | Current failed acceptance is installed gateway-generation reconciliation; persistent activation/readback/restart remain open under #12/#120/#215 |
| Unicode root calculation and PostgreSQL/Tier-0 sibling activation | Integration proven; selected persistent product activation/readback still not complete |
| Direct and reverse Unicode hot planes | Integration proven and measured under their declared boundary |
| General whole-working-set composition and presence | Integration proven; product activation/world admission remain later states |
| Canonical decomposition identity / occurrence separation | Substantial implementation is on `main`; #96 remains open for real-source boundary proof and #102 exact locked-CILI resource/cardinality/replay receipt |
| Sparse addressability and storage-compute economics | Representative composition evidence exists; paired acceleration/removal/storage-class/bloat acceptance remains under #72 |
| Cross-domain whole-route cohesion | Still open under #70; lower component success does not establish the universal machine |
| Typed machine exception registry and recovery | Partial implementation is on `main`; descriptor registry/validation/PostgreSQL projection exist; executable retry/reroute/replay, WHY_NOT and physical fault behavior remain under #56 |
| Universal AST grammar registry and general recipe compiler | Incomplete; owned by the common framework/recipe program rather than source-private decomposers |
| Whole-machine typed numerical Highway | Partial; generated/scoped coordinates and activation/readback machinery exist, while persistent product activation and whole-seed topology remain incomplete |
| Exact non-Unicode source profiles | Partial controlled proof exists for exact tabular/ISO-style profiles; activated-product world admission remains absent |
| Source-estate discovery / provider qualification | Partial mechanism on `main`; exact configured source boundary is #195, provider/template tails are #112/#115 |
| Heterogeneous world admission / foundational seed | Partial mechanism only; no activated-product configured foundational seed closure yet |
| Typed Glicko-2 standing / onboarding | Partial implementation is on `main`: real-opponent calculation, immutable events/state/receipts and durable PostgreSQL path exist; stock recipe activation, testimony lowering, return legs and derived epochs remain under #110 |
| Derived attestations / current-vs-as-of learned belief | Architectural/evidence owner #16 now explicitly treats derived state as usable learned belief with dependence and temporal applicability; executable completion remains with its consumers |
| Unicode-native token tiers / witnessed language realization | Required contracts exist; complete discourse/cognition/readiness/realization path remains under #18/#17/#169/#218 |
| Clean-seed indirect inference before production-user admission | Release-blocking requirement under #116; configured seed and installed public-route proof remain incomplete |
| Typed filtered indexed search / answerability / cognition | Native guidance/operator/solver/search kernels are on `main`; real providers, PostgreSQL indexed path, guidance-loop integration, realization and public-route acceptance remain #17/#60/#132/#18 |
| Gödel procedural learning / habits / muscle memory | Architecture/owners #19/#169 exist; complete executable discovery and activation remain open |
| Software-development OODA | Clean executable owner #221; consumes admitted repo/build/tool state rather than a private coding intelligence |
| Cross-repository defect-family generalization | Clean owner #222: `find one bug, fix 50,000`; consumes #221 + #168 + #19/#169 and must validate semantics/counterexamples |
| Exact model source admission | #223 owns `/vault/models` artifact graphs/source profiles; exact checkpoint admission is separate from model behavior and target compilation |
| General model behavior / AImaps / target compiler | Still incomplete under #20/#71/#129; source models are witnesses, not native cognition ontology |
| Entity worlds, personal webs, entitlement policy, product surfaces | Unimplemented/incomplete under Phase 7 owners |
| Federation, distributed placement, ARM/Raspberry Pi product support | Unimplemented/incomplete under placement/federation owners |
| Accepted installed product / release | Not yet; #22 remains terminal acceptance |

### Historical operating evidence is not present product state

The deployed old iteration remains valuable behavioral evidence. Its historical data
volumes, query demonstrations, chess behavior, model experiments and failure incidents
may supply counterexamples and acceptance fixtures. They do not define clean-product
implementation state or current blockers.

Likewise, a historical failed refactor run remains evidence of a defect that existed at
that revision. Once the defect is corrected on a later authoritative revision and the
same boundary is verified, the roadmap advances and the old run moves into the evidence
history rather than continuing to block the program by prose.

## Machine traffic map

```text
verified source and build authority
              |
              v
common framework + grammar/recipe compiler
              |
              v
typed exceptions / traps / faults / recovery
              |
              v
package activation + universal AST composition + PostgreSQL bulk presence/deposit
              |
              v
product Unicode root + typed numerical Highway
              |
              v
testimony / lineage + exact heterogeneous source profiles and world admission
              |
              v
source discovery/qualification + reusable template correction
              |
              v
configured foundational seed closure
              |
              v
adjudication + typed operators + filtered indexed search/A* + answerability
              |
              v
semantic acts + recomposition/effects + OODA/Goedel learning
              |
              +------------------+
              |                  |
              v                  v
model witness/target       entity worlds/personal webs
compilation                entitlement/identity/surfaces
              |                  |
              +--------+---------+
                       v
             placement + federation
                       |
                       v
       package + generated architecture manual
                       |
                       v
        complete-product acceptance + release
```

This is a capability/dependency graph, not a global runtime waterfall. Once active,
Laplace repeatedly observes, calculates, persists, searches, realizes/effects, and
observes consequences. Source families do not form a global source waterfall; recipe
DAGs may interleave exact work according to actual dependencies.

## Operational stages and owners

| Stage | Primary outcome | Owners | Current proof state / active tail |
| --- | --- | --- | --- |
| `foundation.acquire-build` | Verified upstream, toolchain, package, host prerequisites and receipt foundation | #3, #8, #12, #54 | Host prerequisites complete; exact-main build/product-path proof passes; persistent installation/readback remains |
| `framework.execution` | One registry/context/batch/recipe/provider lifecycle and whole-route cohesion | #4, #5, #6, #10, #57, #58, #70 | Partial; common framework exists in slices, generic provider/recipe/cohesion closure remains |
| `machine.handle-exceptions` | Generated exceptions, faults, recovery, replay, WHY_NOT | #56 | Partial on `main`; descriptors/projection proven, executable recovery/fault-injection tail remains |
| `bootstrap.dependencies` | Framework-mediated package/runtime activation | #3, #12, #120 | Host bootstrap complete; persistent activation currently fails at installed gateway-generation reconciliation |
| `substrate.compose-physicality` | Universal AST identity, Merkle composition, physicality, trajectory | #7, #13, #96 | Composition/decomposition repairs on `main`; real-source #102/#96 closure remains |
| `substrate.bulk-deposit` | Whole-working-set presence and transactional persistence | #15, #72 | Integration proven in representative lanes; complete-ingest/economics tails remain |
| `bootstrap.unicode-root` | Product-deposited coherently activated Unicode atom floor | #13, #14 | Integration proven; persistent selected-product activation/readback remains |
| `substrate.highway` | Append-only typed coordinates for machine namespaces | #52 | Partial; current product activation blocks selected persistent-world activation, not host setup |
| `evidence.record-lineage` | Occurrences, testimony, derivation, dependence roots | #16 | Partial; evidence structure exists, cross-source/adjudication/derived publication continue |
| `world.discover-qualify-sources` | Deterministic discovery and qualified providers | #112, #195 | Partial; configured estate is explicit, full provider qualification/coverage remains |
| `world.infer-source-templates` | Reusable source-template proposals/corrections | #115 | Incomplete |
| `world.admit-witnesses` | Exact profiles and heterogeneous world admission | #53, #59, #195 | Partial mechanism; activated-product world admission absent |
| `evidence.adjudicate` | Typed standing, return legs, contradiction/referential epochs | #16, #110 | Partial on `main`; stock recipe activation, testimony lowering, return legs and later epochs remain |
| `query.guidance-search` | Typed guidance/operators/filter-first indexed search/completion | #17, #60, #132 | Native kernels on `main`; real indexed providers, PostgreSQL/public routes and complete guidance loop remain |
| `cognition.realize-effect` | Semantic-act selection, reversible realization/effects | #18, #218 | Incomplete; task-scoped habits/safety/audience boundaries are documented separately from realization implementation |
| `learning.discovery-ooda` | Evidence learning, Goedel discovery, proceduralization | #19, #169 | Incomplete executable closure; architecture and acceptance owners exist |
| `development.ooda` | Repo/code edit, build/test/runtime observation and learned development procedure | #221 | Explicit clean owner; implementation open |
| `development.defect-family` | Cross-repository structural defect-family discovery and generic-owner repair | #222 | Explicit clean owner; implementation open |
| `world.configure-foundational-seed` | Configured heterogeneous foundation closure | #53, #115, #195 | Incomplete |
| `model.admit-sources` | Exact checkpoint artifact graphs/source profiles | #223 | Open; prerequisite to model behavior/AImap/target work |
| `model.ingest-generate` | Model behavior evidence, AImaps and target compilation | #20, #61, #71, #129 | Incomplete; source admission is no longer conflated with this lane |
| `product.materialize-entity-world` | Entity worlds, personal webs, entitlements/surfaces | #21, #62, #63, #64, #68 | Incomplete |
| `runtime.federate-nodes` | Placement, federation and portability | #21, #65, #66, #67 | Incomplete |
| `delivery.activate-product` | Persistent installed package/services/state/readback | #12, #21, #50, #69, #120, PR #215 | **Active:** host bootstrap/product-path/package composition cleared; installed gateway-generation reconciliation is current failed acceptance |
| `delivery.accept-seeded-inference` | Clean configured-seed indirect derivation through public product | #116 | Incomplete |
| `delivery.release-product` | Complete-product acceptance and release publication | #22 | Incomplete terminal gate |

## Current critical path

The current program is not waiting for PR #128 or setup-host. The immediate delivery
vertical is:

1. **Finish the active #215 persistent-delivery repair** at the current failed boundary:
   reconcile the installed activation gateway generation while preserving the
   `laplace-runner` ownership law.
2. Execute persistent product activation from the accepted package, then prove exact
   PostgreSQL 18.6 identity, Unicode/Highway activation, receipts, application readback,
   restart and later-host-boot persistence.
3. Retain the exact current product-path evidence as cleared prerequisite proof; do not
   re-run work merely to satisfy stale prose. Re-run only when a changed artifact or
   acceptance boundary requires it.
4. Close the exact #102 real-CILI resource/cardinality/replay receipt if it is not
   already contained in retained passing evidence; broad custom-stack success is
   progress but does not substitute for that exact fixture.
5. Continue substrate execution correctness through #4/#10/#177/#179/#171 as its
   dependency/evidence boundaries permit, rather than treating branch-estate history as
   a reason to suspend independent valid work.
6. Finish common framework obligations exposed by real execution: recipe/provider
   lifecycle, #56 exception/recovery semantics, and #70 unrelated-program cohesion.
7. Complete configured source admission (#195/#53/#112/#115), then close the
   heterogeneous foundational world boundary rather than treating one corpus as the
   seed.
8. Finish #110 typed standing/return-leg publication and wire that exact lane into
   cognition without turning it into global meaning/truth.
9. Complete #17/#60/#132/#18 native cognition → semantic completion → realization and
   the public installed route with exact WHY_NOT and measured physical-plan receipts.
10. Complete Goedel procedural learning (#19/#169), including development consumers
    #221/#222 where applicable.
11. Admit model sources exactly under #223, then prove model behavior/AImaps/target
    compilation under #20/#71/#129 without flattening native state.
12. Build entity-world/product/federation/placement surfaces over the same machine.
13. Before production-user admission, execute #116 against the installed configured
    seed with required negative controls.
14. Close #183's **actual** unique branch behavior ledger and all other Phase 0–7 exit
    predicates, then execute #22 complete installed-product acceptance and release.

This list is updated from evidence. If step 1 clears, the roadmap must move to the next
real failed/open predicate instead of continuing to print step 1 forever.

## Parallel lanes that do not change the critical path

- **Repository integrity:** #183 reconciles current unique required branch behavior to
  `main`; archive/preserve refs do not become blockers merely by existing.
- **Semantic ownership:** #184 keeps direct corrections and cross-repo evidence bound to
  one clean semantic owner rather than creating duplicate issues.
- **Authority and acceptance:** maintain requirement/authority trace, deliberate-defect
  coverage, generated-manual inputs and proof-state accuracy as mechanisms land.
- **DevOps and receipts:** use the current owning PR/branch, repair its real failed
  acceptance, and retain content-addressed evidence beyond transient Actions copies.
  Do not resurrect PR #128 as an integration finish line.
- **Source reconnaissance:** inventory exact releases, files, licenses, grammars,
  identifiers, joins, denominators, errors and negative controls without claiming world
  admission or writing source-private engines.
- **Portability preparation:** identify ARM/toolchain/PostgreSQL constraints and real
  hardware fixtures without claiming support before package/semantic acceptance.
- **Product/funding communication:** publish measured progress, failures and remaining
  boundaries without presenting unimplemented benefits as delivered.

## DevOps control plane

GitHub issue #23 owns the whole product. `contracts/finish-line-program.json` owns the
machine-readable progress/closure law. #180 owns finish-line governance; #183 owns
branch-estate reconciliation; #184 owns cross-repository semantic reconciliation; #22
owns terminal acceptance.

The old rule that PR #128 was “the single integration finish line” is retired because
PR #128 is merged. A current owning PR is an implementation vehicle, not the product
finish line. Its red check is repaired at the actual failed boundary; a sibling branch
is not created merely to avoid that failure.

Required checks are evidence gates, not release state. The aggregate `product-path`
mechanism has now passed on current `main`; #54 still owns making the aggregate
change-sensitive authority and durable receipt semantics match merge policy rather than
pretending the execution path does not exist.

## Funding and time

The critical scarce resource is sustained engineering time. Patreon support currently
helps replace hours spent earning day-to-day income with hours available for design,
implementation, testing, documentation, measurement, and public demonstrations. It
does not purchase invention authority, truth, or an unimplemented feature promise.
Future Patreon-linked entitlements remain a planned witnessed/governed product
mechanism, not a current product capability.
