/// @file
/// @brief Statement parsing entry points and statement-specific recursive-descent routines.

#include "axc/Parse/Parser.h"

#include "../Internal/ParserInternal.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

std::unique_ptr<CompoundStmt> Parser::parseCompoundStmt() {
    return detail::StatementParser(*this).parseCompoundStmt();
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    return detail::StatementParser(*this).parseStatement();
}

std::unique_ptr<Stmt> Parser::parseReturnStmt() {
    return detail::StatementParser(*this).parseReturnStmt();
}

std::unique_ptr<Stmt> Parser::parseDeferStmt() {
    return detail::StatementParser(*this).parseDeferStmt();
}

std::unique_ptr<Stmt> Parser::parseLetStmt() {
    return detail::StatementParser(*this).parseLetStmt();
}

LetBinding Parser::parseLetBinding() {
    return detail::StatementParser(*this).parseLetBinding();
}

std::unique_ptr<Stmt> Parser::parseIfStmt() {
    return detail::StatementParser(*this).parseIfStmt();
}

std::unique_ptr<Stmt> Parser::parseWhileStmt() {
    return detail::StatementParser(*this).parseWhileStmt();
}

std::unique_ptr<Stmt> Parser::parseForStmt() {
    return detail::StatementParser(*this).parseForStmt();
}

std::unique_ptr<Stmt> Parser::parseDoWhileStmt() {
    return detail::StatementParser(*this).parseDoWhileStmt();
}

std::unique_ptr<Stmt> Parser::parseSwitchStmt() {
    return detail::StatementParser(*this).parseSwitchStmt();
}

std::unique_ptr<Stmt> Parser::parseBreakStmt() {
    return detail::StatementParser(*this).parseBreakStmt();
}

std::unique_ptr<Stmt> Parser::parseContinueStmt() {
    return detail::StatementParser(*this).parseContinueStmt();
}

std::unique_ptr<Stmt> Parser::parseExprStmt() {
    return detail::StatementParser(*this).parseExprStmt();
}

