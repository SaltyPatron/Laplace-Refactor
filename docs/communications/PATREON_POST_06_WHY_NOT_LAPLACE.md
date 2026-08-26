# Why Not, Laplace?

There is an annoying question hiding underneath almost every promise made about
intelligent machines:

> Why can't it do that?

The usual answers are often unsatisfying.

The model does not know.

The context window is full.

The service timed out.

The safety system refused.

The search did not find anything.

The database failed.

The answer has low confidence.

Those statements describe radically different conditions, but software frequently
collapses them into one vague failure—or worse, emits a plausible answer anyway.

Laplace is intended to answer:

> Why not, Laplace?

as a machine operation.

## Intelligence is finite because everything physical is finite

Humans have finite time, attention, memory, evidence, energy, and authority.

Computers have finite CPU, memory, storage, bandwidth, I/O, and time.

Laplace will be finite too.

Otherwise I would not be building a computer architecture.

I would be claiming magic.

Every cognition or query program therefore needs a declared boundary:

```text
evidence available
evidence inaccessible
authority granted
time budget
CPU budget
memory budget
I/O budget
database boundary
search boundary
permitted effects
```

The machine's result is not only an answer value.

It also includes the disposition the execution actually earned.

## These are different machine states

```text
complete
optimal under a closed declared boundary
known upper bound
best found under a declared budget
partial
unsupported
denied
resource exhausted
provider unavailable
hardware fault
contradicted
unknown
```

A found path proves reachability and an upper bound.

It does not automatically prove the shortest path.

A search budget expiring does not prove that no answer exists.

Missing evidence does not prove a claim false.

Denied authority does not make an effect impossible.

A storage device failing does not mean the world is unknowable.

And a contradiction discovered by a healthy machine is not an infrastructure error.

This is much more useful than a scalar such as:

```text
confidence: 0.63
```

A scalar throws away why the result is uncertain.

Laplace should be able to return something closer to:

```text
disposition: known-upper-bound
reason: search boundary is incomplete
known path: [...]
shortest path proven: false
searched state: [...]
inaccessible evidence: [...]
continuation requires:
  - authority to evidence source X
  - additional budget Y
```

That is actionable machine state.

## Hardware fault belongs in an intelligence architecture

This sounds almost comically obvious once Laplace is treated as a computer.

A processor does not respond to a bus error by inventing a friendly value and
continuing.

It raises an exception.

Laplace needs the cognitive equivalent.

For example:

```text
disposition: hardware-fault
component: storage-provider-2
fault: failed durability contract
affected instruction: DEPOSIT_WORLD_STATE
last durable receipt: R
state known durable through: R
work after R: uncommitted
restartable: yes
accepted alternate provider: none
semantic result publishable: no
```

That is not merely better logging.

It is machine semantics.

The exception model has to define:

- which faults are precise and which arrive asynchronously;
- which instructions may be restarted;
- when retry is legal;
- when execution may reroute to another accepted provider;
- what can replay from the last durable receipt;
- which results remain publishable;
- how scalar, batch, SQL, native, and managed routes expose the same condition; and
- which defects indicate broken implementation rather than an honest unknown.

## A* is part of accepting finitude

Laplace's use of A* and indexed typed queries is not a claim that the machine can search
the entire universe.

It is a method for spending finite work on the most promising admissible frontier while
retaining the difference between:

```text
optimal
best found under a bound
known upper bound
not established
```

The query defines legal transitions, costs, direction, time, evidence rules, and the
completion boundary.

Indexes, geometry, sparse operators, caches, and nearest-neighbor calculations may
help generate the frontier efficiently.

They do not get to declare that the first nearby result is the truth.

## “I don't know” can be evidence of a working machine

There is a cultural problem in current AI products: fluent output is often treated as
success, while an exact refusal to overclaim looks like weakness.

I want Laplace to invert that incentive.

If the machine can prove a path but cannot prove it is shortest, saying so is correct.

If three apparently independent sources all descend from one copied claim, collapsing
that dependence is correct.

If the relevant evidence is private and the current actor lacks authority, stopping is
correct.

If a provider violated its durability contract, refusing to publish the semantic
result is correct.

If the current calculus lacks an operation required by the goal, recording that
deficit is more useful than hallucinating around it.

The machine should also say what would permit continuation.

That turns failure into a frontier:

```text
missing evidence
    → identify evidence requirement

unsupported operation
    → identify calculus deficit

resource exhausted
    → name resumable state and additional budget

hardware fault
    → identify durable boundary and reroute options

contradiction
    → preserve both claims and schedule adjudication
```

## This remains architecture, not a completed capability

The clean refactor now encodes typed finite-machine results and processor-grade
exceptions as requirements, Gherkin scenarios, roadmap stages, and GitHub ownership.

The complete exception machinery is not implemented yet.

Neither is the complete cognition/search engine.

That distinction matters:

```text
requirement encoded
    ≠ implementation complete
    ≠ integration proven
    ≠ product activated
```

But encoding it now changes how every later mechanism has to be designed and tested.

Every provider, instruction, recipe, query, effect, and receipt needs to answer the
same question:

> If you could not complete the requested operation, exactly why not?

No magic.

No generic `internal error` pretending all failures are equal.

No fluent approximation quietly promoted into proof.

Just the machine state it actually earned.

## Development links

- Machine exceptions and recovery: https://github.com/SaltyPatron/Laplace-Refactor/issues/56
- Typed query and search: https://github.com/SaltyPatron/Laplace-Refactor/issues/17
- Answerability and shortest-path certification: https://github.com/SaltyPatron/Laplace-Refactor/issues/60

DISCLAIMER: I am heavily using AI in the development of Laplace—obviously. This post
was drafted with assistance from OpenAI Codex. I reviewed and edited the final version
for accuracy, and I agree with and stand behind everything stated here as my honest
opinions. The exception and finite-cognition contracts are currently encoded
architecture; the complete mechanisms remain unimplemented.
