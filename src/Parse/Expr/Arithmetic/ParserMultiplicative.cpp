#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseMultiplicative() {
    return detail::ExpressionParser(*this).parseMultiplicative();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parseMultiplicative() {
    auto expr = parseUnary();
    while (parser_.match(TokenKind::Star) || parser_.match(TokenKind::Slash) || parser_.match(TokenKind::Percent)) {
        const TokenKind op = parser_.previous().kind;
        const SourceRange lhsRange = expr->range;
        auto rhs = parseUnary();
        BinaryOp binOp = BinaryOp::Mul;
        if (op == TokenKind::Slash) {
            binOp = BinaryOp::Div;
        } else if (op == TokenKind::Percent) {
            binOp = BinaryOp::Mod;
        }
        expr = std::make_unique<BinaryExpr>(binOp, std::move(expr), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }
    return expr;
}

}  // namespace detail

}  // namespace axc
