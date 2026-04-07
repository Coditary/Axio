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
    std::vector<Annotation> annotations {};
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
    std::int64_t alignment = 0;
};

/// @brief Enum-level metadata parameter.
struct EnumParam {
    std::string name {};
    Type type {};
    SourceRange range {};
};

/// @brief Single enum element, optionally with payload types or nested flag items.
struct EnumElement {
    std::string name {};
    std::vector<Type> payloadTypes {};
    std::vector<std::unique_ptr<Expr>> payloadValues {};
    std::vector<std::unique_ptr<Decl>> nestedDecls {};
    bool isFlagGroup = false;
    std::optional<std::uint64_t> constantValue {};
    SourceRange range {};
};

/// @brief Enum declaration, including flag-style enums.
struct EnumDecl final : Decl {
    EnumDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Enum, std::move(name), range) {}

    std::vector<EnumParam> parameters {};
    std::vector<EnumElement> elements {};
    bool isFlags = false;
};

/// @brief Field or dynamic member declared inside a class.
struct ClassMember {
    std::string name {};
    Type type {};
    std::unique_ptr<Expr> dynamicValue {};
    SourceRange range {};
    Visibility visibility = Visibility::Private;
};

/// @brief Class declaration with fields and method declarations.
struct ClassDecl final : Decl {
    ClassDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Class, std::move(name), range) {}

    std::vector<std::string> includedStructs {};
    std::vector<ClassMember> members {};
    std::vector<std::unique_ptr<Decl>> methods {};
};

/// @brief Function parameter as parsed from a signature.
struct Parameter {
    std::string name {};
    Type type {};
    bool isCompileTime = false;
    SourceRange range {};
    bool isConst = false;
};

/// @brief Function or method declaration.
struct FunctionDecl final : Decl {
    FunctionDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Function, std::move(name), range) {}

    std::vector<Parameter> compileParameters {};
    std::vector<Parameter> runtimeParameters {};
    std::vector<Type> returnTypes {};
    std::unique_ptr<CompoundStmt> body {};
    std::string llvmBody {};
    SourceRange llvmBodyRange {};
    bool isExtern = false;
    bool isLlvm = false;
    std::string receiverType {};

    /// @brief Returns whether the function has no runtime return values.
    [[nodiscard]] bool returnsVoid() const {
        return returnTypes.empty() || (returnTypes.size() == 1 && returnTypes.front().isVoid());
    }

    /// @brief Number of runtime values produced by the function.
    [[nodiscard]] std::size_t returnValueCount() const {
        return returnsVoid() ? 0U : returnTypes.size();
    }
};

/// @brief Parsed source file consisting of declarations plus package metadata.
struct TranslationUnit {
    std::string packageName {};
    SourceRange packageRange {};
    std::vector<PreprocessorDirective> preprocessorDirectives {};
    std::vector<std::unique_ptr<Decl>> declarations {};
};

}  // namespace axc
