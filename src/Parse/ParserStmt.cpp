#include "axc/Parse/Parser.h"

#include "axc/Support/Diagnostic.h"

namespace axc {

std::unique_ptr<CompoundStmt> Parser::parseCompoundStmt() {
    if (!expect(TokenKind::LBrace, "expected '{' to start block")) {
        return nullptr;
    }

    const SourceRange start = previous().range;
    auto block = std::make_unique<CompoundStmt>(start);
    skipSeparators();
    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
        if (auto statement = parseStatement()) {
            block->statements.push_back(std::move(statement));
        } else {
            synchronizeStatement();
        }
        skipSeparators();
    }

    expect(TokenKind::RBrace, "expected '}' to close block");
    block->range = combine(start, previous().range);
    return block;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    skipSeparators();
    if (check(TokenKind::LBrace)) {
        return parseCompoundStmt();
    }
    if (check(TokenKind::KwReturn)) {
        return parseReturnStmt();
    }
    if (check(TokenKind::KwLet)) {
        return parseLetStmt();
    }
    if (check(TokenKind::KwIf)) {
        return parseIfStmt();
    }
    return parseExprStmt();
}

std::unique_ptr<Stmt> Parser::parseReturnStmt() {
    const SourceRange start = advance().range;
    std::vector<std::unique_ptr<Expr>> values;

    if (!isSeparator(current()) && !check(TokenKind::RBrace)) {
        do {
            values.push_back(parseExpression());
        } while (match(TokenKind::Comma));
    }

    SourceRange end = start;
    if (!values.empty()) {
        end = values.back()->range;
    }
    consumeOptionalStatementTerminator();
    return std::make_unique<ReturnStmt>(std::move(values), combine(start, end));
}

std::unique_ptr<Stmt> Parser::parseLetStmt() {
    const SourceRange start = advance().range;
    std::vector<LetBinding> bindings;
    bindings.push_back(parseLetBinding());
    if (bindings.back().name.empty()) {
        return nullptr;
    }

    SourceRange range = combine(start, bindings.back().range);
    while (match(TokenKind::Comma)) {
        bindings.push_back(parseLetBinding());
        if (bindings.back().name.empty()) {
            return nullptr;
        }
        range = combine(range, bindings.back().range);
    }

    std::unique_ptr<Expr> initializer;
    if (match(TokenKind::Equal)) {
        initializer = parseExpression();
        range = combine(range, initializer->range);
    }

    consumeOptionalStatementTerminator();
    return std::make_unique<LetStmt>(std::move(bindings), std::move(initializer), true, range);
}

LetBinding Parser::parseLetBinding() {
    LetBinding binding;
    if (!(check(TokenKind::Identifier) || check(TokenKind::KwWeak))) {
        diagnostics_.error(current().range, "expected variable name after 'let'");
        return binding;
    }
    advance();

    binding.name = previous().lexeme;
    binding.range = previous().range;

    if (!check(TokenKind::Equal) && !check(TokenKind::Comma) && !isSeparator(current()) && !check(TokenKind::RBrace)) {
        binding.explicitType = parseType();
        binding.range = combine(binding.range, binding.explicitType.range);
    }

    return binding;
}

std::unique_ptr<Stmt> Parser::parseIfStmt() {
    const SourceRange start = advance().range;
    auto condition = parseExpression();
    auto thenBlock = parseCompoundStmt();
    std::unique_ptr<Stmt> elseBranch;
    SourceRange end = thenBlock ? thenBlock->range : condition->range;

    skipSeparators();
    if (match(TokenKind::KwElse)) {
        if (check(TokenKind::KwIf)) {
            elseBranch = parseIfStmt();
        } else {
            elseBranch = parseCompoundStmt();
        }
        if (elseBranch) {
            end = elseBranch->range;
        }
    }

    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBlock), std::move(elseBranch), combine(start, end));
}

std::unique_ptr<Stmt> Parser::parseExprStmt() {
    auto expression = parseExpression();
    const SourceRange range = expression->range;
    consumeOptionalStatementTerminator();
    return std::make_unique<ExprStmt>(std::move(expression), range);
}

}  // namespace axc
