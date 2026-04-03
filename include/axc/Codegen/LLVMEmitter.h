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

struct EmitOptions {
    std::filesystem::path llvmIrOutput {};
    std::filesystem::path objectOutput {};
    std::filesystem::path binaryOutput {};
    bool linkBinary = false;
};

class LLVMEmitter {
  public:
    LLVMEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    bool emit(const TranslationUnit& translationUnit, const EmitOptions& options);

  private:
    const SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
};

}  // namespace axc
