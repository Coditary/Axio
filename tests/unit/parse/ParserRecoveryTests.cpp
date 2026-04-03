#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Parser_RecoversFromBrokenTopLevelAndKeepsFollowingDeclarations) {
    auto dir = axc::unit::makeTempDir("parser_recovery_top");
    const auto path = dir.path / "top_level.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn ok() int { return 1 }\n"
                                    "oops\n"
                                    "struct Pair {\n"
                                    "    left int\n"
                                    "    right int\n"
                                    "}\n"
                                    "fn alsoOk() int { return 2 }\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "expected top-level declaration", axc::DiagnosticSeverity::Error));
    AXC_EXPECT(axc::unit::findFunction(file.unit, "ok") != nullptr);
    AXC_EXPECT(axc::unit::findStruct(file.unit, "Pair") != nullptr);
    AXC_EXPECT(axc::unit::findFunction(file.unit, "alsoOk") != nullptr);
    AXC_EXPECT_EQ(file.unit.declarations.size(), 3U);
    return true;
}

AXC_TEST(Parser_RecoversWithinBlockAndReportsMultipleStatementErrors) {
    auto dir = axc::unit::makeTempDir("parser_recovery_stmt");
    const auto path = dir.path / "statements.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let = 1\n"
                                    "    let good int = 2\n"
                                    "    let , other int = 3\n"
                                    "    return good\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT_EQ(file.diagnostics.diagnostics().size(), 2U);
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "expected variable name after 'let'", axc::DiagnosticSeverity::Error));

    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 2U);

    const auto& goodLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[0]);
    AXC_EXPECT_EQ(goodLet.bindings.size(), 1U);
    AXC_EXPECT_EQ(goodLet.bindings[0].name, std::string("good"));

    const auto& ret = static_cast<const axc::ReturnStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(ret.values.size(), 1U);
    AXC_EXPECT_EQ(ret.values[0]->kind, axc::ExprKind::DeclRef);
    return true;
}

AXC_TEST(Parser_RecoversAfterBrokenCompileArgumentCall) {
    auto dir = axc::unit::makeTempDir("parser_recovery_compile_call");
    const auto path = dir.path / "compile_call.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn log{level int}(value int) int { return value }\n"
                                    "fn main() int {\n"
                                    "    let broken int = log{3}(\n"
                                    "    let good int = 4\n"
                                    "    return good\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "expected ')' after call arguments", axc::DiagnosticSeverity::Error));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT(!mainFn->body->statements.empty());
    return true;
}
