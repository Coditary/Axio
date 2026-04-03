#pragma once

#include <memory>
#include <string>

#include "axc/AST/AST.h"
#include "axc/Lex/Token.h"

namespace axc::detail {

inline bool isComparisonToken(TokenKind kind) {
    return kind == TokenKind::EqualEqual || kind == TokenKind::BangEqual || kind == TokenKind::Less ||
           kind == TokenKind::LessEqual || kind == TokenKind::Greater || kind == TokenKind::GreaterEqual ||
           kind == TokenKind::KwIn;
}

inline bool isEnumOperationIdentifier(const Token& token) {
    return token.kind == TokenKind::Identifier &&
           (token.lexeme == "set" || token.lexeme == "unset" || token.lexeme == "toggle" || token.lexeme == "is" || token.lexeme == "isnot");
}

inline BinaryOp enumOperationFromLexeme(const std::string& lexeme) {
    if (lexeme == "set") {
        return BinaryOp::Set;
    }
    if (lexeme == "unset") {
        return BinaryOp::Unset;
    }
    if (lexeme == "toggle") {
        return BinaryOp::Toggle;
    }
    if (lexeme == "is") {
        return BinaryOp::Is;
    }
    return BinaryOp::IsNot;
}

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
        case TokenKind::KwIn:
            return BinaryOp::InRange;
        default:
            return BinaryOp::Equal;
    }
}

inline bool isBuiltinTypeName(TokenKind kind) {
    return kind == TokenKind::KwInt || kind == TokenKind::KwVoid || kind == TokenKind::KwStr || kind == TokenKind::KwError ||
           kind == TokenKind::KwBool || kind == TokenKind::KwI2 || kind == TokenKind::KwI8 || kind == TokenKind::KwI16 ||
           kind == TokenKind::KwI32 || kind == TokenKind::KwI64 || kind == TokenKind::KwU8 || kind == TokenKind::KwU16 ||
           kind == TokenKind::KwU32 || kind == TokenKind::KwU64 || kind == TokenKind::KwShort || kind == TokenKind::KwLong ||
           kind == TokenKind::KwDouble || kind == TokenKind::KwFloat || kind == TokenKind::KwF8 || kind == TokenKind::KwF16 ||
           kind == TokenKind::KwF32 || kind == TokenKind::KwF64 || kind == TokenKind::KwChar;
}

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
        case ExprKind::NullLiteral:
            return std::make_unique<NullLiteralExpr>(expr.range);
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
        case ExprKind::Range: {
            const auto& range = static_cast<const RangeExpr&>(expr);
            return std::make_unique<RangeExpr>(cloneExpr(*range.start), cloneExpr(*range.end), range.inclusive, expr.range);
        }
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            std::vector<std::unique_ptr<Expr>> compileArgs;
            std::vector<std::unique_ptr<Expr>> runtimeArgs;
            for (const auto& arg : call.compileArguments) {
                compileArgs.push_back(cloneExpr(*arg));
            }
            for (const auto& arg : call.runtimeArguments) {
                runtimeArgs.push_back(cloneExpr(*arg));
            }
            return std::make_unique<CallExpr>(cloneExpr(*call.callee), std::move(compileArgs), std::move(runtimeArgs), call.nullSafe, expr.range);
        }
        case ExprKind::Member: {
            const auto& member = static_cast<const MemberExpr&>(expr);
            return std::make_unique<MemberExpr>(cloneExpr(*member.base), member.member, member.nullSafe, expr.range);
        }
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            std::vector<std::unique_ptr<Expr>> values;
            for (const auto& value : init.values) {
                values.push_back(cloneExpr(*value));
            }
            return std::make_unique<InitializerExpr>(init.typeName, std::move(values), init.initKind, expr.range);
        }
        case ExprKind::CompileCall: {
            const auto& call = static_cast<const CompileCallExpr&>(expr);
            std::vector<std::unique_ptr<Expr>> args;
            for (const auto& arg : call.arguments) {
                args.push_back(cloneExpr(*arg));
            }
            return std::make_unique<CompileCallExpr>(call.callee, std::move(args), expr.range);
        }
        case ExprKind::Dialect: {
            const auto& dialect = static_cast<const DialectExpr&>(expr);
            return std::make_unique<DialectExpr>(dialect.dialectName, dialect.content, expr.range);
        }
    }
    return std::make_unique<IntegerLiteralExpr>(0, expr.range);
}

}  // namespace axc::detail
