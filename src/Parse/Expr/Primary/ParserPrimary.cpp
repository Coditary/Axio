#include "axc/Parse/Parser.h"

#include <cstdlib>

#include "../../Internal/ParserInternal.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

std::unique_ptr<Expr> Parser::parsePrimary() {
    return detail::ExpressionParser(*this).parsePrimary();
}

std::unique_ptr<Expr> Parser::parseArrayLiteral() {
    return detail::ExpressionParser(*this).parseArrayLiteral();
}

std::unique_ptr<Expr> Parser::parseBraceArrayLiteral() {
    return detail::ExpressionParser(*this).parseBraceArrayLiteral();
}

std::vector<std::unique_ptr<Expr>> Parser::parseArgumentList(TokenKind closing) {
    return detail::ExpressionParser(*this).parseArgumentList(closing);
}

namespace detail {

std::unique_ptr<Expr> ExpressionParser::parsePrimary() {
    if (parser_.match(TokenKind::IntegerLiteral)) {
        return std::make_unique<IntegerLiteralExpr>(std::strtoll(parser_.previous().lexeme.c_str(), nullptr, 10), parser_.previous().range);
    }
    if (parser_.match(TokenKind::StringLiteral)) {
        return std::make_unique<StringLiteralExpr>(parser_.previous().lexeme, parser_.previous().range);
    }
    if (parser_.match(TokenKind::KwNull)) {
        return std::make_unique<NullLiteralExpr>(parser_.previous().range);
    }
    if (parser_.match(TokenKind::FloatLiteral)) {
        return std::make_unique<FloatLiteralExpr>(std::strtod(parser_.previous().lexeme.c_str(), nullptr), parser_.previous().range);
    }
    if (parser_.match(TokenKind::CharLiteral)) {
        return std::make_unique<CharLiteralExpr>(parser_.previous().lexeme.empty() ? 0 : parser_.previous().lexeme.front(), parser_.previous().range);
    }
    if (parser_.match(TokenKind::KwTrue)) {
        return std::make_unique<BoolLiteralExpr>(true, parser_.previous().range);
    }
    if (parser_.match(TokenKind::KwFalse)) {
        return std::make_unique<BoolLiteralExpr>(false, parser_.previous().range);
    }
    if (parser_.match(TokenKind::Identifier) || parser_.match(TokenKind::KwInt) || parser_.match(TokenKind::KwStr) || parser_.match(TokenKind::KwBool) ||
        parser_.match(TokenKind::KwError) || parser_.match(TokenKind::KwI2) || parser_.match(TokenKind::KwI8) || parser_.match(TokenKind::KwI16) ||
        parser_.match(TokenKind::KwI32) || parser_.match(TokenKind::KwI64) || parser_.match(TokenKind::KwU8) || parser_.match(TokenKind::KwU16) ||
        parser_.match(TokenKind::KwU32) || parser_.match(TokenKind::KwU64) || parser_.match(TokenKind::KwShort) || parser_.match(TokenKind::KwLong) ||
        parser_.match(TokenKind::KwDouble) || parser_.match(TokenKind::KwFloat) || parser_.match(TokenKind::KwF8) || parser_.match(TokenKind::KwF16) ||
        parser_.match(TokenKind::KwF32) || parser_.match(TokenKind::KwF64) || parser_.match(TokenKind::KwChar)) {
        return std::make_unique<DeclRefExpr>(parser_.previous().lexeme, parser_.previous().range);
    }
    if (parser_.match(TokenKind::Dollar)) {
        const SourceRange start = parser_.previous().range;
        if (!parser_.expect(TokenKind::Identifier, "expected compile function name after '$'")) {
            return std::make_unique<DeclRefExpr>("<error>", start);
        }
        const std::string callee = parser_.previous().lexeme;
        parser_.expect(TokenKind::LParen, "expected '(' after compile function name");
        auto args = parseArgumentList(TokenKind::RParen);
        parser_.expect(TokenKind::RParen, "expected ')' after compile function arguments");
        return std::make_unique<CompileCallExpr>(callee, std::move(args), parser_.combine(start, parser_.previous().range));
    }
    if (parser_.match(TokenKind::DialectBlock)) {
        const std::string lexeme = parser_.previous().lexeme;
        const auto split = lexeme.find('\n');
        std::string dialect = split == std::string::npos ? lexeme : lexeme.substr(0, split);
        std::string content = split == std::string::npos ? std::string() : lexeme.substr(split + 1);
        return std::make_unique<DialectExpr>(std::move(dialect), std::move(content), parser_.previous().range);
    }
    if (parser_.match(TokenKind::KwNew)) {
        const SourceRange start = parser_.previous().range;
        bool weak = parser_.match(TokenKind::KwWeak);
        if (!parser_.expect(TokenKind::Identifier, "expected type name after 'new'")) {
            return std::make_unique<DeclRefExpr>("<error>", start);
        }
        const std::string typeName = parser_.previous().lexeme;
        parser_.expect(TokenKind::LParen, "expected '(' after type name");
        auto args = parseArgumentList(TokenKind::RParen);
        parser_.expect(TokenKind::RParen, "expected ')' after initializer arguments");
        return std::make_unique<InitializerExpr>(typeName, std::move(args), weak ? InitKind::Weak : InitKind::Arc, parser_.combine(start, parser_.previous().range));
    }
    if (parser_.match(TokenKind::Star)) {
        const SourceRange start = parser_.previous().range;
        if (!parser_.expect(TokenKind::Identifier, "expected type name after '*'")) {
            return std::make_unique<DeclRefExpr>("<error>", start);
        }
        const std::string typeName = parser_.previous().lexeme;
        parser_.expect(TokenKind::LParen, "expected '(' after type name");
        auto args = parseArgumentList(TokenKind::RParen);
        parser_.expect(TokenKind::RParen, "expected ')' after initializer arguments");
        return std::make_unique<InitializerExpr>(typeName, std::move(args), InitKind::Unique, parser_.combine(start, parser_.previous().range));
    }
    if (parser_.check(TokenKind::LBracket)) {
        return parseArrayLiteral();
    }
    if (parser_.check(TokenKind::LBrace) && !parser_.isCompileArgCallStart()) {
        return parseBraceArrayLiteral();
    }
    if (parser_.match(TokenKind::LParen)) {
        auto expr = parseExpression();
        parser_.expect(TokenKind::RParen, "expected ')' after expression");
        return expr;
    }

    parser_.diagnostics_.error(parser_.current().range, "expected expression");
    auto fallback = std::make_unique<IntegerLiteralExpr>(0, parser_.current().range);
    parser_.advance();
    return fallback;
}

std::unique_ptr<Expr> ExpressionParser::parseArrayLiteral() {
    const SourceRange start = parser_.current().range;
    parser_.expect(TokenKind::LBracket, "expected '[' to start array literal");
    std::vector<std::unique_ptr<Expr>> values;
    if (!parser_.check(TokenKind::RBracket)) {
        do {
            values.push_back(parseExpression());
        } while (parser_.match(TokenKind::Comma));
    }
    parser_.expect(TokenKind::RBracket, "expected ']' after array literal");
    return std::make_unique<InitializerExpr>("[]", std::move(values), InitKind::ArrayLiteral, parser_.combine(start, parser_.previous().range));
}

std::unique_ptr<Expr> ExpressionParser::parseBraceArrayLiteral() {
    const SourceRange start = parser_.current().range;
    parser_.expect(TokenKind::LBrace, "expected '{' to start array literal");
    std::vector<std::unique_ptr<Expr>> values;
    if (!parser_.check(TokenKind::RBrace)) {
        do {
            values.push_back(parseExpression());
        } while (parser_.match(TokenKind::Comma));
    }
    parser_.expect(TokenKind::RBrace, "expected '}' after array literal");
    return std::make_unique<InitializerExpr>("[]", std::move(values), InitKind::ArrayLiteral, parser_.combine(start, parser_.previous().range));
}

std::vector<std::unique_ptr<Expr>> ExpressionParser::parseArgumentList(TokenKind closing) {
    std::vector<std::unique_ptr<Expr>> args;
    parser_.skipSeparators();
    if (parser_.check(closing)) {
        return args;
    }
    do {
        args.push_back(parseExpression());
        parser_.skipSeparators();
    } while (parser_.match(TokenKind::Comma));
    return args;
}

}  // namespace detail

}  // namespace axc
