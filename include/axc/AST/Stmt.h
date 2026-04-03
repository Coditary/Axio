#pragma once

#include "axc/AST/Expr.h"

namespace axc {

enum class StmtKind {
    Compound,
    Return,
    Expr,
    Let,
    If,
};

struct Stmt {
    explicit Stmt(StmtKind kind, SourceRange range) : kind(kind), range(range) {}
    virtual ~Stmt() = default;

    StmtKind kind;
    SourceRange range;
};

struct CompoundStmt final : Stmt {
    explicit CompoundStmt(SourceRange range) : Stmt(StmtKind::Compound, range) {}

    std::vector<std::unique_ptr<Stmt>> statements {};
};

struct ReturnStmt final : Stmt {
    ReturnStmt(std::vector<std::unique_ptr<Expr>> values, SourceRange range)
        : Stmt(StmtKind::Return, range), values(std::move(values)) {}

    std::vector<std::unique_ptr<Expr>> values {};
};

struct ExprStmt final : Stmt {
    ExprStmt(std::unique_ptr<Expr> expression, SourceRange range)
        : Stmt(StmtKind::Expr, range), expression(std::move(expression)) {}

    std::unique_ptr<Expr> expression;
};

struct LetBinding {
    std::string name {};
    Type explicitType {};
    SourceRange range {};
};

struct LetStmt final : Stmt {
    LetStmt(std::vector<LetBinding> bindings, std::unique_ptr<Expr> initializer, bool mutableStorage, SourceRange range)
        : Stmt(StmtKind::Let, range), bindings(std::move(bindings)), initializer(std::move(initializer)), mutableStorage(mutableStorage) {}

    std::vector<LetBinding> bindings {};
    std::unique_ptr<Expr> initializer;
    bool mutableStorage = true;
};

struct IfStmt final : Stmt {
    IfStmt(std::unique_ptr<Expr> condition,
           std::unique_ptr<CompoundStmt> thenBlock,
           std::unique_ptr<Stmt> elseBranch,
           SourceRange range)
        : Stmt(StmtKind::If, range),
          condition(std::move(condition)),
          thenBlock(std::move(thenBlock)),
          elseBranch(std::move(elseBranch)) {}

    std::unique_ptr<Expr> condition;
    std::unique_ptr<CompoundStmt> thenBlock;
    std::unique_ptr<Stmt> elseBranch;
};

}  // namespace axc
