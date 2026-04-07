#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

#include "axc/AST/AST.h"

namespace axc {

class DiagnosticEngine;
class SourceManager;

/// @brief Output paths and switches controlling LLVM emission.
struct EmitOptions {
    std::filesystem::path llvmIrOutput {};
    std::filesystem::path objectOutput {};
    std::filesystem::path binaryOutput {};
    bool linkBinary = false;
};

/// @brief Public entry point for lowering the AST to LLVM IR and native artifacts.
class LLVMEmitter {
  public:
    /// @brief Create an emitter bound to one source manager and diagnostic engine.
    LLVMEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    /// @brief Emit LLVM IR, object files, and/or a native binary.
    bool emit(const TranslationUnit& translationUnit, const EmitOptions& options);

  private:
    const SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
};

}  // namespace axc
