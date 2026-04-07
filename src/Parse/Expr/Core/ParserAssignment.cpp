#include "axc/Parse/Parser.h"

#include "../../Internal/ParserInternal.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseAssignment() {
    return detail::ExpressionParser(*this).parseAssignment();
}

namespace detail {

ExpressionParser::ExpressionParser(Parser& parser) : parser_(parser) {}

std::unique_ptr<Expr> ExpressionParser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<Expr> ExpressionParser::parseAssignment() {
    auto lhs = parsePipe();
    if (parser_.match(TokenKind::Equal)) {
        const SourceRange lhsRange = lhs->range;
        auto rhs = parseAssignment();
        return std::make_unique<BinaryExpr>(BinaryOp::Assign, std::move(lhs), std::move(rhs), parser_.combine(lhsRange, rhs->range));
    }
    if (auto compoundOp = detail::tokenToCompoundBinaryOp(parser_.current().kind); compoundOp.has_value()) {
        parser_.advance();
        const SourceRange lhsRange = lhs->range;
        auto rhs = parseAssignment();
        auto compoundValue = std::make_unique<BinaryExpr>(*compoundOp, detail::cloneExpr(*lhs), std::move(rhs), parser_.combine(lhsRange, rhs->range));
        return std::make_unique<BinaryExpr>(BinaryOp::Assign, std::move(lhs), std::move(compoundValue), parser_.combine(lhsRange, parser_.previous().range));
    }
    if (parser_.match(TokenKind::KwAs)) {
        const SourceRange lhsRange = lhs->range;
        Type targetType = DeclarationParser(parser_).parseType();
        return std::make_unique<CastExpr>(std::move(lhs), std::move(targetType), parser_.combine(lhsRange, parser_.previous().range));
    }
    return lhs;
}

}  // namespace detail

}  // namespace axc
