#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseShift() {
    return detail::ExpressionParser(*this).parseShift();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parseShift() {
    auto expr = parseAdditive();
    while (parser_.match(TokenKind::ShiftLeft) || parser_.match(TokenKind::ShiftRight)) {
        const TokenKind op = parser_.previous().kind;
        const SourceRange lhsRange = expr->range;
        auto rhs = parseAdditive();
        expr = std::make_unique<BinaryExpr>(op == TokenKind::ShiftLeft ? BinaryOp::ShiftLeft : BinaryOp::ShiftRight, std::move(expr), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }
    return expr;
}

}  // namespace detail

}  // namespace axc
