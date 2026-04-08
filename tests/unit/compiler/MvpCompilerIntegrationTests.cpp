#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

#include <filesystem>

AXC_TEST(Compiler_CheckOnlyHandlesModuleImportsAndCycles) {
    auto dir = axc::unit::makeTempDir("compiler_mvp_modules");
    const auto app = dir.path / "app.ax";
    const auto foo = dir.path / "foo" / "bar.ax";
    const auto other = dir.path / "foo" / "other.ax";

    AXC_EXPECT(axc::unit::writeFile(app,
                                    "package app\n"
                                    "import foo.bar{value}\n"
                                    "fn main() int {\n"
                                    "    return value\n"
                                    "}\n"));
    AXC_EXPECT(axc::unit::writeFile(foo,
                                    "package foo.bar\n"
                                    "pub import foo.other{helper}\n"
                                    "pub const value int = helper\n"));
    AXC_EXPECT(axc::unit::writeFile(other,
                                    "package foo.other\n"
                                    "pub import foo.bar{value}\n"
                                    "pub const helper int = 4\n"));

    AXC_EXPECT(axc::unit::compileCheckOnly(app));
    return true;
}

AXC_TEST(Compiler_EmitsLlvmForSimpleMvpProgram) {
    auto dir = axc::unit::makeTempDir("compiler_mvp_llvm");
    const auto input = dir.path / "main.ax";
    const auto output = dir.path / "out" / "program";

    AXC_EXPECT(axc::unit::writeFile(input,
                                    "package app\n"
                                    "fn add(a int, b int) int {\n"
                                    "    return a + b\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let values int[] = [1, 2, 3]\n"
                                    "    let total int = add(len(values), 4)\n"
                                    "    return total\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToLlvmIr(input, output));
    const auto irPath = std::filesystem::path(output).concat(".ll");
    const std::string ir = axc::unit::readFile(irPath);
    AXC_EXPECT(!ir.empty());
    AXC_EXPECT_CONTAINS(ir, "define i32 @main()");
    AXC_EXPECT_CONTAINS(ir, "define i32 @add");
    return true;
}

AXC_TEST(Compiler_ProducesRunnableBinaryForSimpleReturnValue) {
    auto dir = axc::unit::makeTempDir("compiler_mvp_binary");
    const auto input = dir.path / "main.ax";
    const auto output = dir.path / "bin" / "program";

    AXC_EXPECT(axc::unit::writeFile(input,
                                    "package app\n"
                                    "fn twice(x int) int {\n"
                                    "    return x * 2\n"
                                    "}\n"
                                    "fn main() int {\n"
                                    "    let value int = 21\n"
                                    "    return twice(value)\n"
                                    "}\n"));

    AXC_EXPECT(axc::unit::compileToBinary(input, output));
    const auto status = axc::unit::runBinary(output);
    AXC_EXPECT(status.has_value());
    AXC_EXPECT_EQ(*status, 42);
    return true;
}
