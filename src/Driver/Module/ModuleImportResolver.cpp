#include "ModuleImportResolver.h"

#include "axc/Support/Diagnostic.h"

namespace axc::detail {

namespace {

void importSymbol(ModuleImportBindings& bindings, const std::string& name, const ModuleSymbol& symbol) {
    auto [it, inserted] = bindings.importedSymbols.emplace(name, symbol);
    if (!inserted && it->second.qualifiedName != symbol.qualifiedName) {
        bindings.importedSymbols.erase(it);
        bindings.ambiguousNames.insert(name);
    }
}

void registerVisibleModule(ModuleImportBindings& bindings,
                           DiagnosticEngine& diagnostics,
                           SourceRange range,
                           const std::string& visibleName,
                           const std::string& actualModuleName) {
    auto [it, inserted] = bindings.visibleModules.emplace(visibleName, actualModuleName);
    if (!inserted && it->second != actualModuleName) {
        diagnostics.error(range, "ambiguous imported module name '" + visibleName + "'");
    }
}

}  // namespace

ModuleImportResolver::ModuleImportResolver(DiagnosticEngine& diagnostics,
                                           const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces)
    : diagnostics_(diagnostics), moduleInterfaces_(moduleInterfaces) {}

ModuleImportBindings ModuleImportResolver::collectBindings(const TranslationUnit& unit) const {
    ModuleImportBindings bindings;

    for (const auto& decl : unit.declarations) {
        if (decl->kind == DeclKind::Import) {
            continue;
        }
        bindings.localNames.insert(decl->localName);
    }

    for (const auto& decl : unit.declarations) {
        if (decl->kind != DeclKind::Import) {
            continue;
        }

        const auto& importDecl = static_cast<const ImportDecl&>(*decl);
        auto interfaceIt = moduleInterfaces_.find(importDecl.name);
        if (interfaceIt == moduleInterfaces_.end()) {
            continue;
        }
        const ModuleInterface& interface = interfaceIt->second;
        registerVisibleModule(bindings, diagnostics_, importDecl.range, importDecl.name, importDecl.name);
        if (!importDecl.alias.empty()) {
            registerVisibleModule(bindings, diagnostics_, importDecl.range, importDecl.alias, importDecl.name);
        }

        if (importDecl.importedNames.empty()) {
            for (const auto& [name, symbol] : interface.exportedSymbols) {
                importSymbol(bindings, name, symbol);
            }
            continue;
        }

        for (const auto& name : importDecl.importedNames) {
            auto exportIt = interface.exportedSymbols.find(name);
            if (exportIt != interface.exportedSymbols.end()) {
                importSymbol(bindings, name, exportIt->second);
                continue;
            }
            if (interface.declaredSymbols.contains(name)) {
                diagnostics_.error(importDecl.range,
                                   "symbol '" + name + "' is private inside module '" + importDecl.name + "'");
            } else {
                diagnostics_.error(importDecl.range,
                                   "module '" + importDecl.name + "' does not export '" + name + "'");
            }
        }
    }

    return bindings;
}

}  // namespace axc::detail
