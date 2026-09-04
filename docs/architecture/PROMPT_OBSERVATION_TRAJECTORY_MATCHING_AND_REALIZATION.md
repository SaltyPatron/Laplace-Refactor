# Prompt observation, trajectory matching, tier fallback, and realization

Status: inventor-direct architecture clarification, 2026-09-04. This record is additive to the Constitution, Invention Model, cognition execution law, #16/#17/#18/#19/#110/#132/#168/#169/#170/#182, and the prompt-trunk/structural-geometry records. It exists to prevent the input and output halves of Laplace cognition from being separated, flattened, or reimplemented as conventional intent routing or token generation.

## 1. Exact prompt law

A user prompt is ordinary Laplace content.

```text
exact input bytes
-> selected Unicode/grammar decomposition
-> canonical compositions over the Unicode floor
-> one canonical prompt trunk/root
-> exact physicality/trajectory state
-> one occurrence carrying user/session/turn/source/time/context
```

The prompt trunk is not a summary, topic, embedding, intent label, or lossy feature vector. The exact content remains reconstructable bit-for-bit under the selected reversible recipe. Same exact content reuses the same canonical identity; each occurrence remains independently reconstructable.

The prompt is simultaneously:

- exact content;
- a Merkle-DAG composition of smaller exact content;
- a building block that may occur inside larger content;
- an ordered trajectory/physicality object;
- a cognition root for the current turn;
- later, an addressable object that may itself be compared, quoted, inspected, or reused.

No later interpretation is allowed to replace or rewrite that original prompt state.

## 2. Observation is not attestation

Production observations and seeded testimony are different state classes.

### User prompts

Admitting a user prompt creates canonical content, occurrence/observation state, physicality/trajectory state, and the normal execution/readback receipts.

**It creates zero semantic attestations merely because the user said it.**

For:

```text
User: "The Earth is flat."
```

Laplace knows exactly that this user produced this content in this turn. Cognition may later infer that the utterance functions as an assertion of proposition `P := Earth-is-flat`, but that inference still does not manufacture a seed-style testimony row from the prompt.

A contradiction exists only if a later cognition/adjudication program compares the interpreted proposition with eligible independently admitted facts, standards, calculations, or observations under compatible scope and obtains conflicting state. The prompt itself is not a contradiction.

If an independently adjudicated outcome establishes that the user's assertion was wrong, that later outcome may create a legitimate typed matchup for the user's reliability lane. The user can therefore lose earned standing without the original prompt ever becoming an attestation.

```text
prompt observation
-> interpret assertion candidate
-> independently adjudicate/test proposition
-> observe outcome
-> update user reliability standing where the rating recipe declares a legitimate matchup
```

The immutable utterance remains unchanged.

### Laplace cognition and generated output

Laplace's internal cognition, intermediate states, hypotheses, selected operations, receipts, and generated responses likewise create **zero independent semantic attestations merely by being produced**.

They may create:

- derived calculation state;
- hypotheses;
- semantic-act state;
- execution/program/receipt state;
- generated content and its occurrence;
- dependence-linked derived propositions;
- observed consequences of an executed action.

None of those becomes an independent root supporting itself.

### Seeded testimony

The selected seed estate supplies the initial attributed semantic/factual/linguistic/pragmatic scaffolding. Examples include, as applicable, WordNet, OMW/CILI, Wiktionary/dictionaries, Universal Dependencies, ConceptNet, ATOMIC-family resources, FrameNet, PropBank, VerbNet, SemLink, standards, curated factual sources, multilingual corpora and other configured seed profiles.

Those sources are admitted through their declared profiles and may contribute explicit typed attestations, grammar/sense/role mappings, definitions, relations, facts, examples, corrections, negative evidence and provenance according to their source contracts.

Production text/image/audio/video/game/code/user activity normally extends the world first as exact observed content/occurrences/trajectories. It does not need to mint semantic attestations for every observation in order to participate in cognition.

## 3. Exact-first prompt comparison

When cognition asks "have I seen/thought about something like this before?", the candidate-discovery order is exact-first and lossless-first.

Representative ladder:

