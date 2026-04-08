#include "axc/AST/ASTPrinter.h"

#include <algorithm>

namespace axc {

namespace {

const char* visibilityPrefix(Visibility visibility) {
    return visibility == Visibility::Public ? "pub " : "";
}

}  // namespace

void ASTPrinter::print(const TranslationUnit& unit) const {
    if (!unit.packageName.empty()) {
        out_ << "Package " << unit.packageName << '\n';
    }
    for (const auto& decl : unit.declarations) {
        printDecl(*decl, 0);
    }
}

void ASTPrinter::indent(int level) const {
    for (int i = 0; i < level; ++i) {
        out_ << "  ";
    }
}

void ASTPrinter::printDecl(const Decl& decl, int level) const {
    indent(level);
    switch (decl.kind) {
        case DeclKind::Import: {
            const auto& importDecl = static_cast<const ImportDecl&>(decl);
            out_ << visibilityPrefix(importDecl.visibility) << "Import " << importDecl.name;
            if (!importDecl.alias.empty()) {
                out_ << " as " << importDecl.alias;
            }
            if (!importDecl.importedNames.empty()) {
                out_ << " {";
                for (std::size_t i = 0; i < importDecl.importedNames.size(); ++i) {
                    if (i > 0) {
                        out_ << ", ";
                    }
                    out_ << importDecl.importedNames[i];
                }
                out_ << '}';
            }
            out_ << '\n';
            break;
        }
        case DeclKind::GlobalVar: {
            const auto& global = static_cast<const GlobalVarDecl&>(decl);
            out_ << visibilityPrefix(global.visibility) << (global.mutableStorage ? "GlobalLet " : "GlobalConst ") << global.name;
            if (!global.type.name.empty()) {
                out_ << ':';
                printType(global.type);
            }
            out_ << '\n';
            break;
        }
        case DeclKind::Function: {
            const auto& fn = static_cast<const FunctionDecl&>(decl);
            out_ << visibilityPrefix(fn.visibility) << "Function " << fn.name;
            out_ << '\n';
            for (const auto& param : fn.parameters) {
                indent(level + 1);
                out_ << "Param " << (param.isConst ? "const " : "") << param.name << ':';
                printType(param.type);
                out_ << '\n';
            }
            if (fn.returnType.has_value()) {
                indent(level + 1);
                out_ << "Returns ";
                printType(*fn.returnType);
                out_ << '\n';
            } else if (fn.returnsVoid()) {
                indent(level + 1);
                out_ << "Returns void\n";
            }
            if (fn.body) {
                printStmt(*fn.body, level + 1);
            }
            break;
        }
        case DeclKind::Struct: {
            const auto& st = static_cast<const StructDecl&>(decl);
            out_ << visibilityPrefix(st.visibility) << "Struct " << st.name << '\n';
            for (const auto& field : st.fields) {
                indent(level + 1);
                out_ << "Field " << field.name << ':';
                printType(field.type);
                out_ << '\n';
            }
            break;
        }
        case DeclKind::Enum: {
            const auto& en = static_cast<const EnumDecl&>(decl);
            out_ << visibilityPrefix(en.visibility) << "Enum " << en.name << '\n';
            for (const auto& element : en.elements) {
                indent(level + 1);
                out_ << "Element " << element.name << '\n';
            }
            break;
        }
        case DeclKind::Class: {
            const auto& cl = static_cast<const ClassDecl&>(decl);
            out_ << visibilityPrefix(cl.visibility) << "Class " << cl.name << '\n';
            for (const auto& member : cl.members) {
                indent(level + 1);
                out_ << visibilityPrefix(member.visibility) << "Member " << member.name << ':';
                printType(member.type);
                out_ << '\n';
            }
            for (const auto& method : cl.methods) {
                printDecl(*method, level + 1);
            }
            break;
        }
    }
}

void ASTPrinter::printType(const Type& type) const {
    out_ << type.name;
    for (std::size_t i = 0; i < type.pointerDepth; ++i) {
        out_ << '*';
    }
    for (const auto& extent : type.arrayExtents) {
        out_ << '[';
        if (extent.has_value()) {
            out_ << *extent;
        }
        out_ << ']';
    }
}

}  // namespace axc
