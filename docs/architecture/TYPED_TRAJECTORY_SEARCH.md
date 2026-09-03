# Typed trajectory search

## Status

This document binds existing Laplace invention laws into one executable search contract.
It does not replace the Constitution, Invention Model, or
`NATIVE_COGNITION_MATHEMATICS.md`. It exists because the implementation boundary was
previously distributed across identity, physicality, evidence, standing, and cognition
issues.

Primary implementation owner: #60 under #17.

## Core invariant

Laplace does not require a second universal graph in order to perform path finding.
The search program is generated from the existing layered substrate:

1. **Canonical content identity** — equal canonical content converges to one identity.
2. **Physicality trajectory** — exact order, containment, multiplicity, recurrence,
   gap/ordinal, placement, geometry, and sequence are calculated structural state.
3. **Occurrence** — records that a canonical realization appeared at a particular
   source/context/time without reminting the content.
4. **Attestation/testimony** — attributable semantic claims remain separate from exact
   structure.
5. **Consensus/standing** — typed earned state such as Glicko-2 may guide a query but
   never becomes universal truth, adjacency, or distance.
6. **Indexes/perfcaches** — generate bounded candidates and accelerate exact reads;
   they do not become canonical authority.

The resulting search is a compiler/executor over the substrate, not nearest-neighbor
lookup over a flattened embedding.

## Canonical identity versus path occurrence

A path may revisit one canonical object:

```text
A -> B -> C -> B -> D
```

`B` remains one identity. The trajectory records two visits.

Two unrelated trajectories may converge:

```text
Gamma_1 -> X
Gamma_2 -> X
```

`X` is shared while the two paths remain independently reconstructable.

This is load-bearing for transpositions, repeated text, game states, temporal series,
program/data-flow, mathematical transitions, audio/video sequences, and other domains.

## Physicality is the sequence carrier

Physicality is not an optional visualization layer. It is how Laplace preserves exact
structural path information while canonical content deduplicates.

A physicality trajectory can retain, according to its recipe:

- ordered constituent identities;
- ordinals and gaps;
- run length / multiplicity;
- repeated visits;
- geometric coordinates;
- packed exact payload channels;
- container/path identity;
- reconstruction information.

Bit-packed trajectory channels and their native decode are representation mechanisms
for exact path state. The packed form, live geometry, and semantic testimony remain
distinct typed classes.

## Merkle-DAG and reuse law

Same-content/same-hash convergence means repeated structure is not recomputed merely
because it occurs many times.

Search providers must exploit, where applicable:

- canonical subtree reuse;
- Merkle-DAG ancestor/descendant reuse;
- run-length reuse;
- repeated-constituent reuse;
- trajectory prefix/subpath reuse;
- convergent-state/transposition reuse;
- evidence/standing memoization at a closed epoch.

The intended `O(tier)` or bounded-by-selected-structural-altitude behavior is a
measurement obligation, not a slogan. Receipts must report work against unique
canonical structure, selected tiers, trajectory vertices, relation cells, and repeated
occurrence volume.

## Query-relative state space

A query compiles to a finite search program declaring at least:

```text
start state(s)
terminal predicate
admissible transition families
relation direction / arity / composition rules
world / evidence epoch / time / context / authority scope
source and dependence rules
hard filters
transition cost law
heuristic law
state identity and dominance law
reopen semantics
tie semantics
path multiplicity
CPU / memory / I/O / database / frontier limits
completion certificate
```

Different questions over the same canonical entities can therefore generate different
legal state spaces. Social, causal, containment, provenance, chess, code-dependency,
translation, and arbitrary-reachability paths are not one graph with different labels.

## Indexed frontier generation

Frontiers are generated set-wise from only the providers admitted by the current
program.

Candidate providers include:

- physicality containment / constituent / ordinal / recurrence / trajectory indexes;
- PostGIS/GiST and other declared geometry indexes;
- Hilbert/locality or other generated spatial keys;
- relation type / direction / endpoint indexes;
- source / context / world / valid-time / observation-time / evidence-epoch indexes;
- dependence-root indexes;
- typed standing/rating-key indexes;
- modality/highway masks and perfcaches as rebuildable accelerators.

Filters are pushed into candidate generation when semantically safe. Exact predicates
remain authoritative. An accelerator miss cannot prove absence unless its completeness
contract proves that claim.

## A-star contract

When Laplace claims A-star, it executes:

```text
f(s) = g(s) + h(s)
```

and must have:

- nonnegative transition costs for the claimed optimum;
- an admissible heuristic for the declared state space;
- consistency proof or explicit reopen semantics;
- complete typed state identity/dominance, not endpoint-only deduplication;
- deterministic tie rules;
- declared one/all/top-k path semantics;
- finite search/evidence/resource boundary;
- an optimality certificate inside that boundary.

If those conditions do not hold, the receipt names the actual bounded/best-first law
and returns reachability, known upper bound, partial, incomplete-boundary, exhausted,
or another exact disposition instead of falsely claiming shortest path.

## Path cost is not one global edge score

A query may consume independent channels such as:

- exact structural transition cost;
- semantic relation compatibility;
- trajectory/geometry distance or deviation;
- temporal/context mismatch;
- epistemic standing and uncertainty;
- contradiction/dependence penalties or hard exclusions;
- computation/I/O/resource cost;
- remaining completion obligations.

The query recipe defines how those channels become `g`, `h`, a partial order, or a
separate ranking law. Raw hop count, raw witness count, one global relation weight, or
one Glicko `mu` cannot substitute for the program.

## Receipts

Every path result retains enough information to reproduce and inspect the calculation:

- canonical start/goal and compiled query identity;
- world/evidence/time/context/authority boundaries;
- every typed transition and its structural/calculated/witnessed/derived origin;
- eligible evidence roots and standing epoch;
- frontier/index provider used;
- `g`, `h`, and tie/comparison values when applicable;
- pruned states and typed reasons;
- reopen events;
- terminal/optimality/upper-bound certificate;
- continuation condition for incomplete results.

A returned path can therefore be drilled through semantic relations, witnesses,
consensus/standing, and physicality trajectories instead of becoming an opaque score.

## Cross-domain proof

The same machinery must be exercised against unrelated domains:

- exact composition/containment paths;
- semantic/taxonomic paths;
- causal/temporal paths;
- game/state-transition trajectories;
- code/repository dependency paths;
- provenance/dependence paths.

A private chess pathfinder, private text graph walk, or UI-only graph traversal does
not satisfy the architecture.

## Implementation ownership

- #7 — canonical identity/composition/trajectory persistence
- #16 — testimony, lineage, adjudication, immutable evidence epochs
- #110 — typed earned standing
- #17 — guidance, indexed frontier generation, finite search
- #60 — executable connection/A-star boundary
