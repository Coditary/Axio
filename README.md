# Axio Compiler Prototype

Axio is a modular compiler prototype for a C-like language, implemented in C++ with a build pipeline that targets LLVM IR, native object files, and final binaries.

The repository is structured so that lexer, parser, AST, metaprogramming passes, diagnostics, and LLVM lowering stay isolated. The goal is to make new language features land in focused modules instead of turning the compiler into a single giant file.

## What is already implemented

- CMake project with LLVM integration and optional re2c-based lexer generation
- C-like surface syntax for `struct`, `extern`, functions, local variables, integer arithmetic, string literals, pointer syntax, address-of, dereference, and `return`
- AST-driven parsing with a clean separation between lexing, parsing, meta passes, and code generation
- Diagnostic engine with source ranges, line and column rendering, and caret markers
- Simple metaprogramming hooks:
  - annotations such as `@inline`
  - compile-time file embedding through `__embed_text("path")`
- LLVM backend that emits `.ll`, `.o`, and a linked binary

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
  - token model and lexer, with `src/Lex/Lexer.re` as the re2c grammar seed
- `include/axc/AST`
  - syntax tree definitions
- `include/axc/Parse`, `src/Parse`
  - recursive descent parser for the current language subset
- `include/axc/Meta`, `src/Meta`
  - compile-time annotation and embedding validation pipeline
- `include/axc/Codegen`, `src/Codegen`
  - LLVM IR lowering, object emission, and final binary linking
- `include/axc/Driver`, `src/Driver`
  - compiler orchestration and CLI-facing pipeline control

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
