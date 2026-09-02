# Old-iteration live product counterexamples — 2026-09-02

Status: observed historical-product evidence and clean-product acceptance input. This document preserves new live observations **without replacing, deleting, or narrowing** the prior architecture, issues, audits, requirements, or implementation work. The old `SaltyPatron/Laplace` repository remains behavioral evidence/counterexample only; its implementation is not clean-product authority.

## Why this record exists

A live old-iteration session on 2026-09-02 exposed several defects at once:

1. the UCI/chess path still does not execute the full Laplace cognition/chess program that the invention requires;
2. Chess Lab transcript content can exist while the containing product panel collapses to a title bar;
3. player standing/rating state is visibly corrupted at a scale that changes roster ordering, not merely formatting;
4. glome/trajectory visuals still fail to communicate genuine depth and the point markers render effectively black/invisible in the observed browser;
5. the new clarification that `physicality.coord`, packed `physicality.trajectory`, and the realized coordinate curve are different structural objects must remain wired into every visualization and metric path.

These observations are additive evidence for existing owners such as old-repo #833 and clean-repo #110/#136/#139/#168/#21. They do not supersede those issues.

## 1. Chess gauntlet: current old UCI is still the truncated machine

The live Chess Lab gauntlet showed a 100-game configuration at search depth 6 with the Stockfish-strength limiter enabled and the UI displaying an Elo cap of 2850. After six completed games, the visible score was:

```text
Laplace wins: 1
Draws:        0
Losses:       5
Game 7:       in progress
```

This is **not a final benchmark result** and must not be promoted to one. It is useful behavioral evidence that the currently deployed old UCI path is not yet demonstrating the invention's intended advantage.

Old-repo #833 already states the architectural reason: the historical UCI path is a truncated forward pass in which classical search remains `PROPOSE` and substrate influence is mostly a thin/root-level `STEER`. The observed loss therefore cannot be used as evidence that the full Laplace Chess Forward Pass has been tested and lost; it is evidence that the full program is still absent from the old execution path.

Clean-product consequences:

- #136 must prove the full cross-modal Chess Forward Pass through the common ISA rather than reproduce the old root-bias path;
- #139 must preserve Cute Chess/Stockfish as the neutral ruler while distinguishing `old truncated UCI`, each ablation rung, and the complete selected program;
- benchmark receipts must identify which providers/operators actually executed at descendant search states;
- a scoreboard is insufficient unless the executing program, provider set, world/evidence epoch, Stockfish generation/profile, Cute Chess recipe, openings, resource policy, and artifacts are bound in the receipt.

## 2. Chess Lab transcript container collapses despite live transcript support

The live Lab page showed the `Transcript` panel reduced to a thin title/header bar between the game result area and run history. This is inconsistent with the transcript component's intended product behavior.

Current old-repo source already contains:

- a full `Terminal` transcript component with filters, search, live SSE, scrollback, stick-to-bottom behavior, and explicit loss/filtered-line accounting;
- `.terminal { min-height: 22rem; max-height: 34rem; }` in the gauntlet view;
- `.scroll { min-height: 12rem; overflow: auto; }` in the terminal stylesheet.

Therefore declaring minimum heights in leaf CSS is not sufficient acceptance. The actual composed `Panel -> body -> Terminal -> scroll` layout can still collapse at runtime because a parent/grid/flex/fill boundary does not honor the intended intrinsic size.

Required product law:

- reusable containers size from their content and declared layout contract rather than relying on a child-only height declaration;
- bounded scroll regions remain visible and usable when content exceeds the available viewport;
- panels may intentionally collapse only through an explicit user/product state, never accidentally because a parent has zero/min-content height;
- browser-level acceptance measures the resulting DOM box/layout, not merely the presence of CSS properties in source.

A transcript fixture should prove a nonempty live transcript produces a visible scroll region with a positive minimum viewport, and a long transcript remains inside the panel rather than expanding or collapsing the surrounding page.

## 3. Player standings: rating / μ / Elo scale is corrupt and ranking is affected

The live Player Database standings showed obviously non-Elo-scale values. Representative visible rows included:

| Player | Games | Conservative | Rating | RD |
|---|---:|---:|---:|---:|
| penguingm1 | 3,025 | 1,398,028,457 | 1,398,029,157 | ±350 |
| spicycaterpillar | 760 | 202,608,330 | 202,609,030 | ±350 |
| Genghis_K | 408 | 202,531,346 | 202,532,046 | ±350 |
| wonderfultime | 5,340 | 156,673,642 | 156,674,342 | ±350 |
| Hikaru | 2,280 | 44,781,474 | 44,782,174 | ±350 |

