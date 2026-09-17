The September 17, 2026 hosted GNU-alias experiment reduced Stockfish's syntax diagnostics from **95 to 71** over the same **119 files / 1,172,144 bytes**. The completed artifact-only adjudication qualified exactly **24 removed diagnostics, zero added diagnostics**, eight corrected GNU alias declarations, and one explicitly admitted recovery-leaf reclassification in `src/main.cpp`. The existing installed grammar and admitted profile retain **95 diagnostics**; this experiment did not install or select the candidate provider, admit a new profile, or establish executable semantics or performance acceptance.

This record is paired with [the complete JSON evidence record](STOCKFISH_GNU_ALIAS_QUALIFICATION_20260917.json), which retains the actual adjudication result, all 24 exact removed coordinate/full-flag keys, all 26 source-supplement provenance mappings, control inventories, and exact source identities. Its canonical delivery receipt is Git blob `8a58e5a4b04373e1239896340715a49d857c7def` in `SaltyPatron/Laplace-Refactor`.

| Execution | Run / job | Historical result |
|---|---|---|
| Original producer | [35254821053](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35254821053) / [105315742631](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35254821053/job/105315742631) | FAILED |
| First artifact adjudication | [35257310377](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35257310377) / [105324020307](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35257310377/job/105324020307) | FAILED: hidden source absent from uploaded ZIP |
| Corrected artifact adjudication | [35258040884](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35258040884) / [105326478408](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35258040884/job/105326478408) | PASS under the explicit artifact-only criterion |

The producer remains historically **FAILED** with `RuntimeError('named CST changed outside target file')`. It had completed the parser/native corpus work and failed the final assertion that only `src/nnue/simd.h` could have a changed named tree. The corrected adjudicator did not rerun compilation, parsers, or native tests. It authenticated the retained outputs and applied the explicit criterion `full-corpus GNU alias correction with one exact main.cpp recovery-leaf reclassification/v1`. Its actual result is `qualified-under-explicit-artifact-only-criterion`, completed at `2026-09-17T18:18:54Z`.

