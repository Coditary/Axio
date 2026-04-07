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

AXC_TEST(Parser_RecoversAfterInvalidBreakAndContinuePayloads) {
    auto dir = axc::unit::makeTempDir("parser_recovery_break_continue");
    const auto path = dir.path / "break_continue.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    while true {\n"
                                    "        break now\n"
                                    "        let afterBreak int = 1\n"
                                    "        continue later\n"
                                    "        let afterContinue int = 2\n"
                                    "        return afterBreak + afterContinue\n"
                                    "    }\n"
                                    "    switch 0 {\n"
                                    "        case 0 {\n"
                                    "            break done\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 7\n"
                                    "        }\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT(file.diagnostics.diagnostics().size() >= 3U);
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "break does not take a value", axc::DiagnosticSeverity::Error));
    AXC_EXPECT(axc::unit::hasDiagnosticContaining(file.diagnostics, "continue does not take a value", axc::DiagnosticSeverity::Error));

    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 3U);
    AXC_EXPECT_EQ(mainFn->body->statements[0]->kind, axc::StmtKind::While);
    AXC_EXPECT_EQ(mainFn->body->statements[1]->kind, axc::StmtKind::Switch);

    const auto& whileStmt = static_cast<const axc::WhileStmt&>(*mainFn->body->statements[0]);
    AXC_EXPECT_EQ(whileStmt.body->statements.size(), 5U);
    AXC_EXPECT_EQ(whileStmt.body->statements[0]->kind, axc::StmtKind::Break);
    AXC_EXPECT_EQ(whileStmt.body->statements[2]->kind, axc::StmtKind::Continue);

    const auto& switchStmt = static_cast<const axc::SwitchStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(switchStmt.cases.size(), 2U);
    AXC_EXPECT_EQ(switchStmt.cases[0].body->statements[0]->kind, axc::StmtKind::Break);
    AXC_EXPECT_EQ(switchStmt.cases[1].body->statements[0]->kind, axc::StmtKind::Return);
    return true;
}
