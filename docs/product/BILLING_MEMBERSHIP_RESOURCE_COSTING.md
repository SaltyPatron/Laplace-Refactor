# Billing, membership, entitlement, and resource-costing architecture

Tracking parent: #140

This document is normative product intent for Laplace billing/membership work. It exists so provider integrations, product pricing, runtime throttles, and UI cannot drift into separate private definitions.

## 1. Authority

Laplace owns:

- canonical membership identity;
- entitlement calculation;
- throttle and allowance semantics;
- logical-program compilation;
- physical-plan resource estimation and costing;
- request admission and reservation;
- execution under a hard resource ceiling;
- actual usage receipts and reconciliation;
- benchmark-equivalent cost reporting.

External payment/support systems are witnesses/providers. Patreon, Stripe, enterprise contracts, grants, promotions, gifts, and future providers may assert facts that cause a canonical Laplace membership or additive entitlement to exist, but they do not define what that entitlement means at runtime.

The existing architecture already assigns resource estimation and physical-plan costing to the native/common execution spine, entitlement calculation to the native engine, and authentication/billing/provider lifecycle to C# orchestration while treating billing systems as external witnesses. This document makes that boundary concrete for commercial execution.

## 2. Provider-neutral membership

A user's effective Laplace membership is calculated from witnessed provider assertions.

```text
Patreon ---------\
Stripe -----------\
Enterprise --------> provider assertions -> canonical grants -> entitlement epoch
Promotion --------/
Future provider --/
```

A provider assertion records the external principal/member/customer/subscription/product/tier identity, its observed state and time, authenticity/provenance, and the versioned mapping used to interpret it. Raw payment amount is not a stable plan identity.

Default combination law:

1. feature sets union;
2. the highest applicable baseline membership supplies the baseline allowance/throttle envelope;
3. ordinary memberships do not automatically multiply baseline allowances;
4. only explicitly additive products stack;
5. conflicts and overlaps are resolved by a versioned deterministic program and receipted.

Examples of intentionally additive products include concurrency, storage, bulk-capacity, queue-priority, and separately purchased job/export allowance.

## 3. Patreon

Patreon is one valid way to subscribe to/support Laplace. A linked Patreon membership should produce the same canonical Laplace plan and entitlement as an equivalent plan acquired through another provider.

The adapter must ingest authenticated membership/tier/charge-status assertions, preserve exact provenance, process webhooks idempotently, and periodically reconcile current provider state. Provider outage or webhook loss is an observed staleness/reconciliation condition rather than an excuse to invent or erase semantic membership state.

## 4. Stripe

Stripe is expected to provide three rails:

1. direct recurring Laplace subscriptions;
2. recurring or one-time additive throttle/limit products;
3. true a-la-carte high-cost jobs.

Normal included calls do not create one Stripe card transaction per operation. They reserve and consume the member's internal Laplace allowance. Stripe is used for subscription/payment state, funding, invoicing, authorization/capture where appropriate, refunds, and settlement; it is not the authoritative runtime usage ledger.

A-la-carte operations include model export as the first concrete example and may later include very large ingest, bulk synthesis/transformation, dedicated compute, large retained artifacts, or other explicitly classified high-cost work.

## 5. No arbitrary credit economy

The old Laplace repository contains a prototype Billing page with arbitrary Free/Supporter/Pro limits, a generic `credits` balance, `$0.10 = 100 credits`, and hard-coded per-action debits. Those values are historical scaffolding, not product authority. See `SaltyPatron/Laplace#1425`.

Do not migrate those values into the refactor.

A user-facing credit unit may only exist in the future if it is a mathematically defined presentation layer over measured resource economics. The underlying ledger and receipts remain expressed in exact typed resource and monetary dimensions.

## 6. Quote-before-run execution

The billing boundary is part of the common execution boundary:

```text
REQUEST
 -> authenticate principal
 -> resolve provider assertions
 -> calculate canonical entitlement epoch
 -> compile logical program
 -> derive physical resource plan
 -> cost resource plan
 -> emit immutable cost envelope
 -> return run / queue / deny / a-la-carte disposition
 -> reserve allowance and hard resource ceiling atomically
 -> execute only within the reservation
 -> emit execution receipt
 -> reconcile reserved versus actual
 -> release unused allowance and/or settle payment
```

No C#, SQL, HTTP, UI, batch, federation, external-tool, or other execution route may bypass this lifecycle for work governed by membership, throttles, quotas, or separate charges.

## 7. Cost envelope

A preflight cost envelope binds the exact request and execution conditions, including:

- request/content/program identity;
- logical and physical plan identity;
- topology/resource-provider epoch;
- world/evidence/firmware/dependency epochs where relevant;
- membership/entitlement epoch;
- pricing/product epoch;
- expected and hard-max CPU/core-time;
- expected and hard-max memory/byte-seconds;
- storage reads/writes and retained storage;
- network ingress/egress;
- PostgreSQL connection/worker/resource ownership;
- external paid service/tool cost when present;
- expected artifact/output size;
- expected internal marginal cost;
- hard maximum internal cost;
- expected customer price;
- hard maximum customer price;
- membership-covered amount;
- a-la-carte amount;
- estimator confidence/error band;
- throttle/admission disposition.

The estimate must price the same conserved physical plan used by execution: worker arenas, CPU sets, NUMA placement, memory domains, batch width, chunk shape, I/O concurrency, PostgreSQL ownership, nested-library thread budgets, tool processes, and placement/network boundaries. Billing must not invent a second resource model.

## 8. Reservation and reconciliation

Included work reserves an internal allowance before expensive execution. Example:

```text
reserved  1100 work/resource units
actual     827
released   273
```

The canonical accounting dimensions must remain the exact measured resource dimensions even if a derived display score is later added.

