@LP-EXCEPTION-001 @LP-LIMITS-001 @LP-ISA-001 @LP-PLACEMENT-001
Feature: Processor-grade machine exceptions and recovery
  Laplace reports the exact reason an instruction or program cannot complete and
  never turns a physical, program, authority, resource, or epistemic failure into a
  plausible semantic result.

  @LP-TEST-MACHINE-EXCEPTION-TAXONOMY @LP-TEST-HARDWARE-FAULT-DISPOSITION
  Scenario: Hardware failure is not an epistemic unknown
    Given one logical program can encounter hardware, storage, network, provider, resource, authority, program, or semantic conditions
    When the generated exception registry classifies the observed condition
    Then hardware fault, provider unavailable, resource exhausted, authority denied, invalid instruction, implementation defect, semantic contradiction, incomplete boundary, and unknown remain distinct typed results
    And the receipt identifies the affected instruction program physical plan provider node world scope transaction and observed fault
    But a hardware failure cannot become unknown, timeout, contradiction, empty, or a fabricated value

  @LP-TEST-FAULT-REPLAY-BOUNDARY @LP-TEST-FAILED-EFFECT-NONPUBLICATION
  Scenario: A restartable fault replays only from a proven durable boundary
    Given an effectful program has one accepted receipt through durable boundary R
    And its current physical provider fails after R before publication
    When the exception law permits reroute and replay on another accepted provider
    Then every uncommitted output after R is invalidated
    And execution restarts from the declared replay origin with the same logical program inputs epochs authority and completion law
    And the final logical result and receipt meaning match an independently uninterrupted execution
    But an indeterminate or failed effect is never published as semantic success

  @LP-TEST-EXCEPTION-PRIORITY
  Scenario: Simultaneous conditions follow one generated priority law
    Given an invalid effect program also reaches a cancelled provider whose storage reports a durability fault
    When native PostgreSQL SQL and Csharp routes classify the program outcome
    Then the same generated priority precision restartability and terminal-disposition law selects the result
    And lower-priority observed conditions remain attached to the receipt rather than disappearing
    But route-specific catch order cannot change machine meaning

  @LP-TEST-EXCEPTION-CROSS-ROUTE-PARITY
  Scenario: Every route exposes the same exception identity and recovery law
    Given one pinned program context provider plan fault injection and durable receipt
    When native PostgreSQL SPI SQL Csharp service and diagnostic routes observe it
    Then exception identity operands priority restart retry reroute replay compensation publication law and why-not state agree
    And every surface resolves those values from the same generated registry
    But an adapter-specific generic internal error fails semantic parity
