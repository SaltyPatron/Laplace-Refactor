# Clean-room implementation rule

## Separation

The dated current-implementation archive is outside this repository and outside all
build, include, package, test-data, and documentation search paths. It is never added
as a remote, submodule, dependency, source mirror, generated-input directory, or code
search root.

## Prohibited reuse

No Laplace implementation artifact from the archive is copied or translated into this
repository. This includes native code, managed code, SQL, schema, tests, scripts,
workflows, manifests, build files, documentation prose, file layout, comments, names,
and generated artifacts.

## Permitted authorities

New work is derived from:

1. The inventor's direct product requirements.
2. Independently published technical standards and research.
3. Independently maintained upstream sources whose identity, clean state, license,
   checksum, and build contract are verified. Existing upstream trees may satisfy this
   rule; current Laplace implementation files may not.
4. New measurements made against the new implementation.

Historical evidence may identify a behavioral defect that a new acceptance scenario
must catch. The scenario is written from the external behavior and invention law, not
from historical implementation details.

## Requirement derivation order

New work keeps these evidence classes distinct and records which class produced each
requirement:

1. Direct invention invariant.
2. Independently published standard or mathematics.
3. Demonstrated externally observable behavior with exact evidence.
4. Historical defect evidence.
5. Derived acceptance condition.
6. New implementation design decision proven against the preceding conditions.

A historical defect does not become architecture merely because it happened, and a
historical success does not become implementation authority merely because it once
worked. This prevents the clean implementation from being shaped around incidental
old code while retaining every real behavioral lesson.

## File provenance

Every product source file is new work for this repository. Generated files state their
generator and contract input. Upstream source remains outside the product source tree
and retains its upstream license and checksum record.

## Verification

Repository checks reject:

- references to the archive path;
- an old repository remote;
- source roots outside this repository and the verified dependency cache;
- generated output without declared inputs;
- dependencies absent from the lock;
- product code with no associated requirement and executable acceptance identifier.

Laplace output cannot be the sole evidence that Laplace is correct. Product claims use
the independent evidence appropriate to the mechanism: standards vectors, separately
implemented calculations, external runtime behavior, direct PostgreSQL state
reconciliation, deliberate defects, package and loaded-object identity, and complete
boundary measurements. Laplace parsers and query surfaces remain useful instruments
whose results require corroboration.
