#pragma once

#include "axc/AST/Stmt.h"

namespace axc {

/// @brief Kinds of top-level declarations stored in the translation unit.
enum class DeclKind {
    Import,
    GlobalVar,
    Struct,
    Enum,
    Class,
    Function,
};

/// @brief Base class for all top-level declarations.
struct Decl {
    Decl(DeclKind kind, std::string name, SourceRange range)
        : kind(kind), name(name), localName(std::move(name)), range(range) {}
    virtual ~Decl() = default;

    DeclKind kind;
    std::string name {};
    std::string localName {};
    SourceRange range {};
    Visibility visibility = Visibility::Private;
    std::string moduleName {};
};

/// @brief Field declaration inside a struct.
struct StructField {
    std::string name {};
    Type type {};
    std::unique_ptr<Expr> defaultValue {};
    std::int64_t bitWidth = -1;
    SourceRange range {};
};

/// @brief `import` declaration with optional aliasing and selective imports.
struct ImportDecl final : Decl {
    ImportDecl(std::string modulePath, std::vector<std::string> moduleSegments, SourceRange range)
        : Decl(DeclKind::Import, std::move(modulePath), range), moduleSegments(std::move(moduleSegments)) {}

    std::vector<std::string> moduleSegments {};
    std::string alias {};
    std::vector<std::string> importedNames {};
};

/// @brief Global variable declaration.
struct GlobalVarDecl final : Decl {
    GlobalVarDecl(std::string name, SourceRange range)
        : Decl(DeclKind::GlobalVar, std::move(name), range) {}

    Type type {};
    std::unique_ptr<Expr> initializer {};
    bool mutableStorage = true;
};

/// @brief Struct declaration.
struct StructDecl final : Decl {
    StructDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Struct, std::move(name), range) {}

    std::vector<StructField> fields {};
};

/// @brief Single enum element.
struct EnumElement {
    std::string name {};
    SourceRange range {};
};

/// @brief Enum declaration.
struct EnumDecl final : Decl {
    EnumDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Enum, std::move(name), range) {}

    std::vector<EnumElement> elements {};
};

/// @brief Field declared inside a class.
struct ClassMember {
    std::string name {};
    Type type {};
    SourceRange range {};
    Visibility visibility = Visibility::Private;
};

/// @brief Class declaration with fields and method declarations.
struct ClassDecl final : Decl {
    ClassDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Class, std::move(name), range) {}

    std::vector<ClassMember> members {};
    std::vector<std::unique_ptr<Decl>> methods {};
};

/// @brief Function parameter as parsed from a signature.
struct Parameter {
    std::string name {};
    Type type {};
    SourceRange range {};
    bool isConst = false;
};

/// @brief Function or method declaration.
struct FunctionDecl final : Decl {
    FunctionDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Function, std::move(name), range) {}

    std::vector<Parameter> parameters {};
    std::optional<Type> returnType {};
    std::unique_ptr<CompoundStmt> body {};
    bool isExtern = false;
    std::string receiverType {};

    /// @brief Returns whether the function has no runtime return values.
    [[nodiscard]] bool returnsVoid() const {
        return !returnType.has_value() || returnType->isVoid();
    }
};

/// @brief Parsed source file consisting of declarations plus package metadata.
struct TranslationUnit {
    std::string packageName {};
    SourceRange packageRange {};
    std::vector<std::unique_ptr<Decl>> declarations {};
};

}  // namespace axc
