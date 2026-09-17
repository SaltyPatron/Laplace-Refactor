# Running the installed cognition surface

On the observed shared host, open https://hart-server:8443/refactor/ for Explore,
Chat and Operator. The existing Original HTTP application forwards this directory
to Refactor's loopback listener at http://127.0.0.1:55434/. Both use the selected
Refactor native package; no additional public listener is required. The local
command is /opt/laplace/current/bin/laplace-cognition, with cognition transport
/opt/laplace/runtime/laplace-cognition.sock.

The current September 17, 2026 installation binds source
`32f65922abfe358b9d690645781af676e8a8f575` and package
`c2e6432f85c72d390b2cfd3cc343106f4fe7a4d34238b48e2cda2bebbc9758a6`.
[Main run 35257154254](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35257154254)
completed successfully, including native PostgreSQL qualification, package
activation, installed substrate, authenticated service and full Stockfish source
admission/readback/repeat. The
[current corpus receipt](STOCKFISH_READBACK_JOIN_INSTALLED_20260917.md)
records the exact source and replay scope.

[Authenticated service reader 35259227562](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35259227562)
verified the enabled, active/running laplace-refactor-cognition.service user unit,
selected-package health, exact installed browser assets, native cognition,
OpenAI streaming/nonstreaming and MCP. Anonymous protected health and MCP requests
returned HTTP 401; the selected operator bearer returned health HTTP 200 and a
complete package-bound product-snapshot event. Warm restart to verified health
took 0.866559 seconds. A cold machine boot was not tested.

The earlier df158/d12 source-readback timeout and its successful read-only
recovery remain in the [historical successor proof](STOCKFISH_SUCCESSOR_READBACK_QUALIFICATION.json).
The current main success does not alter that historical failed workflow.

## Browser access through the existing host gateway

The browser resolves assets and API/event requests relative to its served
directory. Original strips /refactor/ and streams requests to the fixed loopback
listener, preserving the caller's Authorization header.

[Original delivery 35256901378](https://github.com/SaltyPatron/Laplace/actions/runs/35256901378)
completed the latest retained forwarding and TLS checks at 18:08:30 UTC.
Its nine GET requests verified the canonical redirect, three public assets,
anonymous rejection, authenticated health and summary, and a complete
authenticated event frame. The TLS checks verified the hostname and certificate
at https://hart-server:8443/refactor/. That route observation binds the preceding
Refactor package
`c7cd0ecfd651480ebb36f13932325f2f4c7d9da29a5c6257f97cb7b8d4393c20`;
the subsequent current-package proof above verifies the same backend's loopback
authority and protocol. A fresh TLS observation was not repeated after this
package update.

The retained Original tail artifact is
[10513146983](https://github.com/SaltyPatron/Laplace/actions/runs/35256901378/artifacts/10513146983),
376,993 bytes; ZIP SHA-256
`74af349ac137e8e9ffcb75b01700c2ea492d2a542d3737caea3f826ae220f6c9`.

These were server-local requests through the configured LAN TLS route. A browser
on another machine was not exercised. Remote clients still need their ordinary
network/DNS access and trust in the host's configured certificate authority.
The loopback address remains available on the server or through an
operator-established SSH forward.

The Token button accepts the same operator bearer used by the existing managed
Original API. It is held in the browser tab's session storage and sent in the
Authorization header. It is never placed in an event URL. Static browser assets
are public; health, inspection, cognition, source operations, OpenAI, MCP and the
event stream require the selected token. An invalid event token stops the stream
and asks for a replacement. Network interruptions reconnect; Stop aborts the
current request and pending retry. This is bearer access, not an OIDC or SSO login.

## Canonical service selection

Ordinary standalone installs retain loopback operation. The gateway also retains
its existing explicit --bearer-token-file option. Merely installing Refactor
does not make it depend on another repository's credential file.

For this shared host, the normal activation workflow explicitly runs the existing
service owner with --http-auth managed-operator after the new package is activated
and before restarting cognition. This selects the existing single-assignment
/opt/laplace/secrets/operator.env file. The owner validates that file without
evaluating shell syntax, adds its fixed path to the owned user unit, and retains
only the authority mode in the existing authenticated service receipt. No
credential bytes enter the unit, receipt, command arguments or proof artifact.
Subsequent ordinary ensures preserve the selection.

Do not add a systemd drop-in: the owner verifies the exact selected unit and
requires no drop-ins. The shipped user and system unit templates remain unchanged.
The selected user manager is laplace-runner, with runtime directory /run/user/994
and lingering enabled on the observed host. The canonical owner handles its
manager environment; it should be invoked as that account through the documented
setup or normal activation workflow.

The selected-authority readiness proof uses the same retained selection and
credential reader as the server. It checks anonymous HTTP 401 for health and
events, authenticated package-bound health, and exact public asset bytes.
The full proof also checks anonymous MCP 401, the first complete authenticated
product event, and the existing cognition/OpenAI/MCP program results. A service
being enabled is insufficient to establish those results.

## Qualification retained with this change

[Hosted run 35227711080](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35227711080)
executed 36 host/service controls, 25 gateway controls and 17 browser VM controls.
The controls exercise optional authority selection, exact rollback, malformed
credential refusal, real HTTP authorization, the full proof caller's expected
401 boundary, and actual server event framing. Browser controls cover root and
/refactor/ assets and APIs, UTF-8/CRLF split at each byte, coalesced frames,
authentication failures, reconnect IDs, token replacement, reader cleanup, and
first credential entry after an anonymous 401 boot.

The browser tests are part of normal hosted requirements CI. The actual selected
package activation, authenticated service readback and external gateway check
remain distinct deployment observations.
