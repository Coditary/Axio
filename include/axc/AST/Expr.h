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
    NullLiteral,
    DeclRef,
    Unary,
    Binary,
    Cast,
    Range,
    Call,
    Member,
    Initializer,
    CompileCall,
    Dialect,
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
    IsNonNull,
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
    Set,
    Unset,
    Toggle,
    Is,
    IsNot,
    Assign,
    InRange,
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

/// @brief `null` literal expression.
struct NullLiteralExpr final : Expr {
    explicit NullLiteralExpr(SourceRange range) : Expr(ExprKind::NullLiteral, range) {}
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

/// @brief Explicit cast using `expr as Type` syntax.
struct CastExpr final : Expr {
    CastExpr(std::unique_ptr<Expr> value, Type targetType, SourceRange range)
        : Expr(ExprKind::Cast, range), value(std::move(value)), targetType(std::move(targetType)) {}

    std::unique_ptr<Expr> value;
    Type targetType {};
};

/// @brief Range expression used by membership tests and switch range cases.
struct RangeExpr final : Expr {
    RangeExpr(std::unique_ptr<Expr> start, std::unique_ptr<Expr> end, bool inclusive, SourceRange range)
        : Expr(ExprKind::Range, range), start(std::move(start)), end(std::move(end)), inclusive(inclusive) {}

    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
    bool inclusive = false;
};

/// @brief Function or method call expression.
struct CallExpr final : Expr {
    CallExpr(std::unique_ptr<Expr> callee,
             std::vector<std::unique_ptr<Expr>> compileArguments,
             std::vector<std::unique_ptr<Expr>> runtimeArguments,
             bool nullSafe,
             SourceRange range)
        : Expr(ExprKind::Call, range),
          callee(std::move(callee)),
          compileArguments(std::move(compileArguments)),
          runtimeArguments(std::move(runtimeArguments)),
          nullSafe(nullSafe) {}

    std::unique_ptr<Expr> callee;
    std::vector<std::unique_ptr<Expr>> compileArguments {};
    std::vector<std::unique_ptr<Expr>> runtimeArguments {};
    bool nullSafe = false;
};

/// @brief Member access expression, optionally null-safe.
struct MemberExpr final : Expr {
    MemberExpr(std::unique_ptr<Expr> base, std::string member, bool nullSafe, SourceRange range)
        : Expr(ExprKind::Member, range), base(std::move(base)), member(std::move(member)), nullSafe(nullSafe) {}

    std::unique_ptr<Expr> base;
    std::string member {};
    bool nullSafe = false;
};

/// @brief Structured initializer or array-literal expression.
struct InitializerExpr final : Expr {
    InitializerExpr(std::string typeName, std::vector<std::unique_ptr<Expr>> values, InitKind initKind, SourceRange range)
        : Expr(ExprKind::Initializer, range), typeName(std::move(typeName)), values(std::move(values)), initKind(initKind) {}

    std::string typeName {};
    std::vector<std::unique_ptr<Expr>> values {};
    InitKind initKind = InitKind::Value;
};

/// @brief Compile-time intrinsic or meta-function call.
struct CompileCallExpr final : Expr {
    CompileCallExpr(std::string callee, std::vector<std::unique_ptr<Expr>> arguments, SourceRange range)
        : Expr(ExprKind::CompileCall, range), callee(std::move(callee)), arguments(std::move(arguments)) {}

    std::string callee {};
    std::vector<std::unique_ptr<Expr>> arguments {};
};

/// @brief Embedded foreign-language block captured as an expression.
struct DialectExpr final : Expr {
    DialectExpr(std::string dialectName, std::string content, SourceRange range)
        : Expr(ExprKind::Dialect, range), dialectName(std::move(dialectName)), content(std::move(content)) {}

    std::string dialectName {};
    std::string content {};
};

}  // namespace axc
