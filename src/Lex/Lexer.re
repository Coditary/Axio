#include "axc/Lex/Lexer.h"

// This file intentionally documents the token grammar for re2c regeneration.
// The checked-in fallback lexer in src/Lex/Lexer.cpp keeps the project buildable
// on machines where re2c is not installed yet.

/*!re2c
    re2c:yyfill:enable = 0;

    WS = [ \t\n\r]+;
    ID = [A-Za-z_][A-Za-z0-9_]*;
    INT = [0-9]+;
    STR = '"' ([^"\\] | '\\' .)* '"';
*/
