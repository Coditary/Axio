#include "axc/Parse/Parser.h"

#include <cstdlib>

#include "../Internal/ParserInternal.h"
#include "axc/Support/Diagnostic.h"

namespace axc {

std::unique_ptr<FunctionDecl> Parser::parseFunctionDecl(std::vector<Annotation> annotations, bool isExtern, bool isMethod, std::string receiverType) {
    return detail::DeclarationParser(*this).parseFunctionDecl(std::move(annotations), isExtern, isMethod, std::move(receiverType));
}

std::vector<std::unique_ptr<ImportDecl>> Parser::parseImportDecls(std::vector<Annotation> annotations) {
    return detail::DeclarationParser(*this).parseImportDecls(std::move(annotations));
}

std::unique_ptr<GlobalVarDecl> Parser::parseGlobalDecl(std::vector<Annotation> annotations, bool mutableStorage) {
    return detail::DeclarationParser(*this).parseGlobalDecl(std::move(annotations), mutableStorage);
}

std::unique_ptr<StructDecl> Parser::parseStructDecl(std::vector<Annotation> annotations) {
    return detail::DeclarationParser(*this).parseStructDecl(std::move(annotations));
}

std::unique_ptr<EnumDecl> Parser::parseEnumDecl(std::vector<Annotation> annotations) {
    return detail::DeclarationParser(*this).parseEnumDecl(std::move(annotations));
}

std::unique_ptr<ClassDecl> Parser::parseClassDecl(std::vector<Annotation> annotations) {
    return detail::DeclarationParser(*this).parseClassDecl(std::move(annotations));
}

Type Parser::parseType() {
    return detail::DeclarationParser(*this).parseType();
}

Parameter Parser::parseParameter(bool isCompileTime) {
    return detail::DeclarationParser(*this).parseParameter(isCompileTime);
}

std::vector<Type> Parser::parseReturnTypeList() {
    return detail::DeclarationParser(*this).parseReturnTypeList();
}

namespace detail {

DeclarationParser::DeclarationParser(Parser& parser) : parser_(parser) {}

std::unique_ptr<GlobalVarDecl> DeclarationParser::parseGlobalDecl(std::vector<Annotation> annotations, bool mutableStorage) {
    parser_.advance();
    const SourceRange start = parser_.previous().range;
    if (!parser_.expect(TokenKind::Identifier, "expected global name")) {
        return nullptr;
    }

    auto declaration = std::make_unique<GlobalVarDecl>(parser_.previous().lexeme, parser_.combine(start, parser_.previous().range));
    declaration->annotations = std::move(annotations);
    declaration->mutableStorage = mutableStorage;

    if (!parser_.check(TokenKind::Equal) && !parser_.isSeparator(parser_.current()) && !parser_.check(TokenKind::EndOfFile)) {
        declaration->type = parseType();
        declaration->range = parser_.combine(declaration->range, declaration->type.range);
    }

    if (parser_.match(TokenKind::Equal)) {
        declaration->initializer = ExpressionParser(parser_).parseExpression();
        declaration->range = parser_.combine(declaration->range, declaration->initializer->range);
    }

    parser_.consumeOptionalStatementTerminator();
    parser_.skipSeparators();
    return declaration;
}

std::unique_ptr<FunctionDecl> DeclarationParser::parseFunctionDecl(std::vector<Annotation> annotations, bool isExtern, bool isMethod,
                                                                   std::string receiverType) {
    parser_.expect(TokenKind::KwFn, "expected 'fn'");
    const SourceRange start = parser_.previous().range;
    const bool isLlvm = parser_.match(TokenKind::KwLlvm);
    if (!(parser_.check(TokenKind::Identifier) || detail::isBuiltinTypeName(parser_.current().kind))) {
        parser_.diagnostics_.error(parser_.current().range, "expected function name");
        return nullptr;
    }
    parser_.advance();

    auto function = std::make_unique<FunctionDecl>(parser_.previous().lexeme, parser_.combine(start, parser_.previous().range));
    function->annotations = std::move(annotations);
    function->isExtern = isExtern;
    function->isLlvm = isLlvm;
    function->receiverType = std::move(receiverType);

    if (function->isLlvm && function->isExtern) {
        parser_.diagnostics_.error(function->range, "llvm functions cannot be declared extern");
    }

    if (parser_.match(TokenKind::LBrace)) {
        if (!parser_.check(TokenKind::RBrace)) {
            do {
                function->compileParameters.push_back(parseParameter(true));
            } while (parser_.match(TokenKind::Comma));
        }
        parser_.expect(TokenKind::RBrace, "expected '}' after compile-time parameter list");
    }

    parser_.expect(TokenKind::LParen, "expected '(' after function name");
    parser_.skipSeparators();
    if (isMethod) {
        Parameter self;
        self.name = "self";
        self.type.name = function->receiverType;
        self.type.range = function->range;
        self.range = function->range;
        function->runtimeParameters.push_back(std::move(self));
    }
    if (!parser_.check(TokenKind::RParen)) {
        do {
            function->runtimeParameters.push_back(parseParameter(false));
            parser_.skipSeparators();
        } while (parser_.match(TokenKind::Comma));
    }
    parser_.expect(TokenKind::RParen, "expected ')' after parameter list");

    while (parser_.check(TokenKind::Newline)) {
        parser_.advance();
    }
    if (!parser_.check(TokenKind::LBrace) && !parser_.check(TokenKind::LlvmBlock) && !parser_.check(TokenKind::Semicolon) && !parser_.check(TokenKind::Newline) &&
        !parser_.check(TokenKind::EndOfFile)) {
        function->returnTypes = parseReturnTypeList();
    }

    while (parser_.check(TokenKind::Newline)) {
        parser_.advance();
    }
    if (parser_.match(TokenKind::Semicolon)) {
        if (function->isLlvm) {
            parser_.diagnostics_.error(function->range, "llvm functions require a body");
        }
        function->range = parser_.combine(function->range, parser_.previous().range);
        return function;
    }

    if (isExtern && (parser_.check(TokenKind::LBrace) || parser_.check(TokenKind::LlvmBlock))) {
        parser_.diagnostics_.error(parser_.current().range, "extern functions cannot have a body");
    }

    if (function->isLlvm) {
        if (!parser_.expect(TokenKind::LlvmBlock, "expected llvm function body")) {
            return function;
        }
        function->llvmBody = parser_.previous().lexeme;
        function->llvmBodyRange = parser_.previous().range;
        function->range = parser_.combine(function->range, function->llvmBodyRange);
        return function;
    }

    function->body = StatementParser(parser_).parseCompoundStmt();
    if (function->body) {
        function->range = parser_.combine(function->range, function->body->range);
    }
    return function;
}

std::vector<std::unique_ptr<ImportDecl>> DeclarationParser::parseImportDecls(std::vector<Annotation> annotations) {
    parser_.expect(TokenKind::KwImport, "expected 'import'");
    const SourceRange start = parser_.previous().range;

    auto parseOneImport = [&](std::vector<Annotation> importAnnotations) -> std::unique_ptr<ImportDecl> {
        std::string alias;
        if (parser_.check(TokenKind::Identifier)) {
            const Token& first = parser_.current();
            if (parser_.index_ + 1 < parser_.tokens_.size() && parser_.tokens_[parser_.index_ + 1].kind == TokenKind::Identifier) {
                parser_.advance();
                alias = first.lexeme;
            }
        }

        if (!parser_.expect(TokenKind::Identifier, "expected module path after 'import'")) {
            return nullptr;
        }

        std::vector<std::string> segments;
        segments.push_back(parser_.previous().lexeme);
        SourceRange range = parser_.combine(start, parser_.previous().range);
        while (parser_.match(TokenKind::Dot)) {
            if (!parser_.expect(TokenKind::Identifier, "expected module segment after '.'")) {
                break;
            }
            segments.push_back(parser_.previous().lexeme);
            range = parser_.combine(range, parser_.previous().range);
        }

        std::vector<std::string> importedNames;
        if (parser_.match(TokenKind::LBrace)) {
            parser_.skipSeparators();
            if (!parser_.check(TokenKind::RBrace)) {
                do {
                    if (!parser_.expect(TokenKind::Identifier, "expected exported name in import list")) {
                        break;
                    }
                    importedNames.push_back(parser_.previous().lexeme);
                    range = parser_.combine(range, parser_.previous().range);
                    parser_.skipSeparators();
                } while (parser_.match(TokenKind::Comma));
            }
            parser_.expect(TokenKind::RBrace, "expected '}' after import list");
            range = parser_.combine(range, parser_.previous().range);
        }

        std::string modulePath;
        for (std::size_t i = 0; i < segments.size(); ++i) {
            if (i > 0) {
                modulePath += '.';
            }
            modulePath += segments[i];
        }

        auto declaration = std::make_unique<ImportDecl>(modulePath, std::move(segments), range);
        declaration->annotations = std::move(importAnnotations);
        declaration->alias = std::move(alias);
        declaration->importedNames = std::move(importedNames);
        return declaration;
    };

    std::vector<std::unique_ptr<ImportDecl>> declarations;
    if (parser_.match(TokenKind::LParen)) {
        parser_.skipSeparators();
        while (!parser_.check(TokenKind::RParen) && !parser_.check(TokenKind::EndOfFile)) {
            auto declaration = parseOneImport({});
            if (declaration) {
                declarations.push_back(std::move(declaration));
            }
            parser_.consumeOptionalStatementTerminator();
            parser_.skipSeparators();
        }
        parser_.expect(TokenKind::RParen, "expected ')' after import block");
    } else {
        auto declaration = parseOneImport(std::move(annotations));
        if (declaration) {
            declarations.push_back(std::move(declaration));
        }
    }

    parser_.consumeOptionalStatementTerminator();
    parser_.skipSeparators();
    return declarations;
}

std::unique_ptr<StructDecl> DeclarationParser::parseStructDecl(std::vector<Annotation> annotations) {
    parser_.expect(TokenKind::KwStruct, "expected 'struct'");
    const SourceRange start = parser_.previous().range;
    if (!parser_.expect(TokenKind::Identifier, "expected struct name")) {
        return nullptr;
    }

    auto declaration = std::make_unique<StructDecl>(parser_.previous().lexeme, parser_.combine(start, parser_.previous().range));
    declaration->annotations = std::move(annotations);

    if (parser_.match(TokenKind::KwAlign)) {
        parser_.expect(TokenKind::LParen, "expected '(' after align");
        if (parser_.expect(TokenKind::IntegerLiteral, "expected integer alignment")) {
            declaration->alignment = std::strtoll(parser_.previous().lexeme.c_str(), nullptr, 10);
        }
        parser_.expect(TokenKind::RParen, "expected ')' after alignment value");
    }

    parser_.expect(TokenKind::LBrace, "expected '{' after struct name");
    parser_.skipSeparators();
    while (!parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
        StructField field;
        if (!parser_.expect(TokenKind::Identifier, "expected field name")) {
            parser_.synchronizeTopLevel();
            break;
        }
        field.name = parser_.previous().lexeme;
        field.range = parser_.previous().range;

        field.type = parseType();
        field.range = parser_.combine(field.range, field.type.range);

        if (parser_.match(TokenKind::KwBits)) {
            if (!parser_.expect(TokenKind::IntegerLiteral, "expected integer bit width after 'bits'")) {
                break;
            }
            field.bitWidth = std::strtoll(parser_.previous().lexeme.c_str(), nullptr, 10);
            field.range = parser_.combine(field.range, parser_.previous().range);
        }

        if (parser_.match(TokenKind::Equal)) {
            field.defaultValue = ExpressionParser(parser_).parseExpression();
            field.range = parser_.combine(field.range, field.defaultValue->range);
        }

        declaration->fields.push_back(std::move(field));
        parser_.skipSeparators();
    }
    parser_.expect(TokenKind::RBrace, "expected '}' after struct body");
    declaration->range = parser_.combine(declaration->range, parser_.previous().range);
    parser_.consumeOptionalStatementTerminator();
    parser_.skipSeparators();
    return declaration;
}

std::unique_ptr<EnumDecl> DeclarationParser::parseEnumDecl(std::vector<Annotation> annotations) {
    parser_.expect(TokenKind::KwEnum, "expected 'enum'");
    const SourceRange start = parser_.previous().range;
    if (!parser_.expect(TokenKind::Identifier, "expected enum name")) {
        return nullptr;
    }

    auto declaration = std::make_unique<EnumDecl>(parser_.previous().lexeme, parser_.combine(start, parser_.previous().range));
    declaration->annotations = std::move(annotations);

    if (parser_.match(TokenKind::LParen)) {
        if (!parser_.check(TokenKind::RParen)) {
            do {
                EnumParam param;
                if (!parser_.expect(TokenKind::Identifier, "expected enum parameter name")) {
                    break;
                }
                param.name = parser_.previous().lexeme;
                param.type = parseType();
                param.range = parser_.combine(parser_.previous().range, param.type.range);
                declaration->parameters.push_back(std::move(param));
            } while (parser_.match(TokenKind::Comma));
        }
        parser_.expect(TokenKind::RParen, "expected ')' after enum parameter list");
    }

    if (parser_.match(TokenKind::KwAs)) {
        if (parser_.match(TokenKind::KwFlags)) {
            declaration->isFlags = true;
        } else {
            parser_.diagnostics_.error(parser_.current().range, "expected 'Flags' after 'as'");
        }
    }

    parser_.expect(TokenKind::LBrace, "expected '{' after enum declaration");
    parser_.skipSeparators();
    while (!parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
        if (!parser_.expect(TokenKind::Identifier, "expected enum element name")) {
            break;
        }

        EnumElement element;
        element.name = parser_.previous().lexeme;
        element.range = parser_.previous().range;

        if (parser_.match(TokenKind::LParen)) {
            if (!parser_.check(TokenKind::RParen)) {
                do {
                    if (parser_.isTypeStart(parser_.current())) {
                        element.payloadTypes.push_back(parseType());
                        element.range = parser_.combine(element.range, element.payloadTypes.back().range);
                    } else {
                        element.payloadValues.push_back(ExpressionParser(parser_).parseExpression());
                        element.range = parser_.combine(element.range, element.payloadValues.back()->range);
                    }
                } while (parser_.match(TokenKind::Comma));
            }
            parser_.expect(TokenKind::RParen, "expected ')' after enum payload list");
        }

        if (parser_.match(TokenKind::KwAs)) {
            if (!parser_.expect(TokenKind::KwFlag, "expected 'Flag' after 'as'")) {
                break;
            }
            element.isFlagGroup = true;
        }

        if (parser_.match(TokenKind::LBrace)) {
            parser_.skipSeparators();
            while (!parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
                if (!parser_.expect(TokenKind::Identifier, "expected nested enum element")) {
                    break;
                }
                EnumElement nested;
                nested.name = parser_.previous().lexeme;
                nested.range = parser_.previous().range;
                auto nestedDecl = std::make_unique<EnumDecl>(element.name, nested.range);
                nestedDecl->elements.push_back(std::move(nested));
                nestedDecl->isFlags = element.isFlagGroup;
                element.nestedDecls.push_back(std::move(nestedDecl));
                if (parser_.match(TokenKind::Comma)) {
                    parser_.skipSeparators();
                } else {
                    parser_.skipSeparators();
                }
            }
            parser_.expect(TokenKind::RBrace, "expected '}' after nested enum block");
        }

        declaration->elements.push_back(std::move(element));
        parser_.match(TokenKind::Comma);
        parser_.skipSeparators();
    }

    parser_.expect(TokenKind::RBrace, "expected '}' after enum body");
    declaration->range = parser_.combine(declaration->range, parser_.previous().range);
    parser_.consumeOptionalStatementTerminator();
    parser_.skipSeparators();
    return declaration;
}

std::unique_ptr<ClassDecl> DeclarationParser::parseClassDecl(std::vector<Annotation> annotations) {
    parser_.expect(TokenKind::KwClass, "expected 'class'");
    const SourceRange start = parser_.previous().range;
    if (!parser_.expect(TokenKind::Identifier, "expected class name")) {
        return nullptr;
    }

    auto declaration = std::make_unique<ClassDecl>(parser_.previous().lexeme, parser_.combine(start, parser_.previous().range));
    declaration->annotations = std::move(annotations);

    parser_.expect(TokenKind::LBrace, "expected '{' after class name");
    parser_.skipSeparators();
    while (!parser_.check(TokenKind::RBrace) && !parser_.check(TokenKind::EndOfFile)) {
        auto memberAnnotations = TopLevelParser(parser_).parseAnnotations();
        const bool isPublic = parser_.match(TokenKind::KwPub);
        parser_.skipSeparators();
        if (parser_.check(TokenKind::KwFn)) {
            auto method = parseFunctionDecl(std::move(memberAnnotations), false, true, declaration->name);
            if (method) {
                method->visibility = isPublic ? Visibility::Public : Visibility::Private;
                declaration->methods.push_back(std::move(method));
            }
            parser_.skipSeparators();
            continue;
        }

        if (!parser_.expect(TokenKind::Identifier, "expected class member name or included struct")) {
            break;
        }
        const Token first = parser_.previous();

        if (parser_.isSeparator(parser_.current()) || parser_.check(TokenKind::RBrace)) {
            declaration->includedStructs.push_back(first.lexeme);
            parser_.skipSeparators();
            continue;
        }

        ClassMember member;
        member.name = first.lexeme;
        member.type = parseType();
        member.range = parser_.combine(first.range, member.type.range);
        member.visibility = isPublic ? Visibility::Public : Visibility::Private;

        if (parser_.match(TokenKind::FatArrow)) {
            member.dynamicValue = ExpressionParser(parser_).parseExpression();
            member.range = parser_.combine(member.range, member.dynamicValue->range);
        }

        declaration->members.push_back(std::move(member));
        parser_.skipSeparators();
    }

    parser_.expect(TokenKind::RBrace, "expected '}' after class body");
    declaration->range = parser_.combine(declaration->range, parser_.previous().range);
    parser_.consumeOptionalStatementTerminator();
    parser_.skipSeparators();
    return declaration;
}

Type DeclarationParser::parseType() {
    parser_.skipSeparators();
    const Token start = parser_.current();
    Type type;

    while (parser_.match(TokenKind::KwRef) || parser_.match(TokenKind::KwWeak)) {
        type.modifiers.push_back(parser_.previous().kind == TokenKind::KwRef ? TypeModifier::Ref : TypeModifier::Weak);
        type.range = parser_.previous().range;
    }

    while (parser_.match(TokenKind::Star)) {
        type.modifiers.push_back(TypeModifier::Unique);
        type.range = parser_.previous().range;
    }

    const bool isUnsigned = parser_.match(TokenKind::KwUnsigned);

    if (detail::isBuiltinTypeName(parser_.current().kind) || parser_.check(TokenKind::Identifier)) {
        type.name = parser_.advance().lexeme;
        type.range = type.range.begin.offset == 0 && type.range.end.offset == 0 ? parser_.previous().range : parser_.combine(type.range, parser_.previous().range);
        while (parser_.match(TokenKind::Dot)) {
            if (!parser_.expect(TokenKind::Identifier, "expected type name segment after '.'")) {
                break;
            }
            type.name += "." + parser_.previous().lexeme;
            type.range = parser_.combine(type.range, parser_.previous().range);
        }
    } else {
        parser_.diagnostics_.error(parser_.current().range, "expected type");
        type.range = start.range;
        return type;
    }

    if (isUnsigned) {
        if (type.name == "int" || type.name == "i32") {
            type.name = "u32";
        } else if (type.name == "i8") {
            type.name = "u8";
        } else if (type.name == "i16" || type.name == "short") {
            type.name = "u16";
        } else if (type.name == "i64" || type.name == "long") {
            type.name = "u64";
        } else {
            parser_.diagnostics_.error(type.range, "'unsigned' requires an integer base type");
        }
    }

    while (parser_.match(TokenKind::Star)) {
        ++type.pointerDepth;
        type.range = parser_.combine(type.range, parser_.previous().range);
    }

    while (parser_.match(TokenKind::LBracket)) {
        std::optional<std::size_t> extent;
        if (parser_.match(TokenKind::IntegerLiteral)) {
            extent = static_cast<std::size_t>(std::strtoull(parser_.previous().lexeme.c_str(), nullptr, 10));
        }
        parser_.expect(TokenKind::RBracket, "expected ']' after array extent");
        type.arrayExtents.push_back(extent);
        type.range = parser_.combine(type.range, parser_.previous().range);
    }

    return type;
}

Parameter DeclarationParser::parseParameter(bool isCompileTime) {
    Parameter param;
    param.isCompileTime = isCompileTime;
    parser_.skipSeparators();
    param.isConst = parser_.match(TokenKind::KwConst);
    parser_.skipSeparators();
    if (!parser_.expect(TokenKind::Identifier, "expected parameter name")) {
        return param;
    }
    param.name = parser_.previous().lexeme;
    parser_.skipSeparators();
    param.type = parseType();
    param.range = parser_.combine(parser_.previous().range, param.type.range);
    return param;
}

std::vector<Type> DeclarationParser::parseReturnTypeList() {
    std::vector<Type> types;
    if (parser_.match(TokenKind::LParen)) {
        parser_.skipSeparators();
        if (!parser_.check(TokenKind::RParen)) {
            do {
                types.push_back(parseType());
                parser_.skipSeparators();
            } while (parser_.match(TokenKind::Comma));
        }
        parser_.expect(TokenKind::RParen, "expected ')' after return type list");
        return types;
    }

    types.push_back(parseType());
    return types;
}

}  // namespace detail

}  // namespace axc
