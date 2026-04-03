#pragma once

#include "axc/AST/AST.h"

namespace axc {

class DiagnosticEngine;

class Sema {
  public:
    explicit Sema(DiagnosticEngine& diagnostics);

    bool analyze(TranslationUnit& translationUnit) const;

  private:
    DiagnosticEngine& diagnostics_;
    void validateDecl(const Decl& decl) const;
    void validateFunction(const FunctionDecl& fn) const;
    void validateStmt(const Stmt& stmt, const FunctionDecl& fn) const;
    void validateExpr(const Expr& expr) const;
};

}  // namespace axc
