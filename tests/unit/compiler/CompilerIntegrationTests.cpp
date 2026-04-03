#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Compiler_ResolvesImportedQualifiedFunctionsInCheckOnlyMode) {
    auto dir = axc::unit::makeTempDir("compiler_import_fn");
    const auto modulePath = dir.path / "math" / "ops.ax";
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(modulePath,
                                    "fn add(x int, y int) int {\n"
                                    "    return x + y\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "import math.ops\n"
                                    "fn main() int {\n"
                                    "    return math.ops.add(20, 22)\n"
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
                                    "struct Point {\n"
                                    "    x int\n"
                                    "    y int\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
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

AXC_TEST(Compiler_EmbedsReadfileContentIntoLlvmIr) {
    auto dir = axc::unit::makeTempDir("compiler_readfile");
    const auto assetsPath = dir.path / "assets" / "banner.txt";
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "embedded";
    AXC_EXPECT(axc::unit::writeFile(assetsPath, "Axio embedded test banner.\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
                                    "extern fn puts(text str) int;\n"
                                    "fn main() int {\n"
                                    "    puts($readfile(\"assets/banner.txt\"))\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(mainPath, outputBase));
    const std::string ir = axc::unit::readFile(outputBase.string() + ".ll");
    AXC_EXPECT_CONTAINS(ir, "Axio embedded test banner.");
    return true;
}

AXC_TEST(Compiler_FailsCheckOnlyWhenMetaValidationReportsErrors) {
    auto dir = axc::unit::makeTempDir("compiler_meta_error");
    const auto mainPath = dir.path / "main.ax";
    AXC_EXPECT(axc::unit::writeFile(mainPath,
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
                                    "import missing.module\n"
                                    "fn main() int {\n"
                                    "    return 0\n"
                                    "}\n"));

    AXC_EXPECT(!axc::unit::compileCheckOnly(mainPath));
    return true;
}

AXC_TEST(Compiler_ResolvesTransitiveImportedQualifiedTypes) {
    auto dir = axc::unit::makeTempDir("compiler_transitive_imports");
    const auto leafPath = dir.path / "core" / "types" / "point.ax";
    const auto middlePath = dir.path / "geom" / "shapes.ax";
    const auto mainPath = dir.path / "main.ax";
    const auto outputBase = dir.path / "transitive";
    AXC_EXPECT(axc::unit::writeFile(leafPath,
                                    "struct Point {\n"
                                    "    x int\n"
                                    "    y int\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(middlePath,
                                    "import core.types.point\n"
                                    "fn makePoint() core.types.point.Point {\n"
                                    "    return core.types.point.Point(4, 8)\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(mainPath,
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
                                    "    let user = User(\"Axio\")\n"
                                    "    let left int, right int = pair{3}(4)\n"
                                    "    if user? {\n"
                                    "        user?.greet()\n"
                                    "    }\n"
                                    "    return left + right + Color.Green\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(mainPath));
    return true;
}
