# Operation, program, orchestration, OODA, feedback and Gödel

Status: reconstruction dossier. This file exists because prior summaries collapsed these into “loops” or “self-improvement,” which destroys the invention's mutation and authority boundaries.

## 1. Operation

**CURRENT PRODUCT LAW**

An operation is a typed semantic machine transition/instruction. It owns the logical transformation, not UI flow or transport choreography.

An operation contract binds, as applicable:

- typed operands;
- preconditions;
- result/effect kind;
- semantic calculation;
- authority requirements;
- resource contract;
- exception/WHY_NOT behavior;
- deterministic/reproducibility law;
- receipt meaning.

`docs/architecture/BOUNDARIES.md` makes the native engine the semantic owner. SQL and C# reach generated typed ISA bindings; they do not implement private copies of ranking, identity, cognition, trust or modality semantics.

### Historical ancestry

Pre-Hartonomous D&D research explicitly defined atomic operations such as `RollDice`, `CheckSave`, `UpdateEntityStat`, `MoveCharacter` and `ApplyDamage`, separately from composite tasks and OODA/BIPA/HTN decision strategies. This is evidence that “primitive executable operation” and “control/decision loop” were already separate concerns before Hartonomous.

That is recurring-practice/historical evidence, not proof that modern ISA opcodes are direct source descendants of those exact D&D functions.

## 2. Program / recipe

**CURRENT PRODUCT LAW**

A program/recipe composes typed operations under an explicit goal and completion/effect contract.

Examples of different program families:

- source admission;
- prompt interpretation/cognition;
- realization;
- game-state transition;
- model decomposition;
- target compilation;
- authorized external effect;
- evidence adjudication;
- Gödel candidate evaluation.

A program is not a hidden policy network and is not the physical scheduler. It declares logical work; the execution framework chooses lawful physical plans/providers.

## 3. Orchestration

**CURRENT PRODUCT LAW**

Orchestration arranges execution without taking semantic ownership.

Current architecture splits peer orchestration responsibilities approximately as:

```text
C#
  process/service lifecycle
  source discovery
  sessions/transports
  authentication/authorization/billing/UI services
  progress/cancellation/telemetry

SQL/PostgreSQL relational orchestration
  schemas/types/constraints/indexes
  transactions
  set routing/restriction
  typed native bindings
  program submission/result projection

                  ↓ both
          generated typed ISA
                  ↓
       native semantic engine
```

The common execution spine additionally owns validation, batching, resource grants, scheduling, cancellation, effect admission, exception translation and receipts.

### Counterexample

A C# “orchestrator” that chooses semantic truth/ranking, implements conversation meaning or invents a private identity algorithm has crossed the boundary and become a second engine.

Likewise SQL recursive/cursor logic replacing a native semantic kernel is not “just orchestration.”

## 4. OODA

**DIRECT/CURRENT LAW**

OODA is a typed control cycle over world/problem state and consequences:

```text
OBSERVE
    exact current state / claims / consequences

ORIENT
    derive problem-relative representation,
    constraints, obligations and deficits

DECIDE
    compare/select admissible candidate acts/programs

ACT
    execute under explicit authority/effect envelope

OBSERVE CONSEQUENCE
    admit what actually happened as new observed state
```

OODA can occur at different scopes, but the scope does not convert OODA into Gödel.

It controls action selection and consequence closure. It does not automatically create a new calculus, source truth or reusable instruction.

## 5. Feedback

**RECONSTRUCTION TERM; must remain typed**

“Feedback” is not one mutation mechanism. It means some observed consequence/result becomes an input to a later process. The lane determines what may change.

### Ordinary cognition feedback

A prior partial result, unresolved obligation or tool result can update the current guidance/search state. This is fast cognition; it does not rewrite testimony or calculus.

### Effect/outcome feedback

A real action's observed consequence re-enters exact occurrence/testimony/effect state and can be used by future reasoning.

### Evidence feedback

Independent later observations can support/refute/narrow claims and publish a new adjudicated evidence/standing epoch under the evidence law.

### Procedural feedback

Successful and failed cognition executions remain typed trajectories/receipts. They can become evidence for whether a procedure/operator is reusable.

### Physical execution feedback

Latency, CPU/I/O, crossings, estimates and topology measurements can identify an acceleration opportunity. Performance cannot certify semantics.

### Gödel/incompleteness feedback

Persistent constrained vacancies, recurring failure modes, counterexamples or unexplained structure can seed a candidate calculus/program/operator hypothesis.

These lanes must not be collapsed merely because all are “feedback.”

## 6. Evidence learning

