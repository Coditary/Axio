#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Parser_ParsesDestructuringLetAndElseIfChains) {
    auto dir = axc::unit::makeTempDir("parser_stmt_if");
    const auto path = dir.path / "statements.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn pair() (int, int) {\n"
                                    "    return 1, 2\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let left int, right int = pair()\n"
                                    "    if left < right {\n"
                                    "        return left\n"
                                    "    } else if right < left {\n"
                                    "        return right\n"
                                    "    } else {\n"
                                    "        return 0\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 2U);

    const auto& letStmt = static_cast<const axc::LetStmt&>(*mainFn->body->statements[0]);
    AXC_EXPECT_EQ(letStmt.bindings.size(), 2U);
    AXC_EXPECT_EQ(letStmt.bindings[0].name, std::string("left"));
    AXC_EXPECT_EQ(letStmt.bindings[1].name, std::string("right"));
    AXC_EXPECT(letStmt.initializer != nullptr);
    AXC_EXPECT_EQ(letStmt.initializer->kind, axc::ExprKind::Call);

    const auto& ifStmt = static_cast<const axc::IfStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT(ifStmt.thenBlock != nullptr);
    AXC_EXPECT_EQ(ifStmt.thenBlock->statements.size(), 1U);
    AXC_EXPECT(ifStmt.elseBranch != nullptr);
    AXC_EXPECT_EQ(ifStmt.elseBranch->kind, axc::StmtKind::If);

    const auto& elseIf = static_cast<const axc::IfStmt&>(*ifStmt.elseBranch);
    AXC_EXPECT(elseIf.thenBlock != nullptr);
    AXC_EXPECT(elseIf.elseBranch != nullptr);
    AXC_EXPECT_EQ(elseIf.elseBranch->kind, axc::StmtKind::Compound);

    const auto& finalElse = static_cast<const axc::CompoundStmt&>(*elseIf.elseBranch);
    AXC_EXPECT_EQ(finalElse.statements.size(), 1U);
    AXC_EXPECT_EQ(finalElse.statements[0]->kind, axc::StmtKind::Return);
    return true;
}

AXC_TEST(Parser_ParsesBareAndMultiValueReturns) {
    auto dir = axc::unit::makeTempDir("parser_stmt_return");
    const auto path = dir.path / "returns.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn noop() {\n"
                                    "    return\n"
                                    "}\n"
                                    "fn pair() (int, int) {\n"
                                    "    return 1, 2\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));

    const auto* noopFn = axc::unit::findFunction(file.unit, "noop");
    AXC_EXPECT(noopFn != nullptr && noopFn->body != nullptr);
    AXC_EXPECT_EQ(noopFn->body->statements.size(), 1U);
    const auto& bareReturn = static_cast<const axc::ReturnStmt&>(*noopFn->body->statements[0]);
    AXC_EXPECT(bareReturn.values.empty());

    const auto* pairFn = axc::unit::findFunction(file.unit, "pair");
    AXC_EXPECT(pairFn != nullptr && pairFn->body != nullptr);
    AXC_EXPECT_EQ(pairFn->body->statements.size(), 1U);
    const auto& multiReturn = static_cast<const axc::ReturnStmt&>(*pairFn->body->statements[0]);
    AXC_EXPECT_EQ(multiReturn.values.size(), 2U);
    AXC_EXPECT_EQ(multiReturn.values[0]->kind, axc::ExprKind::IntegerLiteral);
    AXC_EXPECT_EQ(multiReturn.values[1]->kind, axc::ExprKind::IntegerLiteral);
    return true;
}

AXC_TEST(Parser_ParsesDeferStatementsInsideVoidFunctions) {
    auto dir = axc::unit::makeTempDir("parser_stmt_defer");
    const auto path = dir.path / "defer.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "extern fn cleanup() int;\n"
                                    "fn main() {\n"
                                    "    defer cleanup()\n"
                                    "    return\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));

    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT(mainFn->returnsVoid());
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 2U);
    AXC_EXPECT_EQ(mainFn->body->statements[0]->kind, axc::StmtKind::Defer);
    AXC_EXPECT_EQ(mainFn->body->statements[1]->kind, axc::StmtKind::Return);
    return true;
}