```text
current prompt trunk P

1. exact canonical identity
   - has this exact content existed before?
   - where did it occur?

2. exact structural reuse
   - exact constituent identities
   - exact enclosing/composed subtrees
   - exact prefixes/suffixes where the recipe defines them
   - exact trajectory prefixes/subpaths
   - exact repeated ordinal/gap/containment motifs

3. structural-altitude / tier fallback
   - if the current whole is insufficiently witnessed or unresolved, descend to smaller exact compositions
   - retain the whole prompt root and every higher-level relation while examining lower tiers

4. structural candidate metrics
   - angular/geodesic calculations on canonical physicality coordinates
   - realized-curve Frechet distance
   - Hausdorff/Karcher/Hilbert and other declared structural calculations
   - motif/AST/trajectory-family comparison

5. semantic/evidence/discourse comparison
   - seeded lexical/sense/grammar/semantic relations
   - usage/occurrence state
   - discourse/session bindings
   - compatible world/time/context
   - contradiction/standing/outcome state
   - prior successful and failed cognition trajectories

6. dynamic next-operation selection
   - candidate paths/skills/programs are evidence for what to try next
   - no structural or semantic candidate source alone becomes automatic dispatch
```

A higher-cost approximate/geometric comparison must never replace a cheaper exact identity/substructure test when the exact test is available.

## 4. Tier fallback is Laplace's native fallback

Laplace does not natively require conventional BPE/SentencePiece/byte-fallback semantics. Those are compatibility projections for target runtimes.

Laplace's native fallback is **structural altitude/tier fallback**.

For text, one possible selected recipe may expose:

```text
utterance/document composition
-> sentence/clause/phrase
-> lexical/morphological composition
-> grapheme or selected segmentation unit
-> Unicode codepoint floor
```

That is illustrative, not a mandatory English ladder. Code, images, audio, games and other modalities expose their own universal-AST compositions over the same canonical floor/typed numeric machinery.

Cognition starts at the highest justified structure and descends only as needed. Falling back does not discard the unresolved parent; it expands the exact structure available to the current program.

Therefore an unknown phrase can still be reasoned about through known words, graphemes, codepoints, surrounding containers, usage trajectories and seeded evidence without reminting a private tokenizer token.

## 5. Prompt trajectory and collision questions

The prompt is an exact trajectory-bearing structure, so cognition may ask several distinct "where does this collide?" questions:

```text
exact content collision?
exact constituent/subtree collision?
exact trajectory prefix/subpath collision?
recurrence/ordinal/gap motif collision?
canonical S3/angular neighborhood collision?
Frechet/Hausdorff structural-curve collision?
lossy projection collision?
semantic relation/evidence intersection?
discourse/pragmatic trajectory intersection?
```

These are different channels. No channel may impersonate another.

### Borsuk-Ulam boundary

For any continuous `f:S^3 -> R^3`, Borsuk-Ulam guarantees at least one antipodal pair with the same projection. Thus a 3-D or lower-dimensional continuous view cannot be globally injective over the full canonical S3 domain.

Consequences:

- projected equality is never canonical identity;
- projected equality is never trajectory equality;
- projected equality is never semantic equivalence;
- projection collision is a candidate/disambiguation signal, not proof;
- intrinsic/full-state exact checks remain available behind lossy views;
- all projection identities/losses are receipted.

Borsuk-Ulam does not make canonical antipodes equivalent and does not govern the discrete exact mantissa-packed trajectory/address carrier.

## 6. Similar surface forms do not hardcode an operation

Consider:

```text
A: "Translate this to Japanese."
B: "Can you give this to me in Japanese?"
C: "Say this in Japanese."
D: "Translate this point three units to the left."
```

The whole-trunk identities are different.

Exact lower-tier collisions may include `this`, `Japanese`, or `Translate`. Those collisions are useful observations but do not define an intent.

The full structural/evidence trajectories may later show A/B/C converging toward a compatible unresolved realization obligation while D diverges toward a geometric transformation. That conclusion must be earned from the whole observation, seeded language/grammar/sense evidence, current discourse, and compatible prior trajectories.

Reject:

