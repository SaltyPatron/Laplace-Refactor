# Chess graph integration — one universal entity world

Chess must not be implemented as a domain island that hands opaque chess features to the common cognition layer. The clean product must preserve the old implementation's useful behavioral discovery: PGNs, player identities, profiles, games, lines, positions, moves, books, lexical concepts, classical calculations, and external identifiers all join the same canonical substrate and therefore the same generic entity-world/search machinery.

Historical behavioral evidence: `SaltyPatron/Laplace` player/game/Explore surfaces plus #491, #574, #833, #840, #1398, #1401, #1404, #1424, #1430.

Clean owners: #7, #16, #17, #18, #53, #60, #68, #132, #136, #139, #164.

## Governing law

```text
same canonical content = same identity
identity != occurrence
identity != external reference
identity != alias / realization
identity != testimony
identity != deterministic calculation
```

Chess-specific adapters may parse/ground chess structures, but they may not create a private graph/search/identity model beside the common substrate.

## PGN admission must produce a connected world

A representative admitted game should expose at least these distinct state classes:

```text
PGN/source artifact
│
├─ Chess_Playing occurrence
│    ├─ player roles (white/black)
│    ├─ event/date/site/round/time-control/result/rating context
│    └─ references one shared ordered LINE
│
├─ Chess_Game / LINE content
│    └─ ordered physicality trajectory
│         P0 -> M0 -> P1 -> M1 -> ... -> Pn
│
├─ Chess_Player canonical/profile identities
│    ├─ name/alias realization evidence
│    ├─ ratings/title/federation/profile facts
│    ├─ explicit cross-provider identity testimony
│    └─ opponent/head-to-head state
│
└─ shared canonical position/move/transition content
     ├─ observed by other PGNs/playings
     ├─ grounded by books/openings
     ├─ calculated structural planes
     ├─ external evaluator calculations
     └─ exact tablebase/catalog facts where applicable
```

No source/game/provider salt may be added to a reusable position/move solely to preserve provenance.

## LINE versus PLAYING versus EVENT

The clean model must preserve the useful grain distinction:

- LINE/game-content: the ordered reusable chess content;
- PLAYING: one source/event occurrence of that line, with players/result/time/provenance;
- EVENT: a tournament/session/named event that may contain many playings.

This permits:

```text
Player A -> Playing 1 -> LINE L <- Playing 2 <- Player B
```

without turning the two source occurrences into two copies of LINE L.

It also permits the same canonical position/move to recur across many unrelated lines and sources while retaining exact occurrence paths.

## Player names, handles, profiles, and external IDs

A human-facing name is not canonical identity by itself and an online handle is not automatically the same referent as a human name.

The clean system must support distinct but connectable state such as:

```text
canonical/reference-governed player identity
  ├─ alias/name realization evidence
  ├─ provider profile identity/handle
  ├─ external identifier (for example a FIDE id)
  ├─ title/federation/rating/profile testimony
  └─ explicit CORRESPONDS_TO/equivalence testimony when warranted
```

Requirements:

- common spelling/order normalization may resolve demonstrably equivalent renderings under a declared identity recipe;
- fuzzy/name similarity alone cannot assert cross-provider identity;
- external identifiers remain typed references/evidence rather than arbitrary salt in canonical human content;
- conflicting profile mappings remain inspectable instead of being silently fused.

## Game evidence connects players to moves and positions

The common search program must be able to answer player-conditioned questions by traversing occurrence context rather than minting player-specific copies of moves.

Logical shape:

```text
canonical position P
  -> candidate move/transition M
       -> occurrence/evidence context Playing G
            -> white/black player
            -> result
            -> event/date/time/rating/source
            -> LINE
```

Therefore:

- `what did player X play here?`
- `which players reached P?`
- `how did rating-band Y perform after M?`
- `what did X play against opponent Z?`

are projections over shared canonical content plus selected occurrence/testimony state.

