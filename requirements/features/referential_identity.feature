Feature: Content identity and referential identity remain distinct
  A name is exact content. Whether an occurrence of that name refers to one person,
  another person, or an unresolved candidate is evidence-bearing referential state.

  Scenario: Shared name content does not merge people
    Given one exact name entity occurs in historical and current game containers
    And independent identity testimony supports two people who share that name
    When the referential resolver adjudicates the occurrences
    Then the exact name remains one content entity
    And each source occurrence remains immutable and attributable
    And at least two candidate person referents remain distinguishable
    And name equality alone contributes no proof that the people are identical

  Scenario: Temporal contradiction generates a split hypothesis
    Given a game occurrence is provisionally assigned to a person
    And independently supported testimony places the game after that person's life interval
    When typed compatibility defects are calculated
    Then the occurrence and life-interval testimony both remain present
    And the current merge receives a temporal identity defect
    And a candidate split is recorded as a derived hypothesis with complete lineage
    But the candidate split is not recorded as an observation or independent witness

  Scenario: External identifiers are testimony rather than content identity
    Given two sources provide federation-scoped player identifiers and effective dates
    When those identifiers support or contradict a candidate person assignment
    Then each identifier remains exact source-scoped content
    And each assertion remains source-bound testimony
    And no identifier or declared name changes the name entity hash
    And missing duplicated reassigned or conflicting identifiers remain representable

  Scenario: Re-adjudication preserves the mistaken historical epoch
    Given referential epoch R1 assigns two name occurrences to one person
    And later independent testimony supports two people
    When referential epoch R2 is completely published
    Then current queries use R2
    And an as-of R1 query reproduces the earlier assignment exactly
    And no source occurrence or testimony from R1 is rewritten

  Scenario: Copied games do not independently corroborate a person assignment
    Given ten databases copied one PGN with the same ambiguous player tag
    And one independently identified event record assigns the player occurrence
    When effective referential evidence is calculated
    Then the copied PGNs retain ten attributable occurrences and one dependence root
    And the independently identified event remains a separate eligible root
    And the receipt exposes raw occurrence count and effective independent support

  Scenario: Fischer live regression detects the current name-key collision
    Given the pinned read-only chess observation receipt from 2026-08-24
    When the fixture resolves Fischer comma Robert J and Robert J Fischer
    Then both inputs reproduce player ID 8e51f1bd3d6645beb0c2b5b8c2a83241
    And that ID reproduces the 1972 Spassky and 2024 Colonial Open occurrence clusters
    And independent FIDE testimony requires at least two candidate referents
    And the test remains incomplete until every game has an assigned candidate set or unresolved receipt
