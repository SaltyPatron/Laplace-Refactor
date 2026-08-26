Feature: Canonical deposition and derived state scale independently and reconcile exactly
  Canonical structure and testimony deposit in append-oriented batches. Adjudication,
  statistics, materialized relations, and indexes publish as explicit derived epochs.

  Scenario: Bulk deposition does not serialize through consensus hot rows
    Given a large corpus with many attestations for the same proposition families
    When the native PostgreSQL engine deposits the corpus in bounded batches
    Then content, physicality, occurrence, testimony, progress, and deposit receipts are durable
    And mutable contention points, SPI plans, transactions, and native calls are bounded by batch count
    But one consensus-row update per input attestation fails the crossing and contention contract

  Scenario: Derived state publishes coherently
    Given a complete canonical testimony boundary
    When the engine calculates adjudication and materialized state for a new epoch
    Then latest-complete queries continue to use the prior epoch during construction
    And the new epoch publishes only after counts, semantic parity, receipts, and source boundaries reconcile
    And no query observes a mixture of the two epochs

  Scenario: Complete status names the exact boundary
    Given canonical deposition is complete and required adjudication is still running
    When status is queried
    Then deposit-complete is true
    And derived-state-complete and complete-ingest are false
    And the receipt reports exact remaining units

  Scenario: Reproducible derived state rebuilds exactly
    Given a derived epoch and its canonical source boundary and recipe
    When its materialized state is independently rebuilt
    Then semantic values, dispositions, and deterministic artifact bytes match
    But changing one root observation or recipe produces a different epoch identity

  Scenario: Pairwise growth remains bounded by a materialization contract
    Given a container with enough constituents to make all pairs prohibitively large
    When co-occurrence and precedence statistics are calculated
    Then canonical ordered structure remains complete
    And persisted derived pairs are bounded by the declared query or inference contract
    And omitted reproducible statistics remain calculable from canonical structure
    But materializing every possible pair fails the growth-bound test

  Scenario: Every index earns publication
    Given a proposed index with representative production cardinality and degree
    When its workload plan, scan threshold, write cost, storage cost, and removal condition are measured
    Then publication succeeds only if the declared query selects and uses the index
    And the package receipt retains the plan and cost evidence

  Scenario: Sparse absence is not a dense noise floor
    Given N typed entities and E witnessed or reproducibly derived typed relations
    And absent, unobserved, unknown, contradicted, and present are distinct machine states
    When canonical and derived storage are published
    Then no N-squared surface of zeros or tiny nonzero values is manufactured for unobserved pairs
    And every persisted pair names its evidence or explicit workload and value contract
    And indexed calculation can still produce omitted reproducible results from canonical state
    But a dense noise-floor materialization mutant fails both growth and semantic-state acceptance

  Scenario: Storage pays for measured addressability without becoming truth
    Given a representative world boundary and a proposed index or perfcache
    When the storage census and paired workload execute with and without that acceleration
    Then the receipt attributes canonical, testimony, derived, index, perfcache, TOAST, WAL, temporary, dead, free, and bloated bytes under declared accounting laws
    And it reports build time, write cost, read CPU, I/O, latency, selected plans, scan or recomputation work avoided, and the removal condition
    And removing the acceleration preserves exact logical results and receipt semantics while its measured performance benefit may disappear
    But database size, row count, index count, or a single statistics snapshot alone cannot establish storage efficiency or index value
