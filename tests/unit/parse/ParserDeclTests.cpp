#include "framework/TestRegistry.h"
#include "support/TestSupport.h"

AXC_TEST(Parser_ParsesImportsAndAnnotations) {
    auto dir = axc::unit::makeTempDir("parser_imports");
    const auto path = dir.path / "module.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "import math.geometry\n"
                                    "@inline\n"
                                    "fn main() int { return 0 }\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT_EQ(file.unit.declarations.size(), 2U);

    const auto* importDecl = axc::unit::findImport(file.unit, "math.geometry");
    AXC_EXPECT(importDecl != nullptr);
    AXC_EXPECT_EQ(importDecl->moduleSegments.size(), 2U);
    AXC_EXPECT_EQ(importDecl->moduleSegments[0], std::string("math"));
    AXC_EXPECT_EQ(importDecl->moduleSegments[1], std::string("geometry"));

    const auto* mainFn = axc::unit::findFunction(file.unit, "main");
    AXC_EXPECT(mainFn != nullptr);
    AXC_EXPECT_EQ(mainFn->annotations.size(), 1U);
    AXC_EXPECT_EQ(mainFn->annotations[0].name, std::string("inline"));
    return true;
}

AXC_TEST(Parser_ParsesClassIncludesAndMethods) {
    auto dir = axc::unit::makeTempDir("parser_class");
    const auto path = dir.path / "class.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "struct Base {\n"
                                    "    age int\n"
                                    "}\n"
                                    "class User {\n"
                                    "    Base\n"
                                    "    fn getAge() int {\n"
                                    "        return self.age\n"
                                    "    }\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* userClass = axc::unit::findClass(file.unit, "User");
    AXC_EXPECT(userClass != nullptr);
    AXC_EXPECT_EQ(userClass->includedStructs.size(), 1U);
    AXC_EXPECT_EQ(userClass->includedStructs[0], std::string("Base"));
    AXC_EXPECT_EQ(userClass->methods.size(), 1U);

    const auto& method = static_cast<const axc::FunctionDecl&>(*userClass->methods[0]);
    AXC_EXPECT_EQ(method.receiverType, std::string("User"));
    AXC_EXPECT_EQ(method.runtimeParameters.size(), 1U);
    AXC_EXPECT_EQ(method.runtimeParameters[0].name, std::string("self"));
    return true;
}

AXC_TEST(Parser_ParsesEnumParametersAndPayloadValues) {
    auto dir = axc::unit::makeTempDir("parser_enum");
    const auto path = dir.path / "enum.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "enum DistanceBand(label int, mult int) {\n"
                                    "    Near(1, 1),\n"
                                    "    Far(3, 4),\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* enumDecl = axc::unit::findEnum(file.unit, "DistanceBand");
    AXC_EXPECT(enumDecl != nullptr);
    AXC_EXPECT_EQ(enumDecl->parameters.size(), 2U);
    AXC_EXPECT_EQ(enumDecl->elements.size(), 2U);
    AXC_EXPECT_EQ(enumDecl->elements[0].payloadValues.size(), 2U);
    AXC_EXPECT(enumDecl->elements[0].payloadTypes.empty());
    return true;
}