The `Conservative = Rating - 2*RD` relationship is visibly preserved for these rows, so the UI is not simply printing the wrong column. More importantly, old-repo `chess.ranked(...)` orders the roster using the raw consensus `rating` or `consensus.eff_mu(rating, rd)` before converting values for display. Thus corrupt standing state changes rankings/lists and is not merely a presentation defect.

The SQL display path divides raw fixed-point rating/RD by `1e9`; seeing billion-scale displayed ratings implies the stored/raw OUTCOME lane has already grown to an implausible magnitude. The exact first corrupt event and numerical mechanism still require a receipted replay; do not guess the root cause from the screenshot alone.

Required clean-product acceptance under #110:

- canonical units for rating, RD, volatility, score mappings, and display conversion are part of the typed recipe/ABI;
- every transition checks finite/range/domain invariants before publication;
- display conversion and server-side sorting consume the same declared typed unit, without one path double-scaling or failing to scale;
- a roster fixture with known prior states and outcomes has independently calculated expected ordering;
- extreme/multi-period replay cannot silently explode magnitude while retaining superficially valid RD;
- a scale/range violation produces a typed fault/WHY_NOT and cannot enter authoritative ranking;
- historical corrupt rows remain diagnostic evidence; they are not normalized away to make the UI plausible.

## 4. Glome/trajectory visualization: depth and point rendering remain defective

The live entity glome view now correctly labels two distinct structural panes:

```text
Packed
  Identity packed as XYZ · M / RLE paint
  Hash-space trajectory on a display shell. Not S3 placement.

Placement
  Live centroid + realized path (entity_curve)
  Glome ball — radius is coherence. Ribbon = realized child coords.
```

That distinction is important and must be preserved. It reflects the inventor-direct architecture clarification:

```text
physicality.coord
    = real four-component structural coordinate

physicality.trajectory
    = exact mantissa-packed BLAKE3/address + ordinal/RLE/metadata manifest

realized curve
    = trajectory IDs resolved to child physicality.coord in order
```

However, in the observed browser:

- the visible trajectories still read largely as a flat/planar drawing rather than a clearly depth-bearing 3D projection of the selected structural state;
- node spheres/balls are effectively black or invisible, even though the source attempts to assign instance colors;
- therefore source-level `setColorAt(...)`, lights, and a nominal 3D canvas do not prove the shipped visual result.

Current old-repo placement code performs one orthographic `X-M` rotation then emits `[rotatedX, y, z]`; packed display normalizes hash-space XYZ to a shell. Those are legitimate view recipes only when explicitly identified as such. They cannot be accepted merely because Three.js is used.

Required visual acceptance:

- a known non-coplanar 4D fixture must project to visibly non-coplanar 3D positions under at least one declared rotation/view;
- changing the 4D rotation must visibly move points whose hidden component differs while preserving exact source `coord` state;
- packed hash/address display and real-placement display remain visually and semantically distinct;
- rendered node markers have measurable non-background/non-black color for known colored fixtures;
- line/ribbon depth, occlusion, camera orbit, and node depth must respond consistently to a non-coplanar fixture;
- a screenshot/pixel or WebGL readback acceptance test must catch black/invisible instance-color regressions;
- no display projection writes back to `physicality.coord`, trajectory payload, Hilbert state, identity, or semantic standing.

Borsuk-Ulam remains a constraint on continuous `S3 -> R3` projection of the real Tier-0 structural manifold. It is not a constraint on the discrete exact BLAKE3 mantissa trajectory packer. This distinction must remain explicit in UI code and documentation.

## 5. Preservation / no-regression rule

This audit adds evidence; it does not replace prior work.

The following existing work remains authoritative within its declared boundary:

- old #833 and related chess-forward-pass issues for the known truncated UCI path;
- clean #110 for typed standing/default-once/return-leg/epoch semantics;
- clean #136 for the complete cross-modal chess proving slice;
- clean #139 for Stockfish/Cute Chess generation and cumulative ablation;
- clean #168 and `PHYSICALITY_COORD_TRAJECTORY_REALIZATION.md` for structural geometry, packed trajectory, realized curves, Borsuk-Ulam, and metric-provider separation;
- clean #21 for reusable product surfaces rather than route-private UI semantics;
- all prior audits, requirements, contracts, tests, and implementation evidence on PR #128.

Any repair must be additive or a deliberate correction with exact provenance. A later implementation must not make these defects disappear by deleting the historical evidence, collapsing units into a new untyped scalar, removing transcript functionality, flattening packed/placement distinction, or replacing the full Chess Forward Pass with a stronger conventional chess engine.