namespace detail {

StatementParser::StatementParser(Parser& parser) : parser_(parser) {}

std::unique_ptr<CompoundStmt> StatementParser::parseCompoundStmt() {
    if (!parser_.expect(TokenKind::LBrace, "expected '{' to start block")) {
        return nullptr;
    }

    const SourceRange start = parser_.previous().range;
    auto block = std::make_unique<CompoundStmt>(start);
    parser_.skipSeparators();
    while (!parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
        if (auto statement = parseStatement()) {
            block->statements.push_back(std::move(statement));
        } else {
            parser_.synchronizeStatement();
        }
        parser_.skipSeparators();
    }

    parser_.expect(TokenKind::RBrace, "expected '}' to close block");
    block->range = parser_.combine(start, parser_.previous().range);
    return block;
}

std::unique_ptr<Stmt> StatementParser::parseStatement() {
    parser_.skipSeparators();
    if (parser_.check(TokenKind::LBrace)) {
        return parseCompoundStmt();
    }
    if (parser_.check(TokenKind::KwReturn)) {
        return parseReturnStmt();
    }
    if (parser_.check(TokenKind::KwDefer)) {
        return parseDeferStmt();
    }
    if (parser_.check(TokenKind::KwLet)) {
        return parseLetStmt();
    }
    if (parser_.check(TokenKind::KwConst)) {
        return parseLetStmt();
    }
    if (parser_.check(TokenKind::KwIf)) {
        return parseIfStmt();
    }
    if (parser_.check(TokenKind::KwWhile)) {
        return parseWhileStmt();
    }
    if (parser_.check(TokenKind::KwFor)) {
        return parseForStmt();
    }
    if (parser_.checkIdentifier("foreach")) {
        parser_.diagnostics_.error(parser_.current().range, "foreach is no longer supported");
        parser_.advance();
        while (!parser_.check(TokenKind::EndOfFile) && !parser_.isSeparator(parser_.current()) && !parser_.check(TokenKind::LBrace)) {
            parser_.advance();
        }
        if (parser_.check(TokenKind::LBrace)) {
            parseCompoundStmt();
        }
        parser_.consumeOptionalStatementTerminator();
        return nullptr;
    }
    if (parser_.check(TokenKind::KwDo)) {
        return parseDoWhileStmt();
    }
    if (parser_.check(TokenKind::KwSwitch)) {
        return parseSwitchStmt();
    }
    if (parser_.check(TokenKind::KwBreak)) {
        return parseBreakStmt();
    }
    if (parser_.check(TokenKind::KwContinue)) {
        return parseContinueStmt();
    }
    return parseExprStmt();
}

std::unique_ptr<Stmt> StatementParser::parseReturnStmt() {
    const SourceRange start = parser_.advance().range;
    std::vector<std::unique_ptr<Expr>> values;

    if (!parser_.isSeparator(parser_.current()) && !parser_.check(TokenKind::RBrace)) {
        do {
            values.push_back(ExpressionParser(parser_).parseExpression());
        } while (parser_.match(TokenKind::Comma));
    }

    SourceRange end = start;
    if (!values.empty()) {
        end = values.back()->range;
    }
    parser_.consumeOptionalStatementTerminator();
    return std::make_unique<ReturnStmt>(std::move(values), parser_.combine(start, end));
}

std::unique_ptr<Stmt> StatementParser::parseDeferStmt() {
    const SourceRange start = parser_.advance().range;
    auto expression = ExpressionParser(parser_).parseExpression();
    SourceRange end = expression ? expression->range : start;
    if (expression && expression->kind != ExprKind::Call) {
        parser_.diagnostics_.error(expression->range, "defer expects a function or method call");
    }
    parser_.consumeOptionalStatementTerminator();
    return std::make_unique<DeferStmt>(std::move(expression), parser_.combine(start, end));
}

std::unique_ptr<Stmt> StatementParser::parseLetStmt() {
    const SourceRange start = parser_.advance().range;
    const bool mutableStorage = parser_.previous().kind == TokenKind::KwLet;
    std::vector<LetBinding> bindings;
    bindings.push_back(parseLetBinding());
    if (bindings.back().name.empty()) {
        return nullptr;
    }

    SourceRange range = parser_.combine(start, bindings.back().range);
    while (parser_.match(TokenKind::Comma)) {
        bindings.push_back(parseLetBinding());
        if (bindings.back().name.empty()) {
            return nullptr;
        }
        range = parser_.combine(range, bindings.back().range);
    }

    std::unique_ptr<Expr> initializer;
    if (parser_.match(TokenKind::Equal)) {
        initializer = ExpressionParser(parser_).parseExpression();
        range = parser_.combine(range, initializer->range);
    }

    parser_.consumeOptionalStatementTerminator();
    return std::make_unique<LetStmt>(std::move(bindings), std::move(initializer), mutableStorage, range);
}

LetBinding StatementParser::parseLetBinding() {
    LetBinding binding;
    if (!(parser_.check(TokenKind::Identifier) || parser_.check(TokenKind::KwWeak))) {
        parser_.diagnostics_.error(parser_.current().range, "expected variable name after 'let'");
        return binding;
    }
    parser_.advance();

    binding.name = parser_.previous().lexeme;
    binding.range = parser_.previous().range;

    if (!parser_.check(TokenKind::Equal) && !parser_.check(TokenKind::Comma) && !parser_.isSeparator(parser_.current()) &&
        !parser_.check(TokenKind::RBrace)) {
        binding.explicitType = DeclarationParser(parser_).parseType();
        binding.range = parser_.combine(binding.range, binding.explicitType.range);
    }

    return binding;
}

std::unique_ptr<Stmt> StatementParser::parseIfStmt() {
    const SourceRange start = parser_.advance().range;
    auto condition = ExpressionParser(parser_).parseExpression();
    auto thenBlock = parseCompoundStmt();
    std::unique_ptr<Stmt> elseBranch;
    SourceRange end = thenBlock ? thenBlock->range : condition->range;

    parser_.skipSeparators();
    if (parser_.match(TokenKind::KwElse)) {
        if (parser_.check(TokenKind::KwIf)) {
            elseBranch = parseIfStmt();
        } else {
            elseBranch = parseCompoundStmt();
        }
        if (elseBranch) {
            end = elseBranch->range;
        }
    }

    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBlock), std::move(elseBranch), parser_.combine(start, end));
}

