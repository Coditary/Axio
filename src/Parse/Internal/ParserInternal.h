#pragma once

#include <memory>
#include <string>

#include "axc/AST/AST.h"
#include "axc/Lex/Token.h"

namespace axc::detail {

/// @brief Top-level parsing helper used by `Parser`.
class TopLevelParser {
  public:
    /// @brief Bind the helper to the shared parser state.
    explicit TopLevelParser(Parser& parser);

    /// @brief Parse the entire translation unit including package and declarations.
    TranslationUnit parseTranslationUnit();
    /// @brief Parse one top-level declaration from the current token.
    std::unique_ptr<Decl> parseTopLevelDecl();

  private:
    Parser& parser_;
};

/// @brief Declaration-oriented parser helper.
class DeclarationParser {
  public:
    /// @brief Bind the helper to the shared parser state.
    explicit DeclarationParser(Parser& parser);

    /// @brief Parse one or more import declarations.
    std::vector<std::unique_ptr<ImportDecl>> parseImportDecls();
    /// @brief Parse a global `let`/`const` declaration.
    std::unique_ptr<GlobalVarDecl> parseGlobalDecl(bool mutableStorage);
    /// @brief Parse a function or method declaration.
    std::unique_ptr<FunctionDecl> parseFunctionDecl(bool isExtern, bool isMethod = false, std::string receiverType = {});
    /// @brief Parse a struct declaration.
    std::unique_ptr<StructDecl> parseStructDecl();
    /// @brief Parse an enum declaration.
    std::unique_ptr<EnumDecl> parseEnumDecl();
    /// @brief Parse a class declaration.
    std::unique_ptr<ClassDecl> parseClassDecl();
    /// @brief Parse a type spelling.
    Type parseType();
    /// @brief Parse a function parameter.
    Parameter parseParameter();
    /// @brief Parse an optional single return type.
    std::optional<Type> parseOptionalReturnType();

  private:
    Parser& parser_;
};

/// @brief Statement-oriented parser helper.
class StatementParser {
  public:
    /// @brief Bind the helper to the shared parser state.
    explicit StatementParser(Parser& parser);

    /// @brief Parse a brace-delimited statement block.
    std::unique_ptr<CompoundStmt> parseCompoundStmt();
    /// @brief Dispatch to the appropriate statement parser based on the current token.
    std::unique_ptr<Stmt> parseStatement();
    /// @brief Parse a `return` statement.
    std::unique_ptr<Stmt> parseReturnStmt();
    /// @brief Parse a `defer` statement.
    std::unique_ptr<Stmt> parseDeferStmt();
    /// @brief Parse a local `let` or `const` statement.
    std::unique_ptr<Stmt> parseLetStmt();
    /// @brief Parse one binding inside a `let`/`const` statement.
    LetBinding parseLetBinding();
    /// @brief Parse an `if`/`else` statement.
    std::unique_ptr<Stmt> parseIfStmt();
    /// @brief Parse a `while` loop.
    std::unique_ptr<Stmt> parseWhileStmt();
    /// @brief Parse a C-style `for` loop.
    std::unique_ptr<Stmt> parseForStmt();
    /// @brief Parse a `do-while` loop.
    std::unique_ptr<Stmt> parseDoWhileStmt();
    /// @brief Parse a `switch` statement and its case patterns.
    std::unique_ptr<Stmt> parseSwitchStmt();
    /// @brief Parse a `break` statement.
    std::unique_ptr<Stmt> parseBreakStmt();
    /// @brief Parse a `continue` statement.
    std::unique_ptr<Stmt> parseContinueStmt();
    /// @brief Parse an expression statement.
    std::unique_ptr<Stmt> parseExprStmt();

  private:
    Parser& parser_;
};

/// @brief Expression-oriented parser helper split by precedence levels.
class ExpressionParser {
  public:
    /// @brief Bind the helper to the shared parser state.
    explicit ExpressionParser(Parser& parser);

    /// @brief Parse a full expression.
    std::unique_ptr<Expr> parseExpression();
    /// @brief Parse assignment expressions.
    std::unique_ptr<Expr> parseAssignment();
    /// @brief Parse logical-or expressions.
    std::unique_ptr<Expr> parseLogicalOr();
    /// @brief Parse logical-and expressions.
    std::unique_ptr<Expr> parseLogicalAnd();
    /// @brief Parse comparison chains.
    std::unique_ptr<Expr> parseComparisonChain();
    /// @brief Parse bitwise-or expressions.
    std::unique_ptr<Expr> parseBitwiseOr();
    /// @brief Parse bitwise-xor expressions.
    std::unique_ptr<Expr> parseBitwiseXor();
    /// @brief Parse bitwise-and expressions.
    std::unique_ptr<Expr> parseBitwiseAnd();
    /// @brief Parse shift expressions.
    std::unique_ptr<Expr> parseShift();
    /// @brief Parse additive expressions.
    std::unique_ptr<Expr> parseAdditive();
    /// @brief Parse multiplicative expressions.
    std::unique_ptr<Expr> parseMultiplicative();
    /// @brief Parse unary expressions.
    std::unique_ptr<Expr> parseUnary();
    /// @brief Parse postfix expressions such as calls and member access.
    std::unique_ptr<Expr> parsePostfix();
    /// @brief Parse primary expressions such as literals and parenthesized expressions.
    std::unique_ptr<Expr> parsePrimary();
    /// @brief Parse typed array initializers.
    std::unique_ptr<Expr> parseArrayLiteral();
    /// @brief Parse brace array literals.
    std::unique_ptr<Expr> parseBraceArrayLiteral();
    /// @brief Parse an argument list terminated by `closing`.
    std::vector<std::unique_ptr<Expr>> parseArgumentList(TokenKind closing);

