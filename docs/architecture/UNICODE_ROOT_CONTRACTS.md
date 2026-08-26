# Unicode root contracts

This document records the seven contracts executed by the Unicode root producer and
its first derived execution plane. Contracts alone do not seed a database or activate
an epoch; the native implementation must execute them and emit independently
validated artifacts and receipts.

## Source authority

`contracts/unicode-source.json` binds Tier-0 v1 to Unicode 17.0.0, UTS #10 revision
53, UAX #14 revision 55, UAX #15 revision 57, UAX #29 revision 47, and UAX #44
revision 36. Its 32 required official files are identified by relative path, byte
size, and SHA-256, rather than by the mutable local `latest` alias.

The source set includes normalization and segmentation properties and conformance
data, complete simple/full casing and case folding, bidi bracket/mirroring data,
East_Asian_Width, Line_Break, atom-level Extended_Pictographic, DUCET, and both full
UCA alternate-handling collation suites. Vertical orientation, Arabic shaping, and
emoji/ZWJ/variation sequences have explicit later realization-plane dispositions;
they are not silently discarded or misrepresented as atom properties.

## Atom record

`contracts/unicode-atom-record.json` defines one record for every position in
`U+0000..U+10FFFF`, in ascending position order. Each record binds:

- the LUP-v1 identity address, the public BLAKE3-128 content ID, and the full
  BLAKE3-256 preimage fingerprint whose first 16 bytes must equal the content ID;
- a placement rank, exact four-component coordinate bits, and 128-bit Hilbert key;
- all 26 declared Tier-0 v1 property fields; and
- sidecar requirements for complete collation mappings, contractions, and reverse
  normalization/composition structures.

The full digest is a persistence collision guard, not a replacement public identity.
Rank, geometry, labels, source, role, tier, and use remain outside content identity.

## DUCET equivalence and Laplace placement

`contracts/ducet-totalization.json` preserves complete Unicode Collation Algorithm
collation-element mappings, including variable markers, contractions, expansions,
implicit weights, and algorithmic Hangul. The retained mapping must pass the complete
official NON_IGNORABLE and SHIFTED suites. Laplace placement selects shifted handling
and identical strength, so canonical equivalents can retain one UCA-equivalence key.

UCA equivalence is not a unique placement rank. Only positions still equal under the
complete equivalence key are ordered by unsigned LUP-v1 position bytes. That final
discriminator is neither a DUCET weight nor a UCA level. Surrogate positions are
non-text LUP addresses and use the declared UTS #10 unassigned implicit-weight
extension without claiming Unicode-text or UTF-8 conformance.

## Super-Fibonacci and Hopf

`contracts/super-fibonacci-hopf.json` pins the bounded CVPR 2022 Super-Fibonacci
formula at `N = 1,114,112`, including operation order and binary64 constants. Its
canonical axes are `(x,y,z,w)`, with complex pairs `z1=x+i*y` and `z2=z+i*w`. The Hopf
view is exactly:

```text
(2*(x*z+y*w), 2*(y*z-x*w), x*x+y*y-z*z-w*w)
```

The four-component point remains canonical physicality. Hopf and display projections
are receipted views and cannot overwrite it. Composite arithmetic centroids are not
renormalized onto S3.

## Hilbert and numeric authority

`contracts/hilbert-numeric.json` pins exact-rational quantization from the stored
binary64 bits, four 32-bit axes in `(x,y,z,w)` order, Skilling's global Gray transform,
bit-plane order, and 16-byte most-significant-first serialization. Independent fixed
vectors close the orientation.

Cross-provider bit reproduction has not been established. Unicode-root publication
therefore has one canonical provider class: the exact inventor-host IntelLLVM
2026.1.1 and oneMKL VML 2026.1.0 inventory declared by full paths and SHA-256s, with
strict floating-point flags, AVX2, high accuracy, FTZ/DAZ disabled, and sequential
execution. Other providers may perform typed structural or coordinate observations
but cannot publish root state. Dependency activation remains blocked until that
oneAPI inventory is admitted by the dependency/package system. The exact installed
bytes are now selected by `dependencies/installed-lock.json`; selection alone does
not activate them, and publication still requires package and loaded-object receipts.

## Atomic physicality

`contracts/unicode-atomic-physicality.json` binds every atom's DUCET placement rank
to one exact Super-Fibonacci point and one generic immutable atomic-point
physicality. The physicality ID includes the entity, recipe version and fingerprint,
geometry epoch, and exact four binary64 coordinate bit patterns. It excludes the
numeric-provider receipt, Hopf view, Hilbert key, and execution receipts. The root
validator reidentifies every emitted physicality from that contract.

## Root stream and Tier-0 execution plane

`contracts/unicode-root-stream.json` defines the complete ordered frame stream: all
1,114,112 atoms, all 1,114,112 DUCET positions, contractions, normalization
compositions, and one terminal manifest. The native builder emits this calculation
once into a sealed canonical spool.

The native Tier-0 framework sink consumes those same frames. It materializes one
dense direct-address record per atom, retains the exact inner atom records as
metadata, requires the complete 1,114,112-position population, validates the full
root stream, validates every hot record, validates the whole metadata/value view,
seals an immutable file, and returns the perfcache
artifact-set fingerprint to the framework. Registry cold-open repeats registered
record and whole-view semantic validation before prefault. The published file is an
inert derivative until the complete sibling generation is admitted and activated.

The whole-view validator proves that the hot plane is a complete, contiguous,
byte-faithful projection of the canonical atom records; it is not a second Unicode
root calculator. Canonical origin remains bound by the framework stream receipt,
the ordered sibling-artifact receipt, and the raw artifact digest. Registry admission
requires all three before the derived plane can become visible.

## Verification boundary

`tools/validate-unicode-root-contracts.py` checks the Unicode root contracts, optionally
verifies every official source and numeric-provider byte, and rejects drift from the
pinned authority boundaries. `tests/unicode_root_contract_tests.py` injects source,
record, DUCET/placement, Hopf, Hilbert, and provider-publication defects.

The full root producer and dense Tier-0 sink now execute through the common framework
and full population. The set-oriented PostgreSQL sibling sink, database/artifact
parity admission, and coherent Unicode-root activation remain implementation work;
the Tier-0 artifact cannot satisfy those missing obligations by existing alone.
