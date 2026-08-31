Feature: Typed Glicko standing is earned through real matchups
  Defaults initialize a new standing coordinate once, then immutable observed outcomes
  update that coordinate from its prior published state without flattening trust,
  relevance, language, or constituent roles into one global score.

  Scenario: A new participant receives its typed default once
    Given source X has canonical identity and one first eligible observation
    And no standing exists for X in relation arena A under rating recipe R
    When the standing engine onboards X in A
    Then X receives the exact rating deviation and volatility defaults declared by A and R
    And the default state records zero eligible matchups and its initialization receipt
    And replaying the same onboarding boundary returns the same state without another initialization

  Scenario: Later observations use earned standing rather than resetting defaults
    Given X has a published default state in arena A
    And matchup M1 has an admissible observed outcome
    When M1 is folded
    Then the first successor is calculated from X's default state
    Given matchup M2 closes in a later rating period
    When M2 is folded
    Then the second successor is calculated from the first successor
    But a mutant that starts M2 from the arena default is rejected

  Scenario: The opponent is a rated participant rather than a permanent neutral constant
    Given X plays the same outcome against newly rated opponent N and established opponent E
    And N and E have different prior rating and deviation states in the same arena
    When the rating program calculates both matchups
    Then the updates reflect the declared opponent states and outcome surprise
    And a fixed neutral opponent after onboarding produces different receipts and fails

  Scenario: A known entity can be new to one arena without losing prior standings
    Given canonical entity X has earned standing as a source in arena A
    And X is observed for the first time as a parser candidate in arena B
    When B onboards X
    Then B receives only B's declared default state
    And A's prior standing remains unchanged and as-of queryable
    And neither arena can silently seed the other

  Scenario: Source type is a prior and the source earns a return-leg standing
    Given a new academically curated source begins from the declared source-type prior
    And the source asserts proposition P
    And a later independent boundary corroborates P
    When proposition and source standing epochs are calculated
    Then the proposition may consume the source's prior typed standing
    And the source receives a later return-leg outcome against the corroborated result
    And the source's earned successor becomes the prior state for later assertions
    But source-type trust cannot permanently replace the source's earned state

  Scenario: A source cannot self-corroborate through consensus it helped create
    Given source X contributes testimony to consensus epoch C1
    When the return-leg rating program evaluates X
    Then C1 cannot count as an independent observation of X's reliability
    And only a later eligible root observation calculation prediction or adjudication boundary can produce the return-leg outcome

  Scenario: Copies do not manufacture matchups
    Given ten sources copy one primary assertion
    And two sources independently observe the same proposition
    When dependence closure is calculated before the rating period
    Then the ten copies remain ten attestations but one eligible root family
    And the two independent observations remain two eligible roots
    And matchup cardinality follows the declared eligible roots rather than raw rows

  Scenario: Outcome kinds remain distinct before score mapping
    Given one proposition is refuted
    And one is uncertain
    And one has no observed evidence
    And one query exhausts its resource boundary
    When the arena recipe maps eligible outcomes to rating results
    Then refute uncertain absent and exhausted remain different typed states
    And unknown is not mapped to a loss
    And absence is not mapped to refutation
    And an implementation fault is not mapped to an epistemic result

  Scenario: Ingestion order cannot change a closed rating period
    Given the same immutable matchup event set is admitted in two different orders
    When both executions close the same rating period under the same prior state and recipe
    Then rating deviation volatility matchup count and receipt are identical
    And each event identity contributes at most once

  Scenario: Standing informs a query without becoming universal relevance
    Given the word Captain is a strong entity anchor in one query
    And punctuation determines quotation scope in another query
    And a function word determines negation in a third query
    When each cognition program selects its typed obligations and arena inputs
    Then each constituent can be decisive in the query where its role applies
    And no permanent noun-over-function-word or punctuation importance order is written to canonical state

  Scenario: Language capability is not an ingestion flag
    Given content in language L has been admitted and reconstructed exactly
    But no accepted recipe has proved semantic-act realization in L
    When the product is asked whether Laplace speaks L
    Then it reports the exact proven and missing language capabilities
    And ingestion or script detection alone cannot produce a positive speaking claim

  Scenario: A rendering matchup credits only the tested realization behavior
    Given a completed semantic act is rendered in language L
    And an observer confirms grammar and semantic preservation but rejects punctuation scope
    When the realization outcome is folded
    Then the responsible grammar semantic and punctuation obligations retain separate outcomes
    And the realizer standing is updated only for the distinctions actually tested
    But one whole-event success cannot award wins to every constituent explanation or route

  Scenario: Formal proof is not decided by popularity standing
    Given the active arithmetic calculus derives two plus two equals four exactly
    And many dependent sources assert two plus two equals five
    When Laplace adjudicates the arithmetic proposition
    Then the exact derivation and its receipt establish the formal result
    And the contrary assertions remain witnessed contradicted testimony
    And Glicko standing cannot turn the formal calculation into a vote