## Head-to-head and opponent world

Repeated games between two players may fold into a typed player-to-opponent standing while preserving each individual playing as provenance/evidence.

The entity world should support traversal such as:

```text
Player A
 -> opponent Player B
 -> their playings/LINEs
 -> positions/moves/openings/motifs
 -> other playings containing those positions/moves
 -> other players/profiles/sources
```

This is a generic entity-world traversal, not a chess-only social graph.

## Grandmaster books join the same world

A book contributes ordinary document physicality whether or not chess-specific grounding succeeds.

Where a variation/diagram/notation is grounded, it must converge on the same LINE/position/move content as PGNs when the chess content is equal.

The same paragraph can therefore participate through separate state classes:

```text
text/document occurrence
lexical/sense state
attributed expert testimony
chess variation grounding
shared LINE / position / move references
```

This is the required bridge by which words such as `fork` and `gambit` can connect lexical sources, book prose, exact board calculations, observed PGN trajectories, players, and later outcomes through one program.

## Classical calculations join the graph without becoming occurrences

Material, PST/PeSTO, rook-file, pawn-structure, motif, Stockfish, Syzygy, and other calculated planes refer to the same canonical board/move state while retaining their own calculation/provider identity.

For a deterministic provider generation:

```text
canonical position/move/transition
+ provider generation
+ recipe/budget
-> calculated result
```

PGN recurrence may increase observed game evidence but cannot multiply one deterministic evaluator opinion. #164 owns the generic calculation identity/dedup contract.

## Generic entity-world materialization

#68 must be able to materialize a player world containing, according to a declared bounded query:

```text
names / aliases / handles
external/provider identities
ratings/title/federation
opponents
playings
events/results
LINEs
positions/moves
openings/ECO/motifs
books/explanations
calculated metrics
sources/provenance/standing
```

Re-centering any node must continue through the generic #17/#60 search and #18 realization path.

A richly witnessed player world and a non-chess entity world must use the same materialization/query/receipt contract.

## Realization law

A raw hash in the old graph is evidence that canonical identity exists even when human realization is incomplete.

The clean product must preserve:

```text
canonical id
-> requested language/domain/context
-> eligible name/alias/notation realization evidence
-> display label
```

Changing a label/alias/language cannot change node identity or topology.

## Acceptance

- [ ] Equivalent canonical chess content from unrelated PGNs/books/self-play converges while occurrences remain distinct.
- [ ] LINE, PLAYING, and EVENT grain are independently reconstructable and not conflated.
- [ ] Player names/aliases/provider handles/external IDs remain distinct typed state and only explicit identity evidence can bridge provider identities.
- [ ] Player-conditioned move/position queries traverse shared occurrence context instead of using player-specific move copies.
- [ ] Two players connected through games/opponent state can be traversed through the same generic entity-world contract.
- [ ] Grounded book variations converge with equal PGN lines/positions/moves.
- [ ] Lexical concepts such as `fork`/`gambit` can join book, board-calculation, game, player and outcome state through the common forward/search program.
- [ ] External/classical calculations attach to shared canonical chess state and remain distinct from game occurrence/testimony.
- [ ] One deterministic evaluator result is not amplified by the number of containing PGNs.
- [ ] Generic graph materialization can traverse player -> playing/LINE -> position/move -> other playing/player/profile/source without route-private chess semantics.
- [ ] Hash-only and human-realized views have byte-identical canonical node/edge ids.

## Deliberate defects

Reject:

- one opaque PGN/chess feature blob handed to cognition;
- source-salted position/move identity;
- provider handle/name fuzzy merge presented as identity;
- player-specific copies of canonical moves;
- book-only vector/RAG world disconnected from canonical chess state;
- Stockfish result copied once per containing LINE/game;
- UI-private traversal that cannot be reproduced through native/PostgreSQL/API/CLI semantics;
- display label participating in canonical graph identity.
