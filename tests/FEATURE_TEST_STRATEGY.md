# Feature Test Strategy

This repository now contains several different kinds of language tests:

- `tests/parse/`
  - single-feature syntax and AST shape checks
- `tests/sema/`
  - warnings, unsupported-feature diagnostics, and negative cases
- `tests/codegen/`
  - LLVM IR shape checks for the currently lowered subset
- `tests/runtime/`
  - end-to-end binary execution for simple programs with deterministic exit codes
- `tests/spec/`
  - large specimen files that ensure broad language samples still parse

Category layout:

- organize tests by concern first, then by phase where it helps
- prefer directories like `arithmetic/`, `nullability/`, `ownership/`, `classes/`, `multi_return/`, `enums/`
- keep one behavioral point per file whenever possible

Important principle:

- Prefer one assertion target per test file when possible.
- Add separate edge-case and failure tests instead of combining many operators in one file.
- Keep broad showcase tests, but never rely on them as the only coverage.
- Prefer boundary cases over exhaustive Cartesian products.
- Do not keep redundant tests like many near-identical `5 + 4`, `5 + 5`, `5 + 6` cases unless they exercise distinct semantics.
- If a feature is already covered by one representative happy path plus boundary/error cases, remove duplicate cases.

The language spec in `AGENTS.md` is much larger than the currently lowered implementation, so many tests intentionally validate parsing and diagnostics for future features even before codegen exists.

Generated granular tests:

- `tools/generate_feature_tests.py`
  - should emit only representative boundary-oriented tests under `tests/generated/`
  - should not generate large redundant matrices when a smaller set covers the same semantics
