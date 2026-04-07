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

class CompilerWorkflow {
  public:
    explicit CompilerWorkflow(const CompileOptions& options);

    bool run() const;

  private:
    bool loadSource(SourceManager& sourceManager, std::string& errorMessage) const;
    bool runFrontEnd(TranslationUnit& unit, SourceManager& sourceManager, DiagnosticEngine& diagnostics) const;
    bool shouldStopAfterAstDump(const TranslationUnit& unit) const;
    bool finalizeDiagnostics(DiagnosticEngine& diagnostics) const;
    std::optional<EmitOptions> buildEmitOptions() const;

    const CompileOptions& options_;
};

}  // namespace axc
