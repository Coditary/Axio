/// @file
/// @brief Recursive module loading, import resolution, qualification, and merge orchestration.

#include "ModuleLoader.h"

#include <utility>
#include <vector>

#include "axc/Support/Diagnostic.h"

namespace axc {

ModuleLoader::ModuleLoader(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics), importResolver_(diagnostics, moduleInterfaces_) {}

bool ModuleLoader::loadInto(TranslationUnit& rootUnit, const std::filesystem::path& entryFile) {
    const std::filesystem::path projectRoot = entryFile.parent_path();
    loadedModules_.clear();
    moduleInterfaces_.clear();
    loadingModules_.clear();
    loadOrder_.clear();

    if (!loadModuleRecursive(projectRoot, entryFile, "", true)) {
        return false;
    }

    rootUnit = TranslationUnit {};
    for (const auto& moduleName : loadOrder_) {
        auto moduleIt = loadedModules_.find(moduleName);
        if (moduleIt == loadedModules_.end()) {
            continue;
        }
        auto& unit = moduleIt->second.unit;
        for (auto& directive : unit.preprocessorDirectives) {
            rootUnit.preprocessorDirectives.push_back(std::move(directive));
        }
        std::vector<std::unique_ptr<Decl>> declarations;
        declarations.swap(unit.declarations);
        for (auto& decl : declarations) {
            if (decl->kind != DeclKind::Import) {
                rootUnit.declarations.push_back(std::move(decl));
            }
        }
    }

    return !diagnostics_.hasErrors();
}

const std::unordered_map<std::string, detail::ModuleInterface>& ModuleLoader::moduleInterfaces() const {
    return moduleInterfaces_;
}

bool ModuleLoader::loadModuleRecursive(const std::filesystem::path& projectRoot,
                                       const std::filesystem::path& filePath,
                                       const std::string& moduleName,
                                       bool isEntryModule) {
    const std::string loadingKey = moduleName.empty() ? filePath.string() : moduleName;
    if (loadingModules_.contains(loadingKey)) {
        diagnostics_.error(SourceRange {}, "cyclic module import involving '" + loadingKey + "'");
        return false;
    }

    if (!moduleName.empty()) {
        auto loadedIt = loadedModules_.find(moduleName);
        if (loadedIt != loadedModules_.end()) {
            return !diagnostics_.hasErrors();
        }
    }

    loadingModules_.insert(loadingKey);

    TranslationUnit parsedUnit;
    if (!parseModuleFile(filePath, parsedUnit)) {
        loadingModules_.erase(loadingKey);
        return false;
    }

    if (parsedUnit.packageName.empty()) {
        diagnostics_.error(SourceRange {}, "module file '" + filePath.string() + "' must declare a package path");
        loadingModules_.erase(loadingKey);
        return false;
    }

    const std::string actualModuleName = parsedUnit.packageName;
    if (!moduleName.empty() && actualModuleName != moduleName) {
        diagnostics_.error(SourceRange {},
                           "module file '" + filePath.string() + "' declares package '" + actualModuleName +
                               "' but was imported as '" + moduleName + "'");
        loadingModules_.erase(loadingKey);
        return false;
    }

    auto loadedIt = loadedModules_.find(actualModuleName);
    if (loadedIt != loadedModules_.end()) {
        loadingModules_.erase(loadingKey);
        return !diagnostics_.hasErrors();
    }

    for (const auto& decl : parsedUnit.declarations) {
        if (decl->kind != DeclKind::Import) {
            continue;
        }
        const auto& importDecl = static_cast<const ImportDecl&>(*decl);
        const std::filesystem::path importedPath = modulePathFor(projectRoot, importDecl);
        if (!loadModuleRecursive(projectRoot, importedPath, importDecl.name, false)) {
            loadingModules_.erase(loadingKey);
            return false;
        }
    }

    detail::ModuleImportBindings bindings = importResolver_.collectBindings(parsedUnit);

    detail::ModuleQualifier qualifier(diagnostics_, moduleInterfaces_);
    qualifier.qualify(parsedUnit, actualModuleName, bindings, !isEntryModule);

    detail::ModuleInterfaceBuilder interfaceBuilder(diagnostics_, moduleInterfaces_);
    detail::ModuleInterface interface;
    if (!interfaceBuilder.build(parsedUnit, actualModuleName, interface)) {
        loadingModules_.erase(loadingKey);
        return false;
    }

    detail::LoadedModule loadedModule;
    loadedModule.filePath = filePath;
    loadedModule.moduleName = actualModuleName;
    loadedModule.unit = std::move(parsedUnit);
    loadedModule.interface = interface;

    moduleInterfaces_[actualModuleName] = interface;
    loadedModules_[actualModuleName] = std::move(loadedModule);
    loadOrder_.push_back(actualModuleName);
    loadingModules_.erase(loadingKey);

    return !diagnostics_.hasErrors();
}

bool ModuleLoader::parseModuleFile(const std::filesystem::path& path, TranslationUnit& unit) {
    std::string errorMessage;
    if (fileParser_.parse(path, unit, errorMessage)) {
        return true;
    }

    diagnostics_.error(SourceRange {}, "failed to load module file '" + path.string() + "': " + errorMessage);
    return false;
}

std::filesystem::path ModuleLoader::modulePathFor(const std::filesystem::path& projectRoot, const ImportDecl& importDecl) const {
    std::filesystem::path path = projectRoot;
    for (const auto& segment : importDecl.moduleSegments) {
        path /= segment;
    }
    path.replace_extension(".ax");
    return path;
}

}  // namespace axc
