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

class ModuleLoader {
  public:
    explicit ModuleLoader(DiagnosticEngine& diagnostics);

    bool loadInto(TranslationUnit& rootUnit, const std::filesystem::path& entryFile);

    [[nodiscard]] const std::unordered_map<std::string, detail::ModuleInterface>& moduleInterfaces() const;

  private:
    bool loadModuleRecursive(const std::filesystem::path& projectRoot,
                             const std::filesystem::path& filePath,
                             const std::string& moduleName,
                             bool isEntryModule);
    bool parseModuleFile(const std::filesystem::path& path, TranslationUnit& unit);
    std::filesystem::path modulePathFor(const std::filesystem::path& projectRoot, const ImportDecl& importDecl) const;

    DiagnosticEngine& diagnostics_;
    detail::ModuleFileParser fileParser_;
    std::unordered_map<std::string, detail::ModuleInterface> moduleInterfaces_ {};
    detail::ModuleImportResolver importResolver_;
    std::unordered_map<std::string, detail::LoadedModule> loadedModules_ {};
    std::unordered_set<std::string> loadingModules_ {};
    std::vector<std::string> loadOrder_ {};
};

}  // namespace axc
