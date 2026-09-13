# Current product-surface counterexamples — 2026-09-12

Status: implementation-damage evidence only. This document does **not** define Laplace product law. It records present UI/product behavior that must not be mistaken for the invention. Governing intent comes from direct inventor evidence, the authority stack, current product law, and the pinned original Laplace product-surface lineage.

## Evidence basis

This record is based on:

1. current-session inventor-supplied screenshots of the running product;
2. the current `SaltyPatron/Laplace` web implementation that produces the same product family;
3. pinned original product-surface evidence and current product requirements.

The screenshots show, among other things:

- an Explore/Highway surface dominated by empty or unavailable layer counts and `Not found in this installation` dispositions;
- a Highway detail surface that exposes relation-band rows and zeros instead of a meaningful domain record/page hierarchy;
- an entity page for a raw implementation-oriented entity (`unicode_latin`) whose primary usable content is an id, generic standing/game-style counters, physicality counts, and a low-level coordinate;
- a U+0000/codepoint detail dominated by implementation facts and raw technical properties rather than functioning as one navigable resource inside a larger reference/world product.

These screenshots are not evidence that Laplace should look this way. They are evidence of the current implementation gap.

## 1. The current shell mistakes inspection for exploration

The inventor-direct product direction is **a league/reference site, not a lab**:

```text
league -> division/conference -> team -> position -> player -> roster -> schedule/results
```

The analogy is architectural, not decorative. The expected product behavior is familiar master/detail navigation with a URL/resource page for each meaningful subject, rosters/memberships, records, standings, schedules/events/results, cross-links, evidence, and supporting visualizations.

The current implementation instead makes low-level substrate inspection a primary interaction:

- generic entity pages;
- raw relation/consensus summaries;
- raw ids and types;
- Graph/Glome/Structure/Links/Provenance/Export tabs;
- direct relation-band tables;
- source/warehouse/debug-oriented navigation.

Those can remain valid supporting views, but they are not substitutes for the intended domain product.

## 2. `Browse from a word` is only an entry mechanism, not the exploration model

Pinned original product evidence supports resolving a surface or canonical entity and then following structure, relations, evidence, graph state, chess, highway, warehouse, walk, matchup, and audit views.

That does **not** reduce Explore to:

```text
type a word -> get a generic entity -> inspect neighbors
```

The intended exploration layer materializes natural worlds and hierarchies. Examples include, without limiting the product to sports:

```text
sports:
league -> conference/division -> team -> position -> player -> roster -> schedule -> game -> result/stat/standing

chess:
player -> career -> opponent -> event -> game -> position -> line/opening -> move/transition

language:
highway -> layer -> hub/frame/synset/class/roleset -> role/position -> surface/sense/lemma -> roster -> witnessed results/standing

books/documents:
collection/work -> edition/source -> book -> part/chapter -> section -> paragraph -> sentence -> constituent structure

D&D / world games:
world/campaign -> region/location/faction -> actor/NPC/player -> inventory/relationship/quest -> event/turn/action -> consequence/history
```

The universal substrate supplies the addressable state and laws; the product materializes the domain's own meaningful resource structure rather than forcing every domain through one generic entity inspector.

## 3. The current Highway UI is a narrowed implementation projection

The current `HighwayLanding.tsx` correctly states an intent to present the language highway as a league table, but the actual implementation is still a narrow hand-authored table over `HIGHWAY_LAYERS` and `/v1/query/bands` counts.

`layers.ts` hardcodes a finite list of current implementation concepts and embeds implementation-state commentary directly into product definitions, including statements such as:

- a sense layer is "the layer that currently picks 'hulk' for 'whale'";
- the definitional layer is "the only one currently answering well end to end";
- highway-mask and dependency-relation entries are described in terms of missing current API/schema support.

Those observations may be useful debugging notes, but they are not stable invention semantics. They make temporary repository state narrate the product model.

A proper Highway surface must derive its resource hierarchy, roster, standings/results, contribution proof, and available reads from active typed state/contracts. Missing implementation must appear as an explicit capability/why-not condition, not become the ontology of the page.

## 4. The generic `PlayerCard` currently projects the sports metaphor onto arbitrary entities too literally

The current entity header installs `PlayerCard` on every entity. For non-chess entities, the card renders generic evidence-status counters (`evidence rows`, `confirmed`, `contested`, `refuted`, `thin`). For chess players it renders source Elo/games/wins/draws/losses.

This is a useful diagnostic/record component, but it is not equivalent to the intended sports/reference-site information architecture.

The sports model means:

- the domain supplies natural roles and hierarchy;
- each resource has a meaningful record page;
- standings and matchups are one view of evidence/competition where appropriate;
- schedules/results/events are actual addressable world state;
- the user can drill through known domain structure without understanding substrate internals.

It does **not** mean every arbitrary entity should be cosmetically converted into a sports player by attaching generic counters.

## 5. Empty installation state currently dominates the product experience

A correct product must degrade honestly when required world state is absent, but `0`, `—`, `not readable yet`, and `not found in this installation` cannot become the dominant product architecture.

Typed absence/unsupported/unseeded/unknown states should answer:

- what capability or world state is absent;
- why it is absent;
- what exact recipe/source/epoch/provider requirement is open;
- what parts of the resource remain navigable anyway;
- whether another admitted source/world/epoch can satisfy the request.

