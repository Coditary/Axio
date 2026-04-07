#pragma once

#include <filesystem>

namespace axc {

/// @brief Command-line controlled compilation settings for one compiler invocation.
///
/// The driver translates CLI flags into this struct before handing control to
/// `Compiler`. The default configuration emits LLVM IR, an object file, and a
/// linked binary for the provided input file.
struct CompileOptions {
    /// Primary source file to compile.
    std::filesystem::path inputFile {};
    /// Output path stem or explicit output path depending on the selected mode.
    std::filesystem::path outputFile {};
    /// Emit textual LLVM IR (`.ll`).
    bool emitLlvmIr = true;
    /// Emit a native object file (`.o`).
    bool emitObject = true;
    /// Emit and optionally link a final native binary.
    bool emitBinary = true;
    /// Stop after the front-end and semantic checks.
    bool checkOnly = false;
    /// Print the parsed AST and skip later compilation phases.
    bool dumpAst = false;
};

/// @brief Facade for running the complete compiler pipeline.
class Compiler {
  public:
    /// @brief Compile one input file according to `options`.
    /// @return `true` on success, `false` if any phase reports an error.
    bool compile(const CompileOptions& options) const;
};

}  // namespace axc
