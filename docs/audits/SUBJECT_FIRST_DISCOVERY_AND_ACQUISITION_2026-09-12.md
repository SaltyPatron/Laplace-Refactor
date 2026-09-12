# Subject-first discovery and acquisition correction — 2026-09-12

Status: working inventor-correction reconciliation. This document does **not** supersede direct inventor evidence, `contracts/authority-stack.json`, source-profile/admission contracts, or product law. It records a product and orchestration distinction that current/legacy UI code repeatedly violates so future agents do not mistake provider-shaped forms for the intended Laplace interaction.

## 1. The user operates on a subject/world, not on an importer

A person should be able to ask Laplace for a meaningful subject such as:

```text
Magnus Carlsen
Baltimore Ravens
Moby Dick
Apollo 11
FrameNet Commerce_buy
U+0041
one repository/project
one D&D campaign/world actor
```

and remain centered on that subject/world while Laplace discovers what exact state is already known, what external sources/providers can contribute, what mappings are proven or only candidate, and what acquisition/admission operations are available.

The ordinary user workflow is **not**:

```text
choose provider page
-> learn provider-specific identifier syntax
-> search provider
-> copy identifier
-> change tab
-> choose second provider
-> paste identifier
-> launch source-specific job
-> repeat for every source
-> leave those pages and search for the resulting entity again
```

That is an implementation-shaped workflow.

The intended workflow is:

```text
find / open subject
  -> resolve current referential candidates and known world
  -> discover eligible provider/source capabilities for that subject kind
  -> search/query providers using the strongest known coordinates each provider supports
  -> retain provider results as distinct candidate external references
  -> calculate/display candidate correspondences, conflicts and missing proof
  -> user/authority selects or approves the exact acquisition boundary when required
  -> common source-profile/admission machinery acquires, verifies, lowers and witnesses it
  -> refresh the SAME subject/world with newly admitted evidence
```

Provider/source is a witness/acquisition coordinate around the subject. It is not the user's primary navigation ontology.

## 2. Current chess import UI is a counterexample

The legacy/current `Chess Lab` import flow is explicitly organized as:

```text
Choose a source: FIDE | Chess.com | Lichess
Choose an experiment
Fill provider-specific parameters
Run a provider-specific job
```

The implementation includes separate `player-profile`, `fide-search`, `fide-profile`, `fide-roster`, and game-fetch job forms. Chess.com/Lichess require an online username; an optional FIDE ID is manually supplied to associate an official identity. The FIDE search produces an actionable table whose next action launches another FIDE-profile job.

Those mechanisms prove useful underlying capabilities exist, but the interaction model is backwards. A user looking for **a player** should not have to operate a mini ETL lab.

Keep the provider capabilities. Remove provider workflow choreography from the ordinary user's burden.

## 3. Identity safeguards remain mandatory

Subject-first does **not** mean fuzzy-name auto-merging.

The existing FIDE contract correctly establishes:

```text
name similarity != identity proof
exact provider id = provider-coordinate lookup, not universal person identity
explicit witnessed association = auditable mapping evidence
```

A provider result must retain at least:

- provider/source authority;
- release or observation time where applicable;
- provider namespace and external identifier;
- exact returned names/handles/aliases;
- source URL/artifact/record identity as applicable;
- profile facts and their source provenance;
- discovery query/coordinate that produced the candidate;
- candidate-to-current-subject mapping state;
- dependence and confidence/standing where a mapping recipe calculates them;
- conflict/ambiguity/unknown disposition.

Laplace may present one candidate as highly likely under a declared calculation. It cannot silently promote similarity to referential identity or erase competing candidates.

## 4. Discovery must be capability-driven

Different providers expose different search coordinates. A provider may support:

- exact provider ID lookup;
- exact handle/username lookup;
- real-name search;
- roster/cohort/ranking enumeration;
- event/team/organization membership traversal;
- source-native cross-links;
- bulk archive discovery;
- no useful remote search at all, only acquisition after an exact artifact/identifier is supplied.

The common discovery planner therefore asks the provider/source descriptor what it can legally and technically do. It does not hardcode one universal `search(name)` call and it does not require the user to understand these differences before beginning.

For a selected subject, planning may use already witnessed coordinates such as names, aliases, provider IDs, organization/team memberships, event occurrences, source-native references or explicit links. Candidate provider calls remain bounded, receipted and authority-aware.

