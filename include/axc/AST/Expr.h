#pragma once

#include "axc/AST/Base.h"

namespace axc {

struct Decl;

/// @brief Runtime and compile-time expression categories supported by the AST.
enum class ExprKind {
    IntegerLiteral,
    FloatLiteral,
    BoolLiteral,
    CharLiteral,
    StringLiteral,
    DeclRef,
    Unary,
    Binary,
    Call,
    Member,
    Initializer,
};

/// @brief Unary operators supported by the language surface.
enum class UnaryOp {
    Negate,
    AddressOf,
    Dereference,
    LogicalNot,
    BitwiseNot,
    PreIncrement,
    PreDecrement,
    PostIncrement,
    PostDecrement,
};

/// @brief Binary operators supported by the language surface.
enum class BinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    BitAnd,
    BitOr,
    BitXor,
    ShiftLeft,
    ShiftRight,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    LogicalAnd,
    LogicalOr,
    Assign,
};

/// @brief Base class for all expression nodes.
struct Expr {
    explicit Expr(ExprKind kind, SourceRange range) : kind(kind), range(range) {}
    virtual ~Expr() = default;

    ExprKind kind;
    SourceRange range;
};

/// @brief Integer literal expression.
struct IntegerLiteralExpr final : Expr {
    explicit IntegerLiteralExpr(std::int64_t value, SourceRange range)
        : Expr(ExprKind::IntegerLiteral, range), value(value) {}

    std::int64_t value = 0;
};

/// @brief Floating-point literal expression.
struct FloatLiteralExpr final : Expr {
    explicit FloatLiteralExpr(double value, SourceRange range)
        : Expr(ExprKind::FloatLiteral, range), value(value) {}

    double value = 0.0;
};

/// @brief Boolean literal expression.
struct BoolLiteralExpr final : Expr {
    explicit BoolLiteralExpr(bool value, SourceRange range)
        : Expr(ExprKind::BoolLiteral, range), value(value) {}

    bool value = false;
};

/// @brief Character literal expression.
struct CharLiteralExpr final : Expr {
    explicit CharLiteralExpr(char value, SourceRange range)
        : Expr(ExprKind::CharLiteral, range), value(value) {}

    char value = 0;
};

/// @brief String literal expression.
struct StringLiteralExpr final : Expr {
    explicit StringLiteralExpr(std::string value, SourceRange range)
        : Expr(ExprKind::StringLiteral, range), value(std::move(value)) {}

    std::string value {};
};

/// @brief Reference to a declaration or symbol name.
struct DeclRefExpr final : Expr {
    explicit DeclRefExpr(std::string name, SourceRange range)
        : Expr(ExprKind::DeclRef, range), name(std::move(name)) {}

    std::string name {};
};

/// @brief Unary operator application.
struct UnaryExpr final : Expr {
    UnaryExpr(UnaryOp op, std::unique_ptr<Expr> operand, SourceRange range)
        : Expr(ExprKind::Unary, range), op(op), operand(std::move(operand)) {}

    UnaryOp op;
    std::unique_ptr<Expr> operand;
};

/// @brief Binary operator application.
struct BinaryExpr final : Expr {
    BinaryExpr(BinaryOp op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs, SourceRange range)
        : Expr(ExprKind::Binary, range), op(op), lhs(std::move(lhs)), rhs(std::move(rhs)) {}

    BinaryOp op;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
};

/// @brief Function or method call expression.
struct CallExpr final : Expr {
    CallExpr(std::unique_ptr<Expr> callee, std::vector<std::unique_ptr<Expr>> arguments, SourceRange range)
        : Expr(ExprKind::Call, range),
          callee(std::move(callee)),
          arguments(std::move(arguments)) {}

    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> arguments {};
};

/// @brief Member access expression.
struct MemberExpr final : Expr {
    MemberExpr(std::unique_ptr<Expr> base, std::string member, SourceRange range)
        : Expr(ExprKind::Member, range), base(std::move(base)), member(std::move(member)) {}

    std::unique_ptr<Expr> base;
    std::string member {};
};

/// @brief Structured initializer or array-literal expression.
struct InitializerExpr final : Expr {
    InitializerExpr(std::string typeName, std::vector<std::unique_ptr<Expr>> values, InitKind initKind, SourceRange range)
        : Expr(ExprKind::Initializer, range), typeName(std::move(typeName)), values(std::move(values)), initKind(initKind) {}

    std::string typeName {};
    std::vector<std::unique_ptr<Expr>> values {};
    InitKind initKind = InitKind::Value;
};

}  // namespace axc
