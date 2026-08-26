# Laplace Is a Computer — So I’m Building the Roadmap Like One

This is a slightly strange development update, because a lot of the work I’ve been
doing recently has not been about adding a flashy new feature.

It has been about making sure Laplace does not get accidentally reduced into a much
smaller and considerably less interesting project while it is being built.

That probably sounds dramatic.

Unfortunately, it is also true.

Laplace has reached a point where looking at one unfinished part of the repository can
give someone a completely wrong idea of what the project is.

If you look at the source-ingestion work by itself, Laplace can resemble an unusually
complicated ETL or knowledge-graph project.

If you look at the Unicode work by itself, it can resemble an elaborate text engine.

If you look at the geometry, indexes, PostgreSQL integration, model experiments, or
conversation requirements independently, each one can be mistaken for the product.

None of them are the product.

They are parts of the machine.

## Laplace is intended to be a computer architecture

I do not mean that as marketing language.

The architecture is increasingly literal:

```text
exact digital content
    → grammar or codec
    → typed universal syntax tree
    → content-addressed Merkle DAG
    → recipes compiled into an instruction set
    → persistent calculated world state
    → indexed query, search, transformation, and realization
```

The Merkle DAG is not just a clever deduplication trick. It is intended to be the
persistent recursively addressable structure of the machine.

Grammars and codecs decode concrete forms.

Recipes describe reusable decomposition, transformation, calculation, and
recomposition programs.

The instruction set defines legal machine operations.

PostgreSQL provides transactional persistence, indexes, and a SQL execution surface.

Perfcaches provide replaceable derived execution planes.

Receipts record exactly what the machine did, what evidence and resources it used,
and what it is actually entitled to claim.

That is why “Laplace can ingest WordNet” would not be a meaningful finish line.

The purpose is not to finish loading datasets.

The sources provide operands, syntax, evidence, contradictions, counterexamples,
examples, realizations, and observations to a persistent calculating machine.

WordNet, Unicode, Git history, images, audio, models, games, scientific references,
people, organizations, Patreon memberships, and future modalities should all enter
the same substrate according to their actual roles. None of them gets a private
little engine.

## Why I’m replacing “nearest” with an actual search program

One of the underlying bets in Laplace is that many AI systems flatten relationships
too early.

They put unlike things into a shared vector space, find something nearby, and then
ask approximation to carry a tremendous amount of semantic responsibility.

But three social connections, three corporate connections, and three arbitrary graph
hops do not mean the same thing merely because the number is three.

A found path also does not prove that it is the shortest path.

So Laplace is intended to compile a question into a typed search program:

```text
question
    → goal and admissible state
    → legal typed transitions
    → indexed, set-oriented frontier generation
    → A* or another declared search law
    → completion certificate or exact incompleteness
```

Geometry, indexes, vectors, caches, and nearest-neighbor operations can still be
extremely useful for generating candidates.

They just do not get to impersonate truth, relevance, or proof that the search is
complete.

This same issue is one reason my earlier model-export experiments stalled. I could
produce artifacts with the correct shapes and make them pass interesting academic
tests, but flattening precedence, containment, semantic role, direction, causality,
time, provenance, and evidence dependence destroys distinctions that cannot be
magically recovered later.

I am therefore banning the vague word “faithful” as a model-export acceptance test.

The machine has to name the exact invariant it claims to preserve and independently
prove that invariant.

## “Why not, Laplace?” is part of the instruction set

Humans are finite.

Computers are finite.

Laplace will be finite too.

The honest result of a calculation is not always an answer.

Sometimes the result is:

```text
missing evidence
incomplete search boundary
authority denied
resource exhausted
unsupported operation
semantic contradiction
provider unavailable
hardware fault
```

Those are not synonyms.

A storage device failing is not the same thing as the machine not knowing something.

A search budget expiring is not the same thing as evidence contradicting the claim.

A denied effect is not proof that the effect is impossible.

So the roadmap now includes a processor-grade exception model: typed exceptions,
traps, faults, cancellation, restart, retry, rerouting, replay from the last proven
durable receipt, and an exact answer to:

> Why not, Laplace?

A processor should not respond to a bus error by inventing a friendly value and
continuing.

Neither should an intelligence system.

## What is actually working today

This is where I want to be extremely precise.

