Feature: Semantic action and exact modality realization
  Cognition selects meaning and action before a typed realizer calculates exact output
  content. A surface sequence cannot become evidence merely because it is fluent.

  Scenario: Semantic plan is independent of output modality
    Given a selected semantic act contains propositions, discourse roles, evidence boundaries, and completion constraints
    When the act is realized as text, an image, audio, code, and a model artifact
    Then every realizer consumes the same semantic act contract
    And each produces modality-typed exact content with a complete receipt
    And modality-specific structure does not create a private identity, evidence, or cognition system

  Scenario: Multilingual realization has no English universal intermediate
    Given one semantic act is requested in Japanese and Spanish
    When both language realizers calculate compositions
    Then each uses witnessed morphology, syntax, roles, precedence, pragmatics, and occurrences for its own language
    And neither result is required to pass through an English token sequence
    And the Japanese result reproduces exact ordered Unicode constituents

  Scenario: Realization does not invent testimony
    Given a semantic plan includes a supported proposition and an unresolved proposition
    When a fluent output could be constructed for both
    Then the supported proposition can be realized with evidence closure
    And the unresolved proposition remains explicitly unknown or hypothetical
    And surface fluency cannot change its epistemic kind

  Scenario: Reused and generated compositions remain distinguishable
    Given one output clause reuses a witnessed sentence and another is newly composed from witnessed constituents and relation laws
    When the output is materialized
    Then the receipt distinguishes reuse from generated composition
    And every constituent, ordinal, transformation, and supporting proposition is traceable
    And changing one ordinal or unsupported constituent fails exact realization acceptance
