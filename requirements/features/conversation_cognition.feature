Feature: Conversation compiles and executes cognition
  A conversational turn is a typed cognition program over persistent layered state.
  A fixed read, selected topic, ranked fact set, or prose scaffold cannot certify the
  complete request-to-act-to-realization chain.

  Scenario: Distinct goals over one topic compile distinct programs
    Given several requests mention the same entity but ask for definition, cause, comparison, correction, calculation, and counterfactual consequence
    When the conversation compiler prepares each request
    Then each logical program retains its goal, referents, constraints, evidence boundary, uncertainty, and completion predicate
    And the programs lower to the typed transitions and calculations required by their own goals
    And routing all requests through one fixed descriptive read fails acceptance

  Scenario: One natural operation cannot certify the general compiler
    Given a definitional utterance compiles through witnessed request and binder cues
    And explanation, clarification, research, calculation, execution, and correction utterances are also in scope
    When compiler coverage is measured
    Then every required semantic operation has a typed logical program and behavioral fixture
    And success on the definitional operation alone cannot satisfy general conversation acceptance

  Scenario: A relational request cannot collapse to one elected topic
    Given a request contains two entities, a directed relation, negation, a temporal constraint, and a decisive function word
    When the logical cognition program is constructed
    Then every entity, role, relation direction, negation, time boundary, and surface distinction remains addressable
    And selecting one rank-one topic before planning fails the result contract

  Scenario: Persistent discourse state is more than recent topic identity
    Given a session contains active entities, propositions, unresolved questions, corrections, ellipsis, and a cross-modal referent
    When a later turn uses pronouns and omitted arguments
    Then reference resolution consults the complete active discourse state and its evidence
    And recency is one receipted measurement rather than the entire state
    And retaining only recent topic identities fails the discourse fixture

  Scenario: Fact selection and surface scaffolding do not establish realization
    Given cognition selects a semantic act that orders supported propositions and preserves one unresolved proposition
    When text is calculated in two structurally different languages
    Then each language uses its own witnessed morphology, syntax, pragmatics, and ordered physicality
    And reused fact content is distinguished from newly generated composition
    And concatenating ranked fact labels with fixed connective text fails realization acceptance

  Scenario: Fast cognition learning and discovery remain distinct in conversation
    Given one answer is produced under an unchanged evidence epoch and calculus
    And later testimony changes adjudicated standing
    And a persistent constrained vacancy later produces a candidate rule
    When the three traces are inspected
    Then the first trace changes neither evidence epoch nor calculus version
    And the second trace changes standing without claiming a new cognition law
    And the third remains an independently tested derived extension until activation
    And an answer changing after a rating update cannot by itself certify all three loops

  Scenario: Conversation lowers goal-directed search through the common ISA
    Given a request requires a supported path satisfying structural, semantic, temporal, source, and discourse constraints
    When the compiler lowers the logical program
    Then it emits a query-scoped typed search program with indexed bulk frontier operations
    And the path result retains every transition and evidence receipt used by act selection
    And a separate entity-to-entity path endpoint that the conversation path never invokes fails cognition lowering

  Scenario: Completion is proven at the semantic-act boundary
    Given a request needs several propositions, one comparison, and a declared output modality
    When candidate acts are evaluated
    Then completion requires every requested semantic component before realization begins
    And a nonempty rank-one row or fixed top count cannot mark the turn complete
    And the result receipt distinguishes complete, unknown, ambiguous, contradicted, and unsupported outcomes
