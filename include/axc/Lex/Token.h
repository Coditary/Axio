#pragma once

#include <string>

#include "axc/Support/SourceLocation.h"

namespace axc {

enum class TokenKind {
    EndOfFile,
    Newline,
    Identifier,
    IntegerLiteral,
    FloatLiteral,
    CharLiteral,
    StringLiteral,
    DialectBlock,

    KwFn,
    KwLet,
    KwReturn,
    KwStruct,
    KwEnum,
    KwClass,
    KwImport,
    KwIf,
    KwElse,
    KwIn,
    KwAs,
    KwFlags,
    KwFlag,
    KwNew,
    KwWeak,
    KwRef,
    KwNull,
    KwExtern,
    KwAlign,
    KwBits,
    KwInt,
    KwVoid,
    KwStr,
    KwError,
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
    Semicolon,
    Dot,
    At,
    Hash,
    Dollar,
    Question,
    QuestionDot,
    Equal,
    EqualEqual,
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
    Minus,
    Star,
    Slash,
    Percent,
    ShiftLeft,
    ShiftRight,
    Arrow,
    Range,
    RangeInclusive,
    FatArrow,
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string lexeme {};
    SourceRange range {};
};

const char* tokenKindName(TokenKind kind);

}  // namespace axc
