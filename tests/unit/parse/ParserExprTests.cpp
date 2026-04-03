#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Parser_ParsesPipeCallsAndCompileArguments) {
    auto dir = axc::unit::makeTempDir("parser_pipe");
    const auto path = dir.path / "pipe.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn inc(x int) int { return x + 1 }\n"
                                    "fn main() int {\n"
                                    "    let a int = 1->inc\n"
                                    "    let b int = inc{3}(a)\n"
                                    "    return b\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 3U);

    const auto& firstLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[0]);
    AXC_EXPECT(firstLet.initializer != nullptr);
    AXC_EXPECT_EQ(firstLet.initializer->kind, axc::ExprKind::Call);

    const auto& secondLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[1]);
    const auto& compileCall = static_cast<const axc::CallExpr&>(*secondLet.initializer);
    AXC_EXPECT_EQ(compileCall.compileArguments.size(), 1U);
    AXC_EXPECT_EQ(compileCall.runtimeArguments.size(), 1U);
    return true;
}

AXC_TEST(Parser_ParsesComparisonChainsAndRanges) {
    auto dir = axc::unit::makeTempDir("parser_ranges");
    const auto path = dir.path / "ranges.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let a int = 3\n"
                                    "    if a < 5 >= 1 { return 1 }\n"
                                    "    if a in 1..=5 { return 2 }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);

    const auto& firstIf = static_cast<const axc::IfStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(firstIf.condition->kind, axc::ExprKind::Binary);
    AXC_EXPECT_EQ(static_cast<const axc::BinaryExpr&>(*firstIf.condition).op, axc::BinaryOp::LogicalAnd);

    const auto& secondIf = static_cast<const axc::IfStmt&>(*mainFn->body->statements[2]);
    const auto& membership = static_cast<const axc::BinaryExpr&>(*secondIf.condition);
    AXC_EXPECT_EQ(membership.op, axc::BinaryOp::InRange);
    AXC_EXPECT_EQ(membership.rhs->kind, axc::ExprKind::Range);
    AXC_EXPECT(static_cast<const axc::RangeExpr&>(*membership.rhs).inclusive);
    return true;
}

AXC_TEST(Parser_ParsesNullabilityAndOwnershipForms) {
    auto dir = axc::unit::makeTempDir("parser_nullability");
    const auto path = dir.path / "nullability.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "class Obj { }\n"
                                    "fn main() int {\n"
                                    "    let ptr Obj = null\n"
                                    "    let weak = new weak Obj()\n"
                                    "    let unique = *Obj()\n"
                                    "    ptr?.call()\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);

    const auto& weakLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(weakLet.initializer->kind, axc::ExprKind::Initializer);
    AXC_EXPECT_EQ(static_cast<const axc::InitializerExpr&>(*weakLet.initializer).initKind, axc::InitKind::Weak);

    const auto& uniqueLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[2]);
    AXC_EXPECT_EQ(static_cast<const axc::InitializerExpr&>(*uniqueLet.initializer).initKind, axc::InitKind::Unique);

    const auto& exprStmt = static_cast<const axc::ExprStmt&>(*mainFn->body->statements[3]);
    AXC_EXPECT_EQ(exprStmt.expression->kind, axc::ExprKind::Call);
    AXC_EXPECT(static_cast<const axc::CallExpr&>(*exprStmt.expression).callee->kind == axc::ExprKind::Member);
    return true;
}

AXC_TEST(Parser_ParsesAllCompileArgumentCallForms) {
    auto dir = axc::unit::makeTempDir("parser_compile_call_forms");
    const auto path = dir.path / "compile_forms.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn add{lhs int}(rhs int) int { return lhs + rhs }\n"
                                    "fn main() int {\n"
                                    "    let a int = add{3}(4)\n"
                                    "    let b int = add(5, a)\n"
                                    "    let c int = add{2, b}()\n"
                                    "    let d int = add{}(1, c)\n"
                                    "    return d\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 5U);

    const auto& firstCall = static_cast<const axc::CallExpr&>(*static_cast<const axc::LetStmt&>(*mainFn->body->statements[0]).initializer);
    AXC_EXPECT_EQ(firstCall.compileArguments.size(), 1U);
    AXC_EXPECT_EQ(firstCall.runtimeArguments.size(), 1U);

    const auto& secondCall = static_cast<const axc::CallExpr&>(*static_cast<const axc::LetStmt&>(*mainFn->body->statements[1]).initializer);
    AXC_EXPECT_EQ(secondCall.compileArguments.size(), 0U);
    AXC_EXPECT_EQ(secondCall.runtimeArguments.size(), 2U);

    const auto& thirdCall = static_cast<const axc::CallExpr&>(*static_cast<const axc::LetStmt&>(*mainFn->body->statements[2]).initializer);
    AXC_EXPECT_EQ(thirdCall.compileArguments.size(), 2U);
    AXC_EXPECT_EQ(thirdCall.runtimeArguments.size(), 0U);

    const auto& fourthCall = static_cast<const axc::CallExpr&>(*static_cast<const axc::LetStmt&>(*mainFn->body->statements[3]).initializer);
    AXC_EXPECT_EQ(fourthCall.compileArguments.size(), 0U);
    AXC_EXPECT_EQ(fourthCall.runtimeArguments.size(), 2U);
    return true;
}

AXC_TEST(Parser_ParsesAddressOfAndDereference) {
    auto dir = axc::unit::makeTempDir("parser_pointer_exprs");
    const auto path = dir.path / "pointer_exprs.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let value int = 1\n"
                                    "    let ptr int* = &value\n"
                                    "    return *ptr\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);

    const auto& ptrLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(ptrLet.initializer->kind, axc::ExprKind::Unary);
    AXC_EXPECT_EQ(static_cast<const axc::UnaryExpr&>(*ptrLet.initializer).op, axc::UnaryOp::AddressOf);

    const auto& ret = static_cast<const axc::ReturnStmt&>(*mainFn->body->statements[2]);
    AXC_EXPECT_EQ(ret.values[0]->kind, axc::ExprKind::Unary);
    AXC_EXPECT_EQ(static_cast<const axc::UnaryExpr&>(*ret.values[0]).op, axc::UnaryOp::Dereference);
    return true;
}
