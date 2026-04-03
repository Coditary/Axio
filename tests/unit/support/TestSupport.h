#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "axc/AST/AST.h"
#include "axc/Codegen/LLVMEmitter.h"
#include "axc/Driver/Compiler.h"
#include "axc/Lex/Lexer.h"
#include "axc/Meta/MetaPipeline.h"
#include "axc/Parse/Parser.h"
#include "axc/Sema/Sema.h"
#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc::unit {

struct TempDir {
    std::filesystem::path path {};

    explicit TempDir(std::filesystem::path path);
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&& other) noexcept;
    TempDir& operator=(TempDir&& other) noexcept;
    ~TempDir();
};

TempDir makeTempDir(const std::string& name);
bool writeFile(const std::filesystem::path& path, const std::string& contents);
std::string readFile(const std::filesystem::path& path);

struct ParsedFile {
    SourceManager sourceManager;
    DiagnosticEngine diagnostics;
    std::vector<Token> tokens {};
    TranslationUnit unit {};

    ParsedFile();
};

bool loadSource(ParsedFile& file, const std::filesystem::path& path);
bool lexSource(ParsedFile& file, const std::filesystem::path& path);
bool parseSource(ParsedFile& file, const std::filesystem::path& path);
bool analyzeSource(ParsedFile& file, const std::filesystem::path& path);
bool analyzeAndRunMeta(ParsedFile& file, const std::filesystem::path& path);

std::string renderDiagnostics(const DiagnosticEngine& diagnostics);
bool hasDiagnosticContaining(const DiagnosticEngine& diagnostics, std::string_view needle, DiagnosticSeverity severity);

const FunctionDecl* findFunction(const TranslationUnit& unit, std::string_view name);
const ClassDecl* findClass(const TranslationUnit& unit, std::string_view name);
const StructDecl* findStruct(const TranslationUnit& unit, std::string_view name);
const EnumDecl* findEnum(const TranslationUnit& unit, std::string_view name);
const ImportDecl* findImport(const TranslationUnit& unit, std::string_view name);

bool compileCheckOnly(const std::filesystem::path& input);
bool compileToLlvmIr(const std::filesystem::path& input, const std::filesystem::path& outputBase);

}  // namespace axc::unit
