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
                                    "    if ptr?.call() && value > 0 {\n"
                                    "        return log{3}(value)\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::lexSource(file, path));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::Arrow));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::QuestionDot));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::AmpAmp));
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

AXC_TEST(Lexer_TokenizesDeferAndInlineLlvmBodies) {
    auto dir = axc::unit::makeTempDir("lexer_defer_llvm");
    const auto path = dir.path / "defer_llvm.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn llvm add(a int, b int) int {\n"
                                    "entry:\n"
                                    "  %sum = add i32 %a, %b\n"
                                    "  ret i32 %sum\n"
                                    "}\n"
                                    "fn main() {\n"
                                    "    defer cleanup()\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::lexSource(file, path));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::KwDefer));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::KwLlvm));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::LlvmBlock));
    return true;
}

AXC_TEST(Lexer_TokenizesScientificNotationUnsignedAndMutationOperators) {
    auto dir = axc::unit::makeTempDir("lexer_scientific_mutation");
    const auto path = dir.path / "scientific.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let lower f64 = 35e3\n"
                                    "    let upper f64 = 35E-2\n"
                                    "    let count unsigned i32 = 1\n"
                                    "    count += int(lower)\n"
                                    "    count <<= 2\n"
                                    "    count++\n"
                                    "    --count\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::lexSource(file, path));

    bool foundLower = false;
    bool foundUpper = false;
    for (const auto& token : file.tokens) {
        if (token.kind == axc::TokenKind::FloatLiteral && token.lexeme == "35e3") {
            foundLower = true;
        }
        if (token.kind == axc::TokenKind::FloatLiteral && token.lexeme == "35E-2") {
            foundUpper = true;
        }
    }

    AXC_EXPECT(foundLower);
    AXC_EXPECT(foundUpper);
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::KwUnsigned));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::PlusEqual));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::ShiftLeftEqual));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::PlusPlus));
    AXC_EXPECT(hasTokenKind(file.tokens, axc::TokenKind::MinusMinus));
    return true;
}
