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

AXC_TEST(Parser_ParsesPackageAndImportBlockWithAlias) {
    auto dir = axc::unit::makeTempDir("parser_package_import_block");
    const auto path = dir.path / "module.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "package app.main\n"
                                    "import (\n"
                                    "  math.ops\n"
                                    "  point geom.point\n"
                                    ")\n"
                                    "fn main() int { return 0 }\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT_EQ(file.unit.packageName, std::string("app.main"));
    AXC_EXPECT_EQ(file.unit.declarations.size(), 3U);

    const auto* firstImport = axc::unit::findImport(file.unit, "math.ops");
    AXC_EXPECT(firstImport != nullptr);
    AXC_EXPECT(firstImport->alias.empty());

    const axc::ImportDecl* aliasedImport = nullptr;
    for (const auto& decl : file.unit.declarations) {
        if (decl->kind == axc::DeclKind::Import && decl->name == "geom.point") {
            aliasedImport = static_cast<const axc::ImportDecl*>(decl.get());
            break;
        }
    }
    AXC_EXPECT(aliasedImport != nullptr);
    AXC_EXPECT_EQ(aliasedImport->alias, std::string("point"));
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

AXC_TEST(Parser_ParsesPubConstImportsAndGlobals) {
    auto dir = axc::unit::makeTempDir("parser_pub_const");
    const auto path = dir.path / "module.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "package math.api\n"
                                    "pub import math.ops{add}\n"
                                    "pub const answer int = 42\n"
                                    "pub fn add(const lhs int, rhs int) int { return lhs + rhs }\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    AXC_EXPECT_EQ(file.unit.packageName, std::string("math.api"));
    AXC_EXPECT_EQ(file.unit.declarations.size(), 3U);

    const auto* importDecl = axc::unit::findImport(file.unit, "math.ops");
    AXC_EXPECT(importDecl != nullptr);
    AXC_EXPECT_EQ(importDecl->visibility, axc::Visibility::Public);
    AXC_EXPECT_EQ(importDecl->importedNames.size(), 1U);
    AXC_EXPECT_EQ(importDecl->importedNames[0], std::string("add"));

    const auto* addFn = axc::unit::findFunction(file.unit, "add");
    AXC_EXPECT(addFn != nullptr);
    AXC_EXPECT_EQ(addFn->visibility, axc::Visibility::Public);
    AXC_EXPECT_EQ(addFn->runtimeParameters.size(), 2U);
    AXC_EXPECT(addFn->runtimeParameters[0].isConst);

    return true;
}

AXC_TEST(Parser_ParsesInlineLlvmFunctions) {
    auto dir = axc::unit::makeTempDir("parser_inline_llvm");
    const auto path = dir.path / "llvm.ax";
    AXC_EXPECT(axc::unit::writeFile(path,
                                    "fn llvm add(a int, b int) int {\n"
                                    "entry:\n"
                                    "  %sum = add i32 %a, %b\n"
                                    "  ret i32 %sum\n"
                                    "}\n"));

    axc::unit::ParsedFile file;
    AXC_EXPECT(axc::unit::parseSource(file, path));
    const auto* addFn = axc::unit::findFunction(file.unit, "add");
    AXC_EXPECT(addFn != nullptr);
    AXC_EXPECT(addFn->isLlvm);
    AXC_EXPECT(addFn->body == nullptr);
    AXC_EXPECT_CONTAINS(addFn->llvmBody, "%sum = add i32 %a, %b");
    AXC_EXPECT_EQ(addFn->runtimeParameters.size(), 2U);
    AXC_EXPECT_EQ(addFn->returnTypes.size(), 1U);
    return true;
}
