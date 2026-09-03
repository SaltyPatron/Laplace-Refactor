# User-prompt observation, contradiction, and Laplace-derived claims

Status: inventor-direct clarification, 2026-09-02. This is additive to #16, #17, #18, #110, #132, #169, #170, #182 and the target-operator synthesis work. It corrects any interpretation in which `UserPrompt`, the user entity, lexical role, user authority, and proposition truth are collapsed into one trust scalar.

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

The proposition is not made true by the fact that a user uttered it. It enters the evidence/adjudication machinery with the user's witness identity, source class, context and claim provenance.

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

## 2. `UserPrompt` source trust is seeded and uniform; the user is a separate evolving witness

The `UserPrompt` source class has one configured/seeded trust prior. **All user prompts begin from the same UserPrompt source trust regardless of which user produced them.** The prompt source type does not become more trusted because a particular user has interacted with Laplace for a long time, and it does not receive a bespoke prior for each account.

The user entity is a different participant. It may acquire its own earned standing over time from independently adjudicated outcomes.

Conceptually:

```text
SourceType: UserPrompt
    seeded prior = one shared low-trust/default profile

User entity U
    witness standing = dynamic, earned from interaction/outcomes
    may be typed by claim/relation/domain/context lane

Prompt occurrence O
    source type = UserPrompt
    witness = U
    exact content = P-containing prompt trunk
```

A claim-evidence recipe may therefore consider both the fixed source-type prior and the current earned standing of the user witness, but they remain separate typed inputs and never become one permanent `user trust` field.

Repeated claims from the same user are repeated observations, not automatically independent corroboration. Dependence/root accounting must prevent one entity repeating the same claim from manufacturing many independent witnesses.

## 3. Instruction, preference, assertion, question, and observation are different interpreted acts over the same observed prompt

Every user turn is still an observation even when cognition interprets it as an instruction, preference, assertion, question, correction, request, or another semantic act.

For example:

```text
"I want the answer in Japanese."
```

is simultaneously:

```text
observed prompt occurrence
    -> exact fact that this user produced this utterance here

interpreted semantic act
    -> request/instruction constraining realization language for this turn
```

If repeated interaction shows a stable preference, Laplace may later ask whether the user wants that behavior to become a persistent default. A confirmed default is persistent user/discourse/firmware state derived from interaction, not a change to the seeded `UserPrompt` source trust.

Repeated successful use may also contribute to procedural learning:

```text
repeated observed preference/instruction pattern
-> candidate persistent user preference/default
-> explicit confirmation or other declared activation law where required
-> firmware schedules Japanese realization earlier for that user/context
-> repeated stable execution may admit a cheaper prepared/compiled realization path
```

That is preference/habit/muscle-memory behavior. It is not epistemic trust in the proposition content of arbitrary future user prompts.

## 4. Lexical class and punctuation have no intrinsic trust level

A noun is not more or less trustworthy than a function word, so-called stop word, punctuation mark, whitespace constituent, morpheme, particle, or another structural unit. **Trust is not a lexical-category property.**

Trust/standing belongs to sources, witnesses, attestations, relation/fact lanes, programs, tools, and other evidence-producing participants under typed matchup/evidence recipes.

For exact observed structure:

```text
noun present at ordinal 3
particle present at ordinal 4
question mark present at ordinal 8
negation marker present at ordinal 2
```

are structural/occurrence facts under the physicality recipe. They are not made more or less certain because one constituent is a noun and another is punctuation.

What may be uncertain is an **interpretation about the constituent**, for example:

```text
Source A attests token X is a NOUN
Source B attests punctuation Y closes a quotation
Grammar program C derives particle Z marks topic under this parse
```

Those attestations/derived interpretations carry their own provenance, evidence and standing. The underlying exact constituent/order remains separate.

Likewise, importance/relevance is query-relative rather than trust:

```text
"Dogs run."
    noun may anchor a referent

"Dogs don't run."
    negation may dominate proposition polarity

"Let's eat, Grandma."
vs
"Let's eat Grandma."
    punctuation changes the parse/meaning

Japanese particle/order examples
    function constituents may determine topic, case, emphasis or scope
```

Therefore a global `noun > stop-word > punctuation` weighting, trust prior, deletion rule, or model-export hierarchy is a type error. A target operator may assign different contribution to constituents for one job only because the selected grammar/semantic/completion program justifies that contribution.

## 5. Contradiction is a semantic/cognitive act supported by evidence, not source-vs-source voting

A conforming cognition program may determine that `P` conflicts with admitted higher-standing testimony, exact standards state, deterministic calculation, or another declared evidence boundary.

For the speed-of-light example, the user supplies one low-prior `UserPrompt` observation rooted in one user witness. Independent standards/calculation/evidence lanes can carry much stronger applicable support for the physical constant. The machine need not defer to the low-trust witness merely because the utterance is recent.

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

## 6. Laplace's own response is a derived claim, not a new independent root

When Laplace concludes `not P`, it can persist a derived claim and separately witness the realized output. The output can later be used as ordinary addressable content, but it does not add independent support to its own derived proposition.

```text
independent evidence roots A,B
    -> Laplace derives not-P
    -> Laplace says "No it isn't"
    -> later Laplace summarizes that response

independent support remains rooted in A,B
```

Whether a product exposes a Laplace-generated derived claim through an `attestation`-shaped API is an ontology/interface decision. It must retain `derived` status and dependence ancestry so it cannot impersonate independently witnessed testimony.

## 7. Consequence for generated operators and tensors

Target operator generation must preserve these distinctions. Feature planes may encode user assertion, seeded UserPrompt source prior, user witness standing, instruction/preference state, independent refutation evidence, deterministic calculation, contradiction state, lexical/grammar role, and selected corrective or realization act as separate channels.

Flattening them into one source-trust, token-importance, noun-weight, stop-word penalty, or punctuation penalty would make the generated target incapable of preserving the machine semantics that already exist in the substrate.

## 8. Acceptance

- [ ] The exact user utterance remains witnessed even when its proposition is refuted.
- [ ] All UserPrompt occurrences begin from the same configured source-type trust prior independent of user identity.
- [ ] The user entity can acquire separate earned standing over time without changing the global UserPrompt prior.
- [ ] Repeating the same claim from one user does not manufacture independent roots.
- [ ] An instruction such as `I want the answer in Japanese` remains an observation and can also compile to a realization constraint.
- [ ] Repeated confirmed preferences can become persistent user/default firmware state without becoming higher factual trust for arbitrary user assertions.
- [ ] Nouns, function words/stop words, punctuation, whitespace and particles have no intrinsic trust ordering.
- [ ] Exact constituent/order/punctuation state remains structural fact while POS/grammar/semantic interpretations retain their own evidence provenance.
- [ ] A global noun-over-stop-word/punctuation trust or importance mutant fails multilingual/negation/punctuation fixtures.
- [ ] A physics-style false assertion can produce a receipted contradiction/correction from independent evidence/calculation without hardcoding `Laplace > User`.
- [ ] Laplace-generated correction/summaries do not increase independent root count.
- [ ] Generated target operators preserve source-prior, witness-standing, claim-kind, grammar-role, evidence and contradiction distinctions rather than one global scalar.
