#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "axc/AST/AST.h"

namespace axc {

class DiagnosticEngine;

class ModuleLoader {
  public:
    explicit ModuleLoader(DiagnosticEngine& diagnostics);

    bool loadInto(TranslationUnit& rootUnit, const std::filesystem::path& entryFile);

  private:
    bool loadModuleRecursive(TranslationUnit& destination,
                             const std::filesystem::path& projectRoot,
                             const std::filesystem::path& importerFile,
                             const ImportDecl& importDecl);
    bool parseFile(const std::filesystem::path& path, TranslationUnit& unit);
    void qualifyModuleDecls(TranslationUnit& unit, const std::string& moduleName) const;
    void qualifyType(Type& type, const std::string& moduleName, const std::unordered_set<std::string>& topLevelNames) const;
    void qualifyExpr(Expr& expr,
                     const std::string& moduleName,
                     const std::unordered_set<std::string>& topLevelNames,
                     const std::unordered_set<std::string>& locals) const;
    void qualifyStmt(Stmt& stmt,
                     const std::string& moduleName,
                     const std::unordered_set<std::string>& topLevelNames,
                     std::unordered_set<std::string> locals) const;
    void qualifyDecl(Decl& decl, const std::string& moduleName, const std::unordered_set<std::string>& topLevelNames) const;
    bool isQualified(const std::string& name) const;
    std::filesystem::path modulePathFor(const std::filesystem::path& projectRoot, const ImportDecl& importDecl) const;
    std::string joinSegments(const std::vector<std::string>& segments) const;
    std::unordered_set<std::string> collectTopLevelNames(const TranslationUnit& unit) const;
    bool isBuiltinTypeName(const std::string& name) const;

    DiagnosticEngine& diagnostics_;
    std::unordered_set<std::string> loadedModules_ {};
    std::unordered_map<std::string, std::filesystem::path> moduleFiles_ {};
};

}  // namespace axc