```text
if token == "translate" -> TRANSLATE_OPERATION
if token == "Japanese" -> SET_LANGUAGE_JA
```

A finite/versioned ISA may contain primitive operations for search, structural calculation, realization, execution, etc.; the hardcoding defect is surface wording deciding which operation fires without the cognition program earning that selection.

## 7. Implicit discourse and social response obligations

Not every user observation is an instruction or question.

Example:

```text
"Hey Laplace, they promoted me to fire captain today."
```

The prompt creates zero attestations. Cognition may nevertheless infer, from the whole exact observation plus seeded/observed language and pragmatic evidence, that it is a personal-news disclosure and that an appropriate response should acknowledge the disclosure, preserve its epistemic status as user-reported, match the speaker's apparent stance when justified, and possibly continue the conversation.

This must not be implemented as:

```text
if contains("promoted") -> CONGRATULATE
```

Changed discourse can change the response obligation: an unwanted promotion, a sarcastic statement, a quotation, a fictional character, or a corrected prior interpretation may require a materially different semantic act even with similar surface words.

The intelligence is selecting the justified semantic response act, not the existence of a canned congratulatory sentence.

## 8. Understanding and response formulation are two coupled halves

A conversation forward pass is incomplete after interpretation.

```text
OBSERVE exact prompt trunk + occurrence
-> COMPARE exact/structural/semantic/prior trajectory state
-> INTERPRET competing senses/referents/acts
-> RESOLVE current discourse/goal/obligations
-> EXECUTE cognition until semantic completion or typed WHY_NOT
-> SELECT response semantic act
-> REALIZE exact requested language/modality content
-> WITNESS/record output occurrence + execution receipt
-> observe later consequence/outcome
```

The response semantic act may include propositions, roles, unresolved state, presentation constraints, pragmatic stance, requested modality/language, register and required distinctions before surface generation begins.

## 9. Realization uses exact-first reuse and inverse tier fallback

Realization is not autoregressive token probability over a fixed vocabulary.

For a completed semantic act, the realizer may:

```text
1. find exact compatible witnessed realizations/substructures
2. reuse exact canonical subtrees when semantically and pragmatically valid
3. compare language-specific morphology/syntax/usage/trajectory evidence
4. descend to smaller realizable structural units when a larger structure is unavailable
5. compose upward into a new exact canonical output
6. verify every constituent/order/grammar/semantic obligation
7. return the exact output identity + complete receipt
```

This is the inverse counterpart of cognition's tier fallback. The realizer can fall from utterance to clause/phrase to lexical/morphological to smaller exact units without leaving Laplace's native representation.

English is never the universal intermediate. The same semantic act may descend and recompose through Japanese, English or another language's own witnessed grammar/usage structure.

Observed n-gram/continuation trajectories may contribute candidate realization structure, but they cannot decide semantic truth or the response act and cannot be the only candidate source.

## 10. Procedural memory, Gödel and self-correction

Successful and failed cognition/realization executions remain persistent typed trajectories. They do not attest to their own semantic correctness.

Gödel may compare those trajectories using exact structural motifs, Fréchet/angular/Hausdorff/etc. candidate calculations and separately typed semantic/outcome state. It may propose:

- a reusable cognition/realization program;
- a new typed operator/calculus candidate;
- a firmware scheduling habit for an already validated skill;
- a semantically equivalent indexed/perfcache/native acceleration after parity proof.

Activation requires the applicable held-out/counterexample/authority laws. A new program or acceleration is not an attestation merely because Laplace generated it.

Self-correction becomes legitimate when later independent world observations, formal calculations, user/tool consequences, or other outcome-bearing evidence test the program or participant. Those outcomes may update typed standing or trigger a new candidate program while preserving the original execution history.

Thus Laplace can eventually learn on its own without allowing "I thought it, therefore it is true" self-corroboration.

## 11. Required acceptance fixtures

### Exact prompt identity and occurrence

- identical prompt content in two turns has one canonical trunk identity and two independent occurrences;
- a one-byte/codepoint/punctuation/order change produces the expected different affected composition identities;
- exact reconstruction returns the original prompt bit-for-bit;
- prompt admission produces zero semantic attestations.

