# What If an FFN Is Just a Really Expensive Learned Lookup Table?

That title is intentionally provocative.

It is also not entirely a joke.

A transformer feed-forward network can be written conceptually as:

```text
input
    → compare against a large bank of learned detectors or keys
    → activate a weighted set of matches
    → combine their learned value vectors
    → return the result to the residual stream
```

The implementation is dense matrix multiplication, and the learned behavior can be
distributed across many neurons. I am not claiming that every neuron is a neat little
database row containing one human-readable fact.

But the computational interpretation is still useful:

```text
learned key
    → fuzzy activation
    → learned value contribution
```

There is published transformer research that describes feed-forward layers as learned
key/value memories for essentially this reason.

Laplace asks a blunt systems question:

> If a relationship is already explicit, typed, witnessed, persistent, and indexable,
> why should I pay to approximately rediscover it through a giant opaque learned bank
> every time I need it?

## “The capital of France” should not require metaphysics

Consider this relationship:

```text
France --has-capital--> Paris
```

A conventional model can learn internal features associated with `France`, `Paris`,
`capital-of`, geography, sentence completion, and many other things, then combine them
well enough to emit the expected token.

Laplace is intended to make the relation itself addressable.

The query can become something closer to:

```text
goal: resolve has-capital(France)
admissible relation: has-capital
direction: country → capital
indexed result: Paris
evidence: witnessed sources and current standing
realization: "The capital of France is Paris."
```

That is not nearest-neighbor search.

It is an exact typed operation over persistent state.

## Beef stroganoff is an even less glamorous example

Suppose I ask:

> What are the ingredients in beef stroganoff?

If the admitted world state already contains typed ingredient relations, why should the
canonical operation be:

```text
sentence
    → embedding
    → approximate nearest neighbors
    → vaguely similar documents
    → language model
    → reconstructed ingredient list
```

The exact state might instead contain:

```text
Beef Stroganoff --ingredient(required)--> beef
Beef Stroganoff --ingredient(common)----> onion
Beef Stroganoff --ingredient(common)----> sour cream
Beef Stroganoff --ingredient(variant)----> mushroom
Beef Stroganoff --served-with(common)----> egg noodles
```

Then the machine can execute the relation query directly.

It can also preserve the distinctions approximation tends to smear together:

```text
required
optional
traditional
regional variant
substitution
allergen
quantity
preparation
source
recipe version
```

“What ingredients are required?”, “What can replace sour cream?”, and “Which
traditional variants omit mushrooms?” are different typed programs over the same
state.

## Dot products can point exactly when the representation is controlled

There is nothing inherently approximate about a dot product.

If each dimension is a declared ingredient and a recipe is a sparse incidence vector,
then the mushroom dimension can mean exactly `mushroom`.

The dot product or sparse matrix product can calculate exact overlap.

At useful scale, I might execute the same law through an inverted index, bitmap, sparse
matrix, relational join, GIN index, or another accepted physical provider.

Those providers may have different performance characteristics.

They do not get to change what the operation means.

This is one reason Laplace has a typed instruction set and replaceable execution
providers. An exact relational operation should not become a different semantic
operation merely because one machine uses PostgreSQL indexes and another uses a sparse
numeric kernel.

## Softmax has a formal tail and an effective support

There is another important distinction here.

For finite logits, softmax assigns a value greater than zero to every candidate.

But:

```text
nonzero
```

does not necessarily mean:

```text
materially affected the behavior
```

There can be a large difference between formal mathematical support and measured
effective support.

I do not want Laplace to turn that observation into an arbitrary rule such as:

```text
keep the top 20 and call everything else noise
```

That would be another approximation pretending to be law.

The model-ingestion work instead needs a declared measurement program. Depending on
the operation, effective support might be established through causal ablation,
downstream contribution norms, a calibrated null distribution, cumulative mass, or a
different independently tested criterion.

The receipt must retain:

- the exact model, release, layer, head or expert, context, operation, and candidate;
- the observed activation and downstream contribution;
- the threshold law and its calibration evidence;
- the discarded tail and declared loss;
- the behavior before and after removal; and
- the withheld tests and deliberate threshold defect.

Top-k can be a useful physical selection tool.

It cannot become proof that everything outside the selected set was meaningless.

## Pay once, then query what persists

One of Laplace's central economic bets can be summarized as:

```text
pay once:
  identify
  canonicalize
  witness
  persist
  index

pay per question:
  resolve the goal
  select the typed operation
  traverse the relevant indexed state
  calculate standing and completion
  realize the answer
```

Full self-attention constructs a quadratic token-by-token interaction surface for a
sequence. Not every transformer operation is quadratic, and attention can discover
context-dependent relationships that were not stored beforehand.

The sharper question is this:

> How much repeated transient computation is being spent reconstructing persistent
> structure humanity has already observed?

If `Paris --capital-of--> France` is stable witnessed state, it does not become a new
fact because it appears in another prompt.

Laplace tries to make the persistent part persistent, then spend runtime work on the
actual context, goal, evidence boundary, search, transformation, and realization.

## The hard part is coverage

Exact relations are easy to defend for capital cities and ingredient lists.

The real research burden is whether arbitrary useful learned behavior can be
decomposed into sufficiently complete typed persistent structure without losing the
generalization that made the learned system useful.

The machine still has to demonstrate:

- how distinct syntax, sequence, causality, context, discourse, and semantic operators
  cooperate;
- how it handles cases where the required relationship has never been witnessed;
- how it proposes and tests new structure without combinatorial nonsense;
- how natural language and other modalities are realized at useful quality;
- how source engineering remains generic rather than recreating training labor by
  hand; and
- how the complete CPU/PostgreSQL machine performs at representative scale.

Those are not footnotes.

They are the work.

The claim is not that every neural operation is secretly one SQL query.

The claim is that many operations currently learned and reconstructed approximately
may have exact persistent counterparts—and that one machine architecture should be
able to select the right law rather than forcing every question through one fuzzy
space.

That is the bet Laplace still has to earn.

## Development links

- Typed query and A* execution: https://github.com/SaltyPatron/Laplace-Refactor/issues/17
- Model witness and target compilation: https://github.com/SaltyPatron/Laplace-Refactor/issues/20
- Model nonflattening acceptance: https://github.com/SaltyPatron/Laplace-Refactor/issues/61
- Model behavior and measured effective support: https://github.com/SaltyPatron/Laplace-Refactor/issues/71

DISCLAIMER: I am heavily using AI in the development of Laplace—obviously. This post
was drafted with assistance from OpenAI Codex. I reviewed and edited the final version
for accuracy, and I agree with and stand behind everything stated here as my honest
opinions. The FFN key/value interpretation is a useful mechanical model, not a claim
that individual neurons always contain clean human-readable database records. The
general Laplace mechanisms described here remain hypotheses until implemented and
accepted at their declared scope.
