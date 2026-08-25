Feature: Generated AImap operator coordinates
  AImap is a calculated spectral coordinate system over a pinned typed operator
  generated from exact physicality structure and selected epistemic layers. It is
  neither canonical physicality nor a permanent model.

  Scenario: Physical and spectral coordinates remain distinct
    Given an entity has a canonical S3 physicality at a pinned geometry epoch
    And a cognition program generates a 2048-dimensional AImap
    Then the entity retains its unchanged S3 physicality
    And its AImap row is addressed by operator, evidence epoch, domain, and spectral recipe
    And changing or removing the AImap cannot change content identity or physicality

  Scenario: Spectral modes satisfy the generated typed operator
    Given a self-adjoint layer-metrized typed operator and declared mass measure
    When the native sparse eigensolver generates the requested nontrivial modes
    Then every eigenpair passes independently calculated residual, orthogonality, and null-space bounds
    And a dense independent fixture solver agrees on the small exact corpus
    And a perturbed eigenvector that preserves dimensions but violates the equation fails

  Scenario: Krylov orthogonalization applies to basis functions rather than entities
    Given a block Lanczos solve uses finite precision
    When reorthogonalization is required
    Then it operates on Krylov basis columns under the declared measure
    And it does not attempt to make all entity rows mutually orthogonal

  Scenario: Eigenspace gauge does not become semantics
    Given one AImap basis is sign-flipped or orthogonally rotated inside a repeated-eigenvalue subspace
    When the two artifacts are compared
    Then eigenvalues, residuals, principal angles, projectors, and induced operator behavior classify them as equivalent
    And raw coordinate-byte inequality remains visible
    And an uncompensated rotation of a dependent relation operator fails behavior tests

  Scenario: Procrustes alignment has a separate receipt
    Given stable anchor entities connect a generated AImap to a prior epoch or target space
    When orthogonal Procrustes alignment is calculated by SVD
    Then the aligned coordinates preserve internal distances
    And the anchor set, cross-covariance, singular values, transform, error, and source artifacts are receipted
    And the alignment SVD is not reported as relation-operator factorization

  Scenario: Rectangular projection is not misreported as rigid alignment
    Given a generated K-dimensional AImap and four-dimensional physical anchors
    When a rectangular K-to-4 projection is fitted
    Then the result is classified as a projection between distinct coordinate systems
    And it cannot overwrite four AImap columns while retaining an AImap validity claim
    And any published transformed basis must re-pass eigenpair, subspace, distance, and dependent-operator tests

  Scenario: Post-solve orthogonalization cannot invalidate eigenmodes
    Given an eigensolver emits modes orthonormal under the declared mass measure
    When a Euclidean QR post-pass changes those modes
    Then the transformed modes must re-pass the original generalized eigenproblem and mass-orthogonality bounds
    And passing a generic row-span test alone cannot publish the AImap

  Scenario: Requested rank is never imitated by cycling a smaller basis
    Given a target contract requests 2048 dimensions and the generated operator has 256 accepted modes
    When Laplace compiles the target representation
    Then the receipt reports the accepted spectral rank and the target-space construction separately
    And repeating the 256 columns eight times fails rank, covariance, and induced-operator tests
    And pseudorandom untyped columns cannot be reported as substrate representation

  Scenario: Incremental and complete spectral epochs reconcile
    Given new entities and testimony change a published operator epoch
    When an incremental AImap update is calculated
    Then it passes source-boundary, eigenpair, orthogonality, subspace-drift, anchor, and unchanged-region bounds
    And complete recalculation probes agree within the declared contract
    And a change outside the incremental bound requires a complete new spectral epoch before publication
    And no query observes coordinates from mixed epochs

  Scenario: AImap is a modular perfcache rather than canonical authority
    Given an AImap artifact is deleted or deliberately corrupted
    When the same operator program is recalculated from canonical state
    Then a valid artifact with equivalent spectral subspaces is produced
    And corruption is rejected before activation
    And canonical cognition remains executable matrix-free while regeneration occurs
