# Axio Compiler Prototype

Axio is a modular compiler prototype for a C-like language, implemented in C++ with a build pipeline that targets LLVM IR, native object files, and final binaries.

The repository is structured so that lexer, parser, AST, metaprogramming passes, diagnostics, and LLVM lowering stay isolated. The goal is to make new language features land in focused modules instead of turning the compiler into a single giant file.

## What is already implemented

- CMake project with LLVM integration and optional re2c-based lexer generation
- C-like surface syntax for `struct`, `extern`, functions, local variables, integer arithmetic, string literals, line/block comments, pointer syntax, address-of, dereference, `return`, `defer`, and inline LLVM functions
- AST-driven parsing with a clean separation between lexing, parsing, meta passes, and code generation
- explicit module loading with public interfaces, selective imports, and re-exports
- semantic validation for `const` globals, locals, and parameters
- Diagnostic engine with source ranges, line and column rendering, and caret markers
- Simple metaprogramming hooks:
  - annotations such as `@inline`
  - compile-time file embedding through `__embed_text("path")`
- LLVM backend that emits `.ll`, `.o`, and a linked binary

## Language notes

- Comments:
  - `//` comments run until the next newline
  - `/* ... */` comments can span multiple lines
- Functions without an explicit return type are treated as `void`
  - `return` is optional
  - a bare `return` is allowed
- `defer call()` runs the deferred call when the current scope exits, in LIFO order
- `while`, `for`, `foreach`, `do { ... } while ...`, and `switch` are supported
- `switch` cases do not fall through into the next case
- reaching the end of a case body exits the `switch`; use `break` only when you want to exit early from inside nested control flow in that case
- `break` exits the nearest loop or `switch`
- `continue` advances the nearest loop and is rejected inside `switch`
- for non-flag enums, a `switch` without `default` must cover every enum case
- `switch` cases can use single values or ranges, for example `case 1..=3 {}` or `case Color.Red..=Color.Green {}`
- enum-switch exhaustiveness diagnostics list the concrete missing enum members when they can be determined
- overlapping constant switch cases are rejected during semantic analysis
- switch patterns must be compile-time constants; dynamic bounds like `case 1..limit {}` are rejected
- dense value-only switches still lower through LLVM `switch`; range-pattern switches use explicit comparisons before exact-value dispatch
- `fn llvm name(...) ... { ... }` lets you define a function body directly in LLVM IR
  - the inline LLVM body is parsed and verified before Axio accepts it

## Build

Requirements:

- CMake 3.28+
- LLVM development package with `LLVMConfig.cmake`
- C++20 compiler
- `clang++` for final linking
- `re2c` optional for lexer regeneration

Build commands:

```bash
cmake -S . -B build
cmake --build build -j
```

## API Documentation

Axio uses Doxygen-compatible inline comments in public headers and key internal
workflow headers.

The generated docs are not limited to public facades. Private methods and core
internal workflow helpers are documented too, so you can inspect what a method
exists for even when it is not part of the external API.

If `doxygen` is installed, generate browsable HTML and machine-readable XML:

```bash
make docs
```

Outputs:

- `docs/api/html/index.html`
- `docs/api/xml/`

See `docs/API_DOCS.md` for details on coverage and intended usage.

## Run

Compile a source file to LLVM IR, object file, and native binary:

```bash
./build/axc examples/hello.ax -o build/hello
```

Then run the produced program:

```bash
./build/hello
```

Generate only LLVM IR:

```bash
./build/axc examples/hello.ax --emit-llvm-only -o build/hello
```

## Example

See `examples/hello.ax` and `examples/assets/banner.txt`.

## Architecture overview

- `include/axc/Support`, `src/Support`
  - source management and human-readable diagnostics
- `include/axc/Lex`, `src/Lex`
  - token model and lexer, with `src/Lex/Grammar/Lexer.re` as the re2c grammar seed
- `include/axc/AST`
  - syntax tree definitions
- `include/axc/Parse`, `src/Parse`
  - recursive descent parser split by declarations, statements, and expression families
- `src/Driver/Module`
  - single-file parsing, module interfaces, import binding rules, qualification, and recursive loading
- `include/axc/Sema`, `src/Sema`
  - semantic validation for symbols, const rules, ownership checks, and class/member usage
- `include/axc/Meta`, `src/Meta`
  - compile-time annotation and embedding validation pipeline
- `include/axc/Codegen`, `src/Codegen`
  - LLVM IR lowering split by declarations, expressions, statements, and module workflow
- `include/axc/Driver`, `src/Driver`
  - compiler orchestration and CLI-facing pipeline control

## Module model

- each file declares its package path explicitly at the top:

```axio
package math.ops
```

- `pub fn`, `pub struct`, `pub class`, `pub enum`, `pub const`, `pub let`
  - exported as part of the package interface
- declarations without `pub`
  - private to their defining package file set
- bare import of a package injects its exported names into the current local scope
- import blocks are supported
- aliases are supported
- `pub import foo.bar{Name}` remains available as an explicit re-export form

Examples:

```axio
package app.main

import (
  math.ops
  pt geom.point
)

fn main() int {
    let p pt.Point = pt.Point(1, 2)
    return add(p.x, p.y)
}
```

The package interface fingerprint is derived from the exported API surface, so private implementation changes can stay below that boundary in future incremental builds.

## Const model

- `const` globals are immutable
- `const` local bindings are immutable inside their scope
- `const` function parameters cannot be reassigned
- these checks run in semantic analysis before code generation

## Metaprogramming model roadmap

The current codebase establishes the extension points for a multi-stage language model:

1. preprocessing stage
2. compile-time file/data extraction stage
3. annotation-driven AST transforms
4. LLVM IR transformation stage

Right now the repository contains the skeleton and first working examples for stages 2 and 3. The next step is to promote those hooks into a formal plugin/pass API.

## Performance notes

Your performance target is realistic only if the pipeline is designed around parallel front-end work and incremental compilation. This repository lays the modular groundwork, but it is intentionally still a prototype.

The most important future upgrades are:

- parallel parsing and semantic analysis per file or per package
- incremental dependency graph and cached intermediate artifacts
- arena allocation and interned identifiers for lower front-end overhead
- richer recovery parser so diagnostics keep flowing after the first failures
- a dedicated semantic phase with type checking and symbol indexing

GPU acceleration is usually not the first thing that helps a compiler. For most compilers, data layout, caching, dependency scheduling, and parallel front-end execution matter far more.
