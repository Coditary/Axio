# Architecture

## Design goals

- C-like syntax and mental model
- fast front-end evolution without tight coupling between phases
- high-quality diagnostics with room for multi-error recovery
- LLVM as the final backend

## Pipeline

1. `SourceManager`
   - loads source text
   - maps byte offsets to line and column positions

2. `Lexer`
   - tokenizes the source
   - currently ships with a fallback C++ lexer
   - keeps a re2c grammar file so the project can move to generated lexing cleanly

3. `Parser`
   - builds an AST for declarations, statements, and expressions
   - already includes synchronization hooks for future multi-error recovery

4. `Module Loader`
   - parses imported files independently
   - builds module interfaces from the public surface only
   - resolves qualified and selective imports
   - supports explicit re-exports through `pub import module{name}`

5. `Sema`
   - validates symbol usage before codegen
   - checks const storage rules for globals, locals, and parameters
   - validates class/member access, arrays, pointers, enums, and scalar compatibility

6. `LLVMEmitter`
   - lowers AST nodes to LLVM IR
   - emits `.ll`
   - emits native object files through the host target machine
   - links the final binary using `clang++`

## Responsibility Map

- `src/Lex/Core`, `src/Lex/Support`, `src/Lex/Grammar`
  - tokenization only
- `src/Parse/Core`
  - top-level parser control flow
- `src/Parse/Decl`
  - declarations such as `fn`, `struct`, `class`, `enum`, `import`, `let`, `const`
- `src/Parse/Stmt`
  - statements such as `return`, `if`, and local bindings
- `src/Parse/Expr/Arithmetic`
  - additive, multiplicative, bitwise, and shift parsing
- `src/Parse/Expr/Logical`
  - comparisons and logical operators
- `src/Parse/Expr/Primary`
  - literals, calls, postfix access, and initializers
- `src/Driver/Module/ModuleFileParser.cpp`
  - parse a single module file in isolation
- `src/Driver/Module/ModuleImportResolver.cpp`
  - decide which imported names enter local scope and which modules are only namespace-visible
- `src/Driver/Module/ModuleInterface.cpp`
  - build the stable public module interface and API fingerprint
- `src/Driver/Module/ModuleQualifier.cpp`
  - rewrite names to qualified module names
- `src/Driver/Module/ModuleLoader.cpp`
  - orchestrate recursive loading order and merge modules for later phases
- `src/Sema/Decl`
  - declaration and scope validation
- `src/Sema/Expr`
  - expression validation and constant evaluation
- `src/Codegen/Decl`
  - declaration lowering for structs, classes, globals, and functions
- `src/Codegen/Expr`
  - runtime lowering of expressions
- `src/Codegen/Stmt`
  - statement lowering

## How to find bugs fast

- parser bug for `a + b`
  - `src/Parse/Expr/Arithmetic/ParserAdditive.cpp`
- semantic bug for invalid arithmetic or const assignment
  - `src/Sema/Expr/SemaExpr.cpp`
- LLVM codegen bug for arithmetic lowering
  - `src/Codegen/Expr/LLVMEmitterExpr.cpp`
- import, visibility, or re-export bug
  - `src/Driver/Module/ModuleImportResolver.cpp`
  - `src/Driver/Module/ModuleInterface.cpp`
  - `src/Driver/Module/ModuleQualifier.cpp`

## Import Model

- every source file declares its package path explicitly with `package foo.bar`
- `import foo.bar`
  - imports the package and injects all exported names into the local scope
  - also keeps the package path available for qualified type/member access
- `import alias foo.bar`
  - imports the same package under an additional short alias
- import blocks are supported:

```axio
import (
  math.ops
  pt geom.point
)
```

- `pub import foo.bar{Name}`
  - still acts as an explicit re-export mechanism for facade packages
- only `pub` declarations are part of a package interface
- private declarations remain visible only inside their own package

This keeps package identity explicit and makes interface hashes stable when only private implementation changes.

## Why the modules are split this way

- parser changes should not force LLVM backend edits
- diagnostics should be reusable by lexer, parser, semantic analysis, and backend validation
- semantic analysis should stay its own phase between parser and codegen

## Feature growth plan

For a serious language implementation, add these modules next:

- `Sema/`
  - type checking
  - overload and symbol resolution
  - constant evaluation
  - borrow/pointer rules if desired

- `IR/`
  - compiler-owned mid-level IR
  - easier optimization and AST transforms before LLVM lowering

- `Runtime/`
  - ABI helpers
  - allocator hooks
  - reflection metadata support

## Error-quality strategy

To beat the usual weak diagnostics, keep these principles:

- preserve source ranges on every AST node
- separate parsing from semantic validation so more errors can be collected in one run
- keep parser recovery local and cheap
- store related-note chains for follow-up diagnostics
- introduce fix-it hints once the semantic layer exists

## Performance strategy

To approach Go-like compile speed, prioritize this order:

1. cheap tokenization and identifier interning
2. package/file-level parallelism
3. incremental caches with stable hashes
4. compact AST/IR allocation arenas
5. careful LLVM pass selection

GPU offload is not a primary win for a general-purpose compiler frontend, but specialized static analysis or data-parallel transforms could be explored later.
