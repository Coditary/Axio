#pragma once

#include <string>

#include "axc/Support/SourceLocation.h"

namespace axc {

/// @brief Token kinds emitted by the lexer.
enum class TokenKind {
    EndOfFile,
    Newline,
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    CharLiteral,
    StringLiteral,

    KwFn,
    KwLet,
    KwConst,
    KwPub,
    KwPri,
    KwPackage,
    KwReturn,
    KwDefer,
    KwStruct,
    KwEnum,
    KwClass,
    KwImport,
    KwIf,
    KwElse,
    KwSwitch,
    KwCase,
    KwDefault,
    KwWhile,
    KwFor,
    KwDo,
    KwBreak,
    KwContinue,
    KwUnsigned,
    KwExtern,
    KwInt,
    KwVoid,
    KwStr,
    KwBool,
    KwTrue,
    KwFalse,
    KwI2,
    KwI8,
    KwI16,
    KwI32,
    KwI64,
    KwU8,
    KwU16,
    KwU32,
    KwU64,
    KwShort,
    KwLong,
    KwDouble,
    KwFloat,
    KwF8,
    KwF16,
    KwF32,
    KwF64,
    KwChar,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Colon,
    Semicolon,
    Dot,
    Equal,
    EqualEqual,
    PlusEqual,
    MinusEqual,
    StarEqual,
    SlashEqual,
    PercentEqual,
    AmpEqual,
    PipeEqual,
    CaretEqual,
    ShiftLeftEqual,
    ShiftRightEqual,
    Bang,
    BangEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Ampersand,
    AmpAmp,
    Pipe,
    PipePipe,
    Caret,
    Tilde,
    Plus,
    PlusPlus,
    Minus,
    MinusMinus,
    Star,
    Slash,
    Percent,
    ShiftLeft,
    ShiftRight,
};

/// @brief Single lexed token with original source spelling and range.
struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string lexeme {};
    SourceRange range {};
};

/// @brief Human-readable token name used in diagnostics and debug output.
const char* tokenKindName(TokenKind kind);

}  // namespace axc
