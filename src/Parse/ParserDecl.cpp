#include "axc/Parse/Parser.h"

#include <cstdlib>

#include "ParserInternal.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

std::unique_ptr<FunctionDecl> Parser::parseFunctionDecl(std::vector<Annotation> annotations, bool isExtern, bool isMethod, std::string receiverType) {
    expect(TokenKind::KwFn, "expected 'fn'");
    const SourceRange start = previous().range;
    if (!expect(TokenKind::Identifier, "expected function name")) {
        return nullptr;
    }

    auto function = std::make_unique<FunctionDecl>(previous().lexeme, combine(start, previous().range));
    function->annotations = std::move(annotations);
    function->isExtern = isExtern;
    function->receiverType = std::move(receiverType);

    if (match(TokenKind::LBrace)) {
        if (!check(TokenKind::RBrace)) {
            do {
                function->compileParameters.push_back(parseParameter(true));
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RBrace, "expected '}' after compile-time parameter list");
    }

    expect(TokenKind::LParen, "expected '(' after function name");
    skipSeparators();
    if (isMethod) {
        Parameter self;
        self.name = "self";
        self.type.name = function->receiverType;
        self.type.range = function->range;
        self.range = function->range;
        function->runtimeParameters.push_back(std::move(self));
    }
    if (!check(TokenKind::RParen)) {
        do {
            function->runtimeParameters.push_back(parseParameter(false));
            skipSeparators();
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "expected ')' after parameter list");

    while (check(TokenKind::Newline)) {
        advance();
    }
    if (!check(TokenKind::LBrace) && !check(TokenKind::Semicolon) && !check(TokenKind::Newline) && !check(TokenKind::EndOfFile)) {
        function->returnTypes = parseReturnTypeList();
    }

    while (check(TokenKind::Newline)) {
        advance();
    }
    if (match(TokenKind::Semicolon)) {
        function->range = combine(function->range, previous().range);
        return function;
    }

    if (isExtern && check(TokenKind::LBrace)) {
        diagnostics_.error(current().range, "extern functions cannot have a body");
    }

    function->body = parseCompoundStmt();
    if (function->body) {
        function->range = combine(function->range, function->body->range);
    }
    return function;
}

std::unique_ptr<ImportDecl> Parser::parseImportDecl(std::vector<Annotation> annotations) {
    expect(TokenKind::KwImport, "expected 'import'");
    const SourceRange start = previous().range;
    if (!expect(TokenKind::Identifier, "expected module path after 'import'")) {
        return nullptr;
    }

    std::vector<std::string> segments;
    segments.push_back(previous().lexeme);
    SourceRange range = combine(start, previous().range);
    while (match(TokenKind::Dot)) {
        if (!expect(TokenKind::Identifier, "expected module segment after '.'")) {
            break;
        }
        segments.push_back(previous().lexeme);
        range = combine(range, previous().range);
    }

    std::string modulePath;
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) {
            modulePath += '.';
        }
        modulePath += segments[i];
    }

    auto declaration = std::make_unique<ImportDecl>(modulePath, std::move(segments), range);
    declaration->annotations = std::move(annotations);
    consumeOptionalStatementTerminator();
    skipSeparators();
    return declaration;
}

std::unique_ptr<StructDecl> Parser::parseStructDecl(std::vector<Annotation> annotations) {
    expect(TokenKind::KwStruct, "expected 'struct'");
    const SourceRange start = previous().range;
    if (!expect(TokenKind::Identifier, "expected struct name")) {
        return nullptr;
    }

    auto declaration = std::make_unique<StructDecl>(previous().lexeme, combine(start, previous().range));
    declaration->annotations = std::move(annotations);

    if (match(TokenKind::KwAlign)) {
        expect(TokenKind::LParen, "expected '(' after align");
        if (expect(TokenKind::IntegerLiteral, "expected integer alignment")) {
            declaration->alignment = std::strtoll(previous().lexeme.c_str(), nullptr, 10);
        }
        expect(TokenKind::RParen, "expected ')' after alignment value");
    }

    expect(TokenKind::LBrace, "expected '{' after struct name");
    skipSeparators();
    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
        StructField field;
        if (!expect(TokenKind::Identifier, "expected field name")) {
            synchronizeTopLevel();
            break;
        }
        field.name = previous().lexeme;
        field.range = previous().range;

        field.type = parseType();
        field.range = combine(field.range, field.type.range);

        if (match(TokenKind::KwBits)) {
            if (!expect(TokenKind::IntegerLiteral, "expected integer bit width after 'bits'")) {
                break;
            }
            field.bitWidth = std::strtoll(previous().lexeme.c_str(), nullptr, 10);
            field.range = combine(field.range, previous().range);
        }

        if (match(TokenKind::Equal)) {
            field.defaultValue = parseExpression();
            field.range = combine(field.range, field.defaultValue->range);
        }

        declaration->fields.push_back(std::move(field));
        skipSeparators();
    }
    expect(TokenKind::RBrace, "expected '}' after struct body");
    declaration->range = combine(declaration->range, previous().range);
    consumeOptionalStatementTerminator();
    skipSeparators();
    return declaration;
}