The user should not be dropped into a mostly empty implementation table and expected to infer the machine's intended world from missing rows.

## 6. Low-level Unicode/codepoint views are necessary but currently overexposed as product identity

Unicode/DUCET/S3/Hilbert/Tier-0 state is foundational machine infrastructure. It must be inspectable and explainable.

But a codepoint page dominated by raw codepoint position, packed/physicality identifiers, coordinate values, internal type labels, or implementation-specific properties is an engineering/debug view unless the user explicitly asks for that level.

A reference-style codepoint resource should be able to materialize the same canonical entity at multiple useful levels:

- human-readable character/name/script/block/category/normalization/collation/usage views;
- its structural role in compositions and containers;
- linguistic/source testimony and provenance where present;
- mathematical/physicality inspection as a deeper technical tab;
- exact identity/receipt/debug information as an expert view.

The machine foundation should enable the resource page, not consume it.

## 7. Product exploration is evidence of machine function

The league-site decision says every major layer page must prove three things:

1. **what is in the layer/world**;
2. **how it is queried through one stable optimized operation**;
3. **what it contributes to inference, generation, prediction, action, or another declared machine outcome**.

Therefore Explore is not incidental UI. It is one of the machine's glass-box proofs.

A correct product can let a user move from, for example:

```text
NFL
 -> AFC
 -> AFC North
 -> Baltimore Ravens
 -> roster
 -> player
 -> season/game
 -> witnessed statistic/result
 -> source/provenance
 -> related person/team/event/world
```

or:

```text
word surface
 -> lemma
 -> sense
 -> ILI/synset
 -> frame/class/roleset
 -> role
 -> witnessed usage/example
 -> source
 -> another linked world
```

without changing engines. The domain hierarchy and the evidence/physicality/occurrence state are all views over one machine.

## 8. D&D, board, chess, and sports lineage is product/architecture evidence, not entertainment garnish

The pre-Hartonomous D&D work explicitly researched:

- persistent living world state;
- actors/NPCs/players/factions with independent goals;
- natural-language action -> rule adjudication -> state transition -> consequence;
- turns, initiative, scheduling, inventory, relationships, locations, quests, events, and historical state;
- central orchestration separated from primitive operations;
- reactive loops versus longer-horizon planning;
- player feedback/outcome-driven adaptation;
- world evolution even when a player is not directly observing it.

Later Laplace game work generalizes the same machine through exact canonical states, event/physicality trajectories, rules/firmware, typed search, effects, receipts, and observed consequences.

Sports/reference-site product thinking adds another dimension: a living world should be browseable through its natural resource taxonomy, records, rosters, schedules, results, standings, and history.

These lineages help explain why Laplace contains entity worlds, referential identity, occurrences, worlds/time, effects, firmware, OODA, receipts, standing, and domain realization. Treating those as unrelated late application features loses the causal invention history.

## 9. Current implementation damage must not be promoted into requirements

The following current behaviors are counterexamples unless independently reconciled to higher authority:

- a generic word-search/browser presented as the primary meaning of exploration;
- an admin/warehouse/source inspector presented as a product destination rather than an operator/support surface;
- generic entity cards used where a domain-specific resource page is required;
- hardcoded Highway layer inventory defining what the machine is capable of understanding;
- static prose about current bugs/missing API reads embedded as semantic product description;
- zero-count tables substituting for typed capability/absence explanations;
- raw ids/types/coordinates occupying the primary visual hierarchy for ordinary users;
- Graph/Glome visualizations used as the primary navigation model instead of supporting lenses;
- one current installation's admitted data treated as the boundary of what the product is;
- one source/schema/table/view determining domain ontology;
- implementation-specific sports metaphors applied cosmetically rather than materializing real league/team/player/game worlds from substrate state.

## 10. Acceptance consequence

A corrected product-surface acceptance suite must prove more than route availability. At minimum it must include materially different world fixtures and demonstrate that each is generated by the same semantic machine while retaining natural domain navigation.

Representative acceptance families should include:

- language/reference hierarchy;
- books/documents and exact structural containment;
- chess player/game/position/line world;
- sports league/division/team/player/game/stat world;
- D&D/board-game persistent world with actors, rules, inventory/state, events, actions, and consequences;
- person/project/repository/activity professional world;
- source/provenance/operator views;
- cross-world navigation between canonical/referential entities where evidence permits.

Each fixture should prove:

- stable resource URLs/identity;
- natural hierarchy/roster/member navigation;
- event/schedule/result/history where the domain has it;
- exact underlying canonical structure and occurrence/evidence traceability;
- audience/world/time/epoch scoping;
- typed absence and `WHY_NOT` behavior;
- supporting Graph/Glome/trajectory views without requiring them for ordinary navigation;
- parity across API/web/mobile/document realizations where applicable;
- no private domain engine replacing the common ISA.

## Rule for future agents

**Do not learn Laplace product intent by staring at the current UI.**

Use current UI/code only to answer: "what does this implementation currently do?" Reconcile that behavior against inventor-direct product law and pinned product-surface lineage before treating it as a requirement. If the current page is an admin/debug shell where the invention requires a navigable world/resource product, the page is defect evidence.
