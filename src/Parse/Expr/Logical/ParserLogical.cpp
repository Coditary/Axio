#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    return detail::ExpressionParser(*this).parseLogicalOr();
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    return detail::ExpressionParser(*this).parseLogicalAnd();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parseLogicalOr() {
    auto expr = parseLogicalAnd();
    while (parser_.match(TokenKind::PipePipe)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseLogicalAnd();
        expr = std::make_unique<BinaryExpr>(BinaryOp::LogicalOr, std::move(expr), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> ExpressionParser::parseLogicalAnd() {
    auto expr = parseComparisonChain();
    while (parser_.match(TokenKind::AmpAmp)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseComparisonChain();
        expr = std::make_unique<BinaryExpr>(BinaryOp::LogicalAnd, std::move(expr), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }
    return expr;
}

}  // namespace detail

}  // namespace axc
