All 95 current persisted diagnostics match historical rows exactly by **path, byte start, byte end and complete syntax flags**. There are no unmatched current rows or duplicate comparison keys. The current result is **53 ERROR and 42 MISSING nodes across 21 files**. This is a source-context inventory; multiple recovery nodes can describe one parser limitation.

The read-only observer completed in [run 35247042007, job 105289806130](https://github.com/SaltyPatron/Laplace/actions/runs/35247042007/job/105289806130). Its [retained artifact 10507258870](https://github.com/SaltyPatron/Laplace/actions/runs/35247042007/artifacts/10507258870) is 24,943 bytes, SHA256 `9fa5b77a36a50a0d4a1530d96c28ad0fd9b1b9c1867603cbafbf0fcfb5c5dddd`. The exact projection is retained in Refactor Git blob `bb648ba7c183fb36cff59211a83f0c66e7f5641d`. Artifact metadata was checked against that run, size and digest.

The current profile is `c6ec7000cd7d923361f0f2505c35852cc78c21205598af726706e7a3ef25ddab`, under selected package `d12af51d6367e03045e12599d2195894868f8408ebe17e5b313db3032f516490`. The manifest SHA256 is `2346d3970f5ae3619b7ad69bbc80939c816bb14af94249be5c20a44c5ac33adc`. The companion JSON binds the profile, structural receipt, observer, historical document and selected grammar.

| Current source-context group | Nodes | ERROR | MISSING |
|---|---:|---:|---:|
| Macro expansion context | 55 | 31 | 24 |
| Preprocessor structure | 13 | 11 | 2 |
| GNU extension recovery | 26 | 10 | 16 |
| Unresolved ordinary C++ recovery | 1 | 1 | 0 |
| **Total** | **95** | **53** | **42** |

Exactly seven historical rows are absent:

| File and source line | Byte interval | Historical finding |
|---|---|---|
| `src/attacks.h:203` | [7325, 7538) | Declaration in for-loop condition |
| `src/attacks.h:205` | [7401, 7417) | Associated recovery |
| `src/attacks.h:214` | [7552, 7567) | Associated recovery |
| `src/attacks.h:215` | [7577, 7577) | Associated missing-node recovery |
| `src/movegen.h:46` | [1273, 1273) | Deleted conversion operator |
| `src/thread.h:175` | [6047, 6056) | Pointer-to-member expression |
| `src/uci.cpp:54` | [1456, 1459) | Using-declaration pack expansion |

The narrow next candidate is GNU attributes on type aliases. Eight declarations in `src/nnue/simd.h`, lines 171–175 and 177–179, still produce **24 nodes: 8 ERROR and 16 MISSING**. An example is `using vec_i8x8_t __attribute__((may_alias)) = int8x8_t;`. In the [selected grammar's alias declaration](https://github.com/SaltyPatron/Laplace-Refactor/blob/94fe0c1ba60dee178a4782c1082441631638f51b/grammar.js#L869), this position permits only `attribute_declaration`. The selected generated grammar already has a separate `attribute_specifier` rule for GNU attributes. The proposed local change is:

```javascript
repeat(choice($.attribute_declaration, $.attribute_specifier))
```

This candidate has not been generated or executed. Qualification should cover the exact Stockfish aliases, preserved standard-attribute aliases, malformed controls, the existing grammar/native regressions, and a whole119 native comparison with exact reconstruction. The 24 observed rows are a target for that experiment; no reduction is claimed yet.

Two remaining `src/misc.h` rows, at lines 63–64, concern `__extension__ using` aliases and need separate qualification. That file's third diagnostic, at line 339, belongs to the preprocessor group. The single ordinary C++ row in `src/position.cpp:1549` concerns the assignment/comma expression inside an if condition. Since the selected grammar contains condition initializers, comma expressions and parenthesized assignment support, its exact enclosing condition needs reduction before assigning a narrower cause.

The 55 macro-context and 13 preprocessor rows remain recorded. Their grouping does not establish that they are harmless or that expansion resolves them. This audit performed no admission, parser execution, grammar build or database mutation. It does not establish executable C++ semantics. The original historical 102-row documents remain unchanged.