Reservations are atomic, durable, idempotent, and replay-safe. Concurrent operations cannot overspend the same allowance. Cancellation, failure, retry, and crash recovery have explicit release/reconciliation rules.

Execution cannot silently exceed a quoted hard ceiling. Approaching or reaching the ceiling produces a typed queue/why-not/recovery/additional-authority condition through the common machine-exception lifecycle.

Historical cost envelopes and execution receipts are immutable. A later estimator can learn from them but never rewrite what was predicted or authorized at the time.

## 9. Estimator calibration

Every execution receipt should provide the measured counterpart to its preflight estimate. Preserve predicted versus actual CPU, memory, I/O, storage, network, database work, output size, wall time, and derived cost, stratified by recipe/operation, topology/provider, world/cache state, and workload shape.

The current old-Laplace operator UI provides an initial observed workload example: `UDDecomposer` completed all `6021/6021` units and `2,177,867/2,177,867` input, with `686/686` files, displayed staged E/P/A of `14,253,943 / 12,011,828 / 4,324,744`, displayed throughput `16,458 rows/s`, and wall time `30m 58s`. This is evidence that real workload receipts should drive economics; it is not enough by itself to infer CPU/memory/I/O cost. ConceptNet behavior visible in the same session is explicitly unrelated to this billing work.

Repeated/cached work must be estimated from the work that actually remains to execute. Identical content that is already canonicalized or cached must not be priced as a first ingest merely because the original input was large.

## 10. A-la-carte jobs

Separately paid work follows the same preflight and hard-ceiling law.

```text
physical plan
 -> immutable quote
 -> membership contribution/discount
 -> explicit user confirmation
 -> payment/prepayment/authorization binding
 -> atomic job admission
 -> bounded execution
 -> execution/artifact receipt
 -> capture/settlement/release/refund
```

Known bounded work may be fixed-price/prepaid. Short bounded jobs may use authorization/capture where the payment rail and expected duration safely support it. Long-running work must use a durable funded/prepaid arrangement rather than depending on an authorization likely to expire before completion.

Payment success is not execution authority. The exact payment/authorization, quote, resource reservation, execution, and artifact identities must be bound into a single auditable receipt chain. Retry/replay must never double-charge.

## 11. Product tiers and throttles

Actual plan thresholds must be derived after measurement from:

- marginal and allocated infrastructure cost;
- measured throughput and supported concurrency;
- estimator error/safety reserve;
- storage and network costs;
- external paid-service cost;
- abuse/fair-use and capacity risk;
- desired margin;
- payment-channel fees;
- benchmark-equivalent competitor economics.

The intended product strategy is deliberately aggressive: the lowest paid Laplace tier should provide as much useful accepted work as measurements safely support, with a target of comparing favorably to far more expensive incumbent consumer plans. That target must be substantiated by accepted benchmark work and resource receipts, not by arbitrary token or credit arithmetic.

As implementation performance improves, a new product/pricing epoch can increase included capacity without changing membership semantics or rewriting historical receipts.

## 12. Competitor-equivalent pricing

Laplace does not adopt competitor token billing internally. Instead, preserve versioned public provider pricing as witnessed external data and calculate comparison reports.

For each provider/model/pricing epoch, preserve the original price dimensions (input, cached input, output, reasoning/tool dimensions where applicable) and derived quantities such as tokens per penny. Consumer subscriptions with non-token time-window/session/weekly/capacity policies must not be falsely converted into a fixed token allowance.

Equivalent-work comparisons use the same benchmark/task corpus and an acceptance/quality floor. A lower-cost incorrect answer does not count as equivalent work.

A valid claim has the form:

> Laplace completed this accepted workload under these measured receipts for this membership/resource cost; under provider X's published API pricing epoch Y, the same accepted benchmark workload cost or would cost Z.

This allows a low-cost Laplace membership to demonstrate its value with reproducible evidence rather than unsupported subscription-to-token equivalence.

## 13. UI/API contract

Before expensive execution, users must be able to see:

- requested operation/scope;
- effective canonical membership/entitlement;
- provider provenance where useful;
- expected measured work/cost;
- hard authorized work/cost ceiling;
- amount covered by membership;
- allowance remaining before/after reservation;
- throttle/queue disposition;
- available additive upgrade when applicable;
- explicit confirmation for separately charged work.

After execution, expose actual resource use, estimate error, reservation consumed/released, payment settlement for paid work, and final execution/artifact receipts.

HTTP/UI/C# surfaces translate the same native typed decisions; none may contain a private hard-coded pricing or entitlement table.

## 14. Security and accounting invariants

- clients cannot self-assert provider tier, plan, price, entitlement, or resource grant;
- provider events are authenticated and idempotent;
- duplicate/reordered/replayed events cannot duplicate grants;
- execution retry/replay cannot double-consume or double-charge;
- concurrent reservations cannot overspend an allowance;
- mapping/product/pricing changes create new epochs;
- historical assertions, quotes, grants, executions, and settlements remain interpretable under their original epochs;
- provider outages and stale observations remain distinct from cancellation/revocation;
- all money uses exact integer minor/micro units or another declared exact fixed-point representation; no floating monetary authority;
- every cross-system mutation uses durable idempotency identities.

## 15. Tracking

- #140 — parent architecture and acceptance
- #141 — canonical membership and entitlement
- #142 — physical-plan costing, reservation, reconciliation
- #143 — Patreon and Stripe provider adapters
- #144 — benchmark-equivalent provider pricing evidence
- #145 — API/UI quote and receipt surfaces
- #146 — real-workload estimator calibration
- #148 — a-la-carte paid job admission/settlement
- #149 — measured product-tier/throttle policy
- `SaltyPatron/Laplace#1425` — old placeholder billing migration/anti-authority
