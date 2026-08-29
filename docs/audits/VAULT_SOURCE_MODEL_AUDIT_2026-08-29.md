# Vault source and model audit — 2026-08-29

## Conclusion

CILI and OMW are not the only sources. They are two members of the terminal/reference-registry family in a physical estate containing 25 `/vault/Data` directories and 38 `/vault/models` directories. The data root spans standards, registries, lexical and predicate authorities, cross-source bridges, commonsense assertions, multilingual syntax and use, documents, code histories, game observations, multimodal fixtures, and grammar providers. The model root contains full model packages, derived exports, downloader residue, and three code datasets that are not models at all.

This is an observed development inventory, not a declaration that everything on disk belongs in the foundational seed. The complete machine-readable record is [`state/vault-inventory.json`](../../state/vault-inventory.json); [`tools/audit/validate-vault-inventory.py`](../../tools/audit/validate-vault-inventory.py) checks its repository structure and can compare it with the live vault.

At this snapshot:

- `/vault/Data`: 25 top-level directories, 139,759,517,696 allocated bytes.
- `/vault/models`: 38 top-level directories, 600,252,350,464 allocated bytes.
- Only the selected Unicode 17 inputs and ISO 639-3 2026-04-15 artifacts are byte-conformant to existing exact contracts. That does not mean either has been admitted into the activated product or that a foundational seed exists.
- CILI's locally checked-out mapping files do not conform to the declared exact profile. OMW and `omw` are one duplicated, locally mutated estate—not two independent sources.
- No model has a Laplace model profile, behavior experiment, effective-support result, export-preservation receipt, or product-admission receipt.
- The configured heterogeneous foundational boundary has not yet been selected or closed.

## What “source” means here

A directory name cannot decide semantic standing. One release can supply more than one role, and each role must remain attributable:

| Class | What it contributes | What it does not establish |
|---|---|---|
| Standard or registry | Terminal facts, scoped identifiers, release-specific declarations | Universal truth, generic cognition, or seed completion |
| Lexical/predicate source | Synsets, frames, roles, classes, definitions, annotated examples | Unqualified agreement across authorities |
| Mapping/bridge source | Attributable assertions relating exact endpoint releases | Endpoint existence, mapping truth, or consensus by itself |
| Observation corpus | Occurrences, uses, alignments, games, outcomes, histories | Foundational authority merely because it is large |
| Syntax provider | Concrete parse structure and error behavior | Source semantics or semantic identity |
| Model artifact | Exact parameters, configuration, code, and tokenizer/container state | Model behavior, effective support, or semantic authority |
| Derived model export | A child representation made from an exact model package | The source package or a preserved behavior invariant without evidence |
| Fixture | Small positive/negative conformance evidence | Modality or world completeness |

The intended route is therefore:

`exact artifact graph → codec/grammar concrete structure → typed recipe → universal AST → attributable witness/testimony → selected world boundary → receipts/readback`

That route must retain source authority, release, namespace, dependence, evidence epoch, loss, reconstruction, and denominator identity. It cannot flatten different sources into anonymous text rows.

## UAX 29, CILI/ISO, and managed middleware

UAX 29 belongs at the Unicode text edge: grapheme, word, and sentence boundary behavior. It does not parse TSV, Turtle/RDF, XML, CoNLL-U, JSONL, PGN, Parquet, ZIP/TAR, audio/video/image formats, Git histories, or model containers. Those require exact syntax providers or codecs and typed lowering/recomposition recipes. Unicode atoms and boundary properties may be used by those recipes without turning every format into “UAX 29 input.”

The current CILI and ISO implementations are not source-specific .NET middleware parsers. Their selected tabular artifacts compile into the native generic tabular source implementation in [`engine/src/tabular_source.cpp`](../../engine/src/tabular_source.cpp), reach the PostgreSQL wrapper in [`postgres/extension/src/source_admission_pg.c`](../../postgres/extension/src/source_admission_pg.c), and expose managed parity/orchestration. The CILI tests call the same native tabular plan used by ISO. UAX 29 data is separately represented in [`contracts/unicode-source.json`](../../contracts/unicode-source.json).