The clean refactor has proved the complete Unicode-root calculation and its controlled
PostgreSQL and Tier-0 deposition/activation path in integration testing.

That calculation covers all 1,114,112 Unicode codepoint positions. The same canonical
root stream feeds normalized PostgreSQL state and the direct/reverse execution planes,
and the hot lookup paths have been measured rather than guessed.

That is real progress.

It does **not** mean the operational Laplace product is installed and possesses that
Unicode root.

The intended PostgreSQL 18.6 product cluster is not yet established and activated.

The generic whole-working-set composition work is currently unfinished local work.
Its PostgreSQL presence and transactional bulk-deposition route still has to be
completed and published.

The typed numerical highway is not implemented.

The heterogeneous foundational knowledge seed has not begun in an activated product
database.

Conversation, general model compilation, personal webs, federation, Raspberry Pi
support, and the complete product remain unimplemented.

In other words:

```text
architecture encoded
    ≠ implementation complete
    ≠ integration proven
    ≠ product activated
    ≠ knowledge seeded
    ≠ product released
```

I intend to keep making those distinctions even when the less-qualified version would
sound much more impressive.

## The roadmap is now being treated as part of the engineering

I have created a new clean-product GitHub roadmap, expanded the machine into eighteen
operational stages, and opened focused issues for the pieces that were previously too
easy to lose inside broad phase descriptions.

Those include:

- the universal grammar and recipe compiler;
- generic lifecycle and provider-substitution conformance;
- machine exceptions and recovery;
- exact heterogeneous source profiles;
- typed connection and shortest-path certification;
- model-export nonflattening tests;
- people and organizations as universal entity worlds;
- personal-web materialization;
- entitlement and achievement testimony;
- identity, visibility, and authority security;
- authenticated federation;
- physical execution and storage placement;
- ARM and Raspberry Pi portability;
- generated web, mobile, API, CLI, and document surfaces; and
- a processor-style architecture reference manual generated from the actual machine
  contracts.

The immediate critical path is considerably less glamorous:

1. finish and publish generic whole-working-set PostgreSQL presence and deposition;
2. establish the exact product database/package boundary;
3. product-activate Unicode through the common machine path;
4. implement the typed numerical highway;
5. specify exact source releases with Unicode-level diligence;
6. admit the configured heterogeneous foundational world state;
7. build query, cognition, transformation, realization, models, and product surfaces
   over that substrate; and
8. accept and release the complete machine only when the complete machine actually
   passes.

Chess will eventually be an excellent proof of the system.

It is not the knowledge substrate and it is not the next dependency merely because a
PGN parser would make a satisfying demonstration.

## What your support changes

This kind of work is easy to dismiss as “just documentation” until a missing
architectural distinction causes months of implementation to head in the wrong
direction.

The roadmap, requirements, tests, and issue structure are how I am making it harder
for Laplace to be silently reduced to conventional patterns while it is being built.

They also give me a much more honest way to show you what exists, what has been proved,
what failed, what is blocked, and what I am doing next.

But the scarce resource is still time.

Right now, Patreon support helps replace hours I would otherwise spend driving Lyft
with hours I can spend implementing, testing, measuring, documenting, and
demonstrating Laplace.

It does not buy control over the invention.

It does not turn a planned feature into a current product.

It gives the project a better chance of getting the sustained engineering time this
roadmap requires.

I still cannot promise miracles.

I can promise to keep showing the actual machine:

The parts that work.

The parts that do not.

The results that surprised me.

The architecture that still has to earn its claims.

And the exact reason when the answer is:

> Not yet.

## Development links

- Laplace Refactor: https://github.com/SaltyPatron/Laplace-Refactor
- Current architecture/continuation authority work: https://github.com/SaltyPatron/Laplace-Refactor/pull/55
- Whole-product program: https://github.com/SaltyPatron/Laplace-Refactor/issues/23
- Typed machine exceptions and recovery: https://github.com/SaltyPatron/Laplace-Refactor/issues/56

DISCLAIMER: I am heavily using AI in the development of Laplace—obviously. This post
was drafted with assistance from OpenAI Codex. I reviewed and edited the final version
for accuracy, and I agree with and stand behind everything stated here as my honest
opinions. Architecture and roadmap requirements described above are not claims that
the corresponding mechanisms are already implemented.
