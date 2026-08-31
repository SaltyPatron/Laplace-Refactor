# Typed trust matchups, one-time onboarding, and language realization

Status: required architecture direction derived from inventor-direct requirements

This document refines the current Constitution and Invention Model. It does not replace
content identity, physicality, testimony, relation, cognition, or realization law. If a
lower-level implementation, old-repository artifact, statistical convention, or prior
assistant summary conflicts with those authorities, the higher authority wins.

## 1. Defaults, standing, and importance are different things

Laplace must keep three quantities separate:

1. A **default prior** initializes a rating-eligible participant when no prior state
   exists for the exact declared lane. It expresses initial uncertainty. It is not an
   observation, a source endorsement, a proof, or a permanent score.
2. **Standing** is a versioned derived state calculated from immutable typed matchup
   events under a pinned evidence boundary and recipe. It includes the complete rating
   state required by the declared mathematics, not merely one scalar.
3. **Importance** is calculated for the current goal, semantic act, language, context,
   evidence scope, and completion obligations. It is not stored as one timeless entity,
   part-of-speech, source, or relation rank.

Conflating these produces several known failure modes: a source-type literal that can
never be earned away from; a consensus cell that replays the same neutral opponent on
every fold; a witness counter disguised as Glicko-2; a global noun-over-function-word
ranking; or a fluent language fallback that bypasses missing realization evidence.

## 2. What Glicko-2 is for

Glicko-2 was designed to update competitors from outcomes while accounting for the
strength and uncertainty of the opponents they actually faced and for instability in
the rated participant. Its useful state is not a flat score:

```text
rating          current estimated standing
rating deviation uncertainty in that estimate
volatility      expected instability or rate of change
```

The same nominal result against two different opponents is not the same evidence. A
win against a strong established opponent is more surprising than a win against a weak
or highly uncertain opponent, so the update differs. Conversely, repeated unsurprising
results should not move standing as though every observation were equally informative.

Laplace uses this mathematics only when a typed recipe declares a legitimate
outcome-bearing matchup. Glicko-2 does not define relation meaning, truth, traversal,
semantic similarity, physical structure, or a universal ranking. It is one admissible
standing calculation over a declared event stream.

## 3. Matchups are typed events, not flat-score joins

A matchup exists only when a recipe can identify all of the following:

- the rated participants and their roles;
- the exact lane in which they are being compared;
- the proposition, prediction, operation, transition, realization, or capability whose
  outcome is being observed;
- the prior state of both participants at the event boundary;
- the eligible evidence roots and dependence reduction;
- the typed outcome and its declared mapping into the rating calculation;
- valid time, observation time, event order, context, world, authority, and evidence
  boundary;
- the calculation program, parameters, provider, result, and receipt.

A rating lane can be scoped by relation, source role, language, modality, operation,
tool, firmware, context, time, or another typed coordinate when the contract requires
that distinction. The same content entity can therefore have different legitimate
standings in different lanes. One global score cannot substitute for them.

Examples of possible rated participants include a proposition standing cell, source,
witness, analyzer, extraction recipe, relation/operator, language realizer, tool route,
firmware program, chess player, or another outcome-bearing participant. This list does
not make every entity automatically Glicko-rated. Eligibility comes from the typed
match recipe.

Confirmation, refutation, draw, partial support, contradiction, prediction success,
prediction failure, operational success, operational failure, unknown, and absence are
not interchangeable. The recipe declares which states are eligible outcomes and how
they map into the matchup. Absence is never silently treated as a loss.

## 4. New-X onboarding happens exactly once per rating key

`New X` means that a rating-eligible participant or cell has no prior completely
published state for the exact typed rating key.

The lifecycle is:

```text
immutable observation/event is admitted
    -> exact typed rating key is resolved
    -> latest completed state at the event boundary is read
    -> no prior state: initialize from the lane's versioned default prior
    -> execute the first real matchup against the actual opponent state
    -> publish immutable rating-at-event state and receipt
    -> subsequent event reads that published state, never the default again
```

The initialization record is explicit so replay can distinguish `no prior state` from
an all-zero value and can prove which default contract was used. Rating, deviation, and
volatility defaults are versioned parameters. They may differ by a justified lane, but
they cannot be supplied ad hoc by a caller or treated as evidence.

