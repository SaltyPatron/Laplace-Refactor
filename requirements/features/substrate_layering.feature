Feature: Entity physicality occurrence and attestation cannot collapse together
  Entity identifies exact content. Physicality is a calculated realization.
  Occurrence records observed use and context. Attestation records attributable claims.

  Scenario: A declaration cannot instantiate physical structure
    Given an entity whose exact bytes do not form a JPEG structure
    And a source attests that its MIME is image/jpeg
    When the native engine calculates physicality from the entity structure
    Then the MIME claim is retained as an attestation
    And no JPEG physicality is created from that claim
    And a structural classifier result is retained as a derived attestation with lineage
    And neither claim changes the entity identity

  Scenario: One entity has multiple legitimate realizations
    Given one entity is realized under two declared structural recipes
    When the native engine calculates both physicalities
    Then both physicalities reference the same entity identity
    And each physicality retains its recipe, structural form, dimensions, geometry, trajectory, multiplicity, epoch, and receipt
    And neither physicality is treated as a second entity

  Scenario: Usage creates occurrences rather than physicality mutation
    Given an entity and one immutable word physicality
    When one million sentences witness that physicality in different contexts and grammatical roles
    Then one million attributable occurrences and their context relations are queryable
    And the entity identity is unchanged
    And the existing physicality bytes and receipt are unchanged

  Scenario: Exact composition and semantic equivalence are distinct
    Given one entity composed from a precomposed accented codepoint
    And one entity composed from a base codepoint followed by a combining mark
    When a pinned Unicode recipe calculates their canonical equivalence
    Then the exact content identities remain different
    And the equivalence is a versioned derived attestation between the entities
    But no query may alias either identity to the other

  Scenario: Equal physicality summaries do not merge structure
    Given two distinct compositions with equal four-dimensional centroid and radius
    When both are deposited and queried
    Then their content identities, constituents, multiplicities, and trajectories remain distinct
    And geometric equality is only a candidate relation

  Scenario: A packed trajectory carrier retains typed payload classes
    Given one composition trajectory, one typed relation walk, and one exact numeric factor trajectory use the same four-slot carrier width
    When the native engine encodes and decodes every trajectory
    Then the composition returns constituent identities, ordinals, run spans, and structural flags exactly
    And the relation walk returns only its declared relation payload
    And the factor trajectory returns every declared numeric bit pattern exactly
    And physicality type, vertex class, recipe, and receipt select each decoder
    But treating a carrier as entity identity, live coordinates, or factor values solely because its host storage type is compatible fails type safety

  Scenario: Indexed trajectory projections do not replace exact structure
    Given an ordered run-encoded composition occurs in a partitioned physicality store
    When B-tree, GIN, GiST, or BRIN projections generate candidates for their declared query families
    Then exact typed decoding establishes membership, occurrence position, run span, sequence, and container scope
    And contains, precedes, follows, co-occurrence, recurrence, and ancestry agree with independent trajectory reconstruction
    And no complete pairwise expansion is required in canonical storage
    But an index match without exact decoder verification cannot certify a structural relation
