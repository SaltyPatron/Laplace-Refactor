# User-prompt observation, contradiction, and Laplace-derived claims

Status: inventor-direct clarification, 2026-09-02. This is additive to #16, #17, #18, #110, #132, #169, #170, #182 and the target-operator synthesis work. It corrects any interpretation in which a `UserPrompt` source is merely assigned a low scalar trust and then passively blended with other sources.

## 1. A user prompt is exact observation; its embedded proposition is separately adjudicated

For the interaction:

```text
User:    "The speed of light is 14 mph."
Laplace: "No it isn't."
```

Laplace first has an exact observed prompt trunk and occurrence:

```text
prompt content identity
    = exact canonical content of the user's utterance

prompt occurrence
    = this user/session/turn/source produced that content here
```

That observation is exact. It is not made less exact because the proposition expressed by the utterance is false.

Cognition may then interpret part of that prompt as a proposition candidate:

```text
P := speed_of_light == 14 mph
```

The proposition is not made true by the fact that a user uttered it. It enters the evidence/adjudication machinery with the user's source/context/claim provenance.

Therefore these states remain distinct:

```text
USER_UTTERED(P)             observed occurrence fact
USER_ASSERTED(P)            interpreted/attributed claim, if the program establishes that act
P                           proposition under adjudication
SUPPORT(P) / REFUTE(P)      independently rooted evidence/calculation state
LAPLACE_DERIVED(not P)      derived conclusion with complete ancestry
LAPLACE_REALIZED("No it isn't")
                            output occurrence/semantic-act realization
```

No scalar called `user trust` may collapse those classes.

## 2. Source standing is claim-relative; it is not deference

A UserPrompt can be the highest-authority evidence for some questions and very weak evidence for others.

Examples:

```text
"Answer me in Japanese."
    -> primary evidence about requested realization language

"My preferred name is Tony."
    -> potentially primary evidence in the user's authorized personal world

"The speed of light is 14 mph."
    -> evidence that the user uttered/asserted the proposition,
       not authoritative evidence that the physical proposition is true
```

The selected query/evidence program decides which source/claim lanes are relevant. Source identity, source type, relation family, world/context, evidence roots, standing, RD/volatility and time remain typed inputs; they do not imply that the machine should defer to the source on every proposition.

## 3. Contradiction is a semantic/cognitive act supported by evidence, not source-vs-source voting

A conforming cognition program may determine that `P` conflicts with admitted higher-standing testimony, exact standards state, deterministic calculation, or another declared evidence boundary.

The resulting state can be represented conceptually as:

```text
observed prompt trunk
-> interpret assertion act and proposition P
-> retrieve/calculate eligible evidence for P and not-P
-> retain source/provenance/dependence/standing separately
-> adjudicate contradiction
-> bind semantic act CORRECT/REFUTE(P)
-> realize "No it isn't" or a more explanatory correction
-> witness the act, output, and receipt
```

The output is justified by the evidence/calculation path. It is not justified because `Laplace` is assigned a higher hardcoded authority than `User`.

## 4. Laplace's own response is a derived claim, not a new independent root

When Laplace concludes `not P`, it can persist:

```text
DerivedClaim {
  proposition: not P,
  program: cognition_program_id,
  evidence_roots: [...],
  calculations: [...],
  standing_epoch: ...,
  derivation_receipt: ...
}
```

and separately witness the realized output:

```text
"No it isn't."
```

The output can later be used as ordinary addressable content, but it does not add independent support to its own derived proposition.

```text
independent evidence roots A,B
    -> Laplace derives not-P
    -> Laplace says "No it isn't"
    -> later Laplace summarizes that response

independent support remains rooted in A,B
```

Whether a product chooses to expose a Laplace-generated derived claim through an `attestation`-shaped API is an ontology/interface decision. It must retain `derived` status and dependence ancestry so it cannot impersonate independently witnessed testimony.

## 5. Laplace may contradict a human without becoming an authority oracle

The machine must be allowed to out-calculate or out-reason a particular human claim when the selected evidence/calculus supports that conclusion. Preventing that would make deterministic calculation and evidence adjudication subordinate to speaker identity.

The enforceable safety/epistemic boundary is instead:

- Laplace cannot bootstrap independent authority from its own descendants;
- exact calculation cannot be overridden by popularity where the calculus establishes the result;
- uncertain/contradictory external-world claims remain uncertain when the admitted evidence does not close them;
- a confident realization cannot exceed the entitlement of the underlying evidence/receipt;
- later independent outcome evidence may update the standing of the program/source/operator that produced the claim.

## 6. Consequence for generated operators and tensors

Target operator generation must preserve this distinction. A feature plane may encode:

```text
user asserted P
source standing for this claim family
independent refutation evidence
formal/deterministic calculation
contradiction state
selected corrective semantic act
```

but those are separate typed channels. Flattening them into one source-trust scalar would make a generated model incapable of learning the difference between:

```text
"I prefer Japanese"          -- user-authoritative intent
"The speed of light is 14 mph" -- externally adjudicable factual assertion
```

Thus target Q/K compatibility, V/O contribution, evidence routing, contradiction handling and realization operators must be generated from the typed claim/evidence state rather than from a global hierarchy of speakers.

## 7. Acceptance

- [ ] The exact user utterance remains witnessed even when its proposition is refuted.
- [ ] `USER_UTTERED(P)`, `USER_ASSERTED(P)`, `P`, evidence about `P`, a Laplace derived claim, and the realized response are separately queryable state classes.
- [ ] A physics-style false assertion can produce a receipted contradiction/correction from independent evidence/calculation without hardcoding `Laplace > User`.
- [ ] A user preference/instruction fixture makes the same UserPrompt source authoritative in the appropriate user-intent lane.
- [ ] Laplace-generated correction/summaries do not increase independent root count.
- [ ] A later independent measurement/formal/outcome check can update the standing of the program/operator that made the correction.
- [ ] Generated target operators preserve claim-kind/source/evidence/contradiction distinctions; one global source-trust scalar fails the fixture.
- [ ] Realization confidence cannot exceed the completion/evidence entitlement recorded by the cognition receipt.
