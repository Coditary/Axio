#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "axc/Driver/Compiler.h"
#include "axc/Lex/Lexer.h"
#include "axc/Parse/Parser.h"
#include "axc/Sema/Sema.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        return false;
    }
    return true;
}

bool loadFile(axc::SourceManager& sourceManager, const std::filesystem::path& path) {
    std::string error;
    if (!sourceManager.loadFromFile(path, error)) {
        std::cerr << error << "\n";
        return false;
    }
    return true;
}

bool writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path);
    if (!out) {
        std::cerr << "FAIL: could not write test file '" << path << "'\n";
        return false;
    }
    out << contents;
    return true;
}

std::filesystem::path makeTempTestDir(const std::string& name) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto unique = std::to_string(std::hash<std::string> {}(name + std::to_string(now)));
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / ("axio_" + name + "_" + unique);
    std::filesystem::create_directories(dir);
    return dir;
}

void removeTree(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

bool testSourceManagerLineColumn() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "axio_support_test.ax";
    {
        std::ofstream out(path);
        out << "fn main() int {\n";
        out << "    return 7\n";
        out << "}\n";
    }

    axc::SourceManager sourceManager;
    if (!loadFile(sourceManager, path)) {
        return false;
    }

    const axc::LineColumn lc = sourceManager.lineColumn(axc::SourceLocation {20});
    std::filesystem::remove(path);
    return expect(lc.line == 2, "lineColumn should map offset to second line") &&
           expect(lc.column >= 1, "lineColumn should produce a positive column");
}

bool testDiagnosticCollection() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "axio_diag_test.ax";
    {
        std::ofstream out(path);
        out << "let x = 1\n";
    }

    axc::SourceManager sourceManager;
    if (!loadFile(sourceManager, path)) {
        return false;
    }

    axc::DiagnosticEngine diagnostics(sourceManager);
    diagnostics.warning(sourceManager.range(0, 3), "sample warning");
    diagnostics.error(sourceManager.range(4, 5), "sample error");
    std::filesystem::remove(path);

    return expect(diagnostics.diagnostics().size() == 2, "diagnostic engine should store diagnostics") &&
           expect(diagnostics.hasErrors(), "diagnostic engine should report errors when an error exists");
}

axc::DiagnosticEngine runSemaOnFile(axc::SourceManager& sourceManager, const std::filesystem::path& path) {
    loadFile(sourceManager, path);
    axc::DiagnosticEngine diagnostics(sourceManager);
    axc::Lexer lexer(sourceManager, diagnostics);
    std::vector<axc::Token> tokens = lexer.lex();
    axc::Parser parser(std::move(tokens), diagnostics);
    axc::TranslationUnit unit = parser.parseTranslationUnit();
    axc::Sema sema(diagnostics);
    sema.analyze(unit);
    return diagnostics;
}

bool testClassMissingMemberDiagnostic() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "axio_sema_class_test.ax";
    {
        std::ofstream out(path);
        out << "class User {\n";
        out << "    name str\n";
        out << "}\n";
        out << "fn main() int {\n";
        out << "    let user = User()\n";
        out << "    return user.age\n";
        out << "}\n";
    }

    axc::SourceManager sourceManager;
    axc::DiagnosticEngine diagnostics = runSemaOnFile(sourceManager, path);
    std::filesystem::remove(path);
    bool found = false;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.message.find("has no member 'age'") != std::string::npos) {
            found = true;
            break;
        }
    }
    return expect(found, "sema should report missing class members");
}

bool testMultiReturnConditionDiagnostic() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "axio_sema_multi_test.ax";
    {
        std::ofstream out(path);
        out << "fn pair() (int, int) {\n";
        out << "    return 1, 2\n";
        out << "}\n";
        out << "fn main() int {\n";
        out << "    if pair() { return 1 }\n";
        out << "    return 0\n";
        out << "}\n";
    }

    axc::SourceManager sourceManager;
    axc::DiagnosticEngine diagnostics = runSemaOnFile(sourceManager, path);
    std::filesystem::remove(path);
    bool found = false;
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.message.find("if conditions must be single values") != std::string::npos) {
            found = true;
            break;
        }
    }
    return expect(found, "sema should reject multi-return expressions in if conditions");
}