The first event consumes the initialized state. The second event starts from the first
published update. Reinitializing an existing participant, or substituting the default
opponent because the real opponent was not resolved, is a hard semantic defect.

Late or corrected observations do not overwrite history. Laplace deterministically
replays the selected immutable event boundary into a new complete epoch, preserves
prior rating-at-event states, and makes as-of queries explicit. Identical event
boundaries must produce identical results regardless of ingest order.

## 5. Trust is earned through both legs of the evidence cycle

A source or witness can provide testimony about a proposition. That testimony can
participate in calculating the proposition's current standing, using the witness or
source's prior rating state when the recipe declares it as the opponent.

That alone does not make source trust earnable. There must also be a return leg from a
later, separately observed or independently adjudicated result:

```text
source/witness asserts or opposes proposition
    -> proposition is later independently corroborated, refuted, calculated, or tested
    -> the exact outcome bears on the asserting/opposing participant
    -> the participant's appropriate rating lane is updated
```

The return leg must preserve dependence. A source cannot improve itself by producing
many mirrors, quotations, model descendants, derived claims, or self-generated
explanations. Agreement among dependent descendants remains one root family. A
participant cannot use its own output as independent certification of itself.

The two legs are distinct events with distinct receipts. A proposition update and a
source update cannot recursively certify each other inside one closed transaction or
one fabricated outcome.

Source type, relation type, grammar role, modality, language, tool class, and firmware
class may supply default priors or select a recipe. They do not permanently determine
individual standing. A source initially classified as academically curated can earn a
higher or lower source-specific state through later outcomes. A user prompt can be the
strongest available evidence of what that user requested while remaining weak evidence
for an unrelated external-world proposition. Trust is always trust **for a typed claim
or capability under a declared context**.

## 6. Exact and formal results do not enter a popularity contest

Content identity, physicality structure, and a valid formal derivation are not source
votes. Exact trajectory containment, precedence, ordinals, and reconstruction follow
from the physicality recipe. A formally closed arithmetic result follows from the
active calculus. Glicko standing cannot override either.

Those exact results can still create matchup evidence about participants that made
claims or performed operations. For example:

```text
formal arithmetic derives 2 + 2 = 4
social source asserts 2 + 2 = 5
```

The arithmetic proposition remains entailed regardless of how many dependent social
copies repeat the contrary claim. The independent derivation outcome may reduce the
reliability of sources or operators that asserted the contradicted proposition. It
does not turn the theorem itself into a Glicko winner.

## 7. Nouns, function words, punctuation, and whitespace have no universal rank

Laplace preserves exact Unicode content and typed structure before asking what is
important for a particular program. A noun is not always more important than a
function word, so-called stop word, punctuation mark, ordinal, or whitespace structure.

Examples:

```text
"A is B"        versus "A is not B"
"only A"        versus "A only"
"dogs bite"     versus "do dogs bite?"
"f(x, y)"       versus "f[x, y]"
"1,000"         versus "1.000" under different locale conventions
"Captain Ahab"  where the exact one-position separator and trajectory order matter
```

The content words may be identical while function words, punctuation, order, or gaps
change relation direction, negation, quantification, sentence force, code syntax,
quotation scope, numeric interpretation, or completion obligations.

Defaults can help bootstrap a declared lane, but the current cognition/realization
program calculates importance from exact AST roles, physicality trajectories,
containment, precedence, relative gaps, morphology, syntax, language, discourse,
evidence, context, goal, and firmware policy. Repeated frequency or a flat
part-of-speech score can be one observation; it cannot become universal semantic
authority.

## 8. What it means for Laplace to speak a language

Laplace does not carry a separate intelligence or opaque language model for each
language. A language is represented through shared content identities plus witnessed
and versioned language-specific state, including as applicable:

- language and script Highway coordinates;
- morphology and inflection;
- syntax and grammatical roles;
- orthography and punctuation conventions;
- word order and trajectory patterns;
- semantics-to-surface mappings;
- pragmatics, register, discourse forms, and ellipsis;
- exact examples, counterexamples, corrections, and observed realization outcomes;
- decomposition, transformation, and realization recipes;
- the evidence and activation epochs that make those recipes usable.

