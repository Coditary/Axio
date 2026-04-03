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