bool testParseImportDecl() {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "axio_parse_import_test.ax";
    if (!writeFile(path, "import math.geometry\nfn main() int { return 0 }\n")) {
        return false;
    }

    axc::SourceManager sourceManager;
    if (!loadFile(sourceManager, path)) {
        removeTree(path);
        return false;
    }

    axc::DiagnosticEngine diagnostics(sourceManager);
    axc::Lexer lexer(sourceManager, diagnostics);
    std::vector<axc::Token> tokens = lexer.lex();
    axc::Parser parser(std::move(tokens), diagnostics);
    axc::TranslationUnit unit = parser.parseTranslationUnit();
    std::filesystem::remove(path);

    if (!expect(unit.declarations.size() == 2, "parser should keep import and function declarations")) {
        return false;
    }
    if (!expect(unit.declarations[0]->kind == axc::DeclKind::Import, "first declaration should be an import")) {
        return false;
    }
    const auto& importDecl = static_cast<const axc::ImportDecl&>(*unit.declarations[0]);
    return expect(importDecl.name == "math.geometry", "import declaration should preserve dotted module path") &&
           expect(importDecl.moduleSegments.size() == 2 && importDecl.moduleSegments[0] == "math" && importDecl.moduleSegments[1] == "geometry",
                  "import declaration should preserve module segments");
}

bool testCompileImportedFunctionCheckOnly() {
    const std::filesystem::path dir = makeTempTestDir("import_function");
    const std::filesystem::path modulePath = dir / "math" / "ops.ax";
    const std::filesystem::path mainPath = dir / "main.ax";

    const bool wroteFiles = writeFile(modulePath,
                                      "fn add(x int, y int) int {\n"
                                      "    return x + y\n"
                                      "}\n") &&
                            writeFile(mainPath,
                                      "import math.ops\n"
                                      "fn main() int {\n"
                                      "    return math.ops.add(20, 22)\n"
                                      "}\n");
    if (!wroteFiles) {
        removeTree(dir);
        return false;
    }

    axc::Compiler compiler;
    axc::CompileOptions options;
    options.inputFile = mainPath;
    options.checkOnly = true;
    const bool ok = compiler.compile(options);
    removeTree(dir);
    return expect(ok, "compiler should resolve and analyze imported qualified function calls");
}

bool testCompileImportedStructCodegen() {
    const std::filesystem::path dir = makeTempTestDir("import_struct");
    const std::filesystem::path modulePath = dir / "geom" / "point.ax";
    const std::filesystem::path mainPath = dir / "main.ax";
    const std::filesystem::path outputBase = dir / "out";

    const bool wroteFiles = writeFile(modulePath,
                                      "struct Point {\n"
                                      "    x int\n"
                                      "    y int\n"
                                      "}\n") &&
                            writeFile(mainPath,
                                      "import geom.point\n"
                                      "fn main() int {\n"
                                      "    let p geom.point.Point = geom.point.Point(7, 9)\n"
                                      "    return p.x\n"
                                      "}\n");
    if (!wroteFiles) {
        removeTree(dir);
        return false;
    }

    axc::Compiler compiler;
    axc::CompileOptions options;
    options.inputFile = mainPath;
    options.outputFile = outputBase;
    options.emitLlvmIr = true;
    options.emitObject = false;
    options.emitBinary = false;
    const bool ok = compiler.compile(options);
    const bool hasIr = std::filesystem::exists(outputBase.string() + ".ll");
    removeTree(dir);
    return expect(ok, "compiler should lower imported qualified struct initializers") &&
           expect(hasIr, "compiler should emit LLVM IR for imported qualified struct use");
}

}  // namespace

int main() {
    if (!testSourceManagerLineColumn()) {
        return 1;
    }
    if (!testDiagnosticCollection()) {
        return 1;
    }
    if (!testClassMissingMemberDiagnostic()) {
        return 1;
    }
    if (!testMultiReturnConditionDiagnostic()) {
        return 1;
    }
    if (!testParseImportDecl()) {
        return 1;
    }
    if (!testCompileImportedFunctionCheckOnly()) {
        return 1;
    }
    if (!testCompileImportedStructCodegen()) {
        return 1;
    }
    return 0;
}
