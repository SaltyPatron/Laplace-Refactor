# Typed trust matchups, tiered tokens, and language realization

Status: required architecture direction derived from inventor-direct requirements

This document refines the current Constitution and Invention Model. It does not replace
content identity, physicality, testimony, relation, cognition, or realization law. If a
lower-level implementation, old-repository artifact, statistical convention, or prior
assistant summary conflicts with those authorities, the higher authority wins.

## 1. Laplace speaks Unicode and renders language

Laplace does not internally speak English, Japanese, Spanish, or another natural
language. Its universal symbolic floor is the complete Unicode codepoint-position
space. In that direct sense, **Laplace speaks Unicode**.

Natural languages are witnessed interpretation and realization systems over exact
Unicode content and shared semantic state. A completed semantic act may be rendered in
English, Japanese, Spanish, code, or another supported modality without changing the
machine's internal language and without passing through English as a universal pivot.

Therefore these are different questions:

```text
What does Laplace speak internally?     Unicode.
What does Laplace render externally?    A requested witnessed language or modality.
```

Language identification, language interpretation, and language realization readiness
are also different results. Resolving an ISO language coordinate, recognizing a script,
or copying a phrase does not prove that Laplace can render every semantic act in that
language.

## 2. Laplace has tiers of tokens, not one tokenizer vocabulary

`Token` is a role a cognition, grammar, query, realization, or target-compilation recipe
assigns to an addressable canonical composition. It is not one permanent vocabulary
entry and it does not define content identity.

The universal floor contains exact Unicode codepoint positions. Above that floor,
recursively composed and deduplicated structures can be addressed at the altitude
required by the current program, including as applicable:

```text
codepoint
grapheme or other exact segmentation unit
morpheme or lexical form
word or punctuation structure
phrase or clause
sentence or utterance
record, field, table, section, document
code token, syntax node, declaration, file, repository tree
image region, audio phrase, game state, model component
any larger typed universal-AST composition
```

This is not a fixed text ladder. Structural tier is a proven floor, not identity or
semantic type. The same canonical content retains the same identity wherever it is
reused, while roles, occurrences, ordinals, source spans, language, grammar, and
interpretation remain witnessed state around it.

A program can operate at the structural altitude that preserves the distinctions it
needs and can descend to exact constituents or ascend through enclosing trajectories.
It does not have to force every calculation through one subword vocabulary.

Consequently Laplace does not need a conventional leading-space vocabulary hack such
as:

```text
"Something"
" Something"
"Something "
```

U+0020, punctuation, line endings, and every other exact constituent already exist in
the Unicode composition and physicality trajectory. The exact larger compositions also
exist when observed or generated, but they are not minted as alternate lexical tokens
merely to teach a tokenizer where a boundary occurred. Laplace does not privilege
space-delimited English tokenization at all.

The same machinery works for space-delimited language, CJK text, combining sequences,
code, tabular data, and every other exact digital structure. Target compilation may
need to lower canonical Laplace content into a conventional runtime's leading-space,
SentencePiece, BPE, byte-fallback, or other tokenizer ABI. That target artifact is a
receipted compatibility projection. It never redefines the native Laplace token system.

## 3. Physicality and attestation remain distinct at every token tier

Physicality answers what the exact composition mechanically contains and how it is
ordered:

```text
identity
constituents
ordinal and run
containment and ancestry
precedence and following
relative gap
recurrence
shape, centroid, radius, locality, and trajectory
```

Those are calculated structural facts under a pinned physicality recipe. They do not
need a source vote.

Attestation answers what a witness, source, analyzer, grammar, or calculation claims
about that content or structure:

```text
language
morphological or grammatical role
referent
meaning
relation
source attribution
context and time
positive, negative, uncertain, or unknown outcome
provenance and dependence
```

A word, punctuation mark, phrase, sentence, or larger structure can therefore be an
exact physical composition while carrying several agreeing or conflicting witnessed
interpretations. Neither layer overwrites the other.

This is why the tiered token system can serve both exact retrieval and AI-like forward
calculation. Physicality supplies the addressable strands; attestations and their
standing supply attributed interpretations; firmware and the current goal determine
which typed strands are relevant to semantic completion.

## 4. Defaults, standing, and importance are different things

Laplace keeps three quantities separate:

1. A **default prior** initializes a rating-eligible participant when no prior state
   exists for the exact declared lane. It expresses initial uncertainty. It is not an
   observation, source endorsement, proof, token importance, or permanent score.
2. **Standing** is a versioned derived state calculated from immutable typed matchup
   events under a pinned evidence boundary and recipe. It includes the complete rating
   state required by the declared mathematics, not merely one scalar.
