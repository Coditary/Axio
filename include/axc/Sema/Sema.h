#pragma once

#include "axc/AST/AST.h"

namespace axc {

class DiagnosticEngine;

/// @brief Front-facing semantic analysis entry point.
class Sema {
  public:
    /// @brief Create semantic analysis bound to one diagnostic engine.
    explicit Sema(DiagnosticEngine& diagnostics);

    /// @brief Analyze a parsed translation unit and emit diagnostics.
    bool analyze(TranslationUnit& translationUnit) const;

  private:
    DiagnosticEngine& diagnostics_;
    void validateDecl(const Decl& decl) const;
    void validateFunction(const FunctionDecl& fn) const;
    void validateStmt(const Stmt& stmt, const FunctionDecl& fn) const;
    void validateExpr(const Expr& expr) const;
};

}  // namespace axc
