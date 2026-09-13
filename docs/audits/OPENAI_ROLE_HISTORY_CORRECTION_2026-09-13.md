# OpenAI role/history correction — 2026-09-13

## Purpose

This record preserves a concrete contradiction between the Laplace invention/transport requirements and an implementation that had been described and tested as multi-message Chat Completions compatibility.

It is a defect record and negative control. It is not evidence that native conversation, OpenAI compatibility, or the complete product is delivered.

## Governing requirements

`docs/product/CONSTITUTION.md`, `docs/product/INVENTION_MODEL.md`, `docs/architecture/COGNITION_EXECUTION.md`, and issue #271 require transport surfaces to lower into the same native cognition/discourse machine without replacing typed state with route-private semantics.

Issue #271 specifically requires preservation of exact user content, message/item roles, order, attachments, tool-call/result identities, and world/actor scope when lowering compatible requests into native discourse/observations.

The transport is therefore not allowed to claim role-preserving multi-message semantics merely because it accepts an array named `messages` or emits a Chat Completions-shaped response.

## Defect observed on main

At `main@08fde45f1855e485a9e7cd32d41c25e4ec547935`, `tools/openai_api_compat.py` accepted `system`, `developer`, `user`, and `assistant` messages, rendered the roles into text markers such as:

```text
<SYSTEM>
<USER>
<ASSISTANT>
```

and joined the supplied messages into one string labeled `OpenAI-compatible conversation transcript:`. The adapter then invoked cognition with that single string as `{"prompt": prompt}`.

`tests/openai_chat_compat_tests.py::test_accepts_multi_message_conversation` asserted those textual markers. The test therefore proved transcript flattening, not preservation of typed message roles/history.

`tools/delivery/product_gateway_live_proof.py` compounded the problem by supplying four messages (`system`, `user`, `assistant`, `user`) and accepting a nonempty assistant response plus streaming framing as the OpenAI chat proof. `.github/workflows/product-cognition-multiturn.yml` then required those four role labels in the proof receipt. None of those checks established that the roles survived as typed native discourse state.

## Why this violates the invention

A transport role is witnessed/request state. Serializing the role label into ordinary prompt content changes the operation:

```text
role-structured observations
    !=
text containing strings that look like role labels
```

The native prompt-conversation boundary currently owns one exact admitted incoming turn and a previous native discourse frame. Its request contract does not expose an arbitrary client-supplied role-history array that the compatibility adapter can truthfully claim to preserve end to end.

Therefore the correct behavior at this boundary is fail-closed compatibility, not transcript simulation.

## Immediate correction

The correction branch `fix/reconcile-invention-truth-20260913` changes the public compatibility boundary to:

- accept exactly one `user` message;
- preserve that message content exactly;
- reject client-supplied multi-message history with `unsupported_conversation_history`;
- reject non-user roles with `unsupported_message_role`;
- retain nonstream/stream transport framing only for the narrower declared boundary;
- make the live proof and workflow record `declared_boundary = single-user-message` and `client_supplied_role_history = false`;
- leave issue #271 open for the real typed native role/history implementation.

The previous transport implementation is isolated behind the guard only to avoid unrelated HTTP/Explore/source plumbing churn in this corrective change. Its old transcript-normalization function is not an accepted semantic owner and must not be re-exposed as compatibility behavior.

## Historical claim correction

Merged PR #338 was renamed and its body corrected on 2026-09-13. It must not be cited as proof that role-preserving conversation or complete OpenAI compatibility was delivered.

The post-merge `product-path` run for merge commit `412086b7fab484a968cd460d69d72d098b8228ab` also concluded failure. Independent of the role-flattening defect, that failed run prevents the merge itself from serving as a successful whole-product proof.

## Required completion

This defect is not closed by accepting one user message. The narrower profile only removes the false claim.

Actual completion under #271 requires, at minimum:

1. client-supplied message roles and order lower into typed native discourse/observation state without textual-role substitution;
2. system/developer/user/assistant distinctions remain inspectable through execution receipts;
3. multi-turn corrections and references consume that state through the same native cognition path;
4. optional durable sessions remain distinct from client-supplied history and cannot merge unrelated conversations;
5. nonstream and streaming responses preserve the same semantic result/disposition;
6. a deliberate transcript-flattening mutant is rejected by acceptance;
7. ordinary compatible clients exercise the exact installed endpoint without a proprietary semantic bypass.

Until those predicates pass, the repository may claim only the exact narrower transport behavior that has actually been proved.