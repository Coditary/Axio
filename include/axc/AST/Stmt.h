#pragma once

#include "axc/AST/Expr.h"

namespace axc {

enum class StmtKind {
    Compound,
    Return,
    Defer,
    Expr,
    Let,
    If,
    While,
    For,
    Foreach,
    DoWhile,
    Switch,
    Break,
    Continue,
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

struct DeferStmt final : Stmt {
    DeferStmt(std::unique_ptr<Expr> call, SourceRange range)
        : Stmt(StmtKind::Defer, range), call(std::move(call)) {}

    std::unique_ptr<Expr> call;
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

struct WhileStmt final : Stmt {
    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<CompoundStmt> body, SourceRange range)
        : Stmt(StmtKind::While, range), condition(std::move(condition)), body(std::move(body)) {}

    std::unique_ptr<Expr> condition;
    std::unique_ptr<CompoundStmt> body;
};

struct ForStmt final : Stmt {
    ForStmt(std::unique_ptr<Stmt> initializer,
            std::unique_ptr<Expr> condition,
            std::unique_ptr<Expr> step,
            std::unique_ptr<CompoundStmt> body,
            SourceRange range)
        : Stmt(StmtKind::For, range),
          initializer(std::move(initializer)),
          condition(std::move(condition)),
          step(std::move(step)),
          body(std::move(body)) {}

    std::unique_ptr<Stmt> initializer;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> step;
    std::unique_ptr<CompoundStmt> body;
};

struct ForeachStmt final : Stmt {
    ForeachStmt(std::string bindingName,
                Type bindingType,
                std::unique_ptr<Expr> iterable,
                std::unique_ptr<CompoundStmt> body,
                SourceRange bindingRange,
                SourceRange range)
        : Stmt(StmtKind::Foreach, range),
          bindingName(std::move(bindingName)),
          bindingType(std::move(bindingType)),
          iterable(std::move(iterable)),
          body(std::move(body)),
          bindingRange(bindingRange) {}

    std::string bindingName {};
    Type bindingType {};
    std::unique_ptr<Expr> iterable;
    std::unique_ptr<CompoundStmt> body;
    SourceRange bindingRange {};
};

struct DoWhileStmt final : Stmt {
    DoWhileStmt(std::unique_ptr<CompoundStmt> body, std::unique_ptr<Expr> condition, SourceRange range)
        : Stmt(StmtKind::DoWhile, range), body(std::move(body)), condition(std::move(condition)) {}

    std::unique_ptr<CompoundStmt> body;
    std::unique_ptr<Expr> condition;
};

struct SwitchCasePattern {
    std::unique_ptr<Expr> value;
    bool isRange = false;
    SourceRange range {};
};

struct SwitchCase {
    std::vector<SwitchCasePattern> patterns {};
    std::unique_ptr<CompoundStmt> body;
    bool isDefault = false;
    SourceRange range {};
};

struct SwitchStmt final : Stmt {
    SwitchStmt(std::unique_ptr<Expr> condition, std::vector<SwitchCase> cases, SourceRange range)
        : Stmt(StmtKind::Switch, range), condition(std::move(condition)), cases(std::move(cases)) {}

    std::unique_ptr<Expr> condition;
    std::vector<SwitchCase> cases {};
};

struct BreakStmt final : Stmt {
    explicit BreakStmt(SourceRange range) : Stmt(StmtKind::Break, range) {}
};

struct ContinueStmt final : Stmt {
    explicit ContinueStmt(SourceRange range) : Stmt(StmtKind::Continue, range) {}
};

}  // namespace axc
