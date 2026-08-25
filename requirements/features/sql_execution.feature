Feature: PostgreSQL executes the typed Laplace ISA at industrial batch scale
  SQL and C# orchestrate one native PostgreSQL server engine. The observable result,
  receipt, and performance contract do not depend on which orchestrator submitted the
  vector.

  Background:
    Given a package receipt identifying the loaded native engine and PostgreSQL extensions
    And a fixture receipt identifying every input byte, record, source, and expected durable count
    And statement, plan, WAL, temporary-byte, CPU, memory, I/O, and client-crossing instrumentation is active

  Scenario: A one-element vector is the single-item operation
    Given a heterogeneous vector of witnessed ISA instructions
    When each instruction is executed in a one-element vector
    And the complete vector is executed through the bulk interface
    Then every content identity is byte-identical between the executions
    And every four-component coordinate and radius is bit-identical between the executions
    And every run, occurrence, testimony, consensus state, and receipt is identical
    And readback preserves the same order and typed outcomes

  Scenario: Complete ingest satisfies both performance boundaries
    Given a qualifying real corpus large enough to sustain steady-state work
    When the complete ingest commits identity, geometry, structure, evidence, consensus, receipts, and durable progress
    Then throughput is at least 500000 input records per second on the declared i7-6850K system
    And elapsed time is no more than 30 seconds per input GB on that system
    And the report includes input and output counts, client crossings, statements, batch sizes, CPU, memory, I/O, WAL, and temporary bytes
    But a per-record server invocation exceeds the crossing boundary and fails this scenario

  Scenario: Caller session state cannot change correctness
    Given pooled connections with different search paths and unrelated caller settings
    When each connection executes the same typed instruction vector
    Then each returns the same typed result and receipt
    And captured product statements contain no correctness-critical session mutation
    And every referenced routine resolves through its declared namespace
    But removing an explicit namespace makes the isolation scenario fail

  Scenario Outline: Every S3 component participates in geometry
    Given two substrate points equal in the other three components
    And the points differ only in <component>
    When native, SQL, C#, scalar, batch, sequential-scan, and indexed routes calculate their relationship
    Then all routes return the same nonzero distance and ordering
    And neighborhood and containment results agree across every route
    But an implementation that omits <component> fails the route-parity assertion

    Examples:
      | component |
      | X |
      | Y |
      | Z |
      | M |

  Scenario: A published index proves the query contract it serves
    Given a representative fixture with declared cardinality, distribution, null semantics, and sort semantics
    And an index receipt binding the operator class, build inputs, bytes, and write cost
    When the public typed server surface is executed with analysis enabled
    Then the intended operator and index appear in the captured plan
    And returned rows and order exactly match a sequential execution
    And scan and block counters show that the published index performed work
    But changing the query sort or null semantics makes the plan assertion fail

  Scenario: Product diagnostics require one bounded server request
    Given an installed product with ingest, conversation, model, and substrate activity
    When an orchestrator requests the versioned diagnostic snapshot
    Then one bounded request returns typed status, counts, loaded artifact identities, active work, and performance counters
    And the response receipt identifies the observation epoch
    But repeated client catalog discovery exceeds the crossing boundary and fails

  Scenario: Temporary object lifecycle is not paid per batch
    Given repeated vectors executed on new and reused pooled connections
    When the complete operation is measured
    Then temporary object creation and discard counts remain within the declared connection-lifecycle boundary
    And the vector result does not spill beyond the declared temporary-byte boundary
    But creating and discarding a temporary relation for every vector fails both assertions
