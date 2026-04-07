#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseComparisonChain() {
    return detail::ExpressionParser(*this).parseComparisonChain();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parseComparisonChain() {
    auto first = parseBitwiseOr();
    std::vector<SourceRange> ranges;
    std::vector<TokenKind> ops;
    std::vector<std::unique_ptr<Expr>> exprs;
    ranges.push_back(first->range);
    exprs.push_back(std::move(first));

    while (detail::isComparisonToken(parser_.current().kind)) {
        const TokenKind op = parser_.advance().kind;
        ops.push_back(op);
        auto rhs = parseBitwiseOr();

        if (op == TokenKind::KwIn) {
            bool inclusive = false;
            if (parser_.match(TokenKind::RangeInclusive)) {
                inclusive = true;
            } else {
                parser_.expect(TokenKind::Range, "expected '..' or '..=' after 'in'");
            }
            auto endExpr = parseBitwiseOr();
            const SourceRange startRange = rhs->range;
            const SourceRange endRange = endExpr->range;
            rhs = std::make_unique<RangeExpr>(std::move(rhs), std::move(endExpr), inclusive, parser_.combine(startRange, endRange));
        }

        ranges.push_back(rhs->range);
        exprs.push_back(std::move(rhs));
    }

    if (ops.empty()) {
        return std::move(exprs.front());
    }

    std::unique_ptr<Expr> result = std::make_unique<BinaryExpr>(detail::tokenToComparisonOp(ops[0]), std::move(exprs[0]), detail::cloneExpr(*exprs[1]), parser_.combine(ranges[0], ranges[1]));
    for (std::size_t i = 1; i < ops.size(); ++i) {
        auto next = std::make_unique<BinaryExpr>(detail::tokenToComparisonOp(ops[i]), detail::cloneExpr(*exprs[i]), detail::cloneExpr(*exprs[i + 1]), parser_.combine(ranges[i], ranges[i + 1]));
        const SourceRange start = result->range;
        const SourceRange end = next->range;
        result = std::make_unique<BinaryExpr>(BinaryOp::LogicalAnd, std::move(result), std::move(next), parser_.combine(start, end));
    }
    return result;
}

}  // namespace detail

}  // namespace axc
