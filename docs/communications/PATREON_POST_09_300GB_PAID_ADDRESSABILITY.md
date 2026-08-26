# 300 GB Is the Cheap Part

I recently made a claim about the old Laplace database that sounded almost too neat:

> It contains hundreds of millions of explicit structural records in less than 300
> GB, and roughly twice as much space is spent on indexes as tables.

Then I gave the development agent the correct instruction:

> Don't trust me. Ask PostgreSQL.

So that is what I did.

On August 26, 2026, I ran a read-only census against the live old-iteration database.
PostgreSQL 18.3 reported an exact database size of:

```text
295,775,270,591 bytes
```

That is about 295.8 GB in decimal units, or about 275 GiB in binary units. PostgreSQL's
pretty-printer labels that binary value `275 GB`, which is a nice reminder that even a
simple storage number needs a unit boundary.

Across 654 user relations, PostgreSQL attributed:

```text
109,191,495,680 bytes  tables, including TOAST
186,482,507,776 bytes  indexes
---------------------
295,674,003,456 bytes  selected user relations
```

So my remembered “about 2:1” was directionally close, but not exact.

The measured allocated-byte ratio was:

```text
1.7078 : 1
```

Indexes occupied about 63.1% of the selected user-relation estate. Tables and TOAST
occupied about 36.9%.

That correction matters. I want this project to be funded by showing the actual
engineering, not by rounding every observation toward the better story.

## What is in those bytes?

I also ran exact row counts against four of the old system's headline populations:

```text
 52,649,911  entities
 25,083,094  physicalities
177,570,489  attestations
141,390,181  consensus records
-----------
396,693,675  total across those four populations
```

If I divide the entire database by only those four counts, I get about 745.6 bytes per
counted row.

That is not a row-width measurement. The database contains hundreds of other
relations, catalogs, partitions, indexes, and other state. It is simply a useful
estate-level sanity check: nearly 397 million major explicit records coexist with the
rest of the old installation in under 300 GB.

That is interesting because Laplace is not trying to represent the world as:

```text
everything relates to everything
with some tiny nonzero floating-point value
```

It is trying to preserve distinctions such as:

```text
this relationship was observed
this source asserted it
this calculation derived it
this evidence contradicts it
this occurrence reused exact structure
this pair is absent from the selected boundary
this pair is unobserved
this answer is unknown
```

Absent, unobserved, unknown, contradicted, and “tiny but nonzero” are not the same
machine state.

## The all-pairs comparison gets ridiculous quickly

The exact old entity count was 52,649,911.

An ordered all-pairs surface over that population would contain:

```text
2,772,013,128,307,921 slots
```

If every current consensus row represented one unique ordered entity pair—which I
have not proven—those rows would occupy only about:

```text
0.0000051006%
```

of that theoretical surface.

Storing one bit for every possible ordered pair would take about 346.5 TB decimal.
Storing one 32-bit float per pair would take about 11.09 PB decimal.

For one scalar surface.

The old database is about 295.8 GB.

This is not a formal graph-density measurement. A consensus record may carry role,
context, authority, time, multiplicity, or other meaning; it is not necessarily one
unique pair. I am using the arithmetic only to show why “create a numeric cell for
every possible interaction” is such an expensive default.

The world appears far more sparse and structured than that.

## Why would indexes be two thirds of the database?

Because storage is not only where Laplace keeps state.

Storage is also how Laplace pays for addressability.

The intended trade looks like this:

```text
pay once:
  identify
  canonicalize
  witness
  persist
  index

then per query:
  resolve the typed operation
  use the relevant index or perfcache
  touch the narrow state required
  return the result and receipt
```

Conventional AI systems often spend enormous runtime compute rebuilding broad,
temporary relevance surfaces and discarding almost all of them after each answer.

Laplace's bet is that many useful structures and relationships can be made persistent,
typed, independently addressable, and reusable.

An index is already a familiar version of that bet:

> I will spend disk now because scanning or recalculating this path every time is
> wasteful.

Laplace extends the same idea to typed perfcaches and other derived execution planes.
The acceleration can be rebuilt or replaced. It can make an operation much faster. It
must never become the truth underneath the operation.

## This does not prove the old indexes are good

Two hundred-ish gigabytes of indexes can also contain a lot of scar tissue.

The old iteration may have redundant indexes, overlapping indexes, abandoned query
paths, write-amplifying structures, dead space, bloat, or indexes that supported
private feature routes created before the architecture became cohesive.

One catalog snapshot cannot tell me which indexes earned their bytes over a
representative observation window.

That is why I opened a clean-refactor issue specifically for this:

> **Measure sparse addressability and storage-compute economics.**

Every clean-product index or perfcache will eventually need to answer:

```text
Which workload needs you?
Which plan selects you?
What cardinality was tested?
How much did you cost to build?
How much write I/O and WAL do you add?
How many bytes do you occupy?
Which scans, I/O, CPU, database calls, or recomputations do you avoid?
What happens when you are removed?
When should you be retired?
```

And the machine must still return the same logical result without the acceleration.
It may be slower. It may perform far more work. It may fail a declared performance
budget. But an index disappearing cannot rewrite knowledge.

## What this result actually proves

It does not prove that complete Laplace will fit in 300 GB.

It does not prove that the old world state is complete or healthy.

It does not prove that a 300 GB database and a 300 GB model checkpoint contain
equivalent information.

It does not prove that I have replaced modern AI inference.

It proves something narrower and still worth investigating:

> At one dated boundary, hundreds of millions of explicit, queryable Laplace records
> and their large addressability estate fit in hundreds of gigabytes—not an all-pairs
> sea measured in hundreds of terabytes or petabytes.

The next step is not to celebrate that number.

The next step is to measure which bytes are canonical state, which bytes purchased
real performance, which bytes are rebuildable, which bytes are waste, and how much
compute each accepted structure prevents the machine from repeating.

That is the kind of work Patreon support helps fund: fewer hours driving, more hours
turning a provocative observation into a reproducible product measurement.

## Development links

- Storage/addressability acceptance: https://github.com/SaltyPatron/Laplace-Refactor/issues/72
- Whole-working-set deposition: https://github.com/SaltyPatron/Laplace-Refactor/issues/15

## AI assistance disclosure

I am heavily using AI in the development and documentation of Laplace. This post was
drafted with Codex from my requirements and a live read-only PostgreSQL measurement. I
reviewed the technical claims, calculations, limitations, and final wording, and I
agree with and stand behind the published version as my honest account of the work.
