Feature: Unicode-native tiered tokens, typed matchups, and language realization
  Laplace speaks Unicode, renders natural language or another modality, addresses
  canonical compositions at the structural tier required by the current program, and
  earns Glicko-2 standing only from typed outcomes.

  @LP-TEST-UNICODE-MACHINE-LANGUAGE
  Scenario: Laplace speaks Unicode and renders language
    Given the machine has the complete Unicode codepoint-position floor
    And one semantic act can be requested in several natural languages
    When cognition and realization traces are inspected
    Then the shared internal symbolic floor is Unicode rather than English or another natural language
    And each natural language is a witnessed interpretation and realization scope over shared semantic state
    And each output language is an exact Unicode composition produced by its own recipe
    And no English token sequence is required as a universal intermediate
    But treating one natural language tokenizer vocabulary as the machine language fails acceptance

  @LP-TEST-TIERED-TOKEN-ADDRESSING
  Scenario: Tokens are addressable at multiple structural tiers
    Given exact Unicode content has recursively composed canonical structures from codepoint leaves through larger universal AST nodes
    When a grammar cognition query or realization recipe selects operands
    Then it may address the canonical compositions at the structural altitude required by that program
    And every selected unit can descend to exact constituents and ascend through enclosing trajectories
    And equal content retains one identity while role occurrence tier source language and interpretation remain outside that identity
    But flattening all operations to one fixed subword segmentation fails acceptance

  @LP-TEST-NATIVE-LEADING-SPACE-NONIDENTITY
  Scenario: Native token identity has no leading-space hack
    Given the exact Unicode sequence contains a word whitespace punctuation or a space-free script
    When its content and trajectory are calculated
    Then each exact constituent and every larger observed or generated composition remains addressable
    And boundaries order gaps containment and recurrence come from structure and physicality
    And the lexical content is not reminted merely because whitespace precedes or follows it
    But making leading-space English tokens or stop-word deletion a native identity law fails acceptance

  @LP-TEST-RATING-DEFAULT-ONCE
  Scenario: A new rating lane consumes its default exactly once
    Given a rating recipe declares a participant lane with versioned initial rating deviation and volatility
    And no completely published state exists for that exact rating key
    And one immutable eligible matchup event has an actual opponent state
    When the rating operation evaluates the event
    Then initialization is recorded separately from testimony and observed performance
    And the first update consumes the declared prior and the actual opponent rating and deviation
    And the resulting rating-at-event state and receipt are published immutably
    When a second eligible event is evaluated for the same key
    Then it consumes the first published state rather than the default
    But an implementation that reinitializes the participant on every fold fails acceptance

  @LP-TEST-RATING-OPPONENT-STATE
  Scenario: Opponent strength carries the information content of the matchup
    Given two participants have equal prior state and equal nominal outcomes
    And one faces a strong established opponent while the other faces a weak or highly uncertain opponent
    When the declared Glicko-2 recipe updates both participants
    Then their expected results and rating updates differ according to the pinned mathematics
    And both opponent rating and opponent deviation remain in the receipt
    But replacing every opponent with one neutral constant fails acceptance
    And preserving opponent deviation while discarding opponent rating also fails acceptance

  @LP-TEST-TRUST-RETURN-LEG
  Scenario: Source trust is earned through an independent return leg
    Given a source-type prior initializes two new sources in the same lane
    And both testify about propositions with complete provenance
    And one proposition is later independently corroborated while the other is independently refuted
    When return-leg matchup events are published
    Then the source-specific standings diverge from the shared prior
    And only evidence roots independent of each source contribute outcomes
    And mirrors quotations model descendants and self-generated explanations cannot manufacture wins
    But permanent source-type literals in place of source-specific state fail acceptance

  @LP-TEST-RATING-UNKNOWN-NONLOSS
  Scenario: Unknown and absence are not losses
    Given testimony is absent or a declared evidence boundary is incomplete
    When the matchup recipe evaluates eligibility
    Then no win loss or draw is fabricated from absence
    And the result retains a typed unknown unsupported or incomplete-boundary disposition
    And the continuation condition names the missing evidence or authority

  @LP-TEST-FORMAL-RESULT-NONVOTING
  Scenario: Formal and exact results do not become votes
    Given the activated arithmetic calculus derives two plus two equals four
    And many dependent social sources assert two plus two equals five
    When proposition standing and source reliability are calculated
    Then the formal proposition remains entailed without a Glicko vote
    And dependent volume cannot override the derivation
    And the independent derivation outcome may update the reliability lanes of sources that asserted the contradiction

  @LP-TEST-RATING-EVENT-TIME-REPLAY
  Scenario: Rating history is event-time and order stable
    Given the same immutable matchup events are admitted in two different processing orders
    When both calculations select the same event-time and evidence boundary
    Then every rating-at-event state and final epoch state is identical
    And a late event publishes a new derived epoch without overwriting prior states
    And as-of queries recover the exact earlier state and receipt

  @LP-TEST-CONTEXTUAL-CONSTITUENT-IMPORTANCE
  Scenario: Nouns function words punctuation and gaps are contextually important
    Given two utterances reuse the same nouns but differ in negation function words punctuation order or one trajectory gap
    When the cognition compiler and language realizer construct their typed programs
    Then every constituent and structural role remains addressable
    And relation direction logical scope sentence force and exact realization follow the language and semantic act
    But a permanent noun-over-stop-word rank fails acceptance
    And global stop-word deletion punctuation stripping or n-gram frequency standing in for structure fails acceptance

  @LP-TEST-LANGUAGE-READINESS-WHY-NOT
  Scenario: Language identification does not certify realization readiness
    Given Laplace can resolve a language coordinate and identify the input script
    And a requested semantic act requires morphology syntax pragmatics or orthography not present in the activated language evidence boundary
    When realization readiness is calculated
    Then the language can be identified while the requested act remains partial unsupported or unknown
    And the why-not receipt names the missing realization obligation
    And no fluent-looking bytes or silent English fallback may claim completion

  @LP-TEST-SEMANTIC-ACT-MULTILINGUAL-REALIZATION
  Scenario: One semantic act renders through independent witnessed language structures
    Given one completed semantic act is requested in English Japanese and another structurally different language
    And each language has an activated realization recipe and sufficient witnessed structure
    When the text realizers calculate exact output
    Then each output is an exact Unicode composition with its own morphology function words punctuation whitespace order and pragmatics
    And every reused and newly composed subtree is identified in the receipt
    And none of the outputs is required to pass through an English token sequence

  @LP-TEST-REALIZER-LANE-RETURN-LEG
  Scenario: Realization outcomes update only the tested lane
    Given a language realizer is newly observed and initializes from its declared prior
    And later user correction or execution evidence tests one language act and context
    When a typed return-leg matchup is admitted
    Then only the tested language realizer operation and context lanes are eligible to update
    And an unrelated language or semantic-act lane retains its own prior or earned state
    And higher standing cannot substitute for missing grammar exact structure evidence or semantic completion
