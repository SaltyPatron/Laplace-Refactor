Feature: Ordered Unicode composition is language independent
  Every script and modality uses the same Unicode atom floor, ordered composition,
  trajectory, identity, evidence, and realization machinery.

  Scenario: A Japanese sentence round trips through its constituent trajectory
    Given the tier-3 composition 事件のことなんだけど...
    When the native engine returns its ordered constituent trajectory
    Then ordinals 1 through 14 identify 事 件 の こ と な ん だ け ど . . . in that order
    And every constituent has the expected tier-1 disposition
    And native, PostgreSQL, SQL, and C# realization return 事件のことなんだけど...
    But reversing two ordinals or using an English word boundary rule fails exact realization
