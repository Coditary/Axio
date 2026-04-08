#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Parser_ParsesCoreMvpDeclarations) {
    auto dir = axc::unit::makeTempDir("parser_mvp_core");
    const auto path = dir.path / "core.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "package app.main\n"
                                    "pub import util.math{add}\n"
                                    "pri const hidden int = 7\n"
                                    "pub struct Vec2 {\n"
                                    "    x int\n"
                                    "    y int\n"
                                    "}\n"
                                    "pub enum Color {\n"
                                    "    Red,\n"
                                    "    Green,\n"
                                    "}\n"
                                    "pub class Counter {\n"
                                    "    value int\n"
                                    "    pub fn inc(step int) int {\n"
                                    "        return value + step\n"
                                    "    }\n"
                                    "}\n"
                                    "extern fn puts(msg str) int\n"
                                    "pub fn main() int {\n"
                                    "    return 0\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    AXC_EXPECT_EQ(file.unit.packageName, std::string("app.main"));
    AXC_EXPECT(axc::unit::findImport(file.unit, "util.math") != nullptr);
    AXC_EXPECT(axc::unit::findStruct(file.unit, "Vec2") != nullptr);
    AXC_EXPECT(axc::unit::findEnum(file.unit, "Color") != nullptr);
    AXC_EXPECT(axc::unit::findClass(file.unit, "Counter") != nullptr);
    AXC_EXPECT(axc::unit::findFunction(file.unit, "puts") != nullptr);
    AXC_EXPECT(axc::unit::findFunction(file.unit, "main") != nullptr);
    return true;
}

AXC_TEST(Parser_ParsesControlFlowAndArrays) {
    auto dir = axc::unit::makeTempDir("parser_mvp_stmt");
    const auto path = dir.path / "stmt.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn main() int {\n"
                                    "    let items int[] = [1, 2, 3]\n"
                                    "    let acc int = 0\n"
                                    "    let i int = 0\n"
                                    "    while i < len(items) {\n"
                                    "        acc += 1\n"
                                    "        i++\n"
                                    "    }\n"
                                    "    for let j int = 0; j < 2; j++ {\n"
                                    "        acc += j\n"
                                    "    }\n"
                                    "    do {\n"
                                    "        acc += 1\n"
                                    "    } while false\n"
                                    "    switch acc {\n"
                                    "        case 1 { acc += 1 }\n"
                                    "        default { acc += 2 }\n"
                                    "    }\n"
                                    "    defer sink(acc)\n"
                                    "    return acc\n"
                                    "}\n"
                                    "extern fn sink(value int) int\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT(!file.diagnostics.hasErrors());
    const axc::FunctionDecl* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr);
    AXC_EXPECT(mainFn->body != nullptr);
    AXC_EXPECT(!mainFn->returnsVoid());
    return true;
}
