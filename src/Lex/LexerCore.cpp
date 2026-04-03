#include "axc/Lex/Lexer.h"

#include <cctype>
#include <string_view>
#include <unordered_map>

#include "LexerInternal.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

std::vector<Token> Lexer::lex() {
    static const std::unordered_map<std::string_view, TokenKind> keywords {
        {"fn", TokenKind::KwFn},
        {"let", TokenKind::KwLet},
        {"return", TokenKind::KwReturn},
        {"struct", TokenKind::KwStruct},
        {"enum", TokenKind::KwEnum},
        {"class", TokenKind::KwClass},
        {"import", TokenKind::KwImport},
        {"if", TokenKind::KwIf},
        {"else", TokenKind::KwElse},
        {"in", TokenKind::KwIn},
        {"as", TokenKind::KwAs},
        {"Flags", TokenKind::KwFlags},
        {"Flag", TokenKind::KwFlag},
        {"new", TokenKind::KwNew},
        {"weak", TokenKind::KwWeak},
        {"ref", TokenKind::KwRef},
        {"null", TokenKind::KwNull},
        {"extern", TokenKind::KwExtern},
        {"align", TokenKind::KwAlign},
        {"bits", TokenKind::KwBits},
        {"int", TokenKind::KwInt},
        {"void", TokenKind::KwVoid},
        {"str", TokenKind::KwStr},
        {"error", TokenKind::KwError},
        {"bool", TokenKind::KwBool},
        {"true", TokenKind::KwTrue},
        {"false", TokenKind::KwFalse},
        {"i2", TokenKind::KwI2},
        {"i8", TokenKind::KwI8},
        {"i16", TokenKind::KwI16},
        {"i32", TokenKind::KwI32},
        {"i64", TokenKind::KwI64},
        {"u8", TokenKind::KwU8},
        {"u16", TokenKind::KwU16},
        {"u32", TokenKind::KwU32},
        {"u64", TokenKind::KwU64},
        {"short", TokenKind::KwShort},
        {"long", TokenKind::KwLong},
        {"double", TokenKind::KwDouble},
        {"float", TokenKind::KwFloat},
        {"f8", TokenKind::KwF8},
        {"f16", TokenKind::KwF16},
        {"f32", TokenKind::KwF32},
        {"f64", TokenKind::KwF64},
        {"char", TokenKind::KwChar},
    };

    const std::string_view source = sourceManager_.source();
    std::vector<Token> tokens;
    std::size_t cursor = 0;

    auto push = [&](TokenKind kind, std::size_t begin, std::size_t end, std::string lexeme = {}) {
        tokens.push_back(Token {kind, std::move(lexeme), SourceRange {SourceLocation {begin}, SourceLocation {end}}});
    };

    while (cursor < source.size()) {
        const unsigned char ch = static_cast<unsigned char>(source[cursor]);

        if (ch == '\n') {
            push(TokenKind::Newline, cursor, cursor + 1, "\\n");
            ++cursor;
            continue;
        }

        if (std::isspace(ch)) {
            ++cursor;
            continue;
        }

        if (source[cursor] == '/' && cursor + 1 < source.size() && source[cursor + 1] == '/') {
            cursor += 2;
            while (cursor < source.size() && source[cursor] != '\n') {
                ++cursor;
            }
            continue;
        }

        if (source[cursor] == '/' && cursor + 1 < source.size() && source[cursor + 1] == '*') {
            const std::size_t begin = cursor;
            cursor += 2;
            while (cursor + 1 < source.size() && !(source[cursor] == '*' && source[cursor + 1] == '/')) {
                ++cursor;
            }
            if (cursor + 1 >= source.size()) {
                diagnostics_.error(sourceManager_.range(begin, source.size()), "unterminated block comment");
                break;
            }
            cursor += 2;
            continue;
        }

        if (source[cursor] == '%' && cursor + 1 < source.size() && source[cursor + 1] == '%') {
            const std::size_t begin = cursor;
            cursor += 2;

            const std::size_t nameBegin = cursor;
            while (cursor < source.size() && detail::isIdentifierContinue(static_cast<unsigned char>(source[cursor]))) {
                ++cursor;
            }

            if (cursor >= source.size() || source[cursor] != '{') {
                diagnostics_.error(sourceManager_.range(begin, cursor), "expected '{' after dialect name");
                continue;
            }

            const std::string dialectName(source.substr(nameBegin, cursor - nameBegin));
            ++cursor;
            const std::size_t contentBegin = cursor;
            while (cursor + 2 < source.size() && !(source[cursor] == '}' && source[cursor + 1] == '%' && source[cursor + 2] == '%')) {
                ++cursor;
            }

            if (cursor + 2 >= source.size()) {
                diagnostics_.error(sourceManager_.range(begin, source.size()), "unterminated dialect block");
                break;
            }

            const std::string content(source.substr(contentBegin, cursor - contentBegin));
            cursor += 3;
            push(TokenKind::DialectBlock, begin, cursor, dialectName + "\n" + content);
            continue;
        }

        if (std::isdigit(ch)) {
            const std::size_t begin = cursor;
            while (cursor < source.size() && std::isdigit(static_cast<unsigned char>(source[cursor]))) {
                ++cursor;
            }
            if (cursor < source.size() && source[cursor] == '.' && cursor + 1 < source.size() && std::isdigit(static_cast<unsigned char>(source[cursor + 1]))) {
                ++cursor;
                while (cursor < source.size() && std::isdigit(static_cast<unsigned char>(source[cursor]))) {
                    ++cursor;
                }
                push(TokenKind::FloatLiteral, begin, cursor, std::string(source.substr(begin, cursor - begin)));
            } else {
                push(TokenKind::IntegerLiteral, begin, cursor, std::string(source.substr(begin, cursor - begin)));
            }
            continue;
        }

        if (source[cursor] == '\'' && cursor + 2 < source.size()) {
            const std::size_t begin = cursor++;
            char value = source[cursor];
            if (value == '\\' && cursor + 1 < source.size()) {
                ++cursor;
                switch (source[cursor]) {
                    case 'n': value = '\n'; break;
                    case 't': value = '\t'; break;
                    case '\\': value = '\\'; break;
                    case '\'': value = '\''; break;
                    default: value = source[cursor]; break;
                }
            }
            ++cursor;
            if (cursor >= source.size() || source[cursor] != '\'') {
                diagnostics_.error(sourceManager_.range(begin, cursor), "unterminated char literal");
                continue;
            }
            ++cursor;
            push(TokenKind::CharLiteral, begin, cursor, std::string(1, value));
            continue;
        }

        if (detail::isIdentifierStart(ch)) {
            const std::size_t begin = cursor;
            while (cursor < source.size() && detail::isIdentifierContinue(static_cast<unsigned char>(source[cursor]))) {
                ++cursor;
            }
            std::string lexeme(source.substr(begin, cursor - begin));
            const auto it = keywords.find(lexeme);
            if (it != keywords.end()) {
                push(it->second, begin, cursor, lexeme);
            } else {
                push(TokenKind::Identifier, begin, cursor, lexeme);
            }
            continue;
        }

        if (source[cursor] == '"') {
            const std::size_t begin = cursor++;
            while (cursor < source.size() && source[cursor] != '"') {
                if (source[cursor] == '\\' && cursor + 1 < source.size()) {
                    cursor += 2;
                } else {
                    ++cursor;
                }
            }

            if (cursor >= source.size()) {
                diagnostics_.error(sourceManager_.range(begin, source.size()), "unterminated string literal");
                break;
            }

            ++cursor;
            push(TokenKind::StringLiteral, begin, cursor, detail::unescapeString(source.substr(begin + 1, cursor - begin - 2)));
            continue;
        }

        const std::size_t begin = cursor;

        auto two = [&](char a, char b) {
            return cursor + 1 < source.size() && source[cursor] == a && source[cursor + 1] == b;
        };
        auto three = [&](char a, char b, char c) {
            return cursor + 2 < source.size() && source[cursor] == a && source[cursor + 1] == b && source[cursor + 2] == c;
        };

        if (three('.', '.', '=')) {
            cursor += 3;
            push(TokenKind::RangeInclusive, begin, cursor, "..=");
            continue;
        }
        if (two('.', '.')) {
            cursor += 2;
            push(TokenKind::Range, begin, cursor, "..");
            continue;
        }
        if (two('?', '.')) {
            cursor += 2;
            push(TokenKind::QuestionDot, begin, cursor, "?.");
            continue;
        }
        if (two('=', '=')) {
            cursor += 2;
            push(TokenKind::EqualEqual, begin, cursor, "==");
            continue;
        }
        if (two('!', '=')) {
            cursor += 2;
            push(TokenKind::BangEqual, begin, cursor, "!=");
            continue;
        }
        if (two('<', '=')) {
            cursor += 2;
            push(TokenKind::LessEqual, begin, cursor, "<=");
            continue;
        }
        if (two('>', '=')) {
            cursor += 2;
            push(TokenKind::GreaterEqual, begin, cursor, ">=");
            continue;
        }
        if (two('&', '&')) {
            cursor += 2;
            push(TokenKind::AmpAmp, begin, cursor, "&&");
            continue;
        }
        if (two('|', '|')) {
            cursor += 2;
            push(TokenKind::PipePipe, begin, cursor, "||");
            continue;
        }
        if (two('<', '<')) {
            cursor += 2;
            push(TokenKind::ShiftLeft, begin, cursor, "<<");
            continue;
        }
        if (two('>', '>')) {
            cursor += 2;
            push(TokenKind::ShiftRight, begin, cursor, ">>");
            continue;
        }
        if (two('-', '>')) {
            cursor += 2;
            push(TokenKind::Arrow, begin, cursor, "->");
            continue;
        }
        if (two('=', '>')) {
            cursor += 2;
            push(TokenKind::FatArrow, begin, cursor, "=>");
            continue;
        }

        ++cursor;
        switch (source[begin]) {
            case '(': push(TokenKind::LParen, begin, cursor, "("); break;
            case ')': push(TokenKind::RParen, begin, cursor, ")"); break;
            case '{': push(TokenKind::LBrace, begin, cursor, "{"); break;
            case '}': push(TokenKind::RBrace, begin, cursor, "}"); break;
            case '[': push(TokenKind::LBracket, begin, cursor, "["); break;
            case ']': push(TokenKind::RBracket, begin, cursor, "]"); break;
            case ',': push(TokenKind::Comma, begin, cursor, ","); break;
            case ';': push(TokenKind::Semicolon, begin, cursor, ";"); break;
            case '.': push(TokenKind::Dot, begin, cursor, "."); break;
            case '@': push(TokenKind::At, begin, cursor, "@"); break;
            case '#': push(TokenKind::Hash, begin, cursor, "#"); break;
            case '$': push(TokenKind::Dollar, begin, cursor, "$"); break;
            case '?': push(TokenKind::Question, begin, cursor, "?"); break;
            case '=': push(TokenKind::Equal, begin, cursor, "="); break;
            case '!': push(TokenKind::Bang, begin, cursor, "!"); break;
            case '<': push(TokenKind::Less, begin, cursor, "<"); break;
            case '>': push(TokenKind::Greater, begin, cursor, ">"); break;
            case '&': push(TokenKind::Ampersand, begin, cursor, "&"); break;
            case '|': push(TokenKind::Pipe, begin, cursor, "|"); break;
            case '^': push(TokenKind::Caret, begin, cursor, "^"); break;
            case '~': push(TokenKind::Tilde, begin, cursor, "~"); break;
            case '+': push(TokenKind::Plus, begin, cursor, "+"); break;
            case '-': push(TokenKind::Minus, begin, cursor, "-"); break;
            case '*': push(TokenKind::Star, begin, cursor, "*"); break;
            case '/': push(TokenKind::Slash, begin, cursor, "/"); break;
            case '%': push(TokenKind::Percent, begin, cursor, "%"); break;
            default:
                diagnostics_.error(sourceManager_.range(begin, cursor), "unexpected character in input");
                break;
        }
    }

    tokens.push_back(Token {TokenKind::EndOfFile, "", SourceRange {SourceLocation {source.size()}, SourceLocation {source.size()}}});
    return tokens;
}

}  // namespace axc
