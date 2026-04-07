#pragma once

#include <vector>
#include <unordered_map>

#include "ModuleModel.h"

namespace axc {

class DiagnosticEngine;

namespace detail {

/// @brief Rewrites unqualified names into module-qualified AST references.
class ModuleQualifier {
  public:
    /// @brief Create a qualifier using already-built module interfaces.
    ModuleQualifier(DiagnosticEngine& diagnostics,
                    const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces);

    /// @brief Qualify declaration, type, and expression references inside `unit`.
    void qualify(TranslationUnit& unit,
                 const std::string& moduleName,
                 const ModuleImportBindings& bindings,
                 bool qualifyLocalDeclNames) const;

  private:
    /// @brief Name-resolution inputs that stay constant while qualifying one unit.
    struct ResolutionContext {
        std::string moduleName {};
        const ModuleImportBindings* bindings = nullptr;
        bool qualifyLocalDeclNames = false;
    };

    /// @brief Result of resolving a possibly-qualified dotted path.
    struct QualifiedPath {
        std::string baseQualifiedName {};
        std::vector<std::string> trailingSegments {};
        bool resolved = false;
    };

    /// @brief Qualify names inside one declaration subtree.
    void qualifyDecl(std::unique_ptr<Decl>& decl, const ResolutionContext& context) const;
    /// @brief Qualify names inside one statement subtree.
    void qualifyStmt(std::unique_ptr<Stmt>& stmt, const ResolutionContext& context, std::unordered_set<std::string> locals) const;
    /// @brief Qualify names inside one expression subtree.
    void qualifyExpr(std::unique_ptr<Expr>& expr, const ResolutionContext& context, const std::unordered_set<std::string>& locals) const;
    /// @brief Rewrite one syntactic type name to its fully qualified form when needed.
    void qualifyType(Type& type, const ResolutionContext& context) const;
    /// @brief Resolve a dotted path as either a visible module-qualified symbol or a local/module reference.
    QualifiedPath resolveQualifiedPath(const std::string& name, const ResolutionContext& context, SourceRange range) const;
    /// @brief Resolve a single-segment name inside local/import/module scope rules.
    std::string resolveSimpleName(const std::string& name,
                                  const ResolutionContext& context,
                                  const std::unordered_set<std::string>& locals,
                                  SourceRange range) const;
    /// @brief Resolve a type name using module visibility and builtin-type rules.
    std::string resolveTypeName(const std::string& name, const ResolutionContext& context, SourceRange range) const;
    /// @brief Build a member-expression chain representing a qualified path.
    std::unique_ptr<Expr> buildQualifiedExpr(const QualifiedPath& path, SourceRange range) const;
    /// @brief Find the longest visible module prefix inside a dotted name.
    std::string longestKnownModulePrefix(const std::string& name, const ResolutionContext& context) const;
    /// @brief Return whether a module name is visible from the current context.
    bool isVisibleModuleName(const std::string& candidate, const ResolutionContext& context) const;
    /// @brief Return whether a name refers to a builtin scalar type.
    bool isBuiltinTypeName(const std::string& name) const;

    /// Shared diagnostic sink.
    DiagnosticEngine& diagnostics_;
    /// Interfaces used to resolve public names and module paths.
    const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces_;
};

}  // namespace detail

}  // namespace axc
