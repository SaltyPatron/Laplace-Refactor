# O(tier), bidirectional structure and perfcache

Status: reconstruction dossier. This file distinguishes current law, historical evidence and synthesis.

## 1. Current machine law: structure is reusable in both directions

Source owner: `docs/architecture/OPERATIONAL_MODEL.md` plus current perfcache/ISA contracts.

Runtime source admission is not intended to perform one database round-trip per constituent or recursively rediscover an already-known object. The current operational spine specifies:

```text
exact source bytes
  -> grammar/codec emits exact structural units
  -> recipes lower to universal typed AST
  -> Tier-0 leaves resolve from active atom plane without PostgreSQL crossings
  -> parents compose leaf -> trunk in memory
  -> equal identities deduplicate across the whole working set
  -> trunk -> leaf membership probes execute at bounded set grain per tier
  -> only novel canonical entities/physicalities/compositions/occurrences deposit
  -> testimony/source state deposits separately
  -> derived/perfcache work reconciles after the canonical boundary closes
```

This establishes two native directions:

### Leaf -> trunk

Given known constituents, construct higher structure by deterministic composition and Merkle identity. Equal already-known subtrees converge rather than being recreated per source/occurrence.

### Trunk -> leaf

Given a composite identity/physicality, recover or test its constituent structure through the stored trajectory/composition addressability rather than reparsing an unrelated source representation.

The structure is therefore not a disposable ingestion artifact. It is persistent machine address space.

## 2. What `O(tier)` means in the reconstruction

**CURRENT LAW + HISTORICAL EVIDENCE**

The strongest defensible interpretation is not “all Laplace operations have absolute complexity O(tier).” That would be an unsupported complexity claim.

The invention claim is narrower and architectural:

> For operations whose logical work is walking/composing the persistent structural hierarchy, work should scale with the structural levels/tier path that actually must be traversed, while already-known constituent/subtree state is addressed/reused rather than recursively reconstructed through row-at-a-time SQL or source reparsing.

This matters to more than ingestion:

- exact decomposition/recomposition;
- containment and ancestry;
- structural CRUD/update planning;
- source replay;
- text/language reconstruction;
- chess line/game prefixes and repeated positions/moves;
- modality structures;
- cognition programs that need constituent/container transitions;
- generated target structures;
- fast-path compilation where stable tier walks recur.

Historical Hartonomous/Laplace material explicitly describes an `O(tier)` walker over mantissa-packed vertices plus identity resolution. Current law preserves the underlying execution principle while tightening type/semantic boundaries.

## 3. Merkle reuse is the reason the hierarchy matters

A repeated exact subtree should not be paid for as new canonical content every time it appears.

Conceptually:

```text
same exact subtree S
     /      |      \
source A  source B  source C
  occ 1     occ 2     occ 3
```

Canonical content/structure for `S` is shared. Occurrence/source/context/testimony preserve the three uses.

This is one reason identity excludes source/context/occurrence. If source were salted into canonical content identity, reuse would be destroyed and the `O(tier)`/Merkle economy would collapse into source-specific copies.

## 4. Run/multiplicity handling is structural, not duplication

Repeated constituents and repeated structures retain multiplicity/order through occurrence/trajectory/run metadata rather than minting a new identity merely because an item appears twice or 10,000 times.

This is particularly important in domains such as text, games, audio, images, model structures and code where repeated primitives/subtrees are normal.

## 5. Perfcache is not a generic cache file

**CURRENT PRODUCT LAW**

`docs/architecture/OPERATIONAL_MODEL.md` defines perfcache as a **family of typed immutable acceleration modules**, not one sorted record file and not canonical truth.

Current module/access-law families include or anticipate:

- dense direct-address planes;
- reverse-identity planes;
- sparse ordered planes;
- normalization/span planes;
- adjacency/provider planes;
- spatial/locality planes;
- domain/modality-specific planes;
- materialized/generated operator planes where semantically valid.

The modules share an activation/lifecycle framework, not necessarily a record layout or lookup algorithm.

