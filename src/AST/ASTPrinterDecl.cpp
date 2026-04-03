#include "axc/AST/ASTPrinter.h"

#include <algorithm>

namespace axc {

void ASTPrinter::print(const TranslationUnit& unit) const {
    for (const auto& directive : unit.preprocessorDirectives) {
        out_ << "Preprocessor #" << directive.name;
        for (const auto& arg : directive.arguments) {
            out_ << ' ' << arg;
        }
        out_ << '\n';
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
    for (const auto& annotation : decl.annotations) {
        out_ << "Annotation @" << annotation.name << '\n';
        indent(level);
    }
    switch (decl.kind) {
        case DeclKind::Import: {
            const auto& importDecl = static_cast<const ImportDecl&>(decl);
            out_ << "Import " << importDecl.name << '\n';
            break;
        }
        case DeclKind::Function: {
            const auto& fn = static_cast<const FunctionDecl&>(decl);
            out_ << "Function " << fn.name << '\n';
            for (const auto& param : fn.compileParameters) {
                indent(level + 1);
                out_ << "CompileParam " << param.name << ':';
                printType(param.type);
                out_ << '\n';
            }
            for (const auto& param : fn.runtimeParameters) {
                indent(level + 1);
                out_ << "Param " << param.name << ':';
                printType(param.type);
                out_ << '\n';
            }
            if (!fn.returnTypes.empty()) {
                indent(level + 1);
                out_ << "Returns";
                for (const auto& type : fn.returnTypes) {
                    out_ << ' ';
                    printType(type);
                }
                out_ << '\n';
            }
            if (fn.body) {
                printStmt(*fn.body, level + 1);
            }
            break;
        }
        case DeclKind::Struct: {
            const auto& st = static_cast<const StructDecl&>(decl);
            out_ << "Struct " << st.name << '\n';
            for (const auto& field : st.fields) {
                indent(level + 1);
                out_ << "Field " << field.name << ':';
                printType(field.type);
                if (field.bitWidth >= 0) {
                    out_ << " bits " << field.bitWidth;
                }
                out_ << '\n';
            }
            break;
        }
        case DeclKind::Enum: {
            const auto& en = static_cast<const EnumDecl&>(decl);
            out_ << "Enum " << en.name;
            if (en.isFlags) {
                out_ << " as Flags";
            }
            out_ << '\n';
            for (const auto& element : en.elements) {
                indent(level + 1);
                out_ << "Element " << element.name;
                if (element.isFlagGroup) {
                    out_ << " as Flag";
                }
                out_ << '\n';
            }
            break;
        }
        case DeclKind::Class: {
            const auto& cl = static_cast<const ClassDecl&>(decl);
            out_ << "Class " << cl.name << '\n';
            for (const auto& member : cl.members) {
                indent(level + 1);
                out_ << "Member " << member.name << ':';
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
    const bool hasUnique = std::find(type.modifiers.begin(), type.modifiers.end(), TypeModifier::Unique) != type.modifiers.end();
    const bool hasRef = std::find(type.modifiers.begin(), type.modifiers.end(), TypeModifier::Ref) != type.modifiers.end();
    const bool hasWeak = std::find(type.modifiers.begin(), type.modifiers.end(), TypeModifier::Weak) != type.modifiers.end();

    const bool prefixQualifiers = !hasUnique && type.name != "Obj";
    if (prefixQualifiers) {
        if (hasRef) {
            out_ << "ref ";
        }
        if (hasWeak) {
            out_ << "weak ";
        }
    }
    for (const auto& modifier : type.modifiers) {
        switch (modifier) {
            case TypeModifier::Ref:
                break;
            case TypeModifier::Weak:
                break;
            case TypeModifier::Unique:
                break;
        }
    }
    out_ << type.name;
    for (std::size_t i = 0; i < type.pointerDepth; ++i) {
        out_ << '*';
    }
    if (hasUnique) {
        if (hasRef) {
            out_ << " ref";
        }
        if (hasWeak) {
            out_ << " weak";
        }
        out_ << " *";
    } else if (!prefixQualifiers) {
        if (hasRef) {
            out_ << " ref";
        }
        if (hasWeak) {
            out_ << " weak";
        }
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