A provider that cannot be searched with the available coordinates returns a typed unavailable/open-obligation result. It does not disappear and it does not force the entire workflow onto a separate page.

## 5. Provider discovery and source admission are different operations

Do not collapse:

```text
provider/source capability discovery
candidate external reference discovery
referential correspondence/adjudication
artifact/profile acquisition
source profile qualification
source admission
world/evidence epoch publication
subject-world materialization
```

A search result is not admitted truth. A downloaded profile is not a merged person. A verified artifact is not world admission. A source admission is not referential convergence unless the admitted testimony supports it and the referential operation accepts it under its own law.

The existing generic contracts remain useful:

- `source-profile-model.json` defines the exact authority/release/artifact/grammar/recipe/witness boundary needed to admit source state;
- `source-admission.json` admits unlike observations onto one epistemic playing field and explicitly allows cross-source whole-working-set resolution;
- referential identity retains provider mappings and conflicts instead of collapsing a person to one SaaS/provider account.

Subject-first discovery sits **above/alongside** these contracts as a product/executive planning operation. It chooses which provider/source capabilities are relevant to the current subject and goal; it does not replace source admission or identity law.

## 6. One person/player world can accumulate heterogeneous evidence

For a chess player, a useful world may include, where actually discovered/admitted:

```text
person / player referential candidate
  |
  +-- names / aliases
  +-- FIDE provider identity and official profile observations
  +-- Chess.com provider identity/profile observations
  +-- Lichess provider identity/profile observations
  +-- PGN player occurrences
  +-- tournament/event occurrences
  +-- teams/federations/organizations
  +-- ratings with source/time/control context
  +-- opponents and head-to-head occurrences
  +-- games / shared canonical lines
  +-- positions / moves / openings / motifs
  +-- books/articles/commentary that explicitly refer to or ground the player/game
  +-- classical/model calculations over shared positions
  +-- mapping propositions among provider identities with evidence/conflict state
```

The same idea generalizes beyond chess. A person may have repository/employer/school/service/account/achievement witnesses. A sports team may have league, official site, schedule, statistics, media and event witnesses. A book may have publisher/library/Gutenberg/author/edition/review/reference sources. The user stays on the subject while source/provider state expands around it.

## 7. Discovery should exploit the world already present

Provider discovery is not only outbound web/API search. Existing admitted state can generate stronger acquisition coordinates.

Examples:

- a PGN player occurrence exposes names and events that may nominate provider lookups;
- an admitted FIDE identity may give the exact provider coordinate needed for official refresh;
- a Chess.com/Lichess profile may carry declared real-name/URL/provider facts that become mapping candidates;
- an event roster can nominate players not yet admitted individually;
- a book citation or explicit URL can nominate another source artifact;
- an external reference coordinate can remain unresolved until its provider/source profile is available, then be revisited under a new epoch.

This is a natural OODA/product interaction:

```text
observe current subject world
-> orient around missing/valuable source coordinates for the user's goal
-> decide which bounded discovery/acquisition actions are eligible
-> act through provider/source operations
-> observe returned provider state
-> admit selected exact evidence
-> rematerialize the subject world
```

That is not Gödel discovery. It operates inside the current calculus using known provider/source capabilities. A failure of the current calculus to represent or explain a persistent constrained pattern belongs to Gödel's separate discovery path.

## 8. UX: one reusable subject workspace

A subject detail/workspace should expose ordinary content first and provider operations contextually.

Representative structure:

```text
Magnus Carlsen

Overview | Career | Games | Events | Openings | Evidence | Sources | Structure | Calculations

Known identities / providers
  FIDE        1503014       verified/linked        refreshed <time>
  Chess.com   MagnusCarlsen witnessed candidate    <coverage>
  Lichess     DrNykterstein witnessed candidate    <coverage>
  ...

Available evidence to acquire
  FIDE        official profile/rating generation   [review/acquire]
  Chess.com   profile + available game archives    [review/acquire]
  Lichess     profile + game stream/archive         [review/acquire]
  books       references/citations discovered       [review candidates]
  events      roster/result sources                 [review candidates]

Mapping questions
  <candidate A> corresponds to <candidate B> because <evidence>
  competing candidate / missing proof / conflict shown explicitly
```

The exact labels/layout are product-design choices. The invariant is that the **subject remains selected** while providers and acquisition actions are facets of that subject.

## 9. Convenience does not bypass authority

An intelligent workflow can:

