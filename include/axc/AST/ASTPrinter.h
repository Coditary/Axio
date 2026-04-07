#pragma once

#include <ostream>

#include "axc/AST/AST.h"

namespace axc {

/// @brief Utility for producing a human-readable tree dump of the AST.
class ASTPrinter {
  public:
    /// @brief Create a printer writing to `out`.
    explicit ASTPrinter(std::ostream& out);

    /// @brief Print a translation unit and all nested declarations.
    void print(const TranslationUnit& unit) const;

  private:
    void indent(int level) const;
    void printDecl(const Decl& decl, int level) const;
    void printStmt(const Stmt& stmt, int level) const;
    void printExpr(const Expr& expr, int level) const;
    void printType(const Type& type) const;

    std::ostream& out_;
};

}  // namespace axc
