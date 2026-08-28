Feature: Perfcaches calculate and accelerate complete coherent substrate epochs
  The Unicode root calculation produces canonical database rows and atom planes together.
  Source-derived planes publish only after their declared canonical boundary is complete.

  Scenario: Unicode root has one calculation and two bulk outputs
    Given no Tier-0 database rows and no active Unicode atom plane
    And a pinned Unicode DUCET and geometry source manifest
    When the native Unicode decomposer calculates the canonical ordered atom stream
    Then it writes a receipt-bound bootstrap artifact from those calculated records
    And the database bulk sink consumes those exact records without recalculating them
    And the database retains every canonical entity and physicality required for referential integrity
    And the artifact retains the direct-address acceleration representation
    And exact database-to-artifact parity is required before their shared epoch activates

  Scenario: Stale or independently calculated bootstrap state is rejected
    Given a Tier-0 artifact that did not originate from the active decomposer execution and source receipt
    When database deposition or epoch activation is attempted
    Then the operation fails even if its version label row count and sampled identities appear correct

  Scenario: Interrupted Unicode root publication preserves the prior epoch
    Given a coherent active Unicode database and cache epoch
    And a newly calculated content-addressed Unicode bootstrap artifact
    When database bulk deposition parity verification or activation fails
    Then the prior database and cache epoch remains active
    And the new artifact is inactive and safely reconcilable
    And dependent programs and source admission cannot observe a half-published epoch

  Scenario: A warmed generation replaces another without restarting PostgreSQL
    Given PostgreSQL readers are pinned to one active immutable perfcache epoch
    And a replacement generation has been published but is not active
    When the control plane maps validates and prefaults every required section
    And the registry atomically activates the replacement epoch
    Then existing readers finish against their pinned generation
    And new readers use the replacement generation without a cold artifact read
    And the old mapping retires only after its final reader drains
    But restarting the postmaster as the perfcache update mechanism fails acceptance

  Scenario: Content composes before PostgreSQL
    Given a verified dense Unicode atom plane for the active standards and geometry epoch
    When a batch of canonical content is decomposed
    Then every leaf identity coordinate locality key and segmentation property resolves without PostgreSQL
    And every parent identity centroid trajectory and structural altitude is calculated leaf to trunk in memory
    And equal Merkle identities merge across the complete working set before durable presence is queried
    And durable novelty uses at most one set-oriented membership round per participating tier

  Scenario: Access law follows the typed key space
    Given dense Unicode sparse normalization reverse identity adjacency and domain-specific planes
    When the native registry activates them
    Then each module executes its declared direct ordered bounded-result or module-defined access law
    And one common mapper validates integrity dependencies lifecycle and loaded identity
    But forcing every plane through a sorted fixed-record binary search fails activation

  Scenario Outline: Every modality can earn its own mapped acceleration module
    Given canonical <modality> state and a representative hot decomposition reconstruction and query workload
    When a typed <modality> perfcache is generated verified prefaulted and activated
    Then its key value source recipe lookup complexity and result shape are explicit
    And covered leaves substructures transitions or spans resolve without PostgreSQL crossings
    And leaf-to-trunk composition and trunk-to-leaf reconstruction preserve Merkle identity order roles multiplicity runs and exact bytes
    And removing the module changes measured performance but not canonical logical results evidence or receipt semantics
    But the Unicode atom plane cannot impersonate the <modality> module

    Examples:
      | modality |
      | language structure |
      | chess state and transition |
      | image tile and channel |
      | audio sample frame and channel |
      | video frame track and timing |
      | code syntax and symbol |
      | model tensor and operator |

  Scenario: Source-derived planes publish after a closed world-admission boundary
    Given a prior coherent perfcache epoch and a new canonical admission in progress
    When readers reconstruct render or route CRUD operations
    Then they continue using the prior coherent epoch
    And no row-level ingest operation mutates the mapped artifacts
    When canonical deposition and required derived reconciliation close their source boundary
    Then the new planes are generated and verified in bulk
    And the complete artifact set activates atomically with a receipt
    And subsequent covered operations avoid PostgreSQL I O and crossings while preserving canonical parity