Language identification and language readiness are different results. Recognizing a
script, resolving an ISO language coordinate, or copying a phrase does not prove that
Laplace can realize every semantic act in that language. A composition may also be
multilingual and legitimately bind several language scopes.

`Laplace can render language L for semantic act A` is therefore a typed readiness
calculation over a pinned language, recipe, evidence, and world epoch. Readiness may be
complete, partial, ambiguous, unsupported, or unknown. It is never inferred merely
from fluent-looking output.

## 9. What Laplace renders

Cognition first selects a typed semantic act. A modality realizer then calculates exact
output content from that act.

For text, the result is an exact Unicode composition and trajectory containing the
language-specific morphology, content words, function words, punctuation, whitespace,
order, register, and pragmatic form required by the act. The receipt identifies every
reused witnessed subtree, every newly composed structure, every transformation, the
supporting propositions, the language/recipe epoch, and the final content identity.

Laplace may also render code, images, audio, model artifacts, queries, commands, or
other typed modalities. Those realizers consume the same semantic-act contract but use
modality-specific concrete structure. An English token string is never the universal
intermediate.

If a required language or realization obligation is missing, Laplace returns a typed
why-not result naming the missing morphology, syntax, referent, evidence condition,
recipe, authority, or other boundary. It must not silently fall back to English,
concatenate labels with fixed prose, or fabricate a plausible continuation.

Observed success, correction, misunderstanding, execution result, or another later
consequence may create immutable matchup events for the applicable language realizer,
operator, tool, or firmware lane. Earned standing can steer later selection, but it
cannot replace grammar, exact structure, evidence closure, or semantic completion.

## 10. Firmware controls calculation without changing truth

Firmware chooses which eligible trajectories receive work, which typed evidence lanes
are consulted, how ambiguity is investigated, which contradictions are sought, what
uncertainty is sufficient, and how the selected semantic act should be expressed. It
may select among language realizers or tool routes using their relevant earned standing
and the current goal.

Firmware cannot:

- turn a default prior into evidence;
- merge unrelated rating lanes;
- change exact identity or physical structure;
- make a source assertion true;
- hide missing language readiness;
- grant itself authority;
- mark a nonempty result as semantic completion.

Its choices and the standing inputs they consumed remain content-addressed and
replayable.

## 11. Required acceptance and deliberate defects

The complete implementation must prove:

- one-time initialization followed by carry-forward of the published state;
- real opponent rating and deviation affect the update;
- volatility is retained and used where the declared Glicko-2 contract requires it;
- source-specific standing can diverge from its source-type prior;
- return-leg corroboration and refutation update only the participants and lanes the
  outcome actually tests;
- dependent copies and self-descendants cannot manufacture matchups;
- rating-at-event and as-of epoch replay are deterministic and order-independent;
- formal and exact results remain non-voting;
- contextual role changes can make a noun, function word, punctuation mark, ordinal,
  or gap decisive;
- multilingual realization uses each language's witnessed structure without an
  English intermediate;
- incomplete language readiness yields typed abstention rather than silent fallback;
- direct native, generated ISA, PostgreSQL, managed orchestration, and public product
  routes preserve the same semantics and receipts.

Deliberate defects must reject:

- constant neutral opponent on every fold;
- re-defaulting an existing participant;
- discarding opponent rating while keeping only opponent deviation;
- substituting witness count for rating;
- permanent source literals in place of earned state;
- missing return leg;
- dependent evidence amplification;
- absence or unknown mapped to a loss;
- one global rating across unrelated lanes;
- historical-state overwrite or ingest-order-dependent replay;
- global noun/stop-word/punctuation importance;
- English stop-word deletion or punctuation stripping applied to every language;
- fluent output accepted despite missing semantic or language obligations;
- silent English fallback.

Issue #110 owns the concrete implementation. Issues #16, #17, #18, #19, #53, and #22
consume or gate the resulting product behavior. Old-repository issues #1303, #1321,
#511, and #514 are retained only as measured counterexamples; they do not define the
clean implementation's ABI, schema, defaults, or completion state.
