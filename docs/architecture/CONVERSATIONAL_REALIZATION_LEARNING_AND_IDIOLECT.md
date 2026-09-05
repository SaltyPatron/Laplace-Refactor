# Conversational realization learning and learned idiolect

Status: inventor-direct architecture clarification, 2026-09-04.

Primary semantic owner: #18 (`persistent discourse, semantic acts, and modality realization`).
Procedural-learning owner: #169 (`Gödel procedural memory / habits / muscle memory`).
Supporting owners: #16/#110 for witnessed outcomes and standing, #17/#182 for cognition and prompt-root execution, #218 for task-scoped user adaptation plus safety/audience admissibility, and #184 for cross-repository cohesion.

This document does **not** create a new personality subsystem, user-profile ontology, inference engine, or issue owner. It records a consequence of the existing Laplace machine: repeated conversational realization choices and their observed consequences can become learned realization programs and habits, producing an emergent conversational idiolect or "accent" while semantic content, truth, audience, safety, and user-specific adaptation remain separate state.

## 1. Conversation is an unusually dense learning stream

A conversation continuously produces exact ordered observations:

```text
prompt / turn occurrence
-> interpreted semantic act and obligations
-> selected cognition program
-> selected realization program
-> exact emitted Unicode composition
-> user continuation / correction / clarification / acceptance / rejection
-> later task or external consequence
```

Each turn has physical structure, order, containment, gap, recurrence, speaker/session/time context, discourse relations, selected program identity, and eventually some amount of outcome evidence.

This means conversational learning does not need a separate opaque style model. The ordinary substrate already retains the operands required to discover recurring realization behavior.

Laplace's own generated output is immediately a legitimate witnessed physical observation of what Laplace emitted. It is **not** independent evidence that the output was good. Later user/world outcomes provide the return legs that can validate, reject, narrow, or contextualize the realization program.

Silence or mere repetition must not automatically count as success. Positive, negative, neutral, unresolved, changed-precondition, and missing-outcome states remain distinct.

## 2. Semantic act and realization are different state

The same completed semantic act may admit many valid surface realizations.

For the same proposition/result:

```text
2 + 2 = 4
```

all of the following may be legitimate realizations under different active scopes:

```text
Four.
That gives you four.
2 + 2 evaluates to 4.
Yep — four.
```

The semantic result does not change merely because the realization does.

Required separation:

```text
semantic proposition / act / obligations
!= language / grammar realization scope
!= register / phrasing / discourse strategy
!= learned conversational idiolect
!= user-specific accommodation
!= audience/presentation admissibility
!= safety/effect admissibility
```

A learned realization habit may choose among semantically valid realizations. It cannot repair an incomplete semantic act by making the prose sound fluent, change truth, erase required distinctions, or bypass audience/safety obligations.

## 3. "Accent" means an emergent realization idiolect, not one scalar personality

The useful analogy is an accent or idiolect: a recognizable pattern formed by many small recurring realization choices rather than one global `PERSONALITY = X` field.

Eligible learned realization dimensions can include, when relevant:

- sentence and paragraph length;
- terse versus elaborated answers;
- lexical/register choices;
- contractions and conversational particles;
- punctuation and whitespace conventions;
- ordering of explanation, example, evidence, caveat, and conclusion;
- prose versus table/list/code presentation when semantically interchangeable;
- amount and placement of headings;
- preferred clarification forms;
- how prior turns are referenced;
- transition phrases and discourse markers;
- code-comment and diagnostic realization conventions;
- modality-specific phrasing, prosody, timing, or presentation where admitted;
- language-specific pragmatics that cannot be reduced to English style coordinates.

These choices remain typed and scoped. There is no requirement to flatten them into one style vector or one "friendly/formal" score.

Two Laplace instances can begin from the same foundational seed and later develop measurably different realization habits because their witnessed conversational histories differ. That difference is learned experience, not different truth.

## 4. System idiolect and user accommodation are separate

Laplace may learn broad realization procedures from many validated conversations and acquire a default idiolect for a firmware/instance/context family.

Separately, #218 permits user/principal-scoped procedural adaptation where repeated outcomes justify it.

These are different lanes:

```text
system / firmware / instance realization habit
    = what has generally worked for this realization context

user-scoped realization habit
    = what has repeatedly worked with this principal for this task/context family
```

Neither lane is a free-form psychological profile.

A user's vocabulary or prose style does not by itself establish age, personality, diagnosis, protected attribute, intent, or another unrelated latent claim. User accommodation is evidence about successful interaction procedures, not authority to infer who the person "is."

