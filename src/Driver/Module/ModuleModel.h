#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "axc/AST/AST.h"

namespace axc::detail {

/// @brief One named declaration exported or declared by a module.
struct ModuleSymbol {
    std::string publicName {};
    std::string qualifiedName {};
    DeclKind kind = DeclKind::Function;
    Visibility visibility = Visibility::Private;
    std::string signature {};
};

/// @brief Public interface summary for one loaded module.
struct ModuleInterface {
    std::string moduleName {};
    std::unordered_map<std::string, ModuleSymbol> declaredSymbols {};
    std::unordered_map<std::string, ModuleSymbol> exportedSymbols {};
    std::string apiFingerprint {};
};

/// @brief Parsed module plus its filesystem origin and computed interface.
struct LoadedModule {
    std::filesystem::path filePath {};
    std::string moduleName {};
    TranslationUnit unit {};
    ModuleInterface interface {};
};

/// @brief Name-resolution view produced from a file's imports.
struct ModuleImportBindings {
    std::unordered_map<std::string, ModuleSymbol> importedSymbols {};
    std::unordered_set<std::string> ambiguousNames {};
    std::unordered_set<std::string> localNames {};
    std::unordered_map<std::string, std::string> visibleModules {};
};

/// @brief Join a module path and local declaration name into one qualified name.
inline std::string qualifyModuleSymbol(const std::string& moduleName, const std::string& localName) {
    return moduleName.empty() ? localName : moduleName + "." + localName;
}

}  // namespace axc::detail
