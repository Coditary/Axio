#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

namespace {

bool hasTokenKind(const std::vector<axc::Token>& tokens, axc::TokenKind kind) {
    for (const auto& token : tokens) {
        if (token.kind == kind) {
            return true;
        }
    }
    return false;
}

bool hasIdentifierLexeme(const std::vector<axc::Token>& tokens, std::string_view lexeme) {
    for (const auto& token : tokens) {
        if (token.kind == axc::TokenKind::Identifier && token.lexeme == lexeme) {
            return true;
        }
    }
    return false;
}

}  // namespace

AXC_TEST(Lexer_TokenizesOperatorsAndCompileSyntax) {
    auto dir = axc::unit::makeTempDir("lexer_ops");
    const auto path = dir.path / "ops.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let value int = 1->inc\n"
                                    "    if ptr?.call() && 3 in 1..=5 {\n"
                                    "        return log{3}(value)\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::lexSource(file, path));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::Arrow));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::QuestionDot));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::AmpAmp));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::RangeInclusive));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::KwIn));
    return true;
}

AXC_TEST(Lexer_SkipsCommentsAndCapturesDialectBlocks) {
    auto dir = axc::unit::makeTempDir("lexer_dialect");
    const auto path = dir.path / "dialect.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "// comment\n"
                                    "fn main() int {\n"
                                    "    /* block */\n"
                                    "    let sql str = %%SQL{\nSELECT 1\n}%%\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::lexSource(file, path));
    bool foundDialect = false;
    for (const auto& token : file.tokens) {
        if (token.kind == axc::TokenKind::DialectBlock) {
            foundDialect = true;
            AXC_EXPECT_CONTAINS(token.lexeme, "SQL");
            AXC_EXPECT_CONTAINS(token.lexeme, "SELECT 1");
        }
    }
    AXC_EXPECT(foundDialect);
    return true;
}

AXC_TEST(Lexer_PreservesUnicodeIdentifiers) {
    auto dir = axc::unit::makeTempDir("lexer_unicode");
    const auto path = dir.path / "unicode.ax";
    AXC_EXPECT(axc::unit::writeFile(path, "fn main() int { let länge int = 3\n return länge }\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::lexSource(file, path));
    AXC_EXPECT(hasIdentifierLexeme(file.tokens, "länge"));
    return true;
}

AXC_TEST(Lexer_TokenizesEmptyCompileArgumentCallsAndFlagMembers) {
    auto dir = axc::unit::makeTempDir("lexer_compile_empty");
    const auto path = dir.path / "compile_empty.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn log{level int}(value int) int { return value }\n"
                                    "fn main() int {\n"
                                    "    let a int = log{}(3, 4)\n"
                                    "    let s ActorState = ActorState{Team.Blue}\n"
                                    "    return a\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::lexSource(file, path));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::LBrace));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::RBrace));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::Dot));
    return true;
}
