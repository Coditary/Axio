#include "axc/Parse/Parser.h"

#include <utility>

#include "../Internal/ParserInternal.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

Parser::Parser(std::vector<Token> tokens, DiagnosticEngine& diagnostics)
    : tokens_(std::move(tokens)), diagnostics_(diagnostics) {}

TranslationUnit Parser::parseTranslationUnit() {
    return detail::TopLevelParser(*this).parseTranslationUnit();
}

std::vector<Annotation> Parser::parseAnnotations() {
    return detail::TopLevelParser(*this).parseAnnotations();
}

bool Parser::parsePreprocessorDirective(TranslationUnit& unit) {
    return detail::TopLevelParser(*this).parsePreprocessorDirective(unit);
}

std::unique_ptr<Decl> Parser::parseTopLevelDecl() {
    return detail::TopLevelParser(*this).parseTopLevelDecl();
}

namespace detail {

TopLevelParser::TopLevelParser(Parser& parser) : parser_(parser) {}

TranslationUnit TopLevelParser::parseTranslationUnit() {
    TranslationUnit unit;

    parser_.skipSeparators();
    if (parser_.match(TokenKind::KwPackage)) {
        const SourceRange start = parser_.previous().range;
        if (!parser_.expect(TokenKind::Identifier, "expected package path after 'package'")) {
            return unit;
        }
        unit.packageName = parser_.previous().lexeme;
        unit.packageRange = parser_.combine(start, parser_.previous().range);
        while (parser_.match(TokenKind::Dot)) {
            if (!parser_.expect(TokenKind::Identifier, "expected package segment after '.'")) {
                break;
            }
            unit.packageName += '.';
            unit.packageName += parser_.previous().lexeme;
            unit.packageRange = parser_.combine(unit.packageRange, parser_.previous().range);
        }
        parser_.consumeOptionalStatementTerminator();
        parser_.skipSeparators();
    }

    while (!parser_.check(TokenKind::EndOfFile)) {
        parser_.skipSeparators();
        if (parser_.check(TokenKind::EndOfFile)) {
            break;
        }

        if (parsePreprocessorDirective(unit)) {
            continue;
        }

        if (auto declaration = parseTopLevelDecl()) {
            unit.declarations.push_back(std::move(declaration));
        } else {
            parser_.synchronizeTopLevel();
        }
    }

    return unit;
}

std::vector<Annotation> TopLevelParser::parseAnnotations() {
    std::vector<Annotation> annotations;

    while (parser_.match(TokenKind::At)) {
        const SourceRange start = parser_.previous().range;
        if (!parser_.expect(TokenKind::Identifier, "expected annotation name after '@'")) {
            break;
        }

        Annotation annotation;
        annotation.name = parser_.previous().lexeme;
        annotation.range = parser_.combine(start, parser_.previous().range);

        if (parser_.match(TokenKind::LParen)) {
            if (!parser_.check(TokenKind::RParen)) {
                do {
                    if (!(parser_.check(TokenKind::Identifier) || parser_.check(TokenKind::StringLiteral) || parser_.check(TokenKind::IntegerLiteral))) {
                        parser_.diagnostics_.error(parser_.current().range, "expected annotation argument");
                        break;
                    }
                    annotation.arguments.push_back(parser_.advance().lexeme);
                    annotation.range = parser_.combine(annotation.range, parser_.previous().range);
                } while (parser_.match(TokenKind::Comma));
            }
            parser_.expect(TokenKind::RParen, "expected ')' after annotation arguments");
            annotation.range = parser_.combine(annotation.range, parser_.previous().range);
        }

        annotations.push_back(std::move(annotation));
        parser_.skipSeparators();
    }

    return annotations;
}

bool TopLevelParser::parsePreprocessorDirective(TranslationUnit& unit) {
    if (!parser_.match(TokenKind::Hash)) {
        return false;
    }

    const SourceRange start = parser_.previous().range;
    if (!parser_.expect(TokenKind::Identifier, "expected preprocessor directive after '#'")) {
        return true;
    }

    PreprocessorDirective directive;
    directive.name = parser_.previous().lexeme;
    directive.range = parser_.combine(start, parser_.previous().range);

    while (!parser_.check(TokenKind::EndOfFile) && !parser_.isSeparator(parser_.current())) {
        directive.arguments.push_back(parser_.advance().lexeme);
        directive.range = parser_.combine(directive.range, parser_.previous().range);
    }

    unit.preprocessorDirectives.push_back(std::move(directive));
    parser_.skipSeparators();
    return true;
}

std::unique_ptr<Decl> TopLevelParser::parseTopLevelDecl() {
    if (!parser_.pendingTopLevelDecls_.empty()) {
        auto declaration = std::move(parser_.pendingTopLevelDecls_.front());
        parser_.pendingTopLevelDecls_.erase(parser_.pendingTopLevelDecls_.begin());
        return declaration;
    }

    parser_.skipSeparators();
    std::vector<Annotation> annotations = parseAnnotations();
    parser_.skipSeparators();
    const bool isPublic = parser_.match(TokenKind::KwPub);
    parser_.skipSeparators();
    const bool isExtern = parser_.match(TokenKind::KwExtern);
    parser_.skipSeparators();

    DeclarationParser declarationParser(parser_);
    auto applyVisibility = [&](std::unique_ptr<Decl> declaration) {
        if (declaration) {
            declaration->visibility = isPublic ? Visibility::Public : Visibility::Private;
        }
        return declaration;
    };
    if (parser_.check(TokenKind::KwImport)) {
        auto declarations = declarationParser.parseImportDecls(std::move(annotations));
        for (auto& declaration : declarations) {
            if (declaration) {
                declaration->visibility = isPublic ? Visibility::Public : Visibility::Private;
                parser_.pendingTopLevelDecls_.push_back(std::move(declaration));
            }
        }
        if (parser_.pendingTopLevelDecls_.empty()) {
            return nullptr;
        }
        auto declaration = std::move(parser_.pendingTopLevelDecls_.front());
        parser_.pendingTopLevelDecls_.erase(parser_.pendingTopLevelDecls_.begin());
        return declaration;
    }
    if (parser_.check(TokenKind::KwLet)) {
        return applyVisibility(declarationParser.parseGlobalDecl(std::move(annotations), true));
    }
    if (parser_.check(TokenKind::KwConst)) {
        return applyVisibility(declarationParser.parseGlobalDecl(std::move(annotations), false));
    }
    if (parser_.check(TokenKind::KwFn)) {
        return applyVisibility(declarationParser.parseFunctionDecl(std::move(annotations), isExtern));
    }
    if (parser_.check(TokenKind::KwStruct)) {
        return applyVisibility(declarationParser.parseStructDecl(std::move(annotations)));
    }
    if (parser_.check(TokenKind::KwEnum)) {
        return applyVisibility(declarationParser.parseEnumDecl(std::move(annotations)));
    }
    if (parser_.check(TokenKind::KwClass)) {
        return applyVisibility(declarationParser.parseClassDecl(std::move(annotations)));
    }

    parser_.diagnostics_.error(parser_.current().range, "expected top-level declaration");
    return nullptr;
}

}  // namespace detail

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
            check(TokenKind::KwLet) || check(TokenKind::KwConst) || check(TokenKind::KwPub) || check(TokenKind::KwPackage) || check(TokenKind::At) || check(TokenKind::Hash)) {
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
