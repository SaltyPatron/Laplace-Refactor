# Running the installed cognition surface

On the observed shared host, open https://hart-server:8443/refactor/ for Explore,
Chat and Operator. The existing Original HTTP application forwards this directory
to Refactor's loopback listener at http://127.0.0.1:55434/. Both use the selected
Refactor native package; no additional public listener is required. The local
command is /opt/laplace/current/bin/laplace-cognition, with cognition transport
/opt/laplace/runtime/laplace-cognition.sock.

The September 17, 2026 installation observation binds source
`df158f21bfecaf4045d065b10c21cd92464ce3ec` and package
`d12af51d6367e03045e12599d2195894868f8408ebe17e5b313db3032f516490`.
[Main run 35231752869](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35231752869)
passed package activation, installed substrate and the authenticated service
proof. Its separate initial source-readback job timed out; the already committed
source subsequently passed complete readback in
[run 35242450404](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35242450404).
The main workflow's original failed outcome remains unchanged.

[Authenticated service reader 35233873945](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35233873945)
verified the enabled, active/running laplace-refactor-cognition.service user unit,
selected-package health, exact installed browser assets, native cognition,
OpenAI streaming/nonstreaming and MCP. Anonymous protected health and MCP requests
returned HTTP 401; the selected operator bearer returned health HTTP 200 and a
complete package-bound product-snapshot event. Warm restart to verified health
took 0.917228 seconds. A cold machine boot was not tested.

## Browser access through the existing host gateway

The browser resolves assets and API/event requests relative to its served
directory. Original strips /refactor/ and streams requests to the fixed loopback
listener, preserving the caller's Authorization header.

[Original delivery 35240399243](https://github.com/SaltyPatron/Laplace/actions/runs/35240399243)
verified the actual forwarding route with nine GET requests: the canonical
redirect, three public assets, anonymous rejection and authenticated success for
health and summary, and a complete authenticated event frame. The final TLS
check at 15:36:48 UTC verified the hostname, certificate and selected Refactor
package at https://hart-server:8443/refactor/.

This was a server-local request through the configured LAN TLS route. A browser
on another machine was not exercised. Remote clients still need their ordinary
network/DNS access and trust in the host's configured certificate authority.
The loopback address remains available on the server or through an
operator-established SSH forward.

[Authenticated route reader 35241779535](https://github.com/SaltyPatron/Laplace/actions/runs/35241779535)
verified artifact 10505532233 (4,527,079 bytes; ZIP SHA-256
`dbb27abb0f11789cbe0bb2cf05f13395f9a9bc2ad83a90d9153925978ab7be3a`).
Its cache/refactor-front-door.json member has SHA-256
`f595e5f23ccde5e12dfd8a45d35264ace6149c0186704637f4058d0957a96cbd`.

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