AXC_TEST(Parser_ParsesSwitchAndLoopStatements) {
    auto dir = axc::unit::makeTempDir("parser_stmt_switch_loops");
    const auto path = dir.path / "switch_loops.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let values int[] = {1, 2, 3}\n"
                                    "    let sum int = 0\n"
                                    "    while sum < 3 {\n"
                                    "        sum++\n"
                                    "    }\n"
                                    "    for let i int = 0; i < 3; i++ {\n"
                                    "        sum += i\n"
                                    "    }\n"
                                    "    foreach value in values {\n"
                                    "        sum += value\n"
                                    "    }\n"
                                    "    do {\n"
                                    "        sum--\n"
                                    "    } while sum > 3\n"
                                    "    switch sum {\n"
                                    "        case 0, 1 {\n"
                                    "            return 0\n"
                                    "        }\n"
                                    "        case 2 {\n"
                                    "            return 2\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return sum\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 7U);
    AXC_EXPECT_EQ(mainFn->body->statements[2]->kind, axc::StmtKind::While);
    AXC_EXPECT_EQ(mainFn->body->statements[3]->kind, axc::StmtKind::For);
    AXC_EXPECT_EQ(mainFn->body->statements[4]->kind, axc::StmtKind::Foreach);
    AXC_EXPECT_EQ(mainFn->body->statements[5]->kind, axc::StmtKind::DoWhile);
    AXC_EXPECT_EQ(mainFn->body->statements[6]->kind, axc::StmtKind::Switch);

    const auto& switchStmt = static_cast<const axc::SwitchStmt&>(*mainFn->body->statements[6]);
    AXC_EXPECT_EQ(switchStmt.cases.size(), 3U);
    AXC_EXPECT_EQ(switchStmt.cases[0].patterns.size(), 2U);
    AXC_EXPECT(switchStmt.cases[2].isDefault);
    return true;
}

AXC_TEST(Parser_ParsesBreakAndContinueInsideLoopsAndSwitch) {
    auto dir = axc::unit::makeTempDir("parser_stmt_break_continue");
    const auto path = dir.path / "break_continue.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let sum int = 0\n"
                                    "    while true {\n"
                                    "        sum++\n"
                                    "        if sum == 2 {\n"
                                    "            continue\n"
                                    "        }\n"
                                    "        if sum == 4 {\n"
                                    "            break\n"
                                    "        }\n"
                                    "    }\n"
                                    "    switch sum {\n"
                                    "        case 4 {\n"
                                    "            break\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 0\n"
                                    "        }\n"
                                    "    }\n"
                                    "    return sum\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements[1]->kind, axc::StmtKind::While);
    AXC_EXPECT_EQ(mainFn->body->statements[2]->kind, axc::StmtKind::Switch);

    const auto& whileStmt = static_cast<const axc::WhileStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(whileStmt.body->statements[1]->kind, axc::StmtKind::If);
    AXC_EXPECT_EQ(whileStmt.body->statements[2]->kind, axc::StmtKind::If);

    const auto& continueIf = static_cast<const axc::IfStmt&>(*whileStmt.body->statements[1]);
    AXC_EXPECT_EQ(continueIf.thenBlock->statements[0]->kind, axc::StmtKind::Continue);

    const auto& breakIf = static_cast<const axc::IfStmt&>(*whileStmt.body->statements[2]);
    AXC_EXPECT_EQ(breakIf.thenBlock->statements[0]->kind, axc::StmtKind::Break);

    const auto& switchStmt = static_cast<const axc::SwitchStmt&>(*mainFn->body->statements[2]);
    AXC_EXPECT_EQ(switchStmt.cases[0].body->statements[0]->kind, axc::StmtKind::Break);
    return true;
}

AXC_TEST(Parser_ParsesSwitchRangeCases) {
    auto dir = axc::unit::makeTempDir("parser_stmt_switch_ranges");
    const auto path = dir.path / "switch_ranges.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "fn main(color Color, value int) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red..=Color.Green {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 2\n"
                                    "        }\n"
                                    "    }\n"
                                    "    switch value {\n"
                                    "        case 1..3, 7 {\n"
                                    "            return 3\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 4\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 2U);

    const auto& enumSwitch = static_cast<const axc::SwitchStmt&>(*mainFn->body->statements[0]);
    AXC_EXPECT_EQ(enumSwitch.cases[0].patterns.size(), 1U);
    AXC_EXPECT(enumSwitch.cases[0].patterns[0].isRange);
    AXC_EXPECT_EQ(enumSwitch.cases[0].patterns[0].value->kind, axc::ExprKind::Range);
    AXC_EXPECT(static_cast<const axc::RangeExpr&>(*enumSwitch.cases[0].patterns[0].value).inclusive);

    const auto& intSwitch = static_cast<const axc::SwitchStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(intSwitch.cases[0].patterns.size(), 2U);
    AXC_EXPECT(intSwitch.cases[0].patterns[0].isRange);
    AXC_EXPECT(!intSwitch.cases[0].patterns[1].isRange);
    return true;
}
