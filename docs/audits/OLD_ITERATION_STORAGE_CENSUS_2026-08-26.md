# Old-iteration PostgreSQL storage census

Date: 2026-08-26

Classification: dated read-only historical evidence; not clean-product law, source,
schema authority, implementation acceptance, capacity forecast, or current-state
guarantee

## Observation boundary

The inventor explicitly authorized a read-only measurement against the live old
Laplace database after previously isolating its repair work from the clean refactor.
This audit queried PostgreSQL catalogs and exact parent-table counts only. It did not
inspect old source code, copy schema or index definitions, mutate data, diagnose the
separate repair, or infer clean implementation design.

The server identified itself as:

| Field | Observed value |
| --- | --- |
| Database | `laplace` |
| User | `postgres` |
| PostgreSQL | `18.3` |
| Address | `127.0.0.1:5432` |
| Storage census time | `2026-08-26T21:59:13.976364Z` |
| Exact-count interval | `2026-08-26T21:59:25.036874Z`–`21:59:44.895027Z` |
| Transaction behavior | `BEGIN READ ONLY` followed by `ROLLBACK` |

## Exact catalog measurements

`pg_database_size(current_database())` reported:

| Measure | Bytes | Binary display from PostgreSQL |
| --- | ---: | ---: |
| Whole database | 295,775,270,591 | 275 GB |

The relation census included ordinary, materialized, and partitioned user relations,
excluded `pg_catalog`, `information_schema`, and direct `pg_toast` namespace entries,
and used `pg_table_size`, `pg_indexes_size`, and `pg_total_relation_size`. The table
measure includes each relation's TOAST allocation; the index measure includes its
indexes.

| Measure | Count/bytes | Binary display |
| --- | ---: | ---: |
| User relations | 654 | — |
| Tables including TOAST | 109,191,495,680 bytes | 102 GB |
| Indexes | 186,482,507,776 bytes | 174 GB |
| User relation total | 295,674,003,456 bytes | 275 GB |
| Index/table allocated-byte ratio | 1.7078482771:1 | — |
| Table share of user-relation allocation | 36.92969094% | — |
| Index share of user-relation allocation | 63.07030906% | — |

The 101,267,135-byte difference between whole-database size and the summed selected
user relations is outside this census's selected relation boundary. It is not silently
assigned to a storage class.

## Exact population counts

The exact counts scanned the partitioned parent relations under a ten-minute statement
timeout and an eight-worker maximum. They completed in 19.858153 seconds at this
observed boundary.

| Population | Exact rows |
| --- | ---: |
| Entities | 52,649,911 |
| Physicalities | 25,083,094 |
| Attestations | 177,570,489 |
| Consensus | 141,390,181 |
| Four-population total | 396,693,675 |

Dividing whole-database bytes by only that four-population total produces 745.601176
bytes per counted row. This is an estate-level illustrative quotient, not a physical
row-width claim: the database also contains other relations, catalogs, partition and
index overhead, and state outside these four populations.

## Sparse-surface arithmetic

Using the exact entity count only:

```text
N                                  = 52,649,911
ordered pair slots N²              = 2,772,013,128,307,921
current consensus rows             = 141,390,181
consensus rows / ordered slots      = 0.000000051006317234257371
percentage                         = 0.000005100631723425737100%
one bit per ordered slot            = 346,501,641,038,490.125 bytes
one float32 per ordered slot        = 11,088,052,513,231,684 bytes
```

That is approximately 346.5 TB decimal at one bit per ordered slot and 11.09 PB
decimal at one `float32` per slot.

This is deliberately an illustration, not a graph-density result. The census did not
prove that every consensus row is one unique ordered entity pair; rows may differ in
role, context, authority, time, multiplicity, or other dimensions. The arithmetic
shows the scale of the naïve all-pairs surface and nothing more.

## Query shape

The storage calculation was equivalent to:

```sql
BEGIN READ ONLY;

SELECT current_database(), current_user,
       current_setting('server_version'),
       pg_database_size(current_database());

WITH relations AS (
  SELECT c.oid,
         pg_table_size(c.oid) AS table_bytes,
         pg_indexes_size(c.oid) AS index_bytes,
         pg_total_relation_size(c.oid) AS total_bytes
  FROM pg_class AS c
  JOIN pg_namespace AS n ON n.oid = c.relnamespace
  WHERE c.relkind IN ('r', 'm', 'p')
    AND n.nspname NOT IN ('pg_catalog', 'information_schema')
    AND n.nspname !~ '^pg_toast'
)
SELECT count(*), sum(table_bytes), sum(index_bytes), sum(total_bytes)
FROM relations;

SELECT count(*) FROM laplace.entities;
SELECT count(*) FROM laplace.physicalities;
SELECT count(*) FROM laplace.attestations;
SELECT count(*) FROM laplace.consensus;

ROLLBACK;
```

The executed count statement used `UNION ALL`; it is expanded above only to make the
four measured populations obvious.

## What this proves—and does not

It proves that at the dated boundary:

- the old database was below 300 GB decimal;
- its selected user-relation index allocation was about 1.71 times its table/TOAST
  allocation, not an exact 2:1;
- the four named populations contained 396,693,675 rows; and
- an all-ordered-pairs dense surface over the entity population would be orders of
  magnitude larger than the observed sparse estate.

It does not prove:

- clean Laplace storage efficiency or complete-world capacity;
- that consensus rows are unique entity pairs;
- that the old database is semantically complete or currently healthy;
- that allocated table bytes are all live rows;
- that every old index is useful, unique, unbloated, or worth retaining;
- that PostgreSQL's binary `GB` display is decimal GB;
- that a dense model checkpoint and this database encode equivalent information; or
- that the old schema, partitions, or indexes should be copied.

Issue #72 owns the clean-product proof: exact storage-class attribution, representative
workloads, paired with/without-acceleration execution, bloat and redundancy evidence,
recomputation avoided, semantic parity, and removal conditions.