std::unique_ptr<Stmt> StatementParser::parseWhileStmt() {
    const SourceRange start = parser_.advance().range;
    auto condition = ExpressionParser(parser_).parseExpression();
    auto body = parseCompoundStmt();
    const SourceRange end = body ? body->range : (condition ? condition->range : start);
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body), parser_.combine(start, end));
}

std::unique_ptr<Stmt> StatementParser::parseForStmt() {
    const SourceRange start = parser_.advance().range;

    std::unique_ptr<Stmt> initializer;
    if (!parser_.check(TokenKind::Semicolon)) {
        if (parser_.check(TokenKind::KwLet) || parser_.check(TokenKind::KwConst)) {
            const SourceRange letStart = parser_.advance().range;
            const bool mutableStorage = parser_.previous().kind == TokenKind::KwLet;
            std::vector<LetBinding> bindings;
            bindings.push_back(parseLetBinding());
            if (bindings.back().name.empty()) {
                return nullptr;
            }
            SourceRange range = parser_.combine(letStart, bindings.back().range);
            while (parser_.match(TokenKind::Comma)) {
                bindings.push_back(parseLetBinding());
                if (bindings.back().name.empty()) {
                    return nullptr;
                }
                range = parser_.combine(range, bindings.back().range);
            }
            std::unique_ptr<Expr> initExpr;
            if (parser_.match(TokenKind::Equal)) {
                initExpr = ExpressionParser(parser_).parseExpression();
                range = parser_.combine(range, initExpr->range);
            }
            initializer = std::make_unique<LetStmt>(std::move(bindings), std::move(initExpr), mutableStorage, range);
        } else {
            auto initExpr = ExpressionParser(parser_).parseExpression();
            SourceRange initRange = initExpr ? initExpr->range : start;
            initializer = std::make_unique<ExprStmt>(std::move(initExpr), initRange);
        }
    }
    parser_.expect(TokenKind::Semicolon, "expected ';' after for initializer");

    std::unique_ptr<Expr> condition;
    if (!parser_.check(TokenKind::Semicolon)) {
        condition = ExpressionParser(parser_).parseExpression();
    }
    parser_.expect(TokenKind::Semicolon, "expected ';' after for condition");

    std::unique_ptr<Expr> step;
    if (!parser_.check(TokenKind::LBrace)) {
        step = ExpressionParser(parser_).parseExpression();
    }

    auto body = parseCompoundStmt();
    SourceRange end = body ? body->range : start;
    if (step) {
        end = parser_.combine(start, step->range);
        if (body) {
            end = body->range;
        }
    } else if (condition) {
        end = body ? body->range : condition->range;
    } else if (initializer) {
        end = body ? body->range : initializer->range;
    }
    return std::make_unique<ForStmt>(std::move(initializer), std::move(condition), std::move(step), std::move(body), parser_.combine(start, end));
}

std::unique_ptr<Stmt> StatementParser::parseDoWhileStmt() {
    const SourceRange start = parser_.advance().range;
    auto body = parseCompoundStmt();
    parser_.skipSeparators();
    parser_.expect(TokenKind::KwWhile, "expected 'while' after do block");
    auto condition = ExpressionParser(parser_).parseExpression();
    SourceRange end = condition ? condition->range : (body ? body->range : start);
    parser_.consumeOptionalStatementTerminator();
    return std::make_unique<DoWhileStmt>(std::move(body), std::move(condition), parser_.combine(start, end));
}

