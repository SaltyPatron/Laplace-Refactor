# Name Game performance proof — identity, realization, event physicality and deterministic race

This is a clean-product proving fixture for `#138` Knowledge Arena. It is not a separate game engine
and not an exhaustive product boundary. The named Arena games are examples of how different products
can compile to the same identity, search, realization, event, effect and witness machinery.

A later brainstorm normally adds another proving surface or requirement. It does not silently replace
prior accepted obligations unless an explicit supersession decision says so.

## Core race

A simple understandable challenge is:

```text
Bobby Fischer
      F
Frank Sinatra
      S
Sandy Koufax
      K
...
```

The ordinary rule is:

```text
family-name initial(current) == given-name initial(next)
```

The eligible world may include real people, fictional characters or another declared entity class.
Eligibility is part of the pinned challenge/world scope; it is not one global famousness score.

## The benchmark executes the common machine

A submitted answer must not be validated by a private string parser. The logical path is:

```text
submitted Unicode content
-> common RESOLVE
-> selected identity/reference evidence
-> common REALIZE into structured playable name roles
-> validate current firmware endpoint/orientation
-> check canonical entity against event no-repeat state
-> append accepted event occurrence / reject with typed why-not
-> update next required letter/orientation
```

When Laplace itself chooses the next answer, candidate search/selection also uses the common
`#17/#60/#132` cognition/search path.

## Structured names, not whitespace order

Visible name order cannot define the playable semantic endpoints.

Representative fixture:

```text
entity: Itachi Uchiha

given_name:  Itachi
family_name: Uchiha
```

The same entity may be realized as `Itachi Uchiha` or `Uchiha Itachi` under a selected cultural or
language presentation policy. Its naming roles do not swap merely because the display order changes.

Therefore a naive cheese such as:

```text
Uchiha Itachi
Uchiha Sasuke
```

must not pass simply because both display strings begin with `Uchiha`.

Likewise `Itachi Uchiha` and `Uchiha Itachi` cannot count as two answers when identity evidence says
they are the same referent. No-repeat is checked on canonical entity identity.

## Unicode endpoint contract

Initials derive from the governed structured endpoint under the challenge's Unicode/grapheme policy,
never raw byte indexing. The turn receipt retains the submitted text, canonical entity, selected
realization, role endpoints, normalized initials, language/realization policy and validation result.

## Adversarial realization fixtures

The clean implementation must handle or explicitly abstain on names whose structure defeats token
position heuristics:

- mononyms such as `Cher`;
- terminal identifier names such as `Malcolm X`;
- character names such as `Howard the Duck`, `Dr. Doom`, `Darkwing Duck`;
- particles such as `van`, `de`, `da`, `bin`;
- suffixes/titles such as `Jr.` or `Pope`;
- aliases/handles/nicknames;
- transliterations and culturally different name order;
- forms such as `Monkey D. Luffy`.

Strict two-endpoint mode should fail closed when two playable roles cannot be established. Another
versioned game recipe may explicitly admit mononyms or other structures.

## Doubles / orientation state

The current design candidate treats equal endpoint initials as a **double**:

```text
Dom DeLuise
Donny Darko
Darkwing Duck
```

A double can reverse the active endpoint direction:

```text
normal:  FAMILY(current) -> GIVEN(next)
reverse: GIVEN(current)  -> FAMILY(next)
```

The exact flip/reflip rule is versioned firmware. It is not universal identity law and may evolve
without reminting entities.

## Event trajectory and no-repeat

Accepted play is another physicality trajectory:

```text
Bobby Fischer -> Frank Sinatra -> Sandy Koufax -> ...
```

A self-avoiding mode requires the next canonical entity not already occur in this event. Alternate
spellings, aliases, handles or reversed display order cannot bypass it.

The ordered event is not redundantly materialized as permanent PRECEDES testimony merely to remember
the game sequence.

## Race forms

### Alternating duel

Human and Laplace alternate on one shared trajectory. Each answer creates the opponent's next state.
This permits adversarial frontier strategy: choose a valid entity whose terminal initial leaves few
legal answers.

### Parallel sprint

Both contestants receive byte-equivalent starting state/rules/world and independently build chains
under one server clock.

### Frontier/trap strategy

The objective may include preserving one's own future options while reducing the opponent's next
eligible frontier. Difficulty comes from search/selection policy, not artificial sleep.

## Performance receipt

For every turn retain at least:

```text
contestant
server receive/commit time
resolve duration
identity/realization duration
validation duration
search/selection duration when applicable
canonical entity/result
required initial + orientation before/after
accepted/rejected why-not
frontier/work metrics where measured
```

A match can report valid/invalid answers, median/p95 latency, total wall time, candidates examined,
cheese attempts rejected, rare letters, frontier reductions, double chains and related explainable
facts.

This interactive proof complements rather than replaces lower-level composition/database/energy/
throughput benchmarks.

## Ranked fairness

A ranked generation pins at least:

```text
world/evidence epoch
eligible entity/type/source boundary
start entity or challenge queue
language/name-realization policy
endpoint orientation law
double behavior
no-repeat law
clock/scoring law
server event-order law
```

A later ingest or identity correction creates a new challenge generation. Practice may use live
state; ranked results remain replayable.

## Acceptance

- [ ] `Bobby Fischer -> Frank Sinatra -> Sandy Koufax` proves the ordinary endpoint rule.
- [ ] display token order does not define given/family roles.
- [ ] `Uchiha Itachi` and `Itachi Uchiha` converge when identity evidence selects one referent.
- [ ] `Uchiha Itachi -> Uchiha Sasuke` cannot exploit a first-token parser.
- [ ] alternate rendering/alias of an already-played entity fails a no-repeat fixture.
- [ ] strict two-endpoint mode abstains on `Cher` instead of inventing a surname.
- [ ] `Malcolm X` follows the declared endpoint recipe.
- [ ] particles/titles/suffixes/fictional names are driven by structured realization or typed abstention.
- [ ] Unicode initials obey the declared grapheme/codepoint policy.
- [ ] double orientation changes are firmware state and do not alter canonical identity.
- [ ] alternating and parallel modes use authoritative server timing.
- [ ] a strategic human can legitimately win despite higher average latency by creating harder
      opponent frontiers; receipts explain the result.
- [ ] event ordering is physicality/occurrence state, not invented permanent sequence testimony.
- [ ] web/API/CLI validation routes reproduce the same common identity/realization/search semantics.

## Deliberate defects

Reject whitespace first/last token identity, ASCII-only initials, alias-as-new-piece, name-order-as-
new-piece, client clock authority, private celebrity database, hardcoded `Uchiha`/particle/suffix
exception trees, stochastic ranked state, artificial sleep as difficulty, and any game-private search
or realization engine.

## Non-exhaustive proving-surface law

Knowledge Golf, Highway Race, relays, Collision/CTF, Constraint Crossing, Witness Hunt, Graphle,
COMBINE, chess-map hybrids, Choose Your Own Adventure and The Name Game are representative tests of
one machine. Their purpose is to falsify/reveal capabilities of reusable primitives, not to define a
finite catalog of everything Laplace may do.