3. **Importance** is calculated for the current goal, semantic act, token tier,
   language, context, evidence scope, and completion obligations. It is not stored as a
   timeless entity, part-of-speech, source, relation, or token rank.

Conflating these produces known failure modes: permanent source-type trust, a consensus
cell replaying the same neutral opponent, a witness counter disguised as Glicko-2, a
global noun-over-function-word ranking, or a fluent language fallback that bypasses
missing realization evidence.

## 5. What Glicko-2 is for

Glicko-2 updates competitors from outcomes while accounting for the strength and
uncertainty of the opponents they actually faced and for instability in the rated
participant. Its useful state is not a flat score:

```text
rating            current estimated standing
rating deviation  uncertainty in that estimate
volatility        expected instability or rate of change
```

The same nominal result against two different opponents is not the same evidence. A
win against a strong established opponent is more surprising than a win against a weak
or highly uncertain opponent, so the update differs.

Laplace uses this mathematics only when a typed recipe declares a legitimate
outcome-bearing matchup. Glicko-2 does not define tokenization, relation meaning, truth,
traversal, semantic similarity, physical structure, or universal importance. It is one
admissible standing calculation over a declared event stream.

## 6. Matchups are typed events, not flat-score joins

A matchup exists only when a recipe identifies:

- the rated participants and their roles;
- the exact lane in which they are compared;
- the proposition, prediction, operation, transition, realization, or capability whose
  outcome is observed;
- both prior rating/deviation/volatility states;
- eligible evidence roots and dependence reduction;
- the typed outcome and its declared mapping into the rating calculation;
- valid time, observation time, event order, context, world, authority, and evidence
  boundary;
- the calculation program, parameters, provider, result, and receipt.

A rating lane can be scoped by relation, source role, language, modality, operation,
tool, firmware, token tier, context, time, or another typed coordinate when the
contract requires that distinction. The same content entity can have different
legitimate standings in different lanes. One global score cannot substitute for them.

Co-occurrence, frontier competition, selection, or repeated occurrence does not by
itself create a match. A separately meaningful observed outcome must test the declared
participant and capability.

## 7. New-X onboarding happens exactly once per rating key

`New X` means a rating-eligible participant or cell has no prior completely published
state for the exact typed rating key.

```text
immutable event is admitted
    -> resolve exact typed rating key
    -> read latest completed state at the event boundary
    -> no prior state: initialize from the lane's versioned default prior
    -> execute the first eligible matchup against the actual opponent state
    -> publish immutable rating-at-event state and receipt
    -> every later event consumes that published state, never the default again
```

Initialization is explicit so replay distinguishes `no prior state` from zero. Rating,
deviation, and volatility defaults are versioned parameters, may vary only where a lane
justifies that difference, and are not evidence.

Reinitializing an existing participant or substituting a neutral default because the
real opponent was not resolved is a hard semantic defect. Late or corrected events
publish a new deterministic replay epoch without overwriting prior rating-at-event
history.

## 8. Trust is earned through both legs of the evidence cycle

A source or witness can testify about a proposition. That testimony can participate in
calculating the proposition's standing using the source's applicable prior state.

Trust becomes earned only when a later, separately observed or independently
adjudicated result supplies a return leg:

```text
source asserts or opposes proposition
    -> later independent evidence corroborates, refutes, calculates, or tests it
    -> the exact outcome bears on that participant
    -> the appropriate source/witness/operator/realizer lane is updated
```

Mirrors, quotations, copied datasets, model descendants, and self-generated outputs do
not manufacture independent matches. The proposition-facing leg and participant-facing
return leg are separate events and receipts and cannot recursively certify each other.

Source type, relation type, grammar role, modality, language, tool class, and firmware
class may select a recipe or initialize a prior. They do not permanently determine an
individual participant's earned standing.

## 9. Exact and formal results do not enter a popularity contest

Content identity, physicality structure, and valid formal derivation are not source
votes. Exact trajectory containment, precedence, ordinals, gaps, and reconstruction
follow from the physicality recipe. A formally closed arithmetic result follows from
the active calculus. Glicko standing cannot override either.

Those exact outcomes can still update participants that made claims or performed
operations. If formal arithmetic derives `2 + 2 = 4` while a source asserts `2 + 2 = 5`,
the theorem remains entailed regardless of copied vote volume, while the independent
outcome may reduce the source's relevant reliability lane.

## 10. Nouns, function words, punctuation, whitespace, and token tiers have no universal rank