Cross-user leakage is a defect unless an explicitly authorized aggregate learning program produces a separately scoped system-level habit without exposing or impersonating another user's private state.

## 5. Realization patterns are discovered like other procedures

The #169 learning ladder applies directly:

```text
primitive realization traces
-> recurring structural + discourse + outcome motif
-> candidate reusable realization program
-> held-out evaluation + counterexamples
-> activated realization skill
-> firmware schedules it earlier under matching state = habit
-> repeated stable physical work may gain parity-proven acceleration = muscle memory
```

Structural candidate discovery may use exact AST/physicality motifs, ordinals/gaps, recurrence, S3 calculations, Fréchet, Hausdorff, Karcher-derived summaries, Hilbert locality, or other #168 calculations.

Those signals can answer questions such as:

```text
"Have I repeatedly solved this kind of realization problem with the same structural progression?"
```

They cannot answer by themselves:

```text
"Does this wording mean the same thing?"
"Does this user want this style now?"
"Is this realization correct?"
```

Semantic-act parity, discourse state, language/grammar, current instructions, corrections, and observed outcomes must validate the abstraction.

## 6. Conversational trajectory learning should reduce rediscovery

Early in its experience, Laplace may have to explore many realization alternatives because little outcome history exists.

Conceptually:

```text
few realization observations
-> broad candidate exploration
-> more clarification / conservative defaults
-> weak realization habit standing
```

As compatible outcomes accumulate:

```text
many witnessed realization/outcome traces
-> recurring successful procedures become obvious
-> invalid/corrected forms accumulate negative evidence
-> familiar realization programs are scheduled earlier
-> less exploration is required
-> stable patterns become the machine's conversational idiolect
```

That is ordinary learning: accumulated experience changes future realization behavior.

Foundational language sources provide grammar, lexical meaning, morphology, pragmatics, discourse forms, and other scaffolding so observations land in an already structured space. Conversation then supplies lived usage and outcome evidence rather than requiring the machine to search blindly through an unstructured output universe.

## 7. Return legs remain lane-specific

One response can succeed in some realization dimensions and fail in others.

Example:

```text
Laplace emits response R

factual/semantic content        positive
requested language              positive
register                         negative
verbosity                        negative
safety/audience                  positive
clarification strategy           neutral
```

There is no one global "good response" scalar that should overwrite those distinctions.

A user correction such as:

```text
"Yes, but stop using headings for these."
```

can be a negative/corrective return leg for one presentation lane while preserving positive standing for semantic correctness, language, or task completion.

Likewise:

```text
"That's exactly what I meant."
```

can support the selected semantic/realization program only to the extent permitted by the declared matchup/outcome recipe and dependence boundary.

Glicko-2 may summarize a legitimate typed outcome lane, but the exact event/trajectory remains primary evidence and realization inference must not collapse to one rating.

## 8. Current instruction and context always remain live

Learned conversational style is a scheduling preference over otherwise admissible realization choices.

A current instruction such as:

```text
Be formal.
Keep this to one sentence.
Do not use contractions.
Give me the raw table.
Do not translate this.
```

must override an incompatible learned realization habit for the current act and provide corrective outcome evidence where applicable.

Changed language, modality, audience, task, world, discourse, firmware, or safety/effect state may change which realization habit is eligible.

A habit is not a timeless style law.

## 9. Same semantics, different histories is a required proof

A core acceptance fixture should start two isolated Laplace histories from the same foundational seed and give them different validated conversational experience.

For example:

```text
history A repeatedly rewards:
    concise prose
    minimal headings
    direct corrections
    contractions

history B repeatedly rewards:
    structured explanations
    explicit definitions
    longer examples
    formal register
```

Later both receive the same semantic request under otherwise equivalent conditions.

Required result:

- both compile the same semantic obligations/result where the task is the same;
- both remain factually/semantically equivalent;
- their exact realization compositions may differ in the learned dimensions;
- receipts explain which realization history/program influenced each output;
- neither history changes canonical truth or source evidence;
- resetting/removing the learned realization state removes the divergence without changing the underlying semantic result.

That is an executable demonstration of an emergent learned "accent."

## 10. User-scoped accommodation fixture

For one user, repeated compatible outcomes may establish a scoped preference such as terse technical replies or Japanese realization for a recurring task family.

A different user must not inherit that procedural history merely because the canonical task content is identical.

Acceptance must distinguish:

