#include "axc/Parse/Parser.h"

#include <cctype>

#include "../../Internal/ParserInternal.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/QualifiedName.h"

namespace axc {

std::unique_ptr<Expr> Parser::parsePostfix() {
    return detail::ExpressionParser(*this).parsePostfix();
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parsePostfix() {
    auto expr = parsePrimary();

    while (true) {
        if (parser_.match(TokenKind::PlusPlus)) {
            const SourceRange start = expr->range;
            expr = std::make_unique<UnaryExpr>(UnaryOp::PostIncrement, std::move(expr), parser_.combine(start, parser_.previous().range));
            continue;
        }

        if (parser_.match(TokenKind::MinusMinus)) {
            const SourceRange start = expr->range;
            expr = std::make_unique<UnaryExpr>(UnaryOp::PostDecrement, std::move(expr), parser_.combine(start, parser_.previous().range));
            continue;
        }

        if (parser_.match(TokenKind::Dot)) {
            const SourceRange start = expr->range;
            if (!parser_.expect(TokenKind::Identifier, "expected member name after '.'")) {
                return expr;
            }
            expr = std::make_unique<MemberExpr>(std::move(expr), parser_.previous().lexeme, parser_.combine(start, parser_.previous().range));
            continue;
        }

        if (parser_.check(TokenKind::LParen)) {
            const auto qualifiedName = qualifiedNameFromExpr(*expr);
            const std::string simpleName = qualifiedName.has_value() ? lastQualifiedSegment(*qualifiedName) : std::string();
            const bool looksLikeInitializer = !simpleName.empty() && std::isupper(static_cast<unsigned char>(simpleName.front()));
            if (looksLikeInitializer) {
                parser_.advance();
                auto args = parseArgumentList(TokenKind::RParen);
                parser_.expect(TokenKind::RParen, "expected ')' after initializer arguments");
                const SourceRange start = expr->range;
                expr = std::make_unique<InitializerExpr>(*qualifiedName, std::move(args), InitKind::Value, parser_.combine(start, parser_.previous().range));
                continue;
            }
        }

        if (parser_.match(TokenKind::LParen)) {
            auto args = parseArgumentList(TokenKind::RParen);
            parser_.expect(TokenKind::RParen, "expected ')' after call arguments");
            const SourceRange start = expr->range;

            expr = std::make_unique<CallExpr>(std::move(expr), std::move(args), parser_.combine(start, parser_.previous().range));
            continue;
        }

        break;
    }

    return expr;
}

}  // namespace detail

}  // namespace axc