  private:
    /// Shared parser state and token cursor.
    Parser& parser_;
};

/// @brief Returns whether a token participates in comparison-chain parsing.
inline bool isComparisonToken(TokenKind kind) {
    return kind == TokenKind::EqualEqual || kind == TokenKind::BangEqual || kind == TokenKind::Less ||
           kind == TokenKind::LessEqual || kind == TokenKind::Greater || kind == TokenKind::GreaterEqual;
}

/// @brief Map comparison tokens onto AST binary operators.
inline BinaryOp tokenToComparisonOp(TokenKind kind) {
    switch (kind) {
        case TokenKind::EqualEqual:
            return BinaryOp::Equal;
        case TokenKind::BangEqual:
            return BinaryOp::NotEqual;
        case TokenKind::Less:
            return BinaryOp::Less;
        case TokenKind::LessEqual:
            return BinaryOp::LessEqual;
        case TokenKind::Greater:
            return BinaryOp::Greater;
        case TokenKind::GreaterEqual:
            return BinaryOp::GreaterEqual;
        default:
            return BinaryOp::Equal;
    }
}

/// @brief Map compound assignment tokens onto their desugared binary operator.
inline std::optional<BinaryOp> tokenToCompoundBinaryOp(TokenKind kind) {
    switch (kind) {
        case TokenKind::PlusEqual:
            return BinaryOp::Add;
        case TokenKind::MinusEqual:
            return BinaryOp::Sub;
        case TokenKind::StarEqual:
            return BinaryOp::Mul;
        case TokenKind::SlashEqual:
            return BinaryOp::Div;
        case TokenKind::PercentEqual:
            return BinaryOp::Mod;
        case TokenKind::AmpEqual:
            return BinaryOp::BitAnd;
        case TokenKind::PipeEqual:
            return BinaryOp::BitOr;
        case TokenKind::CaretEqual:
            return BinaryOp::BitXor;
        case TokenKind::ShiftLeftEqual:
            return BinaryOp::ShiftLeft;
        case TokenKind::ShiftRightEqual:
            return BinaryOp::ShiftRight;
        default:
            return std::nullopt;
    }
}

/// @brief Returns whether a token kind is a builtin scalar type name.
inline bool isBuiltinTypeName(TokenKind kind) {
    return kind == TokenKind::KwInt || kind == TokenKind::KwVoid || kind == TokenKind::KwStr ||
           kind == TokenKind::KwBool || kind == TokenKind::KwI2 || kind == TokenKind::KwI8 || kind == TokenKind::KwI16 ||
           kind == TokenKind::KwI32 || kind == TokenKind::KwI64 || kind == TokenKind::KwU8 || kind == TokenKind::KwU16 ||
           kind == TokenKind::KwU32 || kind == TokenKind::KwU64 || kind == TokenKind::KwShort || kind == TokenKind::KwLong ||
           kind == TokenKind::KwDouble || kind == TokenKind::KwFloat || kind == TokenKind::KwF8 || kind == TokenKind::KwF16 ||
           kind == TokenKind::KwF32 || kind == TokenKind::KwF64 || kind == TokenKind::KwChar;
}

/// @brief Clone an expression subtree for parser desugaring.
inline std::unique_ptr<Expr> cloneExpr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::IntegerLiteral:
            return std::make_unique<IntegerLiteralExpr>(static_cast<const IntegerLiteralExpr&>(expr).value, expr.range);
        case ExprKind::FloatLiteral:
            return std::make_unique<FloatLiteralExpr>(static_cast<const FloatLiteralExpr&>(expr).value, expr.range);
        case ExprKind::BoolLiteral:
            return std::make_unique<BoolLiteralExpr>(static_cast<const BoolLiteralExpr&>(expr).value, expr.range);
        case ExprKind::CharLiteral:
            return std::make_unique<CharLiteralExpr>(static_cast<const CharLiteralExpr&>(expr).value, expr.range);
        case ExprKind::StringLiteral:
            return std::make_unique<StringLiteralExpr>(static_cast<const StringLiteralExpr&>(expr).value, expr.range);
        case ExprKind::DeclRef:
            return std::make_unique<DeclRefExpr>(static_cast<const DeclRefExpr&>(expr).name, expr.range);
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            return std::make_unique<UnaryExpr>(unary.op, cloneExpr(*unary.operand), expr.range);
        }
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            return std::make_unique<BinaryExpr>(binary.op, cloneExpr(*binary.lhs), cloneExpr(*binary.rhs), expr.range);
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            std::vector<std::unique_ptr<Expr>> args;
            for (const auto& arg : call.arguments) {
                args.push_back(cloneExpr(*arg));
            }
            return std::make_unique<CallExpr>(cloneExpr(*call.callee), std::move(args), expr.range);
        }
        case ExprKind::Member: {
            const auto& member = static_cast<const MemberExpr&>(expr);
            return std::make_unique<MemberExpr>(cloneExpr(*member.base), member.member, expr.range);
        }
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            std::vector<std::unique_ptr<Expr>> values;
            for (const auto& value : init.values) {
                values.push_back(cloneExpr(*value));
            }
            return std::make_unique<InitializerExpr>(init.typeName, std::move(values), init.initKind, expr.range);
        }
    }
    return std::make_unique<IntegerLiteralExpr>(0, expr.range);
}

}  // namespace axc::detail
