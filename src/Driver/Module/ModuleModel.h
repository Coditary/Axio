#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "axc/AST/AST.h"

namespace axc::detail {

struct ModuleSymbol {
    std::string publicName {};
    std::string qualifiedName {};
    DeclKind kind = DeclKind::Function;
    Visibility visibility = Visibility::Private;
    std::string signature {};
};

struct ModuleInterface {
    std::string moduleName {};
    std::unordered_map<std::string, ModuleSymbol> declaredSymbols {};
    std::unordered_map<std::string, ModuleSymbol> exportedSymbols {};
    std::string apiFingerprint {};
};

struct LoadedModule {
    std::filesystem::path filePath {};
    std::string moduleName {};
    TranslationUnit unit {};
    ModuleInterface interface {};
};

struct ModuleImportBindings {
    std::unordered_map<std::string, ModuleSymbol> importedSymbols {};
    std::unordered_set<std::string> ambiguousNames {};
    std::unordered_set<std::string> localNames {};
    std::unordered_map<std::string, std::string> visibleModules {};
};

inline std::string qualifyModuleSymbol(const std::string& moduleName, const std::string& localName) {
    return moduleName.empty() ? localName : moduleName + "." + localName;
}

}  // namespace axc::detail
