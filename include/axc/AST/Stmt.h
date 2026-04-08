#pragma once

#include "axc/AST/Expr.h"

namespace axc {

/// @brief Statement categories supported by the AST.
enum class StmtKind {
    Compound,
    Return,
    Defer,
    Expr,
    Let,
    If,
    While,
    For,
    DoWhile,
    Switch,
    Break,
    Continue,
};

/// @brief Base class for all statement nodes.
struct Stmt {
    explicit Stmt(StmtKind kind, SourceRange range) : kind(kind), range(range) {}
    virtual ~Stmt() = default;

    StmtKind kind;
    SourceRange range;
};

/// @brief Block statement containing nested statements.
struct CompoundStmt final : Stmt {
    explicit CompoundStmt(SourceRange range) : Stmt(StmtKind::Compound, range) {}

    std::vector<std::unique_ptr<Stmt>> statements {};
};

/// @brief Return statement supporting an optional single return value.
struct ReturnStmt final : Stmt {
    ReturnStmt(std::unique_ptr<Expr> value, SourceRange range)
        : Stmt(StmtKind::Return, range), value(std::move(value)) {}

    std::unique_ptr<Expr> value;
};

/// @brief Deferred call executed when the current scope exits.
struct DeferStmt final : Stmt {
    DeferStmt(std::unique_ptr<Expr> call, SourceRange range)
        : Stmt(StmtKind::Defer, range), call(std::move(call)) {}

    std::unique_ptr<Expr> call;
};

/// @brief Statement wrapper around an expression.
struct ExprStmt final : Stmt {
    ExprStmt(std::unique_ptr<Expr> expression, SourceRange range)
        : Stmt(StmtKind::Expr, range), expression(std::move(expression)) {}

    std::unique_ptr<Expr> expression;
};

/// @brief Single binding introduced by a `let` or `const` statement.
struct LetBinding {
    std::string name {};
    Type explicitType {};
    SourceRange range {};
};

/// @brief Local variable declaration statement.
struct LetStmt final : Stmt {
    LetStmt(std::vector<LetBinding> bindings, std::unique_ptr<Expr> initializer, bool mutableStorage, SourceRange range)
        : Stmt(StmtKind::Let, range), bindings(std::move(bindings)), initializer(std::move(initializer)), mutableStorage(mutableStorage) {}

    std::vector<LetBinding> bindings {};
    std::unique_ptr<Expr> initializer;
    bool mutableStorage = true;
};

/// @brief Conditional statement with optional `else` branch.
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

/// @brief `while` loop statement.
struct WhileStmt final : Stmt {
    WhileStmt(std::unique_ptr<Expr> condition, std::unique_ptr<CompoundStmt> body, SourceRange range)
        : Stmt(StmtKind::While, range), condition(std::move(condition)), body(std::move(body)) {}

    std::unique_ptr<Expr> condition;
    std::unique_ptr<CompoundStmt> body;
};

/// @brief C-style `for` loop with initializer, condition, and step.
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

/// @brief `do { ... } while cond` loop statement.
struct DoWhileStmt final : Stmt {
    DoWhileStmt(std::unique_ptr<CompoundStmt> body, std::unique_ptr<Expr> condition, SourceRange range)
        : Stmt(StmtKind::DoWhile, range), body(std::move(body)), condition(std::move(condition)) {}

    std::unique_ptr<CompoundStmt> body;
    std::unique_ptr<Expr> condition;
};

/// @brief Single switch-case pattern with one exact compile-time value.
struct SwitchCasePattern {
    std::unique_ptr<Expr> value;
    SourceRange range {};
};

/// @brief One `case` or `default` arm inside a switch statement.
struct SwitchCase {
    std::vector<SwitchCasePattern> patterns {};
    std::unique_ptr<CompoundStmt> body;
    bool isDefault = false;
    SourceRange range {};
};

/// @brief `switch` statement over constant case patterns.
struct SwitchStmt final : Stmt {
    SwitchStmt(std::unique_ptr<Expr> condition, std::vector<SwitchCase> cases, SourceRange range)
        : Stmt(StmtKind::Switch, range), condition(std::move(condition)), cases(std::move(cases)) {}

    std::unique_ptr<Expr> condition;
    std::vector<SwitchCase> cases {};
};

/// @brief `break` statement.
struct BreakStmt final : Stmt {
    explicit BreakStmt(SourceRange range) : Stmt(StmtKind::Break, range) {}
};

/// @brief `continue` statement.
struct ContinueStmt final : Stmt {
    explicit ContinueStmt(SourceRange range) : Stmt(StmtKind::Continue, range) {}
};

}  // namespace axc