## 6. Common perfcache lifecycle

The intended lifecycle is:

```text
closed/certified source or calculation boundary
        ↓
set-wise module generation
        ↓
integrity + canonical semantic-parity validation
        ↓
durable content-addressed publication
        ↓
map + validate + prefault next generation
        ↓
atomic registry/epoch switch
        ↓
new readers pin next generation
old readers finish on prior generation
        ↓
retire old mapping after final reader drains
```

This provides several important machine properties:

- data/standards refresh does not require redefining canonical semantics;
- readers see a coherent immutable generation;
- hot execution can use mapped/prefaulted memory;
- a stale/incorrect acceleration can be rejected without rewriting canonical state;
- physical placement/NUMA/prefault choices remain execution decisions rather than product meaning.

## 7. Perfcache and modality

Historical/current evidence supports modality/domain-specific **compiled memory planes above the shared canonical floor**.

This does not mean each modality gets a separate ontology. It means an operation may justify a specialized immutable acceleration over canonical state.

Examples from historical lineage include:

- Unicode/Tier-0 direct planes;
- reverse identity lookup;
- numeric/value planes;
- chess-position/domain ROM ideas;
- future image/audio/domain planes.

The law is:

```text
shared canonical semantics
       ↓
operation/domain declares a repeated lawful read/calculation
       ↓
optional typed acceleration plane
```

not:

```text
cache artifact
       ↓
defines canonical truth
```

## 8. Perfcache as “muscle memory” target

Direct/current Gödel procedural-learning law permits a repeatedly proven cognition procedure to acquire semantically equivalent physical acceleration, including a perfcache/materialized/native fast path.

That does **not** mean perfcache itself learns or decides semantics.

Promotion path:

```text
primitive proven program
   -> repeated measured use
   -> candidate acceleration
   -> held-out semantic/receipt parity
   -> measured physical benefit
   -> versioned activation
```

If the accelerated path changes logical result, evidence boundary, completion, safety/effect obligations or receipt meaning, it is a new program candidate, not “muscle memory.”

## 9. Index/perfcache absence cannot prove semantic absence

An acceleration structure may be incomplete for a declared workload, stale before activation, intentionally sparse, or bounded by its provider contract. Therefore:

```text
accelerator miss != canonical absence
```

unless a completeness contract explicitly makes that equivalence valid for the operation/epoch.

This is the same reason a top-K candidate provider cannot silently become the truth predicate.

## 10. Why O(tier) and perfcache belong together

**SYNTHESIS**

These mechanisms implement the same broader design objective at different levels:

- Merkle/tier structure makes exact content recursively addressable and reusable;
- bidirectional walks avoid repeated whole-object reconstruction;
- set/batch execution prevents per-element boundary crossings;
- perfcache removes repeated physical decode/query/calculation work from hot paths;
- Gödel muscle memory can later compile stable procedures into stronger acceleration.

The intended machine therefore moves from:

```text
rediscover structure repeatedly
```

toward:

```text
address exact known structure
+ calculate only what is actually missing
+ compile repeated lawful work when justified
```

## 11. Counterexamples

Reject these descriptions/implementations:

- “perfcache is just a cache/index optimization”;
- “O(tier) only matters to ingestion”;
- per-row SPI/SQL recursion used as the native hierarchy walker;
- source-specific content identities that destroy subtree convergence;
- one universal perfcache record/access law;
- accelerator state treated as canonical truth;
- cache miss treated as semantic absence without completeness proof;
- fast path accepted because it is faster without semantic/receipt parity;
- packed trajectory payload interpreted directly as live S3 geometry.

## 12. Research still required

- first dated appearance of the `O(tier)` term and exact historical complexity claim;
- first dated implementation of trunk→leaf and leaf→trunk mechanisms;
- perfcache lineage across Unicode, numbers, chess, modality and operator planes;
- exact transition from older cache/ROM implementations to the current typed-module registry;
- representative measured workloads proving where the intended structural work is tier-bounded and where other costs dominate.
