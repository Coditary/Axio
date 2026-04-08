#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "axc/AST/AST.h"
#include "axc/Codegen/LLVMEmitter.h"
#include "axc/Driver/Compiler.h"

namespace axc {

class DiagnosticEngine;
class SourceManager;

/// @brief Internal orchestration object for the CLI compiler pipeline.
///
/// This class wires together source loading, module loading, optional AST
/// dumping, semantic analysis, and LLVM emission.
class CompilerWorkflow {
  public:
    /// @brief Create a workflow using already-parsed compile options.
    explicit CompilerWorkflow(const CompileOptions& options);

    /// @brief Execute the full compilation workflow.
    bool run() const;

  private:
    /// @brief Load the primary input file into the source manager.
    bool loadSource(SourceManager& sourceManager, std::string& errorMessage) const;
    /// @brief Run module loading, parsing, and semantic analysis.
    bool runFrontEnd(TranslationUnit& unit, SourceManager& sourceManager, DiagnosticEngine& diagnostics) const;
    /// @brief Decide whether the workflow should stop after producing an AST dump.
    bool shouldStopAfterAstDump(const TranslationUnit& unit) const;
    /// @brief Render deferred diagnostics and decide whether compilation may continue.
    bool finalizeDiagnostics(DiagnosticEngine& diagnostics) const;
    /// @brief Build the backend emission plan from CLI options.
    std::optional<EmitOptions> buildEmitOptions() const;

    /// Compile options supplied by the CLI layer.
    const CompileOptions& options_;
};

}  // namespace axc
