#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parsePipe() {
    return detail::ExpressionParser(*this).parsePipe();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parsePipe() {
    auto expr = parseLogicalOr();
    while (parser_.match(TokenKind::Arrow)) {
        const SourceRange start = expr->range;
        auto rhs = parseLogicalOr();
        const SourceRange rhsRange = rhs->range;
        std::vector<std::unique_ptr<Expr>> args;
        args.push_back(std::move(expr));
        expr = std::make_unique<CallExpr>(std::move(rhs), std::vector<std::unique_ptr<Expr>> {}, std::move(args), false, parser_.combine(start, rhsRange));
    }
    return expr;
}

}  // namespace detail

}  // namespace axc