That current implementation still covers only declared tabular shapes. It is not evidence that the other vault families can or should pass through the tabular path. Treating Turtle, XML, CoNLL-U, JSONL, archives, media, games, code, Parquet, or model containers as generic delimited text would be a flattening defect. C/C++/PostgreSQL remain the semantic owners; SQL and C# may orchestrate typed operations and providers but cannot install parallel source semantics.

## `/vault/Data` inventory

The status terms below describe local profile readiness, not truth, product admission, or seed completion.

| Entry | Contents and observed version | Role | State and principal gap |
|---|---|---|---|
| `Atomic2020` | ATOMIC 2020 / AAAI 2021; 1,076,880 train, 102,024 dev, 152,209 test tuples | Commonsense proposition testimony and split observations | Versioned, unprofiled; immutable release, digests, typed relations, and readback absent |
| `CILI` | CILI ontology and mappings; commit `dfc99e15…`, v1.0-13 | Reference registry and cross-namespace mapping testimony | Existing selected profile is nonconforming: CRLF-expanded PWN 3.0/3.1 files and missing pinned ZIP; full Turtle ontology and endpoint profiles also absent |
| `ConceptNet` | One 10,157,160,611-byte `assertions.csv`; local release unknown | Commonsense relation testimony | Unversioned and unprofiled; release, license, digest, schema, and denominators absent |
| `FrameNet` | FrameNet 1.7; 1,221 frames, 13,572 LU XMLs, 107 annotated full-text documents | Frame/predicate testimony plus annotated occurrences | Versioned, unprofiled XML estate; artifact graph, XML recipe, endpoint closure absent |
| `Games` | 76 PGNs and 52 archives; rolling through 2026, including explicitly partial material | State-transition and outcome observations | Open/rolling boundary; provenance, selection, PGN recipe, duplicates, and open-ended completion law absent |
| `ISO639` | SIL ISO 639-3 2026-04-15 plus separate CLDR, Glottolog, IANA, LOC material | Language registries and scoped coordinates | Selected SIL ZIP and four tables conform exactly; other registries, cross-mappings, activated admission, and seed closure remain absent |
| `MapNet-0.1` | 5,162 FrameNet 1.3 ↔ WordNet 1.6 mappings; MapNet 0.1 (2009) | Cross-source mapping testimony | Versioned, unprofiled; endpoint releases, stated precision, license, and typed mapping profile unresolved |
| `OMW` | Open Multilingual Wordnet v2.0-1; commit `406bf83…` | Multilingual lexical and reference-mapping testimony | Nonconforming working tree: 1,417 EOL mutations, one deletion, one untracked checkpoint; duplicates lowercase `omw` |
| `OpenSubtitles` | OPUS v2024 English-aligned archives for 11 language pairs | Dialogue/pragmatics and translation observations | Versioned, unprofiled; selected sentence/turn boundary, digests, dependence, and duplicate closure absent |
| `PredicateMatrix.v1.3` | 27-column multilingual predicate mappings across VN/WN/FN/PB/MCR | Cross-source predicate/role mapping testimony | Versioned, unprofiled; endpoint binding, typed schema, standing, and dependence unresolved |
| `ProjectGutenberg` | Small Laplace document subset; local snapshot unknown | Exact document occurrences | Unversioned, unprofiled; snapshot, license/provenance, inventory, digests, and HTML recipe absent |
| `PropBank` | 7,566 frame XMLs and related rolesets/tooling; main ZIP fetched 2026-06-05 | Predicate-sense and role testimony | Immutable commit not recorded; licensed sentence annotations are intentionally absent |
| `SemLink` | SemLink 2.0 alignments among VN/PB/FN/WN | Cross-source mapping testimony | Version observed, but immutable commit, digests, endpoint releases, and adjudication closure absent |
| `Tatoeba` | Sentence/link/audio CSV and archives; undated local export | Translation/use observations and audio-linked occurrences | Snapshot identity, license/provenance, key closure, and audio denominator absent; one archive is byte-duplicated |
| `TreeSitter` | 303 grammar Git repositories; lock declares 299 | Concrete syntax providers and conformance inputs | All locked directories exist, but four revisions disagree and four extra providers have no lock disposition; providers are not source semantics |
| `UCD` | Unicode 17.0.0 UCD/Unihan/UCA/IDNA/security/emoji/XML and tests | Terminal code-point floor and Unicode behavior law | All 33 selected contract files verify; activated-product root and any additional selected profiles remain absent |
| `UD-Treebanks` | Universal Dependencies 2.17; 339 treebanks, 686 CoNLL-U files | Multilingual syntax annotation observations | Versioned, unprofiled; selected treebanks, CoNLL-U recipe, licenses, denominators, and cross-treebank dependence absent |
| `Unicode.BAD-DONOTUSE` | 39.5 GB mixed Unicode web/history mirror | Forensic reference only | Explicitly quarantined; not eligible for selection |
| `VerbNet` | VerbNet 3.4; 329 verb-class XMLs | Predicate classes, roles, restrictions, syntax, semantics | Versioned, unprofiled; immutable commit, digests, XML recipe, endpoint closure absent |
| `Wiktionary` | 11.5 GB XML plus Kaikki and raw wiktextract JSONL representations | Lexical definitions/examples and exact occurrences | `latest` is undated; derivation among representations, dump identity, digests, and exact selected boundary absent |
| `WordFrameNet` | WFN/XWFN mapping archives and extracted material | Cross-source mapping testimony | Local version, authority/license, schema, endpoints, and standing unidentified |
| `Wordnet` | Princeton WordNet 3.0 archive/database | Synset/relation testimony and reference endpoint | Versioned, unprofiled; exact artifact/recipe and CILI PWN 3.0/3.1 endpoint closure absent |
| `code-authority` | CPython `ff64d8de…`, .NET docs `2707b543…`, PostgreSQL `e18b0cb7…`, .NET runtime `25bd04c7…` | Program AST, definition/use, and change-history testimony | Aggregate manifest and profile absent; docs clean, CPython/PostgreSQL heavily locally modified, runtime cleanliness not established |
| `omw` | Same v2.0-1 content and checkpoint as uppercase `OMW` outside `.git` | Duplicate mirror only | Not independent evidence; select one canonical clean release and declare mirror/dependence |
| `test-data` | Audio, image, text, PDF, electronics, neural-model, and mixed fixtures | Positive/negative conformance fixtures | Fixture-only; manifest/expected semantics and some provenance absent; proves no modality complete |