A noun is not always more important than a function word, so-called stop word,
punctuation mark, ordinal, gap, or whitespace structure. The decisive unit may live at
any token tier.

```text
"A is B"        versus "A is not B"
"only A"        versus "A only"
"dogs bite"     versus "do dogs bite?"
"f(x, y)"       versus "f[x, y]"
"1,000"         versus "1.000" under different locale conventions
"Captain Ahab"  where exact order and one intervening Unicode position matter
```

The current program calculates importance from the completed semantic obligation,
exact AST roles, physicality trajectories, containment, precedence, relative gaps,
morphology, syntax, language, discourse, evidence, context, goal, and firmware policy.
Repeated frequency or a flat part-of-speech score can be an observation; it cannot
become universal semantic authority.

## 11. What Laplace renders

Cognition first selects a typed semantic act. A modality realizer then calculates exact
output content from that act.

For natural-language text, the result is an exact Unicode composition and trajectory
containing the language-specific morphology, lexical and grammatical structures,
function words, punctuation, whitespace, order, register, and pragmatic form required
by the act. The receipt identifies reused witnessed subtrees, newly composed
structures, transformations, supporting propositions, the language/recipe epoch, and
the final content identity.

Laplace may also render code, images, audio, model artifacts, queries, commands, or
other typed modalities. Those realizers consume the same semantic-act contract but use
modality-specific concrete structure. An English token string is never the universal
intermediate.

If a required language or realization obligation is missing, Laplace returns a typed
why-not result naming the missing morphology, syntax, referent, evidence condition,
recipe, authority, or other boundary. It does not silently fall back to English,
concatenate labels with fixed prose, or fabricate a plausible continuation.

Observed success, correction, misunderstanding, execution result, or another later
consequence may create immutable matchup events for the applicable language realizer,
operator, tool, or firmware lane. Earned standing can steer later selection but cannot
replace exact structure, language evidence, or semantic completion.

## 12. Firmware controls calculation without changing truth

Firmware chooses which eligible trajectories and token tiers receive work, which typed
evidence lanes are consulted, how ambiguity is investigated, what contradictions are
sought, what uncertainty is sufficient, and how the selected semantic act is realized.
It may select among language realizers or tool routes using applicable earned standing.

Firmware cannot turn a default into evidence, merge unrelated rating lanes, change
exact identity or physicality, make a source assertion true, hide missing realization
readiness, grant itself authority, or mark a nonempty result as semantic completion.
Its decisions and standing inputs remain content-addressed and replayable.

## 13. Required acceptance and deliberate defects

The complete implementation proves:

- the native machine speaks Unicode while natural languages are typed renderings;
- codepoint through higher-composition token tiers remain addressable without one
  global tokenizer vocabulary;
- leading-space, stop-word, and punctuation hacks are not native identity or language
  laws;
- one-time rating initialization followed by carry-forward of published state;
- real opponent rating and deviation affect each eligible update;
- volatility is retained where the declared Glicko-2 contract requires it;
- source-specific standing can diverge from its source-type prior;
- return-leg outcomes update only the participants and lanes actually tested;
- dependent copies and self-descendants cannot manufacture matchups;
- rating-at-event and as-of epoch replay are deterministic and order-independent;
- exact structure and formal results remain non-voting;
- contextual role can make any token tier, function word, punctuation mark, ordinal, or
  gap decisive;
- multilingual realization uses each language's witnessed structure without an
  English intermediate;
- incomplete language readiness yields typed abstention rather than silent fallback;
- direct native, generated ISA, PostgreSQL, managed orchestration, and public product
  routes preserve the same semantics and receipts.

Deliberate defects reject:

- one fixed tokenizer vocabulary as the native language;
- leading-space lexical identity or English space-boundary dependence;
- flattening all token tiers to one segmentation;
- constant neutral opponent on every fold;
- re-defaulting an existing participant;
- discarding opponent rating while retaining only opponent deviation;
- witness count substituted for rating;
- permanent source literals in place of earned state;
- missing return leg or dependent evidence amplification;
- absence or unknown mapped to a loss;
- one global rating across unrelated lanes;
- historical-state overwrite or ingest-order-dependent replay;
- global noun, stop-word, punctuation, or token-tier importance;
- English stop-word deletion or punctuation stripping applied to every language;
- fluent output accepted despite missing semantic or language obligations;
- silent English fallback.

Issue #110 owns the concrete implementation. Issues #16, #17, #18, #19, #53, and #22
consume or gate the resulting behavior. Old-repository issues #1303, #1321, #511, and
#514 are measured counterexamples only and do not define the clean implementation's
ABI, schema, defaults, or completion state.