| Retained artifact | Artifact ID | ZIP bytes | ZIP SHA256 |
|---|---|---:|---|
| Original producer | [10510989568](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35254821053/artifacts/10510989568) | 21,221,497 | `132ad567ac35d4658b1801f47f71a29d6c580fff663e139e739cf1473753881c` |
| Corrected adjudication | [10512793165](https://github.com/SaltyPatron/Laplace-Refactor/actions/runs/35258040884/artifacts/10512793165) | 21,635,952 | `3c52451ead9dfbc0d38f901d8770abfe89c42a67ff80a83e4c597c5ae7c57511` |

The original producer report SHA256 is `847f5de705177db77d1bc0be1dc743fb36cf49857f235b70fbf3f1a8e9469dfd`. Producer operator commit is `af157a8f83da7868126e465a487b91bc7f7fe105`; corrected adjudicator operator commit is `bcb3662de0852e1f469dfb04f99dea3701306360`. The adjudicator source is blob `583d2b4f431f5ddc42a8d6bd05775a26ccacde55` and its workflow is blob `042ed3d51613fc516134e716e7ba074442a4aa6a`. Both artifacts remain separate evidence objects; the original producer ZIP and its failure are preserved.

The source is official [Stockfish commit edb0d9db6731067ec50ce619ff372b463bc4dd5d](https://github.com/official-stockfish/Stockfish/tree/edb0d9db6731067ec50ce619ff372b463bc4dd5d), tree `418af042b3c0aade628c1c98f13942659e67e64d`, Git-archive SHA256 `56b9d83ca09a419501cb412066ff1f80e89f71f57a55da095043d22a51bdd1a2`. Both variants use identical source bytes. The fresh hosted manifest SHA256 is `e1876db00952d81e5aa31c59aa148119c66dce5967a5f52c4d11b52e73614c4e`; the historical installed manifest SHA256 is `2346d3970f5ae3619b7ad69bbc80939c816bb14af94249be5c20a44c5ac33adc`. Their checkout-path fields differ, while the immutable Git tree, archive, and per-artifact identities bind the same input. The hosted product source is `f510bc4dae608dd9f34ab8e5555c19350d14b039`, tree `54f5da72b2cd067925f2f4d2f1187eb77036dbed`, with source manifest blob `5e8b8e8b20c5a77f09be93e1353c91457b633017`.

The baseline grammar derivative is `94fe0c1ba60dee178a4782c1082441631638f51b`. Candidate grammar blob `89388c776ae4ba0f7294f670b8b33b4b42ad1da4` changes only `alias_declaration`: after the alias name and before `=`, repeated attributes may be either `attribute_declaration` or `attribute_specifier`. The generated, unpublished grammar candidate is commit `446e0b8b1a70952f9ff57330059dbc116b2ab065`, tree `bf8b3a9761084c9935f6ef26cf4652cfbc248485`; its retained archive SHA256 is `128b50f61ff4dcecf552c7c4034075d190e08ef2286b64fbd7324501b93a8446`. Changed generated paths are `grammar.js`, `src/grammar.json`, `src/node-types.json`, and `src/parser.c`. Generation reused the authenticated Tree-sitter `tree-sitter 0.26.5` CLI from source `470813116b99578956e67abb7138e993833af67a`, binary SHA256 `83d9f2f0348b132a20a8de92863322cdfc65661fb623976ce6416cf612e9ea5e`. No grammar golden files were updated.

The full-file correction covers these eight aliases in `src/nnue/simd.h`. Byte intervals are half-open. Each alias accounts for one ERROR row and two MISSING rows; the exact 24 keys in the JSON retain the full flags independently of symbol or span-number changes.

| Alias | Type | Source byte interval | Removed diagnostics |
|---|---|---:|---:|
| `vec_i8x8_t` | `int8x8_t` | 5731–5787 | 3 |
| `vec_i16x8_t` | `int16x8_t` | 5788–5845 | 3 |
| `vec_i8x16_t` | `int8x16_t` | 5846–5903 | 3 |
| `vec_u16x8_t` | `uint16x8_t` | 5904–5962 | 3 |
| `vec_i32x4_t` | `int32x4_t` | 5963–6020 | 3 |
| `vec_t` | `int16x8_t` | 6022–6078 | 3 |
| `vec_i8_t` | `int8x16_t` | 6079–6135 | 3 |
| `psqt_vec_t` | `int32x4_t` | 6136–6192 | 3 |

All **144 complete retained CST outputs** were rehashed, covering **72 files per variant**. Complete trees for **70 files** are unchanged. The changed files are exactly `src/nnue/simd.h` and `src/main.cpp`. The SIMD file has the intended eight complete alias declarations and no remaining syntax diagnostics. This evidence does not claim a separately measured equality of every other subtree inside the changed SIMD file.

The admitted `src/main.cpp` exception is exact: at child path `[10,2,2,3,2,0]`, bytes `[1114,1120)`, the `#endif` leaf changes from `type=preproc_directive, named=true` to `type=#endif, named=false`. Its ERROR parent covers the same bytes and remains an ERROR. All other compared tree fields, children, ancestors and parser metadata are unchanged after those two leaf-field substitutions; the redundant rendered S-expression is retained by hash rather than claimed equal. The source file is 1,602 bytes with SHA256 `20ecc928260f46dfa3622ef0a4d810fe880322b1444659d155ce295be84b343a`. This is an explicit recovery-tree difference, not a claim of identical trees or a repaired preprocessor diagnostic.

| Control family | Retained actual result |
|---|---|
| Unfiltered inherited C corpus | 85 passed |
| Unfiltered C++ corpus | 198 passed for baseline; 198 passed for candidate |
| Prior qualified parser controls | 34 per variant; prior named CST equality preserved |
| Alias controls | 18 per variant: eight authentic aliases, three generic GNU/mixed-attribute aliases, four malformed cases, three unchanged standard aliases |
| Alias negative/unchanged checks | Baseline target rejections observed; malformed rejections observed; standard-alias CST equality preserved |
| Native source fixtures | 13 retained fixture records per variant |
| Whole native source reconstruction | All 119 files reconstructed in both variants |
| Native shape inventory | 119 records per variant, authenticated 17-field shapes |
| Unicode payload-byte ordering | Three original passes, three expected deliberate-mutant failures, three restored passes |
| Restoration before native use | Header, engine and test binary restored exactly; three clean disposable-source checks retained |

The artifact-only adjudicator recomputed parser-control assertions from the retained CST JSON and verified the recorded native outputs; it did not execute the controls again. The alias input inventory is blob `1304ebf23108cc079982924026322c249f7f5b3c`, and the prior-control inventory is `35b958c279832720f390602d1f347bcf63b7da7f`. The historical input inventory's original `source-only-unexecuted` status is preserved as input provenance; the actual completed experiment results are recorded separately.

The byte-order controls are `UnicodeCoreProperties.PayloadByteOrderCanonicalizesInputPermutations`, `UnicodeCoreProperties.PayloadByteOrderMatchesUnsignedLexicographicOrder`, and `UnicodeCoreProperties.PayloadByteOrderPreservesExactDuplicateDetection`. The isolated mutant reversed only the first differing unsigned-byte comparison. The original and restored engine SHA256 is `75b3f66cd3cfd66f6b1691904af9f13203fcf277f3f740af65382169b3c4e999`; the original and restored test binary SHA256 is `86d9eac3140da202e681bbc8e38e73296a820a0a21f07e7baeb527a53d319ecc`. Actual original/mutant/restored XML identities, header identities, and exit statuses are retained in the JSON.

| Retained native source plan | Requests | Unresolved witnesses | ERROR/MISSING rows |
|---|---:|---:|---:|
| Baseline | 60,766 | 197,083 | 95 |
| Candidate | 60,759 | 197,059 | 71 |

These counts are native source-plan records. They are not PostgreSQL durable witness counts, recorded-game throughput, or finalized canonical profile identities. Both variants recorded zero semantic testimony and no PostgreSQL admission.

The first adjudication failed because the producer's artifact upload omitted hidden paths. The corrected reader recovered only missing source paths with a hidden component that were already bound by the authenticated 119-file manifest and pre-upload inventory. It fetched exact bytes from the immutable official Stockfish commit without credential headers, rejected redirects, bounded each response, and required matching byte count, SHA256 and Git blob identity. Missing ordinary source files and missing generated evidence still fail. All **16 generated evidence members** were present in the original ZIP; no generated-source fallback was used or allowed.

The corrected artifact contains explicit `immutable-hidden-source-supplements.json` mappings and nonhidden `source-supplements/<sha256>.source` bodies. The following **26 paths / 75,851 bytes** required supplementation; they comprise **25 unique bodies**, because `scripts/.gitattributes` and `tests/.gitattributes` share bytes. These bodies are documented as supplements, not as members of the historical producer ZIP.

| Supplemented immutable source path | Bytes |
|---|---:|
| `.clang-format` | 1,291 |
| `.git-blame-ignore-revs` | 355 |
| `.gitattributes` | 63 |
| `.github/ISSUE_TEMPLATE/BUG-REPORT.yml` | 1,983 |
| `.github/ISSUE_TEMPLATE/config.yml` | 448 |
| `.github/ci/arm_matrix.json` | 886 |
| `.github/ci/libcxx17.imp` | 1,222 |
| `.github/ci/universal_matrix.json` | 1,396 |
| `.github/workflows/arm_compilation.yml` | 3,295 |
| `.github/workflows/avx2_compilers.yml` | 3,610 |
| `.github/workflows/clang-format.yml` | 2,267 |
| `.github/workflows/codeql.yml` | 2,166 |
| `.github/workflows/games.yml` | 1,591 |
| `.github/workflows/iwyu.yml` | 1,674 |
| `.github/workflows/matetrack.yml` | 6,396 |
| `.github/workflows/msvc.yml` | 1,887 |
| `.github/workflows/official_release.yml` | 4,448 |
| `.github/workflows/sanitizers.yml` | 3,058 |
| `.github/workflows/stockfish.yml` | 7,860 |
| `.github/workflows/tests.yml` | 13,416 |
| `.github/workflows/universal_compilation.yml` | 11,706 |
| `.github/workflows/upload_binaries.yml` | 2,746 |
| `.github/workflows/wasm_compilation.yml` | 1,708 |
| `.gitignore` | 345 |
| `scripts/.gitattributes` | 17 |
| `tests/.gitattributes` | 17 |

The producer's final pristine-source checks after its failed named-tree assertion **were not executed**. The three retained clean-source checks and exact restoration identities before native execution do not substitute for those unrun checks. The passing adjudication preserves this limitation.

The remaining **71 diagnostics** are unresolved by this change, including the separate two `__extension__` aliases in `src/misc.h` and the parenthesized comma-expression recovery in `src/position.cpp`. The existing persisted baseline profile is `c6ec7000cd7d923361f0f2505c35852cc78c21205598af726706e7a3ef25ddab` and remains the 95-diagnostic profile.

Identical source bytes and fewer diagnostics do not establish interchangeable canonical witnesses. Numeric symbols, parse shape and unresolved references can change; the grammar's recipe and syntax-authority declarations change with the candidate. A changed interpretation requires its own bound grammar recipe/profile identity before installation or admission. This record computes no finalized profile ID, makes no provider promotion or existing-witness compatibility claim, and establishes neither executable Stockfish semantics nor configured-foundation completion.