## `/vault/models` inventory

### Primary artifact rule

When a complete full-precision safetensors package exists, that package is the primary local model-artifact witness. A PyTorch duplicate, AWQ quantization, GGUF conversion, TorchScript export, compiled engine, or other runtime-specific representation is a subordinate child. A child becomes usable only with an exact parent binding, conversion recipe, declared loss boundary, named preservation invariant, and source/target behavior evidence. It never replaces the full package.

The physical validator checked 16 weight-index files referencing 93 weight files and 32 model-package symlinks. The complete indexed packages close and no checked symlink is broken. The AWQ Qwen 2.5 Coder directory is deliberately excluded: its two declared shards are absent and two `.incomplete` payloads remain.

### Full model-package candidates

| Entry/family | Observed exact version | Contents / modality | Current gap |
|---|---|---|---|
| `Conditional-DETR-R50` | HF `8f8795fb…` | Vision detector; safetensors primary, PyTorch duplicate subordinate | Model profile, duplicate relation, behavior/intervention receipts |
| `DETR-ResNet-101` | HF `7d14702e…` | Vision detector; safetensors primary, PyTorch duplicate subordinate | Same |
| `Florence-2-base`, `Florence-2-large` | HF `5ca5edf5…`, `21a599d4…` | Vision-language packages with custom executable Python | Model/code profile, executable dependency closure, behavior and target invariant |
| `Grounding-DINO-Base` | HF `12bdfa31…` | Text-conditioned vision detector; safetensors primary, PyTorch duplicate subordinate | Model profile, grounding behavior, duplicate relation |
| `RT-DETR-v1-R101` | HF `ff44b691…` | Vision detector | Model profile and behavior/intervention receipts |
| Qwen 2.5 Coder 3B/7B/14B | HF `488639f1…`, `c03e6d35…`, `aedcc2d4…` | Complete safetensors code/language models | Model profiles and behavior/effective-support experiments |
| Qwen3 Coder 30B-A3B | HF `b2cff646…` | Complete 16-shard safetensors MoE code model | Routing, behavior, effective support, and profile |
| Qwen3 Embedding 0.6B/4B | HF `c54f2e6e…`, `5cf2132a…` | Text embedding models | Retrieval behavior, effective support, and profiles |
| Qwen3 Reranker 0.6B/4B | HF `6e9e6983…`, `f16fc5d5…` | Text rerankers | Ranking behavior, effective support, and profiles |
| Qwen3-VL Embedding 2B/8B | HF `929a0c31…`, `a12d6118…` | Image-language embeddings | Multimodal retrieval behavior, effective support, and profiles |
| Qwen3-VL Reranker 2B/8B | HF `76219daf…`, `8e52ab8f…` | Image-language rerankers | Multimodal ranking behavior, effective support, and profiles |
| `models--Qwen--Qwen3.8-27B` | HF `1d4bf0f2…` | 18-shard safetensors language/image package; local name and `qwen3_5` config disagree | Identity reconciliation, profile, behavior/effective support |
| TinyLlama 1.1B Chat | HF `fe8a4ea1…` | Complete safetensors language model | Profile, behavior, and exact parent relation to GGUF exports |
| DeepSeek Coder V2 Lite | HF `e434a23f…` | Complete 4-shard safetensors MoE code model | Routing, behavior, effective support, and profile |
| DeepSeek Coder 33B | HF `61dc97b9…` | Seven safetensors plus seven duplicate PyTorch shards | Safetensors primary; duplicate relation, profile, and behavior evidence absent |
| SAM-Audio large | HF `5f2cd3a9…` | Audio model in `checkpoint.pt` | Typed container/profile and audio intervention evidence |
| Fish Speech 1.5 | HF `275a984d…` | Speech generator/model checkpoints | Multi-checkpoint dependency graph, profile, speech behavior |
| Granite Speech 3.3 8B | HF `315afb31…` | Nine base shards plus adapter weights | Base/adapter dependence profile and speech/effective-support evidence |
| Jina code embeddings 1.5B | HF `39aeb4fb…` | Code embedding model | Retrieval behavior, effective support, and profile |
| Jina reranker v3 | HF `050e171c…` | Text reranker | Ranking behavior, effective support, and profile |
| Microsoft Phi-2 | HF `810d3678…` | Two-shard safetensors language/code model | Profile and behavior/effective-support receipts |
| NVIDIA Canary-Qwen 2.5B | HF `6cfc37ec…` | Speech-language model | Architecture/container profile and speech behavior |
| NVIDIA Music Flamingo | HF `e29cfe92…` | Four-shard safetensors audio-language model | Profile, audio-language behavior, effective support |
| all-MiniLM-L6-v2 | HF `c9745ed1…` | Sentence embedding model | Retrieval behavior, effective support, and profile |
| `yolo11x` | Filename-level identity only | PyTorch vision artifact plus subordinate TorchScript export | Exact upstream source identity, parent/export recipe, invariant, profile, and behavior receipts |

