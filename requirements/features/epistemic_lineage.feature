Feature: Evidence kind lineage dependence and belief remain distinct
  Witnessed, copied, derived, independently corroborated, and currently believed are
  different epistemic states with exact root-observation and epoch receipts.

  Scenario: Derived descendants cannot manufacture independent support
    Given one witnessed root observation supports a proposition
    And one thousand derived claims descend only from that root
    When adjudication calculates effective independent evidence
    Then the root observation contributes once under the declared dependence recipe
    And every derived claim retains a complete path to that root
    And current belief does not report one thousand independent witnesses

  Scenario: Copying and independent corroboration differ
    Given ten sources repeat one primary source
    And two sources independently observe the same proposition
    When source ancestry, quotations, mirrors, and dataset overlap are deposited
    Then the ten copies retain their individual attestations and one dependent root family
    And the two independent observations retain separate eligible roots
    And the adjudication receipt exposes both the raw witness count and effective independent evidence

  Scenario: Model families preserve epistemic ancestry
    Given a base model, three fine-tunes, two distilled descendants, and one unrelated model family
    When their matching behavior is attested
    Then model lineage and shared dataset evidence reduce dependent support under the declared recipe
    And the unrelated model remains a distinct evidence root when its provenance satisfies independence

  Scenario: A derivation cycle adds no evidence
    Given derived claim A depends on derived claim B
    And a proposed update would make B depend on A
    When the engine validates the derivation DAG
    Then the cycle is rejected with the exact offending path
    And neither claim gains standing

  Scenario: Consensus is an immutable as-of epoch
    Given testimony boundary T1 produces consensus epoch C1
    And later contradictory testimony boundary T2 produces consensus epoch C2
    When current and historical standing are queried
    Then the current query uses the latest completely published eligible epoch
    And an as-of C1 query returns the prior standing exactly
    And C2 does not modify C1 or any testimony

  Scenario: Laplace cannot be its only certifier
    Given Laplace reports an epistemic result and its receipt
    When the acceptance test verifies that result
    Then a separate calculation reconstructs root evidence, dependence, and epoch standing
    And a deliberate lineage defect makes the independent comparison fail

  Scenario: Competing world claims remain present while standing diverges
    Given round earth and flat earth are distinct exact propositions
    And every source assertion for each proposition is retained
    And the evidence includes copied claims independent observations calculated geometry predictions causal relations and contradictions
    When adjudication calculates the current standing of both propositions
    Then both propositions remain addressable with their proponents contexts and history
    And copied descendants contribute according to their dependence roots rather than raw count
    And independent cross-domain convergence and successful prediction increase effective support under the declared recipe
    And contradiction and failed prediction reduce standing without deleting either proposition
    And the receipt explains the topology and evidence kinds responsible for the difference
