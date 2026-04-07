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
        if (parser_.match(TokenKind::Question)) {
            const SourceRange start = expr->range;
            expr = std::make_unique<UnaryExpr>(UnaryOp::IsNonNull, std::move(expr), parser_.combine(start, parser_.previous().range));
            continue;
        }

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

        if (parser_.match(TokenKind::Dot) || parser_.match(TokenKind::QuestionDot)) {
            const bool nullSafe = parser_.previous().kind == TokenKind::QuestionDot;
            const SourceRange start = expr->range;
            if (!parser_.expect(TokenKind::Identifier, "expected member name after '.'")) {
                return expr;
            }
            expr = std::make_unique<MemberExpr>(std::move(expr), parser_.previous().lexeme, nullSafe, parser_.combine(start, parser_.previous().range));
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

        if (!parser_.isCompileArgCallStart() && parser_.check(TokenKind::LBrace) && expr->kind == ExprKind::DeclRef) {
            const auto qualifiedName = qualifiedNameFromExpr(*expr);
            if (!qualifiedName.has_value()) {
                break;
            }
            const std::string simpleName = lastQualifiedSegment(*qualifiedName);
            if (simpleName.empty() || !std::isupper(static_cast<unsigned char>(simpleName.front()))) {
                break;
            }
            const SourceRange start = expr->range;
            parser_.advance();
            auto args = parseArgumentList(TokenKind::RBrace);
            parser_.expect(TokenKind::RBrace, "expected '}' after initializer arguments");
            expr = std::make_unique<InitializerExpr>(*qualifiedName, std::move(args), InitKind::Value, parser_.combine(start, parser_.previous().range));
            continue;
        }

        if (parser_.isCompileArgCallStart()) {
            parser_.advance();
            auto compileArgs = parseArgumentList(TokenKind::RBrace);
            parser_.expect(TokenKind::RBrace, "expected '}' after compile-time arguments");
            if (!parser_.check(TokenKind::LParen)) {
                parser_.diagnostics_.error(parser_.current().range, "expected '(' after compile-time arguments");
                return expr;
            }
            parser_.advance();
            auto runtimeArgs = parseArgumentList(TokenKind::RParen);
            parser_.expect(TokenKind::RParen, "expected ')' after call arguments");
            const SourceRange start = expr->range;
            expr = std::make_unique<CallExpr>(std::move(expr), std::move(compileArgs), std::move(runtimeArgs), false, parser_.combine(start, parser_.previous().range));
            continue;
        }

        if (parser_.match(TokenKind::LParen)) {
            auto args = parseArgumentList(TokenKind::RParen);
            parser_.expect(TokenKind::RParen, "expected ')' after call arguments");
            const SourceRange start = expr->range;

            expr = std::make_unique<CallExpr>(std::move(expr), std::vector<std::unique_ptr<Expr>> {}, std::move(args), false, parser_.combine(start, parser_.previous().range));
            continue;
        }

        break;
    }

    return expr;
}

}  // namespace detail

}  // namespace axc
