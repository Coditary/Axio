#include "support/TestSupport.h"

#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>
#include <system_error>

namespace axc::unit {

TempDir::TempDir(std::filesystem::path path) : path(std::move(path)) {}

TempDir::TempDir(TempDir&& other) noexcept : path(std::move(other.path)) {
    other.path.clear();
}

TempDir& TempDir::operator=(TempDir&& other) noexcept {
    if (this != &other) {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
        path = std::move(other.path);
        other.path.clear();
    }
    return *this;
}

TempDir::~TempDir() {
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
}

TempDir makeTempDir(const std::string& name) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto unique = std::to_string(std::hash<std::string> {}(name + std::to_string(now)));
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("axio_unit_" + name + '_' + unique);
    std::filesystem::create_directories(path);
    return TempDir(std::move(path));
}

bool writeFile(const std::filesystem::path& path, const std::string& contents) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << contents;
    return true;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

ParsedFile::ParsedFile() : diagnostics(sourceManager) {}

bool loadSource(ParsedFile& file, const std::filesystem::path& path) {
    std::string error;
    return file.sourceManager.loadFromFile(path, error);
}

bool lexSource(ParsedFile& file, const std::filesystem::path& path) {
    if (!loadSource(file, path)) {
        return false;
    }
    Lexer lexer(file.sourceManager, file.diagnostics);
    file.tokens = lexer.lex();
    return true;
}

bool parseSource(ParsedFile& file, const std::filesystem::path& path) {
    if (!lexSource(file, path)) {
        return false;
    }
    Parser parser(std::move(file.tokens), file.diagnostics);
    file.unit = parser.parseTranslationUnit();
    return true;
}

bool analyzeSource(ParsedFile& file, const std::filesystem::path& path) {
    if (!parseSource(file, path)) {
        return false;
    }
    Sema sema(file.diagnostics);
    sema.analyze(file.unit);
    return true;
}

bool analyzeAndRunMeta(ParsedFile& file, const std::filesystem::path& path) {
    if (!analyzeSource(file, path)) {
        return false;
    }
    MetaPipeline meta(file.sourceManager, file.diagnostics);
    meta.run(file.unit);
    return true;
}

std::string renderDiagnostics(const DiagnosticEngine& diagnostics) {
    std::ostringstream out;
    diagnostics.renderAll(out);
    return out.str();
}

bool hasDiagnosticContaining(const DiagnosticEngine& diagnostics, std::string_view needle, DiagnosticSeverity severity) {
    for (const auto& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.severity == severity && diagnostic.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

const FunctionDecl* findFunction(const TranslationUnit& unit, std::string_view name) {
    for (const auto& decl : unit.declarations) {
        if (decl->kind == DeclKind::Function && decl->name == name) {
            return static_cast<const FunctionDecl*>(decl.get());
        }
    }
    return nullptr;
}

const ClassDecl* findClass(const TranslationUnit& unit, std::string_view name) {
    for (const auto& decl : unit.declarations) {
        if (decl->kind == DeclKind::Class && decl->name == name) {
            return static_cast<const ClassDecl*>(decl.get());
        }
    }
    return nullptr;
}

const StructDecl* findStruct(const TranslationUnit& unit, std::string_view name) {
    for (const auto& decl : unit.declarations) {
        if (decl->kind == DeclKind::Struct && decl->name == name) {
            return static_cast<const StructDecl*>(decl.get());
        }
    }
    return nullptr;
}

const EnumDecl* findEnum(const TranslationUnit& unit, std::string_view name) {
    for (const auto& decl : unit.declarations) {
        if (decl->kind == DeclKind::Enum && decl->name == name) {
            return static_cast<const EnumDecl*>(decl.get());
        }
    }
    return nullptr;
}

const ImportDecl* findImport(const TranslationUnit& unit, std::string_view name) {
    for (const auto& decl : unit.declarations) {
        if (decl->kind == DeclKind::Import && decl->name == name) {
            return static_cast<const ImportDecl*>(decl.get());
        }
    }
    return nullptr;
}

bool compileCheckOnly(const std::filesystem::path& input) {
    Compiler compiler;
    CompileOptions options;
    options.inputFile = input;
    options.checkOnly = true;
    options.emitLlvmIr = false;
    options.emitObject = false;
    options.emitBinary = false;
    return compiler.compile(options);
}

bool compileToLlvmIr(const std::filesystem::path& input, const std::filesystem::path& outputBase) {
    Compiler compiler;
    CompileOptions options;
    options.inputFile = input;
    options.outputFile = outputBase;
    options.emitLlvmIr = true;
    options.emitObject = false;
    options.emitBinary = false;
    return compiler.compile(options);
}

}  // namespace axc::unit
