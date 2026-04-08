#include "axc/Parse/Parser.h"

#include <cctype>

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseUnary() {
    return detail::ExpressionParser(*this).parseUnary();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parseUnary() {
    if (parser_.match(TokenKind::PlusPlus)) {
        const SourceRange opRange = parser_.previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::PreIncrement, std::move(operand), parser_.combine(opRange, operand->range));
    }
    if (parser_.match(TokenKind::MinusMinus)) {
        const SourceRange opRange = parser_.previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::PreDecrement, std::move(operand), parser_.combine(opRange, operand->range));
    }
    if (parser_.match(TokenKind::Minus)) {
        const SourceRange opRange = parser_.previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::Negate, std::move(operand), parser_.combine(opRange, operand->range));
    }
    if (parser_.match(TokenKind::Bang)) {
        const SourceRange opRange = parser_.previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::LogicalNot, std::move(operand), parser_.combine(opRange, operand->range));
    }
    if (parser_.match(TokenKind::Tilde)) {
        const SourceRange opRange = parser_.previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::BitwiseNot, std::move(operand), parser_.combine(opRange, operand->range));
    }
    if (parser_.match(TokenKind::Ampersand)) {
        const SourceRange opRange = parser_.previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::AddressOf, std::move(operand), parser_.combine(opRange, operand->range));
    }
    if (parser_.match(TokenKind::Star)) {
        const SourceRange opRange = parser_.previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::Dereference, std::move(operand), parser_.combine(opRange, operand->range));
    }
    return parsePostfix();
}

}  // namespace detail

}  // namespace axc