### Non-primary and misplaced entries

| Entry | Actual class | Disposition |
|---|---|---|
| `.locks` | Downloader residue | Exclude from all source/model selections |
| `gguf` | TinyLlama F16 and substrate-named derived GGUF exports | Subordinate only; parent binding, provenance, conversion recipe, preservation invariant, and behavior comparison absent |
| Qwen2.5-Coder-7B AWQ | Incomplete quantized derivative | Exclude; both weight shards are absent and partial downloads remain; the complete full-precision Qwen 7B safetensors snapshot is the parent candidate |
| `code-corpus` | 41.2 GB mixed code/repository observation corpus | Not a model; lacks aggregate provenance, authority/license/privacy boundary, deduplication, and repository graph |
| `stack-v2` | 72.7 GB Parquet code corpus | Not a model; no snapshot manifest and several language shard sequences are visibly partial |
| `tiny-codes` | 1.0 GB, nine Parquet parts | Not a model; dataset/config/split identity, schema, license, and row denominators absent |

## Completeness and damage findings

1. The estate is broad enough to prevent a CILI/OMW-only design. Existing source-admission law already names heterogeneous families; implementation coverage has not caught up with that declared scope.
2. Vault-wide line-ending mutation damaged exact Git working trees and makes path presence misleading. CILI, OMW, CPython, and PostgreSQL are affected to different established degrees. These trees must not be normalized in place and then treated as their upstream releases.
3. The two OMW paths waste space and create false apparent corroboration. They are one dependent copy and need one canonical clean release plus an explicit mirror disposition.
4. `Unicode.BAD-DONOTUSE` is correctly quarantined. Its 39.5 GB presence must never be counted toward selected Unicode completeness.
5. Tree-sitter is a grammar-provider estate, not the source universe. Four locked revisions and four extras need reconciliation before it can be called an exact dependency closure.
6. The model root's name is not a trustworthy classifier. It mixes full packages, conversions, cache residue, and code datasets.
7. Full weights establish artifact presence only. No local model currently has the behavioral, intervention, deliberate-defect, support, loss, target-runtime, or readback receipts needed for Laplace admission or compilation claims.
8. No selected foundational seed exists. “Everything in `/vault`” would be an unbounded and internally inconsistent seed definition; “CILI plus OMW” would omit almost every declared source family.

