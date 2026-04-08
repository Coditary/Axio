#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseBitwiseOr() {
    return detail::ExpressionParser(*this).parseBitwiseOr();
}

std::unique_ptr<Expr> Parser::parseBitwiseXor() {
    return detail::ExpressionParser(*this).parseBitwiseXor();
}

std::unique_ptr<Expr> Parser::parseBitwiseAnd() {
    return detail::ExpressionParser(*this).parseBitwiseAnd();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parseBitwiseOr() {
    auto expr = parseBitwiseXor();
    while (parser_.match(TokenKind::Pipe)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseBitwiseXor();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitOr, std::move(expr), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }

    return expr;
}

std::unique_ptr<Expr> ExpressionParser::parseBitwiseXor() {
    auto expr = parseBitwiseAnd();
    while (parser_.match(TokenKind::Caret)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseBitwiseAnd();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitXor, std::move(expr), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> ExpressionParser::parseBitwiseAnd() {
    auto expr = parseShift();
    while (parser_.match(TokenKind::Ampersand)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseShift();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitAnd, std::move(expr), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }
    return expr;
}

}  // namespace detail

}  // namespace axc