std::unique_ptr<Stmt> StatementParser::parseSwitchStmt() {
    const SourceRange start = parser_.advance().range;
    auto condition = ExpressionParser(parser_).parseExpression();
    if (!parser_.expect(TokenKind::LBrace, "expected '{' to start switch body")) {
        return nullptr;
    }

    std::vector<SwitchCase> cases;
    parser_.skipSeparators();
    while (!parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
        SwitchCase entry;
        SourceRange caseKeywordRange = parser_.current().range;
        if (parser_.match(TokenKind::KwCase)) {
            do {
                auto first = ExpressionParser(parser_).parseBitwiseOr();
                if (!first) {
                    break;
                }
                SourceRange rangeOperator = first->range;
                if (parser_.matchRangeOperator(&rangeOperator)) {
                    const SourceRange rangeStart = first->range;
                    auto endExpr = ExpressionParser(parser_).parseBitwiseOr();
                    const SourceRange rangeEnd = endExpr ? endExpr->range : rangeOperator;
                    parser_.diagnostics_.error(parser_.combine(rangeStart, rangeEnd), "switch range cases are no longer supported");
                }
                SwitchCasePattern pattern;
                pattern.range = first->range;
                pattern.value = std::move(first);
                entry.patterns.push_back(std::move(pattern));
            } while (parser_.match(TokenKind::Comma));
        } else if (parser_.match(TokenKind::KwDefault)) {
            entry.isDefault = true;
            caseKeywordRange = parser_.previous().range;
        } else {
            parser_.diagnostics_.error(parser_.current().range, "expected 'case' or 'default' in switch");
            parser_.synchronizeStatement();
            parser_.skipSeparators();
            continue;
        }

        entry.body = parseCompoundStmt();
        SourceRange caseStart = entry.isDefault ? caseKeywordRange : (entry.patterns.empty() ? start : entry.patterns.front().range);
        entry.range = entry.body ? parser_.combine(caseStart, entry.body->range) : caseStart;
        cases.push_back(std::move(entry));
        parser_.skipSeparators();
    }

    parser_.expect(TokenKind::RBrace, "expected '}' to close switch body");
    return std::make_unique<SwitchStmt>(std::move(condition), std::move(cases), parser_.combine(start, parser_.previous().range));
}

std::unique_ptr<Stmt> StatementParser::parseBreakStmt() {
    const SourceRange start = parser_.advance().range;
    SourceRange end = start;
    if (!parser_.isSeparator(parser_.current()) && !parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
        parser_.diagnostics_.error(parser_.current().range, "break does not take a value");
        while (!parser_.isSeparator(parser_.current()) && !parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
            end = parser_.advance().range;
        }
    }
    parser_.consumeOptionalStatementTerminator();
    return std::make_unique<BreakStmt>(parser_.combine(start, end));
}

std::unique_ptr<Stmt> StatementParser::parseContinueStmt() {
    const SourceRange start = parser_.advance().range;
    SourceRange end = start;
    if (!parser_.isSeparator(parser_.current()) && !parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
        parser_.diagnostics_.error(parser_.current().range, "continue does not take a value");
        while (!parser_.isSeparator(parser_.current()) && !parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
            end = parser_.advance().range;
        }
    }
    parser_.consumeOptionalStatementTerminator();
    return std::make_unique<ContinueStmt>(parser_.combine(start, end));
}

std::unique_ptr<Stmt> StatementParser::parseExprStmt() {
    auto expression = ExpressionParser(parser_).parseExpression();
    const SourceRange range = expression->range;
    parser_.consumeOptionalStatementTerminator();
    return std::make_unique<ExprStmt>(std::move(expression), range);
}

}  // namespace detail

}  // namespace axc
