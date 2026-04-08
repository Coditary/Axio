#include "ModuleInterface.h"

#include <algorithm>
#include <sstream>

#include "axc/Support/Diagnostic.h"

namespace axc::detail {

namespace {

std::string visibilityName(Visibility visibility) {
    return visibility == Visibility::Public ? "pub" : "pri";
}

}  // namespace

ModuleInterfaceBuilder::ModuleInterfaceBuilder(DiagnosticEngine& diagnostics,
                                               const std::unordered_map<std::string, ModuleInterface>& moduleInterfaces)
    : diagnostics_(diagnostics), moduleInterfaces_(moduleInterfaces) {}

bool ModuleInterfaceBuilder::build(const TranslationUnit& unit, const std::string& moduleName, ModuleInterface& interface) const {
    interface = ModuleInterface {};
    interface.moduleName = moduleName;

    std::unordered_map<std::string, const Decl*> localDecls;
    for (const auto& decl : unit.declarations) {
        if (decl->kind == DeclKind::Import) {
            continue;
        }
        if (!localDecls.emplace(decl->localName, decl.get()).second) {
            diagnostics_.error(decl->range, "duplicate top-level declaration '" + decl->localName + "'");
            continue;
        }

        ModuleSymbol symbol;
        symbol.publicName = decl->localName;
        symbol.qualifiedName = decl->name;
        symbol.kind = decl->kind;
        symbol.visibility = decl->visibility;
        symbol.signature = declSignature(*decl);
        interface.declaredSymbols[symbol.publicName] = symbol;
        if (decl->visibility == Visibility::Public) {
            addExport(interface, symbol, decl->range);
        }
    }

    for (const auto& decl : unit.declarations) {
        if (decl->kind == DeclKind::Import && decl->visibility == Visibility::Public) {
            addImportExports(interface, static_cast<const ImportDecl&>(*decl));
        }
    }

    finalizeFingerprint(interface);
    return !diagnostics_.hasErrors();
}

void ModuleInterfaceBuilder::addImportExports(ModuleInterface& interface, const ImportDecl& importDecl) const {
    auto importedInterfaceIt = moduleInterfaces_.find(importDecl.name);
    if (importedInterfaceIt == moduleInterfaces_.end()) {
        return;
    }

    const ModuleInterface& importedInterface = importedInterfaceIt->second;
    if (importDecl.importedNames.empty()) {
        for (const auto& [name, symbol] : importedInterface.exportedSymbols) {
            if (interface.declaredSymbols.contains(name)) {
                diagnostics_.error(importDecl.range,
                                   "cannot re-export '" + name + "' because the module already declares a symbol with that name");
                continue;
            }
            addExport(interface, symbol, importDecl.range);
        }
        return;
    }

    for (const auto& name : importDecl.importedNames) {
        auto symbolIt = importedInterface.exportedSymbols.find(name);
        if (symbolIt == importedInterface.exportedSymbols.end()) {
            continue;
        }
        if (interface.declaredSymbols.contains(name)) {
            diagnostics_.error(importDecl.range,
                               "cannot re-export '" + name + "' because the module already declares a symbol with that name");
            continue;
        }
        addExport(interface, symbolIt->second, importDecl.range);
    }
}

std::string ModuleInterfaceBuilder::declSignature(const Decl& decl) const {
    std::ostringstream out;
    out << visibilityName(decl.visibility) << ':';
    switch (decl.kind) {
        case DeclKind::Import:
            out << "import";
            break;
        case DeclKind::GlobalVar: {
            const auto& global = static_cast<const GlobalVarDecl&>(decl);
            out << (global.mutableStorage ? "let " : "const ") << global.localName;
            if (!global.type.name.empty()) {
                out << ':' << typeSignature(global.type);
            }
            break;
        }
        case DeclKind::Struct: {
            const auto& structDecl = static_cast<const StructDecl&>(decl);
            out << "struct " << structDecl.localName << '{';
            for (const auto& field : structDecl.fields) {
                out << field.name << ':' << typeSignature(field.type) << ';';
            }
            out << '}';
            break;
        }
        case DeclKind::Enum: {
            const auto& enumDecl = static_cast<const EnumDecl&>(decl);
            out << "enum " << enumDecl.localName << '{';
            for (const auto& element : enumDecl.elements) {
                out << element.name << ';';
            }
            out << '}';
            break;
        }
        case DeclKind::Class: {
            const auto& classDecl = static_cast<const ClassDecl&>(decl);
            out << "class " << classDecl.localName << '{';
            for (const auto& member : classDecl.members) {
                out << visibilityName(member.visibility) << ' ' << member.name << ':' << typeSignature(member.type) << ';';
            }
            for (const auto& method : classDecl.methods) {
                out << declSignature(*method) << ';';
            }
            out << '}';
            break;
        }
        case DeclKind::Function: {
            const auto& functionDecl = static_cast<const FunctionDecl&>(decl);
            out << "fn " << functionDecl.localName << '(';
            for (const auto& param : functionDecl.parameters) {
                if (!functionDecl.receiverType.empty() && param.name == "self") {
                    continue;
                }
                out << (param.isConst ? "const " : "") << param.name << ':' << typeSignature(param.type) << ';';
            }
            out << ")->";
            if (functionDecl.returnsVoid()) {
                out << "void;";
            } else {
                out << typeSignature(*functionDecl.returnType) << ';';
            }
            break;
        }
    }
    return out.str();
}

std::string ModuleInterfaceBuilder::typeSignature(const Type& type) const {
    std::ostringstream out;
    out << type.name;
    for (std::size_t i = 0; i < type.pointerDepth; ++i) {
        out << '*';
    }
    for (const auto& extent : type.arrayExtents) {
        out << '[';
        if (extent.has_value()) {
            out << *extent;
        }
        out << ']';
    }
    return out.str();
}

void ModuleInterfaceBuilder::addExport(ModuleInterface& interface, const ModuleSymbol& symbol, SourceRange range) const {
    auto it = interface.exportedSymbols.find(symbol.publicName);
    if (it == interface.exportedSymbols.end()) {
        interface.exportedSymbols[symbol.publicName] = symbol;
        return;
    }
    if (it->second.qualifiedName != symbol.qualifiedName) {
        diagnostics_.error(range, "duplicate exported symbol '" + symbol.publicName + "'");
    }
}

void ModuleInterfaceBuilder::finalizeFingerprint(ModuleInterface& interface) const {
    std::vector<std::string> entries;
    entries.reserve(interface.exportedSymbols.size());
    for (const auto& [name, symbol] : interface.exportedSymbols) {
        entries.push_back(name + "=" + symbol.signature);
    }
    std::sort(entries.begin(), entries.end());

    std::string combined;
    for (const auto& entry : entries) {
        combined += entry;
        combined.push_back('\n');
    }
    interface.apiFingerprint = std::to_string(std::hash<std::string> {}(combined));
}

}  // namespace axc::detail