## Implementation boundary from this audit

The next coherent implementation boundary is not another one-off importer and not a model conversion:

1. Preserve this inventory as the physical denominator and detect drift automatically.
2. Define the configured foundational selection across all required families, with explicit include, exclude, observation-only, fixture-only, duplicate, and quarantine dispositions.
3. Restore or acquire exact clean artifacts for selected entries. Immediate known repairs are CILI, one canonical OMW, the four Tree-sitter revisions, the four extra grammar dispositions, and immutable manifests for selected unversioned corpora.
4. Implement reusable syntax-provider/codec families by shape: delimited/fixed width, RDF/Turtle, XML, CoNLL-U, JSON/JSONL, archive graphs, PGN, Git/code, Parquet, media, and full-precision model containers. Each provider must lower to and recompose from the universal AST through typed recipes; none owns source semantics.
5. Close endpoint and dependence topology across WordNet/CILI/OMW and the frame/predicate/bridge sources without turning any mapping assertion into unqualified truth.
6. Admit observations separately from standards and registries, retaining attribution, occurrence, time, split, selection, duplicate, and uncertainty identity.
7. Profile full-precision model packages first. Only then bind and test any required derived export as a receipted child with a named invariant and measured loss/behavior boundary.
8. Execute the selected heterogeneous boundary through the installed PostgreSQL product and require exact product receipts, reconstruction/readback, reference closure, denominators, and negative controls before claiming world admission or seed completion.

The structural audit command is:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/audit/validate-vault-inventory.py
```

The live-vault verification command is:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 tools/audit/validate-vault-inventory.py --verify-physical
```
