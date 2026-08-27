@LP-TEST-PACKAGE-LINUX @LP-TEST-INSTALL-CONVERGENCE @LP-TEST-FRAMEWORK-EPOCH-RECEIPT
Feature: Product activation executes through the bounded delivery authority

  Scenario: The self-hosted runner activates PostgreSQL and Unicode without root checkout execution
    Given an exact activation-eligible installed package and its resource observation
    And a workflow-dispatch request from the protected main product environment
    When laplace-runner authenticates the bounded activation request
    Then an immutable root-owned gateway verifies its own bundle and the signed inputs
    And the gateway activates the isolated PostgreSQL product cluster
    And the gateway consumes the exact cluster receipt to activate Unicode
    And one content-addressed result binds both activation receipts
    But no repository checkout script shell wildcard or arbitrary path executes as root

  Scenario: Deployment authority defects fail before a product effect
    Given a pull-request route stale request changed package changed resource receipt path escape or incomplete restart proof
    When the activation gateway validates the request
    Then the request is rejected for its exact defect
    And no product activation result is published
