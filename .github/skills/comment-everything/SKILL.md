---
name: comment-everything
description: 'Add or improve code comments across an implementation while matching the repository comment density and style. Use when asked to comment code, document a feature, explain ownership or control flow inline, add implementation comments, or perform a comment-only documentation pass.'
argument-hint: 'File, feature, symbol, or changed code to comment'
user-invocable: true
disable-model-invocation: false
---

# Comment Everything

Document the requested code so a future maintainer can understand its responsibilities, invariants, ownership, and non-obvious decisions without turning the source into line-by-line narration.

## Procedure

1. Identify the requested scope.
   - Use the named files, feature, symbol, or current diff as the boundary.
   - If the scope is ambiguous, ask whether to comment one implementation, all changed code, or an entire module.
   - Do not expand into unrelated files merely to make comment style uniform.

2. Establish the local comment style before editing.
   - Read the target code and one or two nearby implementations.
   - Note whether the repository prefers sentence comments, section banners, API summaries, short inline notes, or multi-line explanations.
   - Match the local capitalization, punctuation, terminology, line width, and density.

3. Trace the behavior being documented.
   - Start at the code that owns or computes the behavior, not only wiring or call sites.
   - Follow enough callers and dependencies to verify what each non-obvious block actually guarantees.
   - Treat existing comments as claims to verify; update or remove stale comments instead of preserving misinformation.

4. Comment the important reasoning.
   - Summarize each class and non-trivial function's responsibility and contract.
   - Explain ownership, lifetime, and borrowed versus owned pointers where that affects maintenance.
   - Document invariants, ordering requirements, cache states, sentinel values, column or role meanings, and error behavior.
   - Mark thread boundaries, cancellation behavior, synchronization assumptions, and which thread may access UI or model state.
   - Explain why a branch, workaround, platform-specific format, or unusual API choice exists.
   - Orient readers before complex loops or multi-stage transformations.
   - Describe user-visible consequences where the code alone does not make them obvious.

5. Avoid low-value narration.
   - Do not restate a clear assignment, constructor call, getter, return statement, or condition in English.
   - Do not comment every brace or every line independently.
   - Do not add speculative promises, TODOs without an owner, or comments that can drift from behavior.
   - Prefer names and small structural improvements over comments that compensate for confusing code, but do not refactor behavior during a comment-only request.

6. Preserve behavior and scope.
   - Make comment-only edits unless the user also requested code changes.
   - Keep existing public APIs, formatting, and unrelated user edits intact.
   - Use ASCII unless the file already uses another character set for a clear reason.
   - Apply the repository's comment density to newly generated code in later implementation work, not only during dedicated comment passes.

7. Review comments against the implementation.
   - Read each added comment with the code beneath it and verify that every claim is precise.
   - Check that comments explain why, constraints, or structure rather than duplicating syntax.
   - Ensure important branches and lifecycle boundaries are covered consistently, without leaving one stage undocumented.
   - Remove redundant, contradictory, or obsolete nearby comments encountered inside the requested scope.

8. Validate without changing behavior.
   - Run editor diagnostics for touched files when available.
   - Run a whitespace or formatting check appropriate to the repository.
   - Respect repository and user testing preferences; do not run builds or broad tests when they have been explicitly disabled.
   - Report the documented files and any behavior that remained uncertain enough not to describe as fact.

## Decision Rules

- If the repository has dense explanatory comments, document each logical block and important transition at comparable density.
- If the repository is lightly commented, add comments only for contracts, invariants, ownership, threading, and surprising behavior.
- If a function is self-explanatory but part of a complex workflow, add one orienting function comment rather than line-by-line comments.
- If code and an existing comment disagree, trust verified behavior and update the comment.
- If behavior cannot be verified locally, state the uncertainty to the user instead of encoding a guess in source.

## Completion Criteria

- The requested code has comments at the same density and in the same voice as nearby code.
- Every non-obvious ownership, lifecycle, state, concurrency, and error-handling rule is documented.
- Comments explain intent and constraints rather than paraphrasing syntax.
- Existing comments in scope remain accurate and non-duplicative.
- No runtime behavior or public API changed during a comment-only pass.
- Static diagnostics and whitespace checks pass, subject to the user's validation preferences.
