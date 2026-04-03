#pragma once

#include <ostream>

#include "axc/AST/AST.h"

namespace axc {

class ASTPrinter {
  public:
    explicit ASTPrinter(std::ostream& out);

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