std::unique_ptr<EnumDecl> Parser::parseEnumDecl(std::vector<Annotation> annotations) {
    expect(TokenKind::KwEnum, "expected 'enum'");
    const SourceRange start = previous().range;
    if (!expect(TokenKind::Identifier, "expected enum name")) {
        return nullptr;
    }

    auto declaration = std::make_unique<EnumDecl>(previous().lexeme, combine(start, previous().range));
    declaration->annotations = std::move(annotations);

    if (match(TokenKind::LParen)) {
        if (!check(TokenKind::RParen)) {
            do {
                EnumParam param;
                if (!expect(TokenKind::Identifier, "expected enum parameter name")) {
                    break;
                }
                param.name = previous().lexeme;
                param.type = parseType();
                param.range = combine(previous().range, param.type.range);
                declaration->parameters.push_back(std::move(param));
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "expected ')' after enum parameter list");
    }

    if (match(TokenKind::KwAs)) {
        if (match(TokenKind::KwFlags)) {
            declaration->isFlags = true;
        } else {
            diagnostics_.error(current().range, "expected 'Flags' after 'as'");
        }
    }

    expect(TokenKind::LBrace, "expected '{' after enum declaration");
    skipSeparators();
    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
        if (!expect(TokenKind::Identifier, "expected enum element name")) {
            break;
        }

        EnumElement element;
        element.name = previous().lexeme;
        element.range = previous().range;

        if (match(TokenKind::LParen)) {
            if (!check(TokenKind::RParen)) {
                do {
                    if (isTypeStart(current())) {
                        element.payloadTypes.push_back(parseType());
                        element.range = combine(element.range, element.payloadTypes.back().range);
                    } else {
                        element.payloadValues.push_back(parseExpression());
                        element.range = combine(element.range, element.payloadValues.back()->range);
                    }
                } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "expected ')' after enum payload list");
        }

        if (match(TokenKind::KwAs)) {
            if (!expect(TokenKind::KwFlag, "expected 'Flag' after 'as'")) {
                break;
            }
            element.isFlagGroup = true;
        }

        if (match(TokenKind::LBrace)) {
            skipSeparators();
            while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
                if (!expect(TokenKind::Identifier, "expected nested enum element")) {
                    break;
                }
                EnumElement nested;
                nested.name = previous().lexeme;
                nested.range = previous().range;
                auto nestedDecl = std::make_unique<EnumDecl>(element.name, nested.range);
                nestedDecl->elements.push_back(std::move(nested));
                nestedDecl->isFlags = element.isFlagGroup;
                element.nestedDecls.push_back(std::move(nestedDecl));
                if (match(TokenKind::Comma)) {
                    skipSeparators();
                } else {
                    skipSeparators();
                }
            }
            expect(TokenKind::RBrace, "expected '}' after nested enum block");
        }

        declaration->elements.push_back(std::move(element));
        match(TokenKind::Comma);
        skipSeparators();
    }

    expect(TokenKind::RBrace, "expected '}' after enum body");
    declaration->range = combine(declaration->range, previous().range);
    consumeOptionalStatementTerminator();
    skipSeparators();
    return declaration;
}

