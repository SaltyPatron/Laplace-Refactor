# Chess referential identity audit

Observed read-only: 2026-08-24

## Scope and evidence boundary

This audit uses a repeatable-read transaction against the populated live `laplace`
database. The query is
[`tools/audit/inspect-chess-referential-identity.sql`](../../tools/audit/inspect-chess-referential-identity.sql).
Its five-row CSV receipt is preserved outside the clean source tree at:

```text
/home/ahart/Projects/Laplace-archive-2026-08-24/preservation/
live-chess-referential-identity-observation-2026-08-24.csv
```

The receipt is 8,899 bytes with SHA-256
`1c68f2106293564e869dcb23e0567abdbd8ba9f6039a4478e7007bf782025d95`.
It names PostgreSQL 18.3, a repeatable-read snapshot, transaction timestamps, the
function contracts and their definition digests, every tested name input, and a
bounded game observation. The database continued to receive chess data outside this
snapshot. The receipt is an observation of that exact snapshot, not a timeless count
or a performance result.

Two external FIDE surfaces provide independent referential evidence:

- FIDE's [Robert Fischer biography](https://museum.fide.com/champions/robert-bobby-fischer)
  identifies Robert James Fischer as 1943–2008 and describes his 1972 and 1992
  matches against Boris Spassky.
- FIDE's [2024 Virginia Senior Open report](https://ratings.fide.com/report.phtml?event=358011)
  identifies a contemporary `Fischer, Robert J`, FIDE ID `2008653`, federation USA,
  with rating 1890 in that event.

Those sources establish that at least two people occupy this name family. They do not
by themselves assign every PGN row in the live database to one of those people.

## Measured live behavior

The current name-key surface returns these identities:

| Input | Returned player ID |
| --- | --- |
| `Fischer, Robert J` | `8e51f1bd3d6645beb0c2b5b8c2a83241` |
| `Robert J Fischer` | `8e51f1bd3d6645beb0c2b5b8c2a83241` |
| `Robert James Fischer` | `ae759775449b48c6db104ed9e09708b2` |
| `Bobby Fischer` | `bd0ec892e354d4c0d69beea7d38bfdf1` |

For `8e51f1bd3d6645beb0c2b5b8c2a83241`, the same snapshot returned:

- 1,020 games across parsed years 1950 through 2024;
- 927 games through 1972, 88 from 1973 through 2008, and five after 2008;
- 1972 World Championship games against Spassky;
- 1992 Spassky match games;
- Virginia events in 1997, 1998, and 2000;
- five games from the 2024 Colonial Open;
- ratings including 2785 for six games and 1857 for five games.

The evidence therefore proves a current product defect:

> A normalized name-key identity is being used as a person referent even though the
> observed occurrences and independent FIDE evidence require at least two people.

The evidence does **not** yet prove the complete row partition. Some rows may involve
additional people, malformed dates, copied games, incorrect PGN tags, or source
errors. The resolver must preserve those possibilities and test them.

## Layer-correct interpretation

`Fischer, Robert J` is exact name content. Its content identity is valid and singular.
Repeated occurrences of the same tag should continue to reuse that content entity.
Name equality, however, is not person equality.

Each PGN tag is an occurrence of the name physicality inside a particular game and
source container. It can support a candidate `refers-to` proposition. Stable external
identifiers, birth and death intervals, federation, rating history, event, location,
opponent network, title, and aliases are additional testimony about candidate
referents. None enters the name content hash.

The exact PGN container still calculates its players' tag positions, event tag,
date tag, color role, game containment, and move trajectory without testimony.
Whether a tag refers to Bobby Fischer, FIDE player `2008653`, another person, or an
unresolved referent is epistemic state layered over those exact occurrences.

## Required resolution flow

The accepted product flow is:

```text
exact name content
  -> source-bound name occurrences
  -> candidate referents
  -> identity testimony and dependence roots
  -> temporal / event / rating / opponent compatibility defects
  -> candidate merge or split hypotheses
  -> held-out and external corroboration
  -> immutable referential adjudication epoch
```

A 2024 game provisionally assigned to a person whose independently supported life
interval ended in 2008 creates a temporal compatibility defect against that merge.
The game remains present. The death testimony remains present. The current merge
remains historically queryable. The system generates a split hypothesis with exact
lineage and seeks distinguishing testimony. The hypothesis is not an observation and
cannot certify itself.

If a later epoch supports two referents, every source occurrence receives an explicit
assignment, candidate set, or unresolved status. The system may not silently distribute
rows by a hard year threshold. Partial dates, uncertain dates, exhibitions, copied
records, mistaken tags, and contradictory sources remain representable.

## Stable external identifiers

FIDE and USCF identifiers are high-value testimony because they are designed to
distinguish people across name variants. They remain source-scoped content and
attestations rather than universal identity hashes. The resolver must account for
missing IDs, source mistakes, duplicated IDs, identifier reassignment, federation
transfers, and effective dates.

Official FIDE downloads can provide monthly identity and rating observations, while
tournament reports provide event-scoped links among player ID, date, rating, and
opponents. Source terms and redistribution rights are recorded before any corpus is
deposited. Public download availability alone does not establish redistribution
permission.

## Deliberate-defect acceptance cases

The following implementations must fail:

1. Merge two people solely because their normalized names match.
2. Merge aliases solely because one cluster contains a famous peak rating.
3. Delete or rewrite a contradictory game to make the current person merge coherent.
4. Assign a post-death occurrence without first resolving the referent.
5. Split one person into several people solely because ratings, federation, or name
   spelling changed.
6. Treat an external player ID as part of the name content hash.
7. Count copied databases carrying one PGN as independent corroboration.
8. Convert a generated split hypothesis into an observed person or an independent
   witness.
9. Rewrite an earlier referential consensus epoch after later identity evidence.
10. Force partial or unknown dates through a total temporal ordering.

The positive fixture requires exact preservation of all original content,
physicalities, occurrences, testimony, derivation lineage, and prior epochs while a
new epoch resolves or explicitly leaves unresolved each occurrence.

## Product finding

This defect is not evidence against content addressing. It demonstrates why content
identity and referential identity must remain separate. The current chess surface
correctly gives the same normalized name content the same key; the product failure is
using that key as though it were sufficient evidence of one human being.

The Fischer corpus is therefore a complete-chain acceptance fixture for identity
resolution, temporal contradiction, typed fray detection, split hypothesis generation,
source dependence, held-out corroboration, and epoch-preserving re-adjudication.
