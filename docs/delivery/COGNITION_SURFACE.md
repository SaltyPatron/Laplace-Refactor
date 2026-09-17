# Running the installed cognition surface

The installed Refactor surface serves Explore, Chat and Operator at
http://127.0.0.1:55434/. Its browser, JSON APIs, OpenAI-compatible endpoint and MCP
endpoint use the same installed native package. The local command is
/opt/laplace/current/bin/laplace-cognition; its cognition transport is
/opt/laplace/runtime/laplace-cognition.sock.

The retained installation proof for source
d2d8a3533d1000cd31f56c6fd92bc11365f0ebac and package
b13c3ca7dd94cb691f00371e1bfe28496f8d231405bc4f3e2daac4b023916903 is
[main run 35216221036](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35216221036).
It verifies the enabled, active laplace-refactor-cognition.service user unit,
selected package health, exact installed browser assets, native cognition,
OpenAI-compatible streaming and MCP readback. The authenticated retained source
and service readback is
[run 35220249735](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35220249735).
These observations prove the loopback endpoint and a warm restart; they do not
prove a cold boot or a remote browser connection.

## Browser access through the existing host gateway

The browser resolves assets and API/event requests relative to its served
directory. When the companion Original managed HTTP forwarder is published,
the existing host TLS address can serve the same application at
https://<configured-host>:8443/refactor/. Original strips /refactor/ and streams
requests to the fixed Refactor loopback listener. This route uses the existing
host proxy and certificate; it does not require a second public port or a
different Refactor bind address.

The public forwarder must be published after the selected Refactor service
enforces its operator bearer and its live proof has checked anonymous rejection
and authenticated success. Source support and hosted transport tests alone do
not establish that the host route is reachable. Until those two deployments and
the external readback complete, use the verified loopback address locally, or an
operator-established SSH forward with the host's existing SSH access.

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

[Hosted run 35226808029](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35226808029)
executed 36 host/service controls, 25 gateway controls and 16 browser VM controls.
The controls exercise optional authority selection, exact rollback, malformed
credential refusal, real HTTP authorization, the full proof caller's expected
401 boundary, and actual server event framing. Browser controls cover root and
/refactor/ assets and APIs, UTF-8/CRLF split at each byte, coalesced frames,
authentication failures, reconnect IDs, token replacement and reader cleanup.

The browser tests are part of normal hosted requirements CI. The actual selected
package activation, authenticated service readback and external gateway check
remain distinct deployment observations.
