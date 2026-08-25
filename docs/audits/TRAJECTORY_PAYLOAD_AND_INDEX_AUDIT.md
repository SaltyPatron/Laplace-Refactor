# Trajectory payload and index audit

## Scope

This audit corrects a classification error made while examining three recovered
Git-unpublished source bodies. It distinguishes the trajectory carrier's binary width
from the semantic class carried in those bits, and binds that distinction to a bounded
read-only observation of the live database.

No historical source is implementation authority for the clean product. The recovered
files are evidence of continuity and of prior behavior only.

## Corrected model

The physicality trajectory is a typed payload channel carried in four finite binary64
slots per vertex. The packing pins the exponent and uses sign and mantissa bits as an
exact 212-bit payload:

- 128 bits available to a class-defined identity or value field;
- 16 bits for the packed ordinal field;
- 16 bits for the packed run field;
- 52 bits for class and metadata fields.

For composition physicalities, those fields encode constituent identity, ordinal, run
length, tier, atom, and structural metadata. A full non-deduplicated decoder reconstructs
the ordered occurrences. For factor physicalities, a declared factor class can allocate
the same carrier to exact float32 values. The host `Hash128` type used by one recovered
C# encoder is a 128-bit carrier in that context, not proof that the value is an entity
identity.

The same rule applies to the PostgreSQL geometry type: a packed vertex stored in a
`GeometryZM` column is not thereby a live spatial point. Realized curves resolve
composition identities to live physicality coordinates before curve mathematics.

## Structural calculation

A composition trajectory supplies exact structure without attestation rows:

- membership in the decoded path calculates `contains`;
- decoded positions and run spans calculate `precedes` and `follows`;
- two decoded identities in one scoped container calculate co-occurrence;
- repeated vertices and run spans calculate multiplicity and occurrence positions;
- recursive container-to-child paths calculate tier path and ancestry.

The database does not need every derived pair persisted. Its indexes generate bounded
candidates, after which the typed decoder verifies the exact relation.

## Live read-only observation

On 2026-08-25 UTC, a repeatable-read, read-only catalog and plan inspection observed:

- the partitioned `laplace.physicalities` surface has B-tree entity, type, first-child,
  Hilbert, and observed-time probes; GIN constituent-membership probes; GiST
  N-dimensional coordinate probes; and BRIN observed-time probes;
- `laplace_trajectory_constituent_ids(trajectory)` is an immutable parallel-safe native
  function and is the expression used by the constituent GIN indexes;
- `laplace_trajectory_constituents(trajectory)` is the native full decoder returning
  ordinal, entity ID, run length, and flags;
- a bounded single-constituent query used
  `physicalities_h00_laplace_trajectory_constituent_ids_idx1` through a bitmap index
  scan and returned 20 rows in 13.631 ms on that observed run;
- exact decoding of container `2928d6c775546617a78de69f181204e6` returned five
  ordered vertices whose atom metadata represents `1 4 1 9 8`, with ordinals 1 through
  5 and run length 1;
- the first of those vertices decoded to entity
  `d63bd9a826af91c1fea371965a64e11e`, ordinal 1, run 1, and metadata
  `0x1880000001`; its four transmitted binary64 bit patterns were
  `3ff1645a9671a3fe`, `bff93546c9deb0f7`, `3ff000000070646b`, and
  `3ff0003100010001`;
- the live tier and atom helpers calculated tier 0 and atom position 49 from that
  metadata. Bounded inspection established the content metadata allocation as an atom
  presence bit at 0, tier bits at 1 through 5, and a 21-bit atom field beginning at
  bit 31. The remainder of the 52-bit metadata lane stays recipe-typed rather than
  being assigned one universal meaning.

The timing is an observation of one warmed bounded query, not a complete-product
performance claim. The important result is the executed access path and exact decoder
closure.

The clean implementation now pins the observed vertex as a cross-route carrier vector.
Its low-level decoder returns a typed 128-bit lane, ordinal, run, and 52-bit metadata;
only the composition decoder interprets that 128-bit lane as an entity identity. This
preserves factor and other declared payload recipes without confusing compatible host
width with semantic identity.

## Recovered-source disposition

| Recovered body | SHA-256 | Disposition |
|---|---|---|
| `UniversalT0Pipeline.cs` | `f07a962123891c10af40cb9ab344b4416b099c24c27529e93c729c2d3d900c6a` | Continuity evidence for one shared text decomposition, identity, physicality, and tier-tree path. The exact body was outside Git and was recovered; its implementation is not clean-product source. |
| `ModelFactorBlob.cs` | `74bf65b268b763ab7e6733f56813085b9dd6bf5a3b53b46ceaea6b084af85992` | Continuity evidence for an exact generated factor acceleration artifact with row-addressed factor data and integrity checking. It supports the existing model-factor perfcache requirement; it is not clean-product source. |
| `ModelFactorCodec.cs` | `e3e36241321505247d337c3818aab46133fa878dc0456c943f1da24e7cebab9c` | Continuity evidence for caller-typed factor values carried through the trajectory ABI. Packing four float32 patterns into a 128-bit carrier is not an identity defect. The recovered body alone does not establish cross-route canonical identity behavior, so no further identity claim is made. |

These bodies satisfy the historical stop rule: their exact bytes are preserved, their
architectural signal is represented in current requirements, and this inspection found
no additional product defect class requiring more excavation.
