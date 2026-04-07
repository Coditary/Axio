#pragma once

#include <vector>
#include <unordered_map>

#include "ModuleModel.h"

namespace axc {

class DiagnosticEngine;

namespace detail {

class ModuleQualifier {
  public:
    ModuleQualifier(DiagnosticEngine& diagnostics,
                    const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces);

    void qualify(TranslationUnit& unit,
                 const std::string& moduleName,
                 const ModuleImportBindings& bindings,
                 bool qualifyLocalDeclNames) const;

  private:
    struct ResolutionContext {
        std::string moduleName {};
        const ModuleImportBindings* bindings = nullptr;
        bool qualifyLocalDeclNames = false;
    };

    struct QualifiedPath {
        std::string baseQualifiedName {};
        std::vector<std::string> trailingSegments {};
        bool resolved = false;
    };

    void qualifyDecl(std::unique_ptr<Decl>& decl, const ResolutionContext& context) const;
    void qualifyStmt(std::unique_ptr<Stmt>& stmt, const ResolutionContext& context, std::unordered_set<std::string> locals) const;
    void qualifyExpr(std::unique_ptr<Expr>& expr, const ResolutionContext& context, const std::unordered_set<std::string>& locals) const;
    void qualifyType(Type& type, const ResolutionContext& context) const;
    QualifiedPath resolveQualifiedPath(const std::string& name, const ResolutionContext& context, SourceRange range) const;
    std::string resolveSimpleName(const std::string& name,
                                  const ResolutionContext& context,
                                  const std::unordered_set<std::string>& locals,
                                  SourceRange range) const;
    std::string resolveTypeName(const std::string& name, const ResolutionContext& context, SourceRange range) const;
    std::unique_ptr<Expr> buildQualifiedExpr(const QualifiedPath& path, SourceRange range) const;
    std::string longestKnownModulePrefix(const std::string& name, const ResolutionContext& context) const;
    bool isVisibleModuleName(const std::string& candidate, const ResolutionContext& context) const;
    bool isBuiltinTypeName(const std::string& name) const;

    DiagnosticEngine& diagnostics_;
    const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces_;
};

}  // namespace detail

}  // namespace axc
