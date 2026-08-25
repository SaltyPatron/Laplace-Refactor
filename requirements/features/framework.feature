@LP-TEST-FRAMEWORK-MODULE-REGISTRY @LP-TEST-FRAMEWORK-GENERATED-BINDINGS
Feature: One generated framework registers and exposes every operation

  Scenario: A module declares operations through the shared registry
    Given a module contract with typed values operations and version boundaries
    When the framework generates its native PostgreSQL SQL and C# surfaces
    Then every surface carries the same identifiers types and introduction versions
    And no adapter contains a private semantic dispatcher

  Scenario: An unregistered operation cannot enter execution
    Given an operation absent from the generated registry
    When any route attempts to validate or execute it
    Then the complete program is rejected before output or effect admission

@LP-TEST-FRAMEWORK-EXECUTION-CONTEXT @LP-TEST-FRAMEWORK-EPOCH-RECEIPT
Feature: Every execution is bound to one immutable context and receipt lifecycle

  Scenario: Execution context binds all semantic and physical epochs
    Given a typed program and source identity geometry evidence firmware dependency database perfcache numeric and package epochs
    When the program validates and executes
    Then the immutable context binds every named epoch to the plan and receipt
    And changing any bound epoch changes the receipt

  Scenario: Partial output cannot publish an epoch
    Given a program whose later operation fails validation or execution
    When the framework processes the complete program
    Then no durable output cache generation or effect is admitted
    And the prior coherent epoch remains active

@LP-TEST-FRAMEWORK-CANONICAL-BATCH @LP-TEST-FRAMEWORK-SINK-PROVIDER
Feature: Modules share one canonical batch and replaceable sink provider boundary

  Scenario: One calculation fans out without recalculation
    Given a module that emits a canonical ordered batch stream
    And a persistence sink a perfcache sink and a result sink
    When all sinks consume that stream
    Then each receives the same records order partitions and hashes
    And no sink independently recalculates semantic values

  Scenario: Single item and partitioned batches retain identical semantics
    Given the same logical input as one item one batch and parallel partitions
    When the common pipeline executes each form
    Then the merged typed output and receipt content are identical

@LP-TEST-FRAMEWORK-RESOURCE-APPLICATION @LP-TEST-FRAMEWORK-WHOLE-OPERATION-TRACE
Feature: One authority governs resources and complete operational traceability

  Scenario: Nested execution cannot manufacture machine resources
    Given one conserved parent grant
    When native TBB MKL PostgreSQL managed and tool providers partition work
    Then the sum of child CPU memory and IO grants remains within the parent
    And logical results remain identical to a serial reference route

  Scenario: Every product requirement has an operational stage
    Given the product requirement graph and operation model
    When trace validation runs
    Then every product requirement maps to one or more acyclic operational stages
    And every implementation disposition names existing evidence