### Exact-first matching and tier fallback

- an exact whole-trunk match is discovered before approximate/geometric candidates;
- a novel whole prompt with known lower-tier substructures descends without losing the original root;
- a controlled unknown higher-tier composition remains addressable through lower exact constituents;
- a deliberate fixed tokenizer/BPE/byte-fallback-as-native implementation fails.

### Subtrajectory matching

- a short prompt/AST trajectory can match a subtrajectory of a larger witnessed trajectory without requiring whole-graph comparison;
- order-sensitive Fréchet differs on reversed/reordered paths where Hausdorff may remain unchanged;
- packed trajectory IDs are resolved to real coordinates before geometric curve calculations;
- convergent endpoints do not erase distinct paths/occurrences.

### Projection collisions

- a controlled lossy projection collision leaves canonical identities and full-state trajectories distinct;
- projected equality cannot select a semantic act or certify equivalence;
- Borsuk-Ulam/projection loss is explicit in the receipt.

### Language-operation discrimination

Use A/B/C/D above. A/B/C may converge on compatible language-realization obligations only when the selected evidence supports it; D must remain a distinct geometric case. No keyword/regex dispatcher may pass.

### Implicit discourse expectation

Use the fire-captain disclosure with positive, unwanted/sarcastic, quoted/fictional, and ambiguous variants. The semantic response act must change with discourse/evidence while the literal prompt remains exact. A canned `promoted -> congratulations` mutant fails.

### Observation/attestation boundary

- user prompts create zero attestations;
- internal cognition and generated responses create zero independent attestations;
- seeded sources can contribute explicit typed testimony according to source profiles;
- an independently adjudicated false user assertion can reduce the user's relevant reliability standing through a later matchup without retroactively changing the prompt;
- repeated self-output cannot increase independent evidence-root count.

### Response realization

- one completed semantic act realizes through English, Japanese and another structurally different language using each language's own evidence;
- no English pivot is required;
- exact compatible witnessed subtrees can be reused;
- unavailable larger structures fall back through lower tiers and recompose exactly;
- unsupported realization returns typed WHY_NOT instead of fluent fabrication.

### Procedural learning

- repeated successful/failed traces can produce one candidate reusable program;
- a counterexample rejects an overgeneralized surface shortcut;
- a validated habit reduces exploratory work but changed context can demote it;
- a muscle-memory implementation preserves semantic/result/receipt parity with lower physical work;
- none of these artifacts self-certifies as testimony.

## 12. Deliberate defects

Reject all of the following:

- prompt text stored only as embedding/topic/summary;
- user prompt admission creating semantic attestations;
- internal thoughts or generated prose creating independent testimony;
- keyword/regex intent routing;
- exact prompt match skipped in favor of ANN/KNN/geometric search;
- one fixed tokenizer vocabulary or conventional byte fallback as native representation;
- tier fallback that discards the unresolved parent/root;
- packed trajectory payload treated as live S3 geometry;
- projection collision treated as identity or semantic equivalence;
- Fréchet/angular/geometric similarity treated as semantic authority;
- response act inferred from one privileged noun/token;
- canned social response dispatch from surface words;
- realization as fixed prose scaffolding around retrieved labels;
- English as universal intermediate;
- semantic truth manufactured by fluent continuation;
- self-generated descendants counted as independent evidence;
- Gödel-created program/fast path activated without the declared validation/authority boundary.

## 13. Ownership and non-priority rule

This record does not create a new program phase or reorder the existing finish-line sprint. It sharpens existing owners:

- #7 — exact identity/composition/physicality/trajectory;
- #16/#110 — testimony, dependence, outcomes and earned standing;
- #17/#132 — cognition/provider/search/fold execution;
- #18 — discourse, semantic acts and realization;
- #19/#169 — Gödel procedural learning, habit and muscle memory;
- #168 — structural geometry and projection correctness;
- #170 — whole-observation interpretation;
- #182 — prompt-root microcycle, finite dynamic operation selection and self-reentry.

Implementation work remains scheduled by the existing finish-line program and dependency graph. This document prevents those owners from implementing mutually incompatible partial interpretations.