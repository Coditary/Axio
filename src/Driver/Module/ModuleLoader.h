#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "ModuleFileParser.h"
#include "ModuleImportResolver.h"
#include "ModuleInterface.h"
#include "ModuleQualifier.h"

namespace axc {

class DiagnosticEngine;

/// @brief Loads the entry module and all recursive imports.
class ModuleLoader {
  public:
    /// @brief Create a module loader sharing one diagnostic sink.
    explicit ModuleLoader(DiagnosticEngine& diagnostics);

    /// @brief Load the entry file and merge all imported modules into `rootUnit`.
    bool loadInto(TranslationUnit& rootUnit, const std::filesystem::path& entryFile);

    /// @brief Public interfaces discovered for all loaded modules.
    [[nodiscard]] const std::unordered_map<std::string, detail::ModuleInterface>& moduleInterfaces() const;

  private:
    /// @brief Load one module and all of its imports in dependency order.
    bool loadModuleRecursive(const std::filesystem::path& projectRoot,
                             const std::filesystem::path& filePath,
                             const std::string& moduleName,
                             bool isEntryModule);
    /// @brief Parse a module file into a translation unit.
    bool parseModuleFile(const std::filesystem::path& path, TranslationUnit& unit);
    /// @brief Resolve the source path for an import relative to the project root.
    std::filesystem::path modulePathFor(const std::filesystem::path& projectRoot, const ImportDecl& importDecl) const;

    /// Shared diagnostic sink for all module-loading stages.
    DiagnosticEngine& diagnostics_;
    /// Helper for isolated file parsing.
    detail::ModuleFileParser fileParser_;
    /// Public interfaces collected for all loaded modules.
    std::unordered_map<std::string, detail::ModuleInterface> moduleInterfaces_ {};
    /// Helper that computes visible imported names per translation unit.
    detail::ModuleImportResolver importResolver_;
    /// Fully loaded module set keyed by module name.
    std::unordered_map<std::string, detail::LoadedModule> loadedModules_ {};
    /// Recursion guard for cyclic import detection.
    std::unordered_set<std::string> loadingModules_ {};
    /// Topological-ish load order used when merging modules for later phases.
    std::vector<std::string> loadOrder_ {};
};

}  // namespace axc