std::unique_ptr<ClassDecl> Parser::parseClassDecl(std::vector<Annotation> annotations) {
    expect(TokenKind::KwClass, "expected 'class'");
    const SourceRange start = previous().range;
    if (!expect(TokenKind::Identifier, "expected class name")) {
        return nullptr;
    }

    auto declaration = std::make_unique<ClassDecl>(previous().lexeme, combine(start, previous().range));
    declaration->annotations = std::move(annotations);

    expect(TokenKind::LBrace, "expected '{' after class name");
    skipSeparators();
    while (!check(TokenKind::RBrace) && !check(TokenKind::EndOfFile)) {
        auto memberAnnotations = parseAnnotations();
        if (check(TokenKind::KwFn)) {
            auto method = parseFunctionDecl(std::move(memberAnnotations), false, true, declaration->name);
            if (method) {
                declaration->methods.push_back(std::move(method));
            }
            skipSeparators();
            continue;
        }

        if (!expect(TokenKind::Identifier, "expected class member name or included struct")) {
            break;
        }
        const Token first = previous();

        if (isSeparator(current()) || check(TokenKind::RBrace)) {
            declaration->includedStructs.push_back(first.lexeme);
            skipSeparators();
            continue;
        }

        ClassMember member;
        member.name = first.lexeme;
        member.type = parseType();
        member.range = combine(first.range, member.type.range);

        if (match(TokenKind::FatArrow)) {
            member.dynamicValue = parseExpression();
            member.range = combine(member.range, member.dynamicValue->range);
        }

        declaration->members.push_back(std::move(member));
        skipSeparators();
    }

    expect(TokenKind::RBrace, "expected '}' after class body");
    declaration->range = combine(declaration->range, previous().range);
    consumeOptionalStatementTerminator();
    skipSeparators();
    return declaration;
}

Type Parser::parseType() {
    skipSeparators();
    const Token start = current();
    Type type;

    while (match(TokenKind::KwRef) || match(TokenKind::KwWeak)) {
        type.modifiers.push_back(previous().kind == TokenKind::KwRef ? TypeModifier::Ref : TypeModifier::Weak);
        type.range = previous().range;
    }

    while (match(TokenKind::Star)) {
        ++type.pointerDepth;
        type.modifiers.push_back(TypeModifier::Unique);
        type.range = previous().range;
    }

    if (detail::isBuiltinTypeName(current().kind) || check(TokenKind::Identifier)) {
        type.name = advance().lexeme;
        type.range = type.range.begin.offset == 0 && type.range.end.offset == 0 ? previous().range : combine(type.range, previous().range);
        while (match(TokenKind::Dot)) {
            if (!expect(TokenKind::Identifier, "expected type name segment after '.'")) {
                break;
            }
            type.name += "." + previous().lexeme;
            type.range = combine(type.range, previous().range);
        }
    } else {
        diagnostics_.error(current().range, "expected type");
        type.range = start.range;
        return type;
    }

    while (match(TokenKind::Star)) {
        ++type.pointerDepth;
        type.range = combine(type.range, previous().range);
    }

    while (match(TokenKind::LBracket)) {
        std::optional<std::size_t> extent;
        if (match(TokenKind::IntegerLiteral)) {
            extent = static_cast<std::size_t>(std::strtoull(previous().lexeme.c_str(), nullptr, 10));
        }
        expect(TokenKind::RBracket, "expected ']' after array extent");
        type.arrayExtents.push_back(extent);
        type.range = combine(type.range, previous().range);
    }

    return type;
}

Parameter Parser::parseParameter(bool isCompileTime) {
    Parameter param;
    param.isCompileTime = isCompileTime;
    skipSeparators();
    if (!expect(TokenKind::Identifier, "expected parameter name")) {
        return param;
    }
    param.name = previous().lexeme;
    skipSeparators();
    param.type = parseType();
    param.range = combine(previous().range, param.type.range);
    return param;
}

std::vector<Type> Parser::parseReturnTypeList() {
    std::vector<Type> types;
    if (match(TokenKind::LParen)) {
        skipSeparators();
        if (!check(TokenKind::RParen)) {
            do {
                types.push_back(parseType());
                skipSeparators();
            } while (match(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "expected ')' after return type list");
        return types;
    }

    types.push_back(parseType());
    return types;
}

}  // namespace axc
