#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Compiler_ResolvesImportedQualifiedFunctionsInCheckOnlyMode) {
    auto dir = axc::unit::makeTempDir("compiler_import_fn");
    const auto modulePath = dir.path / "math" / "ops.ax";
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(modulePath,
                                    "package math.ops\n"
                                    "pub fn add(x int, y int) int {\n"
                                    "    return x + y\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import math.ops\n"
                                    "fn main() int {\n"
                                    "    return math.ops.add(20, 22)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_DoesNotInjectBareModuleImportsIntoLocalScope) {
    auto dir = axc::unit::makeTempDir("compiler_bare_import_scope");
    const auto modulePath = dir.path / "math" / "ops.ax";
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(modulePath,
                                    "package math.ops\n"
                                    "pub fn add(x int, y int) int {\n"
                                    "    return x + y\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import math.ops\n"
                                    "fn main() int {\n"
                                    "    return add(20, 22)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_EmitsLlvmIrForImportedQualifiedStructs) {
    auto dir = axc::unit::makeTempDir("compiler_import_struct");
    const auto modulePath = dir.path / "geom" / "point.ax";
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "out";
    AXC_EXPECT(axc::unit::writeFile(modulePath,
                                    "package geom.point\n"
                                    "pub struct Point {\n"
                                    "    x int\n"
                                    "    y int\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import geom.point\n"
                                    "fn main() int {\n"
                                    "    let p geom.point.Point = geom.point.Point(7, 9)\n"
                                    "    return p.x\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    AXC_EXPECT(std::filesystem::exists(outputBase.string() + ".ll"));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    AXC_EXPECT_CONTAINS(ir, "%geom.point.Point");
    return true;
}

AXC_TEST(Compiler_RejectsRemovedCompileFunctionExpressions) {
    auto dir = axc::unit::makeTempDir("compiler_readfile_removed");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "extern fn puts(text str) int;\n"
                                    "fn main() int {\n"
                                    "    puts($readfile(\"assets/banner.txt\"))\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_FailsCheckOnlyWhenMetaValidationReportsErrors) {
    auto dir = axc::unit::makeTempDir("compiler_meta_error");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn main() int {\n"
                                    "    $readfile(42)\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_FailsCheckOnlyWhenImportedModuleIsMissing) {
    auto dir = axc::unit::makeTempDir("compiler_missing_import");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import missing.module\n"
                                    "fn main() int {\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_RunsScientificCastsMutationsAndBoolCoercions) {
    auto dir = axc::unit::makeTempDir("compiler_scientific_casts_mutations");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "scientific_mutations";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn main() int {\n"
                                    "    let base unsigned i32 = 1\n"
                                    "    let scale f64 = 3.5e1\n"
                                    "    let narrowed int = (scale / 10.0) as int\n"
                                    "    let flag bool = 1\n"
                                    "    base += narrowed\n"
                                    "    base <<= 1\n"
                                    "    base++\n"
                                    "    --base\n"
                                    "    if flag && (2 * (3 + 4) == 14) {\n"
                                    "        return base\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToBinary(mainPath, outputBase));
    const auto exitCode = axc::unit::runBinary(outputBase);
    AXC_EXPECT(exitCode.has_value());
    AXC_EXPECT_EQ(*exitCode, 8);
    return true;
}

AXC_TEST(Compiler_RunsNestedIfsWithRecursionAndScopeIsolation) {
    auto dir = axc::unit::makeTempDir("compiler_nested_if_recursion_scope");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "nested_if_recursion_scope";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn fact(n int) int {\n"
                                    "    if n == 0 {\n"
                                    "        return 1\n"
                                    "    }\n"
                                    "    return n * fact(n - 1)\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let value int = 5\n"
                                    "    if value > 0 {\n"
                                    "        if value > 3 {\n"
                                    "            let inner int = fact(3)\n"
                                    "            return inner\n"
                                    "        }\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToBinary(mainPath, outputBase));
    const auto exitCode = axc::unit::runBinary(outputBase);
    AXC_EXPECT(exitCode.has_value());
    AXC_EXPECT_EQ(*exitCode, 6);
    return true;
}

AXC_TEST(Compiler_CliHelpPrintsStructuredUsage) {
    const std::string command = std::string("/home/leodora/Documents/Dev/AI/Axio/build/axc --help > ") + "/tmp/axc_help_output.txt";
    AXC_EXPECT(std::system(command.c_str()) == 0);
    const std::string output = axc::unit::readFile("/tmp/axc_help_output.txt");
    AXC_EXPECT_CONTAINS(output, "usage: axc <input.ax>");
    AXC_EXPECT_CONTAINS(output, "--check-only");
    AXC_EXPECT_CONTAINS(output, "--dump-ast");
    return true;
}

AXC_TEST(Compiler_RunsArrayLiteralAndLenBuiltin) {
    auto dir = axc::unit::makeTempDir("compiler_array_len");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "array_len";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn main() int {\n"
                                    "    let values int[] = {5, 9, 7, 167}\n"
                                    "    return len(values)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToBinary(mainPath, outputBase));
    const auto exitCode = axc::unit::runBinary(outputBase);
    AXC_EXPECT(exitCode.has_value());
    AXC_EXPECT_EQ(*exitCode, 4);
    return true;
}

AXC_TEST(Compiler_RunsSwitchAndLoopControlFlow) {
    auto dir = axc::unit::makeTempDir("compiler_switch_loops");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "switch_loops";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn main() int {\n"
                                    "    let sum int = 0\n"
                                    "    while sum < 3 {\n"
                                    "        sum++\n"
                                    "    }\n"
                                    "    for let i int = 0; i < 3; i++ {\n"
                                    "        sum += i\n"
                                    "    }\n"
                                    "    do {\n"
                                    "        sum--\n"
                                    "    } while sum > 6\n"
                                    "    switch sum {\n"
                                    "        case 0, 1 {\n"
                                    "            return 0\n"
                                    "        }\n"
                                    "        case 5 {\n"
                                    "            return 5\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return sum\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToBinary(mainPath, outputBase));
    const auto exitCode = axc::unit::runBinary(outputBase);
    AXC_EXPECT(exitCode.has_value());
    AXC_EXPECT_EQ(*exitCode, 5);
    return true;
}

AXC_TEST(Compiler_RunsBreakAndContinueControlFlow) {
    auto dir = axc::unit::makeTempDir("compiler_break_continue");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "break_continue";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn main() int {\n"
                                    "    let sum int = 0\n"
                                    "    let i int = 0\n"
                                    "    while true {\n"
                                    "        i++\n"
                                    "        if i == 2 {\n"
                                    "            continue\n"
                                    "        }\n"
                                    "        sum += i\n"
                                    "        if i == 4 {\n"
                                    "            break\n"
                                    "        }\n"
                                    "    }\n"
                                    "    switch sum {\n"
                                    "        case 8 {\n"
                                    "            sum += 5\n"
                                    "            break\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 0\n"
                                    "        }\n"
                                    "    }\n"
                                    "    return sum\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToBinary(mainPath, outputBase));
    const auto exitCode = axc::unit::runBinary(outputBase);
    AXC_EXPECT(exitCode.has_value());
    AXC_EXPECT_EQ(*exitCode, 13);
    return true;
}

AXC_TEST(Compiler_EmitsDeferredCallsForEachReturnPath) {
    auto dir = axc::unit::makeTempDir("compiler_defer_branch_returns");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "defer_branch_returns";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "extern fn cleanup() int;\n"
                                    "fn main() int {\n"
                                    "    defer cleanup()\n"
                                    "    if false {\n"
                                    "        return 1\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = ir.find("call i32 @cleanup()", pos)) != std::string::npos) {
        ++count;
        pos += 1;
    }
    AXC_EXPECT_EQ(count, 2U);
    return true;
}

AXC_TEST(Compiler_RejectsRemovedSwitchRangeCases) {
    auto dir = axc::unit::makeTempDir("compiler_switch_ranges_removed");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "fn classify(color Color, value int) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red..=Color.Green {\n"
                                    "            return 10\n"
                                    "        }\n"
                                    "        case Color.Blue {\n"
                                    "            return 20\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let score int = classify(Color.Green, 0)\n"
                                    "    switch 5 {\n"
                                    "        case 1..3 {\n"
                                    "            return 0\n"
                                    "        }\n"
                                    "        case 4..=6 {\n"
                                    "            return score + 5\n"
                                    "        }\n"
                                    "        default {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_EmitsDenseEnumSwitchAsLlvmSwitch) {
    auto dir = axc::unit::makeTempDir("compiler_dense_enum_switch_ir");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "dense_enum_switch";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "fn score(color Color) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "        case Color.Green {\n"
                                    "            return 2\n"
                                    "        }\n"
                                    "        case Color.Blue {\n"
                                    "            return 3\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    AXC_EXPECT_CONTAINS(ir, "switch i32");
    return true;
}

AXC_TEST(Compiler_RejectsRemovedEnumRangeSwitchCases) {
    auto dir = axc::unit::makeTempDir("compiler_enum_range_switch_removed");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "    Yellow,\n"
                                    "}\n"
                                    "fn score(color Color) int {\n"
                                    "    switch color {\n"
                                    "        case Color.Red..=Color.Blue {\n"
                                    "            return 1\n"
                                    "        }\n"
                                    "        case Color.Yellow {\n"
                                    "            return 2\n"
                                    "        }\n"
                                    "    }\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_RejectsUsingBlockScopedVariableOutsideIf) {
    auto dir = axc::unit::makeTempDir("compiler_scope_if_error");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn main() int {\n"
                                    "    if true {\n"
                                    "        let inner int = 7\n"
                                    "        return inner\n"
                                    "    }\n"
                                    "    return inner\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_CheckOnlyAcceptsNestedContainerTypeCombinations) {
    auto dir = axc::unit::makeTempDir("compiler_nested_container_types");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "struct Vec2 {\n"
                                    "    x int\n"
                                    "    y int\n"
                                    "}\n"
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "class User {\n"
                                    "    favorite Color\n"
                                    "    home Vec2\n"
                                    "}\n"
                                    "struct Holder {\n"
                                    "    owner User\n"
                                    "    point Vec2\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let user = User(Color.Red, Vec2(3, 4))\n"
                                    "    let holder Holder = Holder(user, Vec2(1, 2))\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_RunsStructAndEnumEqualityAndMethodGlobalNameCoexistence) {
    auto dir = axc::unit::makeTempDir("compiler_equality_and_name_coexistence");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "equality_and_name_coexistence";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "struct Pair {\n"
                                    "    left int\n"
                                    "    right int\n"
                                    "}\n"
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "class User {\n"
                                    "    value int\n"
                                    "    fn score() int {\n"
                                    "        return self.value + 1\n"
                                    "    }\n"
                                    "}\n"
                                    "fn score(value int) int {\n"
                                    "    return value + 2\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let a Pair = Pair(1, 2)\n"
                                    "    let b Pair = Pair(1, 2)\n"
                                    "    let user = User(4)\n"
                                    "    if a == b && Color.Red == Color.Red && user == user {\n"
                                    "        let methodScore int = user.score()\n"
                                    "        let globalScore int = score(4)\n"
                                    "        return methodScore + globalScore\n"
                                    "    }\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToBinary(mainPath, outputBase));
    const auto exitCode = axc::unit::runBinary(outputBase);
    AXC_EXPECT(exitCode.has_value());
    AXC_EXPECT_EQ(*exitCode, 11);
    return true;
}

AXC_TEST(Compiler_ResolvesTransitiveImportedQualifiedTypes) {
    auto dir = axc::unit::makeTempDir("compiler_transitive_imports");
    const auto leafPath = dir.path / "core" / "types" / "point.ax";
    const auto middlePath = dir.path / "geom" / "shapes.ax";
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "transitive";
    AXC_EXPECT(axc::unit::writeFile(leafPath,
                                    "package core.types.point\n"
                                    "pub struct Point {\n"
                                    "    x int\n"
                                    "    y int\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(middlePath,
                                    "package geom.shapes\n"
                                    "import core.types.point\n"
                                    "pub fn makePoint() core.types.point.Point {\n"
                                    "    return core.types.point.Point(4, 8)\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import geom.shapes\n"
                                    "fn main() int {\n"
                                    "    let point core.types.point.Point = geom.shapes.makePoint()\n"
                                    "    return point.y\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    AXC_EXPECT_CONTAINS(ir, "%core.types.point.Point");
    AXC_EXPECT_CONTAINS(ir, "@geom.shapes.makePoint");
    return true;
}

AXC_TEST(Compiler_CompilesCompileArgumentCallFormsToLlvmIr) {
    auto dir = axc::unit::makeTempDir("compiler_compile_args_ir");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "compile_args";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn add{lhs int}(rhs int) int {\n"
                                    "    return lhs + rhs\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let a int = add{3}(4)\n"
                                    "    let b int = add(5, a)\n"
                                    "    let c int = add{2, b}()\n"
                                    "    let d int = add{}(1, c)\n"
                                    "    return d\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    AXC_EXPECT_CONTAINS(ir, "define i32 @add(i32 %lhs, i32 %rhs)");
    AXC_EXPECT_CONTAINS(ir, "call i32 @add(i32 3, i32 4)");
    AXC_EXPECT_CONTAINS(ir, "call i32 @add(i32 5, i32");
    return true;
}

AXC_TEST(Compiler_CheckOnlyAcceptsExpandedShowcaseSample) {
    auto dir = axc::unit::makeTempDir("compiler_showcase_sample");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "enum Color() {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "    Blue,\n"
                                    "}\n"
                                    "class User {\n"
                                    "    name str\n"
                                    "    display str => \"Hi \" + name\n"
                                    "    fn greet() str { return display }\n"
                                    "}\n"
                                    "fn pair{bias int}(x int) (int, int) {\n"
                                    "    return x + bias, x + bias + 1\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let user User = new User(\"Axio\")\n"
                                    "    let left int, right int = pair{3}(4)\n"
                                    "    let greeting str = user.greet()\n"
                                    "    return left + right + Color.Green\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_SupportsSelectiveImportsAndReexports) {
    auto dir = axc::unit::makeTempDir("compiler_reexport_imports");
    const auto leafPath = dir.path / "math" / "ops.ax";
    const auto facadePath = dir.path / "math" / "api.ax";
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(leafPath,
                                    "package math.ops\n"
                                    "pub fn add(lhs int, rhs int) int {\n"
                                    "    return lhs + rhs\n"
                                    "}\n"
                                    "fn sub(lhs int, rhs int) int {\n"
                                    "    return lhs - rhs\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(facadePath,
                                    "package math.api\n"
                                    "pub import math.ops{add}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import math.api{add}\n"
                                    "fn main() int {\n"
                                    "    return add(20, 22)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_AllowsSelectiveImportsIntoLocalScope) {
    auto dir = axc::unit::makeTempDir("compiler_selective_import_scope");
    const auto modulePath = dir.path / "math" / "ops.ax";
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(modulePath,
                                    "package math.ops\n"
                                    "pub fn add(lhs int, rhs int) int {\n"
                                    "    return lhs + rhs\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import math.ops{add}\n"
                                    "fn main() int {\n"
                                    "    return add(20, 22)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_RejectsImportingPrivateSymbols) {
    auto dir = axc::unit::makeTempDir("compiler_private_import");
    const auto modulePath = dir.path / "math" / "ops.ax";
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(modulePath,
                                    "package math.ops\n"
                                    "pub fn add(lhs int, rhs int) int {\n"
                                    "    return lhs + rhs\n"
                                    "}\n"
                                    "fn sub(lhs int, rhs int) int {\n"
                                    "    return lhs - rhs\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import math.ops{sub}\n"
                                    "fn main() int {\n"
                                    "    return sub(1, 2)\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_CompilesGlobalConstUsageToLlvmIr) {
    auto dir = axc::unit::makeTempDir("compiler_global_const");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "global_const";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "pub const answer int = 42\n"
                                    "fn main() int {\n"
                                    "    return answer\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    AXC_EXPECT_CONTAINS(ir, "answer");
    return true;
}

AXC_TEST(Compiler_SupportsImportBlocksAndAliases) {
    auto dir = axc::unit::makeTempDir("compiler_import_block_alias");
    const auto mathPath = dir.path / "math" / "ops.ax";
    const auto geomPath = dir.path / "geom" / "point.ax";
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mathPath,
                                    "package math.ops\n"
                                    "pub fn add(lhs int, rhs int) int {\n"
                                    "    return lhs + rhs\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(geomPath,
                                    "package geom.point\n"
                                    "pub struct Point {\n"
                                    "    x int\n"
                                    "    y int\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "import (\n"
                                    "  math.ops\n"
                                    "  pt geom.point\n"
                                    ")\n"
                                    "fn main() int {\n"
                                    "    let point pt.Point = pt.Point(20, 22)\n"
                                    "    return add(point.x, point.y)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_AllowsImplicitVoidFunctionsAndBareReturn) {
    auto dir = axc::unit::makeTempDir("compiler_implicit_void");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn helper() {\n"
                                    "    return\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    helper()\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_EmitsDeferredCallsBeforeReturning) {
    auto dir = axc::unit::makeTempDir("compiler_defer_ir");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "defer";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "extern fn cleanup() int;\n"
                                    "fn main() {\n"
                                    "    defer cleanup()\n"
                                    "    return\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    AXC_EXPECT_CONTAINS(ir, "call i32 @cleanup()");
    return true;
}

AXC_TEST(Compiler_CheckOnlyAcceptsValidInlineLlvmFunctions) {
    auto dir = axc::unit::makeTempDir("compiler_inline_llvm_valid");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn llvm add(a int, b int) int {\n"
                                    "entry:\n"
                                    "  %sum = add i32 %a, %b\n"
                                    "  ret i32 %sum\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    return add(20, 22)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_RejectsInvalidInlineLlvmFunctions) {
    auto dir = axc::unit::makeTempDir("compiler_inline_llvm_invalid");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn llvm broken(a int, b int) int {\n"
                                    "entry:\n"
                                    "  %sum = add i32 %a, %b\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_EmitsInlineLlvmFunctionBodiesToIr) {
    auto dir = axc::unit::makeTempDir("compiler_inline_llvm_ir");
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "inline_llvm";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "package main\n"
                                    "fn llvm add(a int, b int) int {\n"
                                    "entry:\n"
                                    "  %sum = add i32 %a, %b\n"
                                    "  ret i32 %sum\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    return add(20, 22)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    AXC_EXPECT_CONTAINS(ir, "define i32 @add(i32 %a, i32 %b)");
    AXC_EXPECT_CONTAINS(ir, "%sum = add i32 %a, %b");
    return true;
}
