Feature: Selected seed recipe preparation and deployed indirect inference
  The configured seed is authority-selected, source recipes reuse accepted structural
  templates without manufacturing meaning, and the deployed product proves that the
  seed can support indirect inference before production-user observations exist.

  @LP-TEST-SELECTED-SEED-MANIFEST
  Scenario: The configured seed is not the local directory listing
    Given product authority selects exact source release profile artifact and role coordinates
    And a physical discovery catalog observes additional unrelated files under a source root
    When the configured seed manifest is resolved
    Then only the selected coordinates and their closed required artifacts belong to the manifest
    And every missing selected artifact remains an explicit unresolved obligation
    But discovering an unrelated file cannot silently expand the configured seed

  @LP-TEST-SEED-STAGE-SEPARATION
  Scenario: Discovery qualification admission seed completion and release remain distinct
    Given one selected artifact has been inventoried and one provider has parsed it successfully
    When the source preparation state is inspected
    Then inventory and provider qualification are recorded without claiming an accepted template
    And template acceptance does not claim activated-product world admission
    And one admitted profile does not claim configured foundational seed completion
    And configured seed completion does not claim deployed inference acceptance or release

  @LP-TEST-RECIPE-TEMPLATE-INFERENCE
  Scenario: Compatible sources reuse generic structure and retain academic deltas
    Given two selected sources share a qualified container record and reference structure
    And their publications define different academic field semantics and historical scope
    When candidate source recipes are synthesized
    Then the accepted generic syntax and universal-AST lowering can be reused
    And each source retains its own attributed semantic testimony and reference delta
    And neither parser success nor structural resemblance publishes a proposition as truth

  @LP-TEST-RECIPE-TEMPLATE-CORRECTION-REUSE
  Scenario: An accepted correction becomes a versioned reusable prior
    Given a candidate mapping left one observed field unresolved
    And authority corrected the mapping and accepted a new template version with conformance vectors
    When a later compatible selected source is processed
    Then the accepted correction is proposed from its immutable prior version
    And the later source still undergoes source-specific authority and conformance checks
    But the same accepted mapping is not reinitialized as an unrelated new guess

  @LP-TEST-RECIPE-TEMPLATE-UNRESOLVED
  Scenario: Ambiguity and unsupported structure remain explicit
    Given eligible providers disagree or a selected source contains an unmatched field error recovery or lossy value
    When candidate templates are evaluated
    Then every conflict loss unsupported form and unresolved authority decision remains in the completion obligations
    And unrelated safe reusable mappings may continue
    But profile completion and activation cannot silently drop the unresolved structure

  @LP-TEST-CLEAN-SEED-INDIRECT-INFERENCE
  Scenario: A clean activated seed derives and explains an unstated proposition
    Given an installed activated product has the configured foundational seed and zero production-user observations
    And the selected evidence boundary contains the required concepts operators calculus and language realization but excludes direct target attestation
    When the public cognition route asks how trustworthy two plus two equals four is and why
    Then the typed arithmetic calculus produces an exact replayable derivation under ordinary natural arithmetic
    And trust reports formal entailment rather than source popularity or Glicko voting
    And the realized Unicode answer is connected to the actual derivation and survives restart and replay

  @LP-TEST-CLEAN-SEED-DELIBERATE-DEFECTS
  Scenario: The clean-seed gate detects lookup voting scope and missing-operation defects
    Given the deployed indirect-inference fixture is active
    When the target is changed to two plus two equals five or modulo four scope is requested
    Then the product returns the exact contradiction or scoped zero result from calculation
    When addition is unavailable or the derivation receipt is corrupt
    Then the product returns an exact why-not or rejects replay rather than guessing
    And social or user assertions remain testimony that cannot override the formal result
    But a direct lookup private helper production-user dependency or silent English fallback fails acceptance