```text
same content identity
+ user A occurrence/context/history
!=
same content identity
+ user B occurrence/context/history
```

The content remains shared; the realization-program eligibility differs because occurrence/outcome context differs.

An explicit current instruction from either user wins over prior habit state.

## 11. Self-generated style cannot self-certify

Laplace can observe that it emitted a phrase repeatedly. That proves recurrence of its own output, not that the phrase is desirable.

This loop is invalid as independent support:

```text
Laplace emits style S
-> later sees its own output
-> emits style S again
-> counts repetition as independent success
```

Valid promotion requires an admitted outcome contract: user continuation/correction, task completion, independent evaluation, external effect/result, explicit preference, or another eligible return leg.

Self-generated descendants remain useful structural/procedural evidence while dependence prevents them from manufacturing independent corroboration.

## 12. Safety and audience remain invariant

A learned idiolect cannot bypass #218.

If a colloquial habit would surface wording inappropriate for the active audience scope, audience realization constrains the permitted surface form.

If a learned conversational habit would produce an inadmissible effect, the effect is rejected/rerouted under the same common safety contract as any primitive realization path.

A style fast path that omits those checks is not semantically equivalent muscle memory.

## 13. Required receipts

A conversation/realization receipt must be able to expose, as applicable:

```text
semantic act / proposition / unresolved obligations
language + grammar realization scope
candidate realization programs considered
selected realization program / skill / habit identity
system-level realization habit evidence
user/principal-scoped habit evidence
structural trajectory/metric candidate evidence
semantic/discourse parity evidence
current explicit style/presentation constraints
audience + safety admissibility state
exact emitted content identity
later outcome/correction/return leg
updated lane-specific realization standing
```

This makes the learned accent inspectable instead of hiding it in opaque parameters.

## 14. Acceptance

A conforming implementation proves at minimum:

1. repeated validated realization outcomes change later realization scheduling without retraining a checkpoint;
2. the same semantic act can produce different exact but semantically equivalent realizations under different learned histories;
3. two isolated histories starting from the same foundational seed can develop different measured conversational idiolects;
4. semantic/factual state remains unchanged when only the realization habit changes;
5. explicit current style instructions override incompatible learned habits;
6. a user-scoped realization habit does not leak to another user;
7. system-level learned realization behavior is separately scoped from user-specific accommodation;
8. a structurally similar but semantically different turn cannot inherit a realization program solely through Fréchet/geometry;
9. a structurally different paraphrase can reuse an eligible realization skill when semantic/discourse evidence supports it;
10. user correction can demote one presentation/register lane without marking the whole answer globally bad;
11. self-generated output repetition does not count as independent positive return legs;
12. missing/neutral outcome is not silently converted into success;
13. language-specific realization learning does not require an English intermediate;
14. audience/safety constraints remain identical across primitive, habit, firmware, and muscle-memory realization paths;
15. native/PostgreSQL/managed/Conversation/MCP/OpenAI-compatible routes expose the same realization-program identity and receipt meaning.

## 15. Deliberate defects

Reject:

- one global personality/style scalar as the conversation model;
- fixed `friendly/formal/concise` labels replacing learned typed realization state;
- user prose style treated as age/personality/diagnosis evidence without a separately authorized operation;
- one prior preference becoming permanent global style;
- cross-user leakage of private procedural history;
- surface-frequency or n-gram recurrence promoted directly to semantic correctness;
- Fréchet/geometry promoted directly to realization intent;
- output fluency accepted as semantic completion;
- Laplace repeatedly observing its own style counted as independent success;
- silence/missing return leg counted as a win;
- learned style overriding explicit current instructions;
- learned idiolect changing factual/testimony state;
- style metadata entering canonical content identity;
- English style coordinates becoming the universal realization intermediate;
- learned conversational fast paths bypassing audience/safety obligations;
- opaque learned style with no drill-through to observations, programs, outcomes, and receipts.

## 16. Ownership

- #18 owns persistent discourse, semantic acts, realization selection, exact output, and conversational idiolect behavior.
- #169 owns induction/validation of reusable realization programs, habit scheduling, and optional muscle-memory acceleration.
- #16/#110 own witnessed outcomes, dependence, typed matchup events, and derived standing where applicable.
- #17/#182 own the cognition program that produces the completed semantic act before realization.
- #218 owns user/task-scoped adaptation boundaries plus firmware-independent safety/audience admissibility.
- #168 supplies structural metric candidate channels without semantic authority.
- #184 records the cross-repository ownership map and prevents this clarification from becoming another duplicate issue.
