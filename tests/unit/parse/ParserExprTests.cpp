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

AXC_TEST(Parser_ParsesComparisonChains) {
    auto dir = axc::unit::makeTempDir("parser_comparisons");
    const auto path = dir.path / "comparisons.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let a int = 3\n"
                                    "    if a < 5 >= 1 { return 1 }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);

    const auto& firstIf = static_cast<const axc::IfStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(firstIf.condition->kind, axc::ExprKind::Binary);
    AXC_EXPECT_EQ(static_cast<const axc::BinaryExpr&>(*firstIf.condition).op, axc::BinaryOp::LogicalAnd);

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

AXC_TEST(Parser_ParsesScientificUnsignedConversionsAndMutationOperators) {
    auto dir = axc::unit::makeTempDir("parser_scientific_unsigned_mutation");
    const auto path = dir.path / "scientific_mutation.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let count unsigned i32 = 1\n"
                                    "    let amount f64 = 35e3\n"
                                    "    let narrowed int = ((amount + 5) / 10) as int\n"
                                    "    count += narrowed\n"
                                    "    count <<= 1\n"
                                    "    count++\n"
                                    "    --count\n"
                                    "    return count\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);
    AXC_EXPECT_EQ(mainFn->body->statements.size(), 8U);

    const auto& countLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[0]);
    AXC_EXPECT_EQ(countLet.bindings[0].explicitType.name, std::string("u32"));

    const auto& amountLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(amountLet.initializer->kind, axc::ExprKind::FloatLiteral);

    const auto& narrowedLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[2]);
    AXC_EXPECT_EQ(narrowedLet.initializer->kind, axc::ExprKind::Cast);

    const auto& addAssign = static_cast<const axc::ExprStmt&>(*mainFn->body->statements[3]);
    AXC_EXPECT_EQ(addAssign.expression->kind, axc::ExprKind::Binary);
    AXC_EXPECT_EQ(static_cast<const axc::BinaryExpr&>(*addAssign.expression).op, axc::BinaryOp::Assign);
    AXC_EXPECT_EQ(static_cast<const axc::BinaryExpr&>(*static_cast<const axc::BinaryExpr&>(*addAssign.expression).rhs).op, axc::BinaryOp::Add);

    const auto& shiftAssign = static_cast<const axc::ExprStmt&>(*mainFn->body->statements[4]);
    AXC_EXPECT_EQ(static_cast<const axc::BinaryExpr&>(*shiftAssign.expression).op, axc::BinaryOp::Assign);
    AXC_EXPECT_EQ(static_cast<const axc::BinaryExpr&>(*static_cast<const axc::BinaryExpr&>(*shiftAssign.expression).rhs).op, axc::BinaryOp::ShiftLeft);

    const auto& postInc = static_cast<const axc::ExprStmt&>(*mainFn->body->statements[5]);
    AXC_EXPECT_EQ(postInc.expression->kind, axc::ExprKind::Unary);
    AXC_EXPECT_EQ(static_cast<const axc::UnaryExpr&>(*postInc.expression).op, axc::UnaryOp::PostIncrement);

    const auto& preDec = static_cast<const axc::ExprStmt&>(*mainFn->body->statements[6]);
    AXC_EXPECT_EQ(preDec.expression->kind, axc::ExprKind::Unary);
    AXC_EXPECT_EQ(static_cast<const axc::UnaryExpr&>(*preDec.expression).op, axc::UnaryOp::PreDecrement);
    return true;
}

AXC_TEST(Parser_ParsesParenthesizedArithmeticAndLogicalPrecedence) {
    auto dir = axc::unit::makeTempDir("parser_parenthesized_precedence");
    const auto path = dir.path / "precedence.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let math int = 2 * (3 + 4)\n"
                                    "    if (1 || 0) && (3 < 4) {\n"
                                    "        return math\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);

    const auto& mathLet = static_cast<const axc::LetStmt&>(*mainFn->body->statements[0]);
    const auto& mathExpr = static_cast<const axc::BinaryExpr&>(*mathLet.initializer);
    AXC_EXPECT_EQ(mathExpr.op, axc::BinaryOp::Mul);
    AXC_EXPECT_EQ(mathExpr.rhs->kind, axc::ExprKind::Binary);
    AXC_EXPECT_EQ(static_cast<const axc::BinaryExpr&>(*mathExpr.rhs).op, axc::BinaryOp::Add);

    const auto& ifStmt = static_cast<const axc::IfStmt&>(*mainFn->body->statements[1]);
    const auto& logical = static_cast<const axc::BinaryExpr&>(*ifStmt.condition);
    AXC_EXPECT_EQ(logical.op, axc::BinaryOp::LogicalAnd);
    AXC_EXPECT_EQ(static_cast<const axc::BinaryExpr&>(*logical.lhs).op, axc::BinaryOp::LogicalOr);
    return true;
}

AXC_TEST(Parser_ParsesBraceArrayLiteralAndLenCall) {
    auto dir = axc::unit::makeTempDir("parser_array_len");
    const auto path = dir.path / "array_len.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let values int[] = {5, 9, 7, 167}\n"
                                    "    return len(values)\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr && mainFn->body != nullptr);

    const auto& letStmt = static_cast<const axc::LetStmt&>(*mainFn->body->statements[0]);
    AXC_EXPECT_EQ(letStmt.bindings[0].explicitType.name, std::string("int"));
    AXC_EXPECT_EQ(letStmt.bindings[0].explicitType.arrayExtents.size(), 1U);
    AXC_EXPECT_EQ(letStmt.initializer->kind, axc::ExprKind::Initializer);
    AXC_EXPECT_EQ(static_cast<const axc::InitializerExpr&>(*letStmt.initializer).initKind, axc::InitKind::ArrayLiteral);

    const auto& ret = static_cast<const axc::ReturnStmt&>(*mainFn->body->statements[1]);
    AXC_EXPECT_EQ(ret.values[0]->kind, axc::ExprKind::Call);
    const auto& call = static_cast<const axc::CallExpr&>(*ret.values[0]);
    AXC_EXPECT_EQ(call.runtimeArguments.size(), 1U);
    AXC_EXPECT_EQ(call.callee->kind, axc::ExprKind::DeclRef);
    AXC_EXPECT_EQ(static_cast<const axc::DeclRefExpr&>(*call.callee).name, std::string("len"));
    return true;
}