**CURRENT PRODUCT LAW**

Evidence learning changes the evidence/adjudication state under a declared evidence boundary. It publishes a successor evidence/standing epoch without changing the calculus itself.

A new observation may therefore change what Laplace currently believes/stands behind without installing a new way of calculating.

## 7. Gödel

**DIRECT CURRENT LAW**

Gödel is the meta-discovery mechanism for **falsifiable extensions to what/how Laplace can calculate**, not merely another pass through the current control loop.

Current issue #19 separates:

- fast cognition;
- evidence learning;
- calculus discovery.

Current issue #169 extends Gödel explicitly into procedural cognition.

Representative discovery path:

```text
successful + failed executions / typed frays / constrained vacancies
        ↓
identify candidate structural and semantic/outcome families
        ↓
propose candidate
  fact / relation / law / operator / cognition program / firmware operation
        ↓
fit only permitted evidence
        ↓
held-out evaluation
        ↓
counterexample / adversarial search
        ↓
complexity + outcome + completion + resource comparison
        ↓
publish candidate hypothesis
        ↓
explicit calculus/firmware activation authority
        ↓
continue observing independent outcomes
```

Self-generated descendants cannot independently certify the candidate that generated them.

## 8. Historical terminology conflict: Hartonomous “Gödel = multi-scale OODA”

**HISTORICAL / SUPERSEDED WHERE CONFLICTING**

`Hartonomous-002/docs/10-architecture/10-godel-engine.md` (verified in the historical repository) calls the Gödel Engine the orchestration layer wrapping traversal in micro/meso/macro OODA loops.

That document is valuable history: it shows self-reference, trace reuse, frayed-edge analysis and multiple control scales.

But the modern direct/current model has factored the concepts more sharply:

```text
historical Hartonomous:
    “Gödel Engine” umbrella includes multi-scale OODA orchestration

modern Laplace:
    OODA = control/effect cycle
    evidence learning = evidence epoch mutation
    Gödel = candidate meta-calculus/procedural discovery + falsification
```

The reconstruction must preserve the evolution rather than choosing one vocabulary and projecting it backward/forward.

## 9. Memory, skill, habit and muscle memory

**DIRECT CURRENT LAW**

These are separate persistence/promotion levels:

### Memory

Retained observations, executions, trajectories, programs, outcomes and receipts.

### Skill

A reusable versioned cognition program/operator whose semantics are proven under its activation/evidence contract.

### Habit

Firmware-learned **scheduling preference** for proposing a proven skill earlier when current state justifies it.

Habit changes proposal order/expected search work, not truth. Current explicit context/preconditions can block it.

### Muscle memory

A semantically equivalent physical acceleration of a repeatedly proven procedure, for example:

- fused ISA/native operation;
- prepared/indexed provider;
- vector/batch kernel fusion;
- immutable perfcache plane;
- materialized operator;
- memoized closed-epoch result where valid.

Muscle memory changes physical execution, not logical meaning.

## 10. Why this distinction matters

If these layers collapse, the machine becomes impossible to audit:

- orchestration can invent semantics;
- feedback can rewrite truth without evidence law;
- performance optimization can change cognition;
- OODA can self-certify new laws;
- Gödel can become an unbounded self-modifying loop;
- habits can become hidden mandatory policies;
- accelerations can silently skip authority/completion/evidence checks.

The separation makes each mutation answerable:

```text
What changed?
Why was it allowed to change?
What evidence justified it?
What version activated it?
Can the previous state/program replay?
Does the fast path still mean the same thing?
```

## 11. Required acceptance distinctions

A complete proof suite must independently demonstrate:

1. an operation executes with the same semantics through native/PostgreSQL/SQL/C# routes;
2. orchestration/provider changes do not change logical result/receipt meaning;
3. OODA observes an actual consequence rather than an imagined success;
4. evidence learning changes an evidence epoch without mutating calculus;
5. Gödel proposes a candidate that can be rejected by held-out/counterexample evidence;
6. a learned skill remains versioned/replayable;
7. a habit changes scheduling but loses to contrary current state/preconditions;
8. a muscle-memory path measurably reduces physical work while preserving semantic/effect/receipt parity.

## 12. Research still required

- date the first use of OODA/BIPA/HTN and primitive/composite-task abstractions in the D&D corpus;
- trace those terms through Aug–Dec 2025 Hartonomous documents;
- identify commit/document where “Gödel” first appears;
- date the transition from broad multi-scale-OODA umbrella to modern calculus-discovery split;
- build a mutation-authority matrix for cognition, evidence, firmware, calculus, canonical state, effects and acceleration.
