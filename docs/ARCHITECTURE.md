# Architecture

## Design goals

- C-like syntax and mental model
- fast front-end evolution without tight coupling between phases
- high-quality diagnostics with room for multi-error recovery
- explicit metaprogramming stages instead of one magical macro system
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
   - builds an AST for declarations, statements, expressions, and annotations
   - already includes synchronization hooks for future multi-error recovery

4. `MetaPipeline`
   - validates annotations
   - validates compile-time embedding primitives such as `__embed_text`
   - is the intended insertion point for AST rewriting passes

5. `LLVMEmitter`
   - lowers AST nodes to LLVM IR
   - emits `.ll`
   - emits native object files through the host target machine
   - links the final binary using `clang++`

## Why the modules are split this way

- parser changes should not force LLVM backend edits
- metaprogramming should not live inside the parser
- diagnostics should be reusable by lexer, parser, semantic analysis, and backend validation
- future semantic analysis should become its own phase between parser and meta/codegen

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

- `Passes/`
  - AST passes
  - IR passes
  - annotation handlers

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
