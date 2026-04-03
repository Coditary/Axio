#include "axc/Parse/Parser.h"

#include <utility>

#include "ParserInternal.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

TranslationUnit Parser::parseTranslationUnit() {
    TranslationUnit unit;

    while (!check(TokenKind::EndOfFile)) {
        skipSeparators();
        if (check(TokenKind::EndOfFile)) {
            break;
        }

        if (parsePreprocessorDirective(unit)) {
            continue;
        }

        if (auto declaration = parseTopLevelDecl()) {
            unit.declarations.push_back(std::move(declaration));
        } else {
            synchronizeTopLevel();
        }
    }

    return unit;
}

std::vector<Annotation> Parser::parseAnnotations() {
    std::vector<Annotation> annotations;

    while (match(TokenKind::At)) {
        const SourceRange start = previous().range;
        if (!expect(TokenKind::Identifier, "expected annotation name after '@'")) {
            break;
        }

        Annotation annotation;
        annotation.name = previous().lexeme;
        annotation.range = combine(start, previous().range);

        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) {
                do {
                    if (!(check(TokenKind::Identifier) || check(TokenKind::StringLiteral) || check(TokenKind::IntegerLiteral))) {
                        diagnostics_.error(current().range, "expected annotation argument");
                        break;
                    }
                    annotation.arguments.push_back(advance().lexeme);
                    annotation.range = combine(annotation.range, previous().range);
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after annotation arguments");
            annotation.range = combine(annotation.range, previous().range);
        }

        annotations.push_back(std::move(annotation));
        skipSeparators();
    }

    return annotations;
}

bool Parser::parsePreprocessorDirective(TranslationUnit& unit) {
    if (!match(TokenKind::Hash)) {
        return false;
    }

    const SourceRange start = previous().range;
    if (!expect(TokenKind::Identifier, "expected preprocessor directive after '#'")) {
        return true;
    }

    PreprocessorDirective directive;
    directive.name = previous().lexeme;
    directive.range = combine(start, previous().range);

    while (!check(TokenKind::EndOfFile) && !isSeparator(current())) {
        directive.arguments.push_back(advance().lexeme);
        directive.range = combine(directive.range, previous().range);
    }

    unit.preprocessorDirectives.push_back(std::move(directive));
    skipSeparators();
    return true;
}

std::unique_ptr<Decl> Parser::parseTopLevelDecl() {
    skipSeparators();
    std::vector<Annotation> annotations = parseAnnotations();
    skipSeparators();
    const bool isExtern = match(TokenKind::KwExtern);
    skipSeparators();

    if (check(TokenKind::KwImport)) {
        return parseImportDecl(std::move(annotations));
    }
    if (check(TokenKind::KwFn)) {
        return parseFunctionDecl(std::move(annotations), isExtern);
    }
    if (check(TokenKind::KwStruct)) {
        return parseStructDecl(std::move(annotations));
    }
    if (check(TokenKind::KwEnum)) {
        return parseEnumDecl(std::move(annotations));
    }
    if (check(TokenKind::KwClass)) {
        return parseClassDecl(std::move(annotations));
    }

    diagnostics_.error(current().range, "expected top-level declaration");
    return nullptr;
}

bool Parser::isTypeStart(const Token& token) const {
    return token.kind == TokenKind::Identifier || detail::isBuiltinTypeName(token.kind) || token.kind == TokenKind::KwRef ||
           token.kind == TokenKind::KwWeak || token.kind == TokenKind::Star;
}

bool Parser::isSeparator(const Token& token) const {
    return token.kind == TokenKind::Semicolon || token.kind == TokenKind::Newline;
}

bool Parser::isCompileArgCallStart() const {
    if (!check(TokenKind::LBrace)) {
        return false;
    }

    int depth = 0;
    std::size_t probe = index_;
    while (probe < tokens_.size()) {
        const TokenKind kind = tokens_[probe].kind;
        if (kind == TokenKind::LBrace) {
            ++depth;
        } else if (kind == TokenKind::RBrace) {
            --depth;
            if (depth == 0) {
                return probe + 1 < tokens_.size() && tokens_[probe + 1].kind == TokenKind::LParen;
            }
        }
        ++probe;
    }
    return false;
}

void Parser::skipSeparators() {
    while (isSeparator(current())) {
        advance();
    }
}

void Parser::consumeOptionalStatementTerminator() {
    if (check(TokenKind::Semicolon) || check(TokenKind::Newline)) {
        advance();
    }
}

bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

bool Parser::check(TokenKind kind) const {
    return current().kind == kind;
}

const Token& Parser::advance() {
    if (!check(TokenKind::EndOfFile)) {
        ++index_;
    }
    return previous();
}

const Token& Parser::current() const {
    return tokens_[index_];
}

const Token& Parser::previous() const {
    return tokens_[index_ - 1];
}

bool Parser::expect(TokenKind kind, const char* message) {
    if (check(kind)) {
        advance();
        return true;
    }
    diagnostics_.error(current().range, message);
    return false;
}

void Parser::synchronizeTopLevel() {
    while (!check(TokenKind::EndOfFile)) {
        if (isSeparator(current())) {
            skipSeparators();
            return;
        }
        if (check(TokenKind::KwFn) || check(TokenKind::KwStruct) || check(TokenKind::KwEnum) || check(TokenKind::KwClass) ||
            check(TokenKind::At) || check(TokenKind::Hash)) {
            return;
        }
        advance();
    }
}

void Parser::synchronizeStatement() {
    while (!check(TokenKind::EndOfFile)) {
        if (isSeparator(current()) || check(TokenKind::RBrace)) {
            return;
        }
        advance();
    }
}

}  // namespace axc