- automatically discover candidate providers;
- precompute acquisition plans;
- group compatible acquisitions;
- show what new evidence/coverage each plan is expected to add;
- deduplicate already-admitted artifacts/profile generations;
- reuse existing mappings and exact provider coordinates;
- schedule one durable multi-provider job when semantics and authority allow;
- refresh the subject when the durable result publishes.

It cannot silently:

- broaden the user's selected boundary;
- merge people/accounts by name;
- accept a changed provider release/profile without the applicable authority;
- turn discovery into testimony;
- count mirrors as independent sources;
- hide contradictory provider state;
- make one provider the owner of person identity;
- rerun already admitted unchanged work merely because the user revisited the page.

Convenience is achieved by **machine orchestration of typed operations**, not by weakening identity/evidence/source law.

## 10. Reusability requirement

The chess workflow must not be implemented as a one-off `Find chess player everywhere` controller.

The reusable abstraction is closer to:

```text
subject / selected world
+ subject kinds / observed roles
+ current known reference coordinates
+ current goal
+ provider/source capability registry
+ authority/resource boundary

    -> candidate discovery programs
    -> candidate reference results
    -> mapping/adjudication views
    -> acquisition/admission plans
    -> durable jobs and receipts
    -> refreshed subject/world
```

A provider supplies only irreducible provider-specific discovery/acquisition mechanics and source-profile semantics. The common machine owns planning, bounded execution, authorization, jobs, mapping state, source admission, evidence, referential identity, receipts, and materialization.

The same interaction should work for players, people, organizations, teams, works, places, projects, repositories, models and other referential/domain worlds when suitable provider/source capabilities exist.

## 11. Acceptance consequences

A conforming product must prove at least:

1. Searching `Magnus Carlsen` enters a player/person-centered workspace without first asking for FIDE/Chess.com/Lichess.
2. The workspace discovers every configured eligible chess identity/source provider whose capability can be exercised with the available coordinates, and explicitly reports providers that need an unavailable identifier/capability.
3. FIDE exact-ID, FIDE name/roster discovery, online-provider exact-handle acquisition and game/archive acquisition remain distinct provider operations behind the common plan.
4. A candidate discovered by name is not merged solely by name; explicit or calculated mapping evidence and its disposition are inspectable.
5. User-approved/provider-authorized acquisition can combine several compatible sources into one durable plan/job without requiring tab-by-tab submission.
6. Re-running the same selected plan after an ambiguous network retry is idempotent and does not duplicate effective evidence.
7. Newly admitted evidence appears in the same player world after publication; the user does not have to search for the player again.
8. Existing games/events can nominate additional provider/reference candidates without manufacturing identity proof.
9. A conflicting FIDE/online mapping remains visible and does not destroy either provider identity or prior evidence.
10. The same subject-first discovery/acquisition mechanism passes a materially different non-chess fixture.
11. Graph/glome/raw/source/admin views remain available but are not required to perform the ordinary subject acquisition journey.
12. The acquisition receipt identifies discovery operations, selected provider coordinates, exact source profiles/artifacts, mapping decisions, authorization, new/reused state, publication boundary and every unresolved obligation.

## 12. Current implementation defects to retain as negative controls

Reject as product completion:

- source tabs as the required first decision for ordinary entity acquisition;
- separate manual FIDE search followed by copy/paste into a Chess.com/Lichess form;
- provider-specific job catalog exposed as the ordinary user's information architecture;
- asking the user for an external ID the installation could have discovered or already witnessed;
- automatic name-equality merge used to avoid the inconvenience of reconciliation;
- successful provider fetch reported as successful person resolution;
- multiple provider jobs whose results cannot be reopened from the same subject world;
- a source admin catalog presented as a replacement for subject-first acquisition;
- a chess-only aggregation controller that bypasses the generic provider/source and referential machinery.

## Evidence continuity

Historical/current implementation evidence already proves several useful primitives exist: FIDE name/ID/roster discovery, Chess.com/Lichess profile/game acquisition, explicit FIDE association input, provider-specific profile witnessing, chess entity worlds, and common source/profile admission concepts. The defect is not absence of every primitive. The defect is that those primitives are exposed as separate provider/operator workflows instead of being orchestrated from the user's selected subject/world.

The current UI design review already requires browse/inspect/act/verify, automatic discovery of available sources/capabilities without guessing identifiers, persistent collection selection and reusable cross-domain viewers. This correction makes the missing subject-centered acquisition relationship explicit so those generic UI requirements cannot be satisfied by a polished source-admin page alone.
