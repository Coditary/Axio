#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseAdditive() {
    return detail::ExpressionParser(*this).parseAdditive();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parseAdditive() {
    auto expr = parseMultiplicative();
    while (parser_.match(TokenKind::Plus) || parser_.match(TokenKind::Minus)) {
        const TokenKind op = parser_.previous().kind;
        const SourceRange lhsRange = expr->range;
        auto rhs = parseMultiplicative();
        expr = std::make_unique<BinaryExpr>(op == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Sub, std::move(expr), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }
    return expr;
}

}  // namespace detail

}  // namespace axc
