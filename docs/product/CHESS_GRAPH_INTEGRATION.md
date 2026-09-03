# Chess graph integration — one universal entity world

Chess must not be implemented as a domain island that hands opaque chess features to the common cognition layer. PGNs, player identities, profiles, games, lines, positions, moves, books, lexical concepts, classical calculations, and external identifiers all join the same canonical substrate and therefore the same generic entity-world/search machinery.

Historical behavioral evidence: `SaltyPatron/Laplace` player/game/Explore surfaces plus #491, #574, #833, #840, #1398, #1401, #1404, #1424, #1430.

Clean owners: #7, #16, #17, #18, #53, #60, #68, #132, #136, #139, #164, #184.

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
└─ shared canonical position/move/transition/segment content
     ├─ observed by other PGNs/playings
     ├─ grounded by books/openings
     ├─ calculated structural planes
     ├─ external evaluator calculations
     └─ exact tablebase/catalog facts where applicable
```

No source/game/provider salt may be added to reusable position, move, transition or segment content solely to preserve provenance.

## LINE versus PLAYING versus EVENT

- **LINE/game-content**: ordered reusable chess content;
- **PLAYING**: one source/event occurrence of that line, with players/result/time/provenance;
- **EVENT**: tournament/session/named event that may contain many playings.

```text
Player A -> Playing 1 -> LINE L <- Playing 2 <- Player B
```

The same canonical position/move/transition/segment can recur across many unrelated lines and sources while retaining exact occurrence paths.

## Exact transition-segment identity and folding

Position equality and exact line equality are different queries. An observed ordered transition segment must be preservable and matchable as the exact segment rather than silently reduced to an endpoint position.

```text
P7 --M7--> P8 --M8--> P9 --M9--> P10
        │
        └── exact reusable segment S
```

`S` is canonical ordered content under the declared chess trajectory recipe. It is not owned by a player, PGN file, opening name, tournament, or SAN notation. Those are occurrence/reference/testimony/realization state around it.

```text
segment S
  ├─ contained by Playing A
  │    ├─ White -> Player A1
  │    ├─ Black -> Player A2
  │    ├─ Event/Date/Result/Source -> ...
  │    └─ LINE -> full exact trajectory
  ├─ contained by Playing B
  ├─ grounded by Opening/Book occurrence
  └─ referenced by deterministic calculations
```

Required distinction:

```text
find playings containing position P9
!=
find playings containing exact segment S
```

The latter must retain the exact matched subpath/ordinal anchor.

### Fold and expand

A larger line may reuse canonicalized exact subtrajectories rather than copying every transition into a new opaque game blob:

```text
LINE X = [ S_opening, S_historical, T_new, ... ]
```

Recursive expansion must reproduce the exact declared board-transition history:

```text
P0 -> M0 -> P1 -> ... -> Pn
```

Requirements:

- folding may reuse exact segments/subpaths and run/prefix structure before materialization;
- expansion is lossless for the declared canonical chess content;
- a novel transition or segment admitted by one game becomes reusable by later games;
- transpositions/convergent positions may share the endpoint while distinct incoming paths and occurrences remain independently reconstructable;
- occurrence multiplicity may affect evidence/standing under a declared lane but may not cause repeated canonical segment construction merely because many games contain it.

This is an instance of the universal Merkle-DAG reuse law, not a chess-only compression feature.

## Historical segment navigation

A matched historical segment is navigable world state, not merely trivia rendered as text.

```text
OPEN_PLAYING(
    witnessed_playing_id,
    anchor = matched_segment_id
)
```

`OPEN_PLAYING` is illustrative vocabulary. The semantic operation must compile through generic cognition/search, entity-world and realization/effect contracts. A UI callback with private chess semantics does not satisfy it.

The live game and historical playing remain separate contexts. Historical navigation must not mutate the live game. The same generic operation class applies to opening a document at a matched occurrence, inspecting a model circuit in another checkpoint, or reopening a prior execution receipt.

## Player names, handles, profiles, and external IDs

A human-facing name is not canonical identity by itself and an online handle is not automatically the same referent as a human name.

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
- conflicting profile mappings remain inspectable instead of silently fused.

## Game evidence connects players to moves and positions

```text
canonical position P
  -> candidate move/transition M
       -> occurrence/evidence context Playing G
            -> white/black player
            -> result
            -> event/date/time/rating/source
            -> LINE
```

Questions such as these are projections over shared canonical content plus selected occurrence/testimony state:

- `what did player X play here?`
- `which players reached P?`
- `how did rating-band Y perform after M?`
- `what did X play against opponent Z?`
- `who played this exact transition segment?`
- `what is the earliest admitted witness of this exact continuation?`
- `how many distinct move orders reached this canonical position?`

## Head-to-head and opponent world

Repeated games between two players may fold into typed player-to-opponent standing while preserving each individual playing as provenance/evidence.

```text
Player A
 -> opponent Player B
 -> their playings/LINEs
 -> positions/moves/segments/openings/motifs
 -> other playings containing those positions/moves/segments
 -> other players/profiles/sources
