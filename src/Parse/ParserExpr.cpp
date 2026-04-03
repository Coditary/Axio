#include "axc/Parse/Parser.h"

#include <cctype>
#include <cstdlib>

#include "ParserInternal.h"
#include "axc/Support/QualifiedName.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

std::unique_ptr<Expr> Parser::parseExpression() {
    return parseAssignment();
}

std::unique_ptr<Expr> Parser::parseAssignment() {
    auto lhs = parsePipe();
    if (match(TokenKind::Equal)) {
        const SourceRange lhsRange = lhs->range;
        auto rhs = parseAssignment();
        return std::make_unique<BinaryExpr>(BinaryOp::Assign, std::move(lhs), std::move(rhs), combine(lhsRange, rhs->range));
    }
    return lhs;
}

std::unique_ptr<Expr> Parser::parsePipe() {
    auto expr = parseLogicalOr();
    while (match(TokenKind::Arrow)) {
        const SourceRange start = expr->range;
        auto rhs = parseLogicalOr();
        const SourceRange rhsRange = rhs->range;
        std::vector<std::unique_ptr<Expr>> args;
        args.push_back(std::move(expr));
        expr = std::make_unique<CallExpr>(std::move(rhs), std::vector<std::unique_ptr<Expr>> {}, std::move(args), false, combine(start, rhsRange));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();
    while (match(TokenKind::PipePipe)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseLogicalAnd();
        expr = std::make_unique<BinaryExpr>(BinaryOp::LogicalOr, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    auto expr = parseComparisonChain();
    while (match(TokenKind::AmpAmp)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseComparisonChain();
        expr = std::make_unique<BinaryExpr>(BinaryOp::LogicalAnd, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseComparisonChain() {
    auto first = parseBitwiseOr();
    std::vector<SourceRange> ranges;
    std::vector<TokenKind> ops;
    std::vector<std::unique_ptr<Expr>> exprs;
    ranges.push_back(first->range);
    exprs.push_back(std::move(first));

    while (detail::isComparisonToken(current().kind)) {
        const TokenKind op = advance().kind;
        ops.push_back(op);
        auto rhs = parseBitwiseOr();

        if (op == TokenKind::KwIn) {
            bool inclusive = false;
            if (match(TokenKind::RangeInclusive)) {
                inclusive = true;
            } else {
                expect(TokenKind::Range, "expected '..' or '..=' after 'in'");
            }
            auto endExpr = parseBitwiseOr();
            const SourceRange startRange = rhs->range;
            rhs = std::make_unique<RangeExpr>(std::move(rhs), std::move(endExpr), inclusive, combine(startRange, rhs->range));
            rhs->range = combine(startRange, static_cast<RangeExpr*>(rhs.get())->end->range);
        }

        ranges.push_back(rhs->range);
        exprs.push_back(std::move(rhs));
    }

    if (ops.empty()) {
        return std::move(exprs.front());
    }

    std::unique_ptr<Expr> result = std::make_unique<BinaryExpr>(detail::tokenToComparisonOp(ops[0]), std::move(exprs[0]), detail::cloneExpr(*exprs[1]), combine(ranges[0], ranges[1]));
    for (std::size_t i = 1; i < ops.size(); ++i) {
        auto next = std::make_unique<BinaryExpr>(detail::tokenToComparisonOp(ops[i]), detail::cloneExpr(*exprs[i]), detail::cloneExpr(*exprs[i + 1]), combine(ranges[i], ranges[i + 1]));
        const SourceRange start = result->range;
        const SourceRange end = next->range;
        result = std::make_unique<BinaryExpr>(BinaryOp::LogicalAnd, std::move(result), std::move(next), combine(start, end));
    }
    return result;
}

std::unique_ptr<Expr> Parser::parseBitwiseOr() {
    auto expr = parseBitwiseXor();
    while (match(TokenKind::Pipe)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseBitwiseXor();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitOr, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }

    while (detail::isEnumOperationIdentifier(current())) {
        const BinaryOp op = detail::enumOperationFromLexeme(advance().lexeme);
        const SourceRange lhsRange = expr->range;
        auto rhs = parseBitwiseXor();
        expr = std::make_unique<BinaryExpr>(op, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parseBitwiseXor() {
    auto expr = parseBitwiseAnd();
    while (match(TokenKind::Caret)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseBitwiseAnd();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitXor, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseBitwiseAnd() {
    auto expr = parseShift();
    while (match(TokenKind::Ampersand)) {
        const SourceRange lhsRange = expr->range;
        auto rhs = parseShift();
        expr = std::make_unique<BinaryExpr>(BinaryOp::BitAnd, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseShift() {
    auto expr = parseAdditive();
    while (match(TokenKind::ShiftLeft) || match(TokenKind::ShiftRight)) {
        const TokenKind op = previous().kind;
        const SourceRange lhsRange = expr->range;
        auto rhs = parseAdditive();
        expr = std::make_unique<BinaryExpr>(op == TokenKind::ShiftLeft ? BinaryOp::ShiftLeft : BinaryOp::ShiftRight, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseAdditive() {
    auto expr = parseMultiplicative();
    while (match(TokenKind::Plus) || match(TokenKind::Minus)) {
        const TokenKind op = previous().kind;
        const SourceRange lhsRange = expr->range;
        auto rhs = parseMultiplicative();
        expr = std::make_unique<BinaryExpr>(op == TokenKind::Plus ? BinaryOp::Add : BinaryOp::Sub, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseMultiplicative() {
    auto expr = parseUnary();
    while (match(TokenKind::Star) || match(TokenKind::Slash) || match(TokenKind::Percent)) {
        const TokenKind op = previous().kind;
        const SourceRange lhsRange = expr->range;
        auto rhs = parseUnary();
        BinaryOp binOp = BinaryOp::Mul;
        if (op == TokenKind::Slash) {
            binOp = BinaryOp::Div;
        } else if (op == TokenKind::Percent) {
            binOp = BinaryOp::Mod;
        }
        expr = std::make_unique<BinaryExpr>(binOp, std::move(expr), std::move(rhs), combine(lhsRange, rhs->range));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (match(TokenKind::Minus)) {
        const SourceRange opRange = previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::Negate, std::move(operand), combine(opRange, operand->range));
    }
    if (match(TokenKind::Bang)) {
        const SourceRange opRange = previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::LogicalNot, std::move(operand), combine(opRange, operand->range));
    }
    if (match(TokenKind::Tilde)) {
        const SourceRange opRange = previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::BitwiseNot, std::move(operand), combine(opRange, operand->range));
    }
    if (match(TokenKind::Ampersand)) {
        const SourceRange opRange = previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::AddressOf, std::move(operand), combine(opRange, operand->range));
    }
    if (match(TokenKind::Star)) {
        const SourceRange opRange = previous().range;
        auto operand = parseUnary();
        return std::make_unique<UnaryExpr>(UnaryOp::Dereference, std::move(operand), combine(opRange, operand->range));
    }
    return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto expr = parsePrimary();

    while (true) {
        if (match(TokenKind::Question)) {
            const SourceRange start = expr->range;
            expr = std::make_unique<UnaryExpr>(UnaryOp::IsNonNull, std::move(expr), combine(start, previous().range));
            continue;
        }

        if (match(TokenKind::Dot) || match(TokenKind::QuestionDot)) {
            const bool nullSafe = previous().kind == TokenKind::QuestionDot;
            const SourceRange start = expr->range;
            if (!expect(TokenKind::Identifier, "expected member name after '.'")) {
                return expr;
            }
            expr = std::make_unique<MemberExpr>(std::move(expr), previous().lexeme, nullSafe, combine(start, previous().range));
            continue;
        }

        if (check(TokenKind::LParen)) {
            const auto qualifiedName = qualifiedNameFromExpr(*expr);
            const std::string simpleName = qualifiedName.has_value() ? lastQualifiedSegment(*qualifiedName) : std::string();
            const bool looksLikeInitializer = !simpleName.empty() && std::isupper(static_cast<unsigned char>(simpleName.front()));
            if (looksLikeInitializer) {
                advance();
                auto args = parseArgumentList(TokenKind::RParen);
                expect(TokenKind::RParen, "expected ')' after initializer arguments");
                const SourceRange start = expr->range;
                expr = std::make_unique<InitializerExpr>(*qualifiedName, std::move(args), InitKind::Value, combine(start, previous().range));
                continue;
            }
        }

        if (!isCompileArgCallStart() && check(TokenKind::LBrace)) {
            const auto qualifiedName = qualifiedNameFromExpr(*expr);
            if (!qualifiedName.has_value()) {
                break;
            }
            const SourceRange start = expr->range;
            advance();
            auto args = parseArgumentList(TokenKind::RBrace);
            expect(TokenKind::RBrace, "expected '}' after initializer arguments");
            expr = std::make_unique<InitializerExpr>(*qualifiedName, std::move(args), InitKind::Value, combine(start, previous().range));
            continue;
        }

        if (isCompileArgCallStart()) {
            advance();
            auto compileArgs = parseArgumentList(TokenKind::RBrace);
            expect(TokenKind::RBrace, "expected '}' after compile-time arguments");
            if (!check(TokenKind::LParen)) {
                diagnostics_.error(current().range, "expected '(' after compile-time arguments");
                return expr;
            }
            advance();
            auto runtimeArgs = parseArgumentList(TokenKind::RParen);
            expect(TokenKind::RParen, "expected ')' after call arguments");
            const SourceRange start = expr->range;
            expr = std::make_unique<CallExpr>(std::move(expr), std::move(compileArgs), std::move(runtimeArgs), false, combine(start, previous().range));
            continue;
        }

        if (match(TokenKind::LParen)) {
            auto args = parseArgumentList(TokenKind::RParen);
            expect(TokenKind::RParen, "expected ')' after call arguments");
            const SourceRange start = expr->range;

            expr = std::make_unique<CallExpr>(std::move(expr), std::vector<std::unique_ptr<Expr>> {}, std::move(args), false, combine(start, previous().range));
            continue;
        }

        break;
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    if (match(TokenKind::IntegerLiteral)) {
        return std::make_unique<IntegerLiteralExpr>(std::strtoll(previous().lexeme.c_str(), nullptr, 10), previous().range);
    }
    if (match(TokenKind::StringLiteral)) {
        return std::make_unique<StringLiteralExpr>(previous().lexeme, previous().range);
    }
    if (match(TokenKind::KwNull)) {
        return std::make_unique<NullLiteralExpr>(previous().range);
    }
    if (match(TokenKind::FloatLiteral)) {
        return std::make_unique<FloatLiteralExpr>(std::strtod(previous().lexeme.c_str(), nullptr), previous().range);
    }
    if (match(TokenKind::CharLiteral)) {
        return std::make_unique<CharLiteralExpr>(previous().lexeme.empty() ? 0 : previous().lexeme.front(), previous().range);
    }
    if (match(TokenKind::KwTrue)) {
        return std::make_unique<BoolLiteralExpr>(true, previous().range);
    }
    if (match(TokenKind::KwFalse)) {
        return std::make_unique<BoolLiteralExpr>(false, previous().range);
    }
    if (match(TokenKind::Identifier) || match(TokenKind::KwInt) || match(TokenKind::KwStr) || match(TokenKind::KwBool) ||
        match(TokenKind::KwError) || match(TokenKind::KwI2) || match(TokenKind::KwI8) || match(TokenKind::KwI16) ||
        match(TokenKind::KwI32) || match(TokenKind::KwI64) || match(TokenKind::KwU8) || match(TokenKind::KwU16) ||
        match(TokenKind::KwU32) || match(TokenKind::KwU64) || match(TokenKind::KwShort) || match(TokenKind::KwLong) ||
        match(TokenKind::KwDouble) || match(TokenKind::KwFloat) || match(TokenKind::KwF8) || match(TokenKind::KwF16) ||
        match(TokenKind::KwF32) || match(TokenKind::KwF64) || match(TokenKind::KwChar)) {
        return std::make_unique<DeclRefExpr>(previous().lexeme, previous().range);
    }
    if (match(TokenKind::Dollar)) {
        const SourceRange start = previous().range;
        if (!expect(TokenKind::Identifier, "expected compile function name after '$'")) {
            return std::make_unique<DeclRefExpr>("<error>", start);
        }
        const std::string callee = previous().lexeme;
        expect(TokenKind::LParen, "expected '(' after compile function name");
        auto args = parseArgumentList(TokenKind::RParen);
        expect(TokenKind::RParen, "expected ')' after compile function arguments");
        return std::make_unique<CompileCallExpr>(callee, std::move(args), combine(start, previous().range));
    }
    if (match(TokenKind::DialectBlock)) {
        const std::string lexeme = previous().lexeme;
        const auto split = lexeme.find('\n');
        std::string dialect = split == std::string::npos ? lexeme : lexeme.substr(0, split);
        std::string content = split == std::string::npos ? std::string() : lexeme.substr(split + 1);
        return std::make_unique<DialectExpr>(std::move(dialect), std::move(content), previous().range);
    }
    if (match(TokenKind::KwNew)) {
        const SourceRange start = previous().range;
        bool weak = match(TokenKind::KwWeak);
        if (!expect(TokenKind::Identifier, "expected type name after 'new'")) {
            return std::make_unique<DeclRefExpr>("<error>", start);
        }
        const std::string typeName = previous().lexeme;
        expect(TokenKind::LParen, "expected '(' after type name");
        auto args = parseArgumentList(TokenKind::RParen);
        expect(TokenKind::RParen, "expected ')' after initializer arguments");
        return std::make_unique<InitializerExpr>(typeName, std::move(args), weak ? InitKind::Weak : InitKind::Arc, combine(start, previous().range));
    }
    if (match(TokenKind::Star)) {
        const SourceRange start = previous().range;
        if (!expect(TokenKind::Identifier, "expected type name after '*'")) {
            return std::make_unique<DeclRefExpr>("<error>", start);
        }
        const std::string typeName = previous().lexeme;
        expect(TokenKind::LParen, "expected '(' after type name");
        auto args = parseArgumentList(TokenKind::RParen);
        expect(TokenKind::RParen, "expected ')' after initializer arguments");
        return std::make_unique<InitializerExpr>(typeName, std::move(args), InitKind::Unique, combine(start, previous().range));
    }
    if (check(TokenKind::LBracket)) {
        return parseArrayLiteral();
    }
    if (match(TokenKind::LParen)) {
        auto expr = parseExpression();
        expect(TokenKind::RParen, "expected ')' after expression");
        return expr;
    }

    diagnostics_.error(current().range, "expected expression");
    auto fallback = std::make_unique<IntegerLiteralExpr>(0, current().range);
    advance();
    return fallback;
}

std::unique_ptr<Expr> Parser::parseArrayLiteral() {
    const SourceRange start = current().range;
    expect(TokenKind::LBracket, "expected '[' to start array literal");
    std::vector<std::unique_ptr<Expr>> values;
    if (!check(TokenKind::RBracket)) {
        do {
            values.push_back(parseExpression());
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RBracket, "expected ']' after array literal");
    return std::make_unique<InitializerExpr>("[]", std::move(values), InitKind::ArrayLiteral, combine(start, previous().range));
}

std::vector<std::unique_ptr<Expr>> Parser::parseArgumentList(TokenKind closing) {
    std::vector<std::unique_ptr<Expr>> args;
    skipSeparators();
    if (check(closing)) {
        return args;
    }
    do {
        args.push_back(parseExpression());
        skipSeparators();
    } while (match(TokenKind::Comma));
    return args;
}

}  // namespace axc
