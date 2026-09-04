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
    And the generated output itself creates zero independent semantic attestations

  Scenario: Reused and generated compositions remain distinguishable
    Given one output clause reuses a witnessed sentence and another is newly composed from witnessed constituents and relation laws
    When the output is materialized
    Then the receipt distinguishes reuse from generated composition
    And every constituent, ordinal, transformation, and supporting proposition is traceable
    And changing one ordinal or unsupported constituent fails exact realization acceptance

  Scenario: Realization uses inverse structural-tier fallback rather than conventional byte fallback
    Given cognition has completed one semantic act but no exact witnessed utterance composition satisfies every realization obligation
    When the text realizer searches for a valid output
    Then it first reuses any exact compatible larger compositions that are available
    And it may descend to smaller exact language-specific compositions when a larger structure is unavailable
    And it composes upward to a new exact Unicode output while preserving the semantic act and every unresolved obligation
    And the final output remains bit-perfect and fully reconstructable
    But replacing the native fallback with one fixed BPE SentencePiece or conventional byte-fallback vocabulary fails acceptance

  Scenario: Non-imperative personal news can create a discourse response obligation
    Given the exact prompt is "Hey Laplace, they promoted me to fire captain today"
    And prompt admission creates canonical content occurrence and trajectory state with zero semantic attestations
    When cognition interprets the whole observation under the active discourse and seeded language pragmatic evidence
    Then it may derive a personal-news disclosure act and a response obligation without requiring an explicit question or instruction
    And the response semantic act preserves the promotion as user-reported rather than independently verified world testimony
    And positive unwanted sarcastic quoted fictional and ambiguous variants can produce materially different response acts
    But a surface rule such as contains promoted implies congratulate fails acceptance

  Scenario: Response formulation remains downstream of whole-observation cognition
    Given several prompts share words such as translate this to Japanese but differ in structure context or referents
    When cognition compares exact prompt identities substructures trajectories seeded evidence and discourse
    Then similar prompts may converge on compatible semantic obligations only when the complete evidence supports that convergence
    And a geometric transformation prompt can remain semantically distinct despite sharing the surface word translate
    And the realizer receives the selected semantic act only after that cognition boundary closes