```

This is generic entity-world traversal, not a chess-only social graph.

## Grandmaster books join the same world

A book contributes ordinary document physicality whether or not chess-specific grounding succeeds. Grounded variations/diagrams/notation converge on the same LINE/position/move/segment content as PGNs when the chess content is equal.

```text
text/document occurrence
lexical/sense state
attributed expert testimony
chess variation grounding
shared LINE / position / move / segment references
```

This is the bridge by which words such as `fork` and `gambit` can connect lexical sources, book prose, exact board calculations, observed PGN trajectories, players, and later outcomes through one program.

## Classical calculations join the graph without becoming occurrences

Material, PST/PeSTO, rook-file, pawn-structure, motif, Stockfish, Syzygy, and other calculated planes refer to the same canonical board/move state while retaining their own calculation/provider identity.

```text
canonical position/move/transition
+ provider generation
+ recipe/budget
-> calculated result
```

PGN recurrence may increase observed game evidence but cannot multiply one deterministic evaluator opinion. #164 owns the generic calculation identity/dedup contract.

## Generic entity-world materialization

#68 must be able to materialize, under a declared bounded query:

```text
names / aliases / handles
external/provider identities
ratings/title/federation
opponents
playings
events/results
LINEs
positions/moves/segments
openings/ECO/motifs
books/explanations
calculated metrics
sources/provenance/standing
```

Re-centering any node continues through generic search and realization. A richly witnessed player world and a non-chess entity world use the same materialization/query/receipt contract.

## Realization law

```text
canonical id
-> inspect typed entity/structure
-> requested language/domain/context/notation
-> eligible name/alias/structural realization evidence
-> display/output realization
```

Representative chess realizations include:

```text
piece              -> Queen
piece-square/state -> Qd1
move/action         -> Qd1-a4+
position            -> board / FEN-compatible presentation
transition          -> move plus resulting board state
segment             -> variation/move sequence/named line when evidence permits
playing occurrence  -> players + event/date/result/source presentation
```

SAN, PGN, FEN-compatible strings, opening names and UI labels are consumer realizations/serializations. They do not own canonical identity. A typed chess object is not semantically “unrealized” merely because no precomputed display string is stored if the selected realization recipe can derive the requested representation from canonical structure and context.

## Acceptance

- [ ] Equivalent canonical chess content from unrelated PGNs/books/self-play converges while occurrences remain distinct.
- [ ] LINE, PLAYING, and EVENT grain are independently reconstructable and not conflated.
- [ ] Exact repeated transition segments receive reusable canonical identity and are not copied once per PLAYING.
- [ ] Exact-segment lookup and position-only lookup are independently testable.
- [ ] Folding reusable segments plus a novel remainder and recursively expanding them reproduces the exact declared game history.
- [ ] A segment first admitted by one game can be reused by a later game without reminting it or losing either occurrence path.
- [ ] Transposed/convergent positions share canonical endpoint state while distinct paths remain reconstructable.
- [ ] A matched segment can navigate to containing historical PLAYING occurrences through the generic entity-world/realization path without changing live-game context.
- [ ] Player names/aliases/provider handles/external IDs remain distinct typed state and only explicit identity evidence can bridge provider identities.
- [ ] Player-conditioned move/position/segment queries traverse shared occurrence context instead of using player-specific move copies.
- [ ] Grounded book variations converge with equal PGN lines/positions/moves/segments.
- [ ] External/classical calculations attach to shared canonical chess state and remain distinct from game occurrence/testimony.
- [ ] One deterministic evaluator result is not amplified by the number of containing PGNs.
- [ ] Hash-only and human-realized views have byte-identical canonical node/edge ids.
- [ ] Changing SAN/PGN/opening/display realization changes no canonical chess identity.

## Deliberate defects

Reject:

- one opaque PGN/chess feature blob handed to cognition;
- source-salted position/move/segment identity;
- position-only historical matching presented as exact-segment matching;
- endpoint-only path dedup that erases distinct incoming trajectories;
- canonical opening/segment content copied once per PLAYING occurrence;
- folded-segment expansion that cannot reproduce exact transition history;
- provider handle/name fuzzy merge presented as identity;
- player-specific copies of canonical moves;
- book-only vector/RAG world disconnected from canonical chess state;
- Stockfish result copied once per containing LINE/game;
- SAN/PGN/FEN/opening/display strings participating in canonical identity;
- UI-private historical navigation that cannot be reproduced through native/PostgreSQL/API/CLI/entity-world semantics;
- opening a historical playing by mutating/replacing the caller’s live game context;
- display label participating in canonical graph identity.
