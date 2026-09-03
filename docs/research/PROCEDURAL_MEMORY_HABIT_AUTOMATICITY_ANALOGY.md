# Research notes: procedural memory, habits, skills, and automaticity

Status: research/architecture input, 2026-09-02. This note supports `docs/architecture/PROMPT_TRUNK_COGNITIVE_MICROCYCLE_AND_PROCEDURAL_MEMORY.md` and #169. It is not a claim that Laplace reproduces biological neural circuitry.

## Research question

Which findings from human/animal skill and habit learning are useful as **systems-design analogies** for Laplace procedural memory without pretending that one software module is biologically identical to one brain region?

The useful result is distributed rather than one-to-one: repeated experience changes action/program selection, practice can produce automaticity and savings, learned behavior depends on multiple interacting systems, and consolidation/automaticity remain distinct from declarative fact storage.

## Basal ganglia / striatum: action selection, skills, and habits

The neuroscience literature strongly implicates basal-ganglia/striatal circuitry in instrumental learning, action selection, skill acquisition and habit formation, but the mapping is not reducible to a single universal definition of `habit`.

Seger & Spiering's 2011 critical review emphasizes that habit learning has multiple operational definitions across neuroscience and warns against simply equating behaviorally defined habit learning with the basal ganglia as a whole.

Graybiel & Grafton's 2015 review describes the striatum as a meeting point for skills and habits and emphasizes its role in refining action selection and shaping motor/cognitive repertoires.

Useful Laplace analogy:

```text
validated repeated state -> program association
    ≈ learned action/program selection pressure

firmware habit scheduling
    != stored truth or content identity
```

A habit in Laplace should therefore live primarily as learned **selection/scheduling policy over already validated programs**, not as a rewrite of knowledge.

References:

- Carol A. Seger and Brian J. Spiering, *A Critical Review of Habit Learning and the Basal Ganglia*, Frontiers in Systems Neuroscience 5:66 (2011), PMID 21909324, DOI `10.3389/fnsys.2011.00066`.
- Ann M. Graybiel and Scott T. Grafton, *The Striatum: Where Skills and Habits Meet*, Cold Spring Harbor Perspectives in Biology 7(8) (2015), PMID 26238359.
- Henry H. Yin and Barbara J. Knowlton, *The role of the basal ganglia in habit formation*, Nature Reviews Neuroscience 7, 464–476 (2006).

## Motor memory / consolidation: savings and stabilized procedures

Motor-learning literature distinguishes initial acquisition from later consolidation and `savings`—faster relearning after prior practice. Krakauer et al.'s review discusses evidence that different forms of motor learning/consolidation involve cerebellar circuitry and motor cortex, while also stressing that the consolidation story varies by task.

Useful Laplace analogy:

```text
first primitive cognition
    expensive discovery/search

later validated reuse
    skill/program already exists

later optimized reuse
    fewer exploratory operations + prepared/compiled execution
```

This supports keeping three software states distinct:

- discovery of a valid program;
- learned scheduling/automatic proposal of that program;
- physical acceleration of an already stable program.

Reference:

- John W. Krakauer et al., *Consolidation of motor memory*, Trends in Neurosciences / review indexed as PMID 16290273 (2006).

## Multiple interacting systems, not one `habit center`

Reviews of skill/habit learning note contributions from basal ganglia, cerebellum, cortical systems and interactions among them. More recent anatomy also supports direct basal-ganglia/cerebellar network interactions rather than treating them as completely independent modules.

Useful Laplace consequence:

Do not implement:

```text
habit_table == the brain's habit system
muscle_memory_score == the cerebellum
```

Instead keep responsibilities typed:

```text
substrate memory
    retains observations/outcomes/receipts

Gödel discovery
    induces and validates reusable programs

firmware
    schedules proven programs under current state

physical planner/compiler/perfcache/native engine
    implements semantically equivalent acceleration

feedback/OODA
    observes consequences and can demote/retire learned policy
```

Reference:

- Peter L. Strick et al./review, *The basal ganglia and the cerebellum: nodes in an integrated network*, PMID 29643480.

## Automaticity is useful; inflexibility is not a design goal

Biological habits are often operationalized as increasingly automatic/stimulus-driven and, under some definitions, less sensitive to outcome changes. That is useful as an analogy for reduced deliberative cost but not as a product requirement for inflexible behavior.

Laplace must preserve a stronger current-context gate:

```text
recognized practiced state
-> propose practiced skill cheaply
-> revalidate current preconditions/evidence/authority
-> execute fast path only if still valid
```

If the goal, context, language, evidence, authority or counterexample state changes, the machine must fall back to broader cognition rather than blindly repeating a habit.

## Proposed measurable software analogues

The following are testable without claiming biological identity:

### Acquisition cost

Measure primitive program discovery:

- provider batches;
- candidate states;
- branch/reopen count;
- rows/candidates touched;
- CPU/memory/I/O/database crossings;
- elapsed time;
- unresolved obligations before/after.

### Habit / automatic proposal

After validation, measure whether firmware proposes the correct reusable skill earlier:

- scheduling rank;
- exploratory branches avoided;
- time to first valid program binding;
- false activation on negative controls;
- demotion/fallback under changed context.

### Savings

Re-run equivalent or semantically related fixtures and compare primitive versus learned execution cost while preserving the same result/receipt semantics.

### Muscle memory

Compare the same logical skill through:

- primitive expansion;
- prepared/indexed provider plan;
- fused/vector/native/perfcache path.

Require semantic-result and receipt parity plus lower representative physical work.

### Devaluation/counterexample sensitivity

Unlike a rigid biological habit definition, Laplace acceptance should deliberately change the goal/evidence/preconditions and prove the learned habit does **not** fire when the current state invalidates it.

## Language-general proceduralization

A learned cognitive skill should generalize on justified semantic/program state, not one surface construction. For a mechanism explanation, English, Japanese and other languages may present very different order, particles, omitted arguments, segmentation and discourse cues. Primitive interpretation may therefore differ substantially even when both ultimately bind the same reusable skill.

The research analogy here is procedural abstraction: practice can reduce the cost of selecting/executing a known operation without requiring exact repetition of one sensory surface. Laplace must test this mechanically with multilingual/paraphrase fixtures rather than assume it.

## Boundary statement

The neuroscience literature supports using terms such as `skill`, `habit`, `automaticity`, `procedural memory`, `consolidation`, and `savings` as disciplined analogies for measurable software behavior. It does **not** establish that Laplace is neurologically equivalent to a human brain, that cognition has one known finite list of biological operations, or that AGI/ASI follows automatically from this mapping.

The architectural claim to test is narrower and stronger as engineering:

```text
finite active instruction/calculus vocabulary
+ persistent multimodal observations
+ dynamic program composition
+ outcome-driven discovery
+ learned scheduling
+ semantically equivalent physical acceleration
= an inspectable machine that can proceduralize cognition over time
```
