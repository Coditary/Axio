#pragma once

#include "axc/AST/Stmt.h"

namespace axc {

enum class DeclKind {
    Import,
    Struct,
    Enum,
    Class,
    Function,
};

struct Decl {
    Decl(DeclKind kind, std::string name, SourceRange range)
        : kind(kind), name(std::move(name)), range(range) {}
    virtual ~Decl() = default;

    DeclKind kind;
    std::string name {};
    std::vector<Annotation> annotations {};
    SourceRange range {};
};

struct StructField {
    std::string name {};
    Type type {};
    std::unique_ptr<Expr> defaultValue {};
    std::int64_t bitWidth = -1;
    SourceRange range {};
};

struct ImportDecl final : Decl {
    ImportDecl(std::string modulePath, std::vector<std::string> moduleSegments, SourceRange range)
        : Decl(DeclKind::Import, std::move(modulePath), range), moduleSegments(std::move(moduleSegments)) {}

    std::vector<std::string> moduleSegments {};
};

struct StructDecl final : Decl {
    StructDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Struct, std::move(name), range) {}

    std::vector<StructField> fields {};
    std::int64_t alignment = 0;
};

struct EnumParam {
    std::string name {};
    Type type {};
    SourceRange range {};
};

struct EnumElement {
    std::string name {};
    std::vector<Type> payloadTypes {};
    std::vector<std::unique_ptr<Expr>> payloadValues {};
    std::vector<std::unique_ptr<Decl>> nestedDecls {};
    bool isFlagGroup = false;
    std::optional<std::uint64_t> constantValue {};
    SourceRange range {};
};

struct EnumDecl final : Decl {
    EnumDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Enum, std::move(name), range) {}

    std::vector<EnumParam> parameters {};
    std::vector<EnumElement> elements {};
    bool isFlags = false;
};

struct ClassMember {
    std::string name {};
    Type type {};
    std::unique_ptr<Expr> dynamicValue {};
    SourceRange range {};
};

struct ClassDecl final : Decl {
    ClassDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Class, std::move(name), range) {}

    std::vector<std::string> includedStructs {};
    std::vector<ClassMember> members {};
    std::vector<std::unique_ptr<Decl>> methods {};
};

struct Parameter {
    std::string name {};
    Type type {};
    bool isCompileTime = false;
    SourceRange range {};
};

struct FunctionDecl final : Decl {
    FunctionDecl(std::string name, SourceRange range)
        : Decl(DeclKind::Function, std::move(name), range) {}

    std::vector<Parameter> compileParameters {};
    std::vector<Parameter> runtimeParameters {};
    std::vector<Type> returnTypes {};
    std::unique_ptr<CompoundStmt> body {};
    bool isExtern = false;
    std::string receiverType {};
};

struct TranslationUnit {
    std::vector<PreprocessorDirective> preprocessorDirectives {};
    std::vector<std::unique_ptr<Decl>> declarations {};
};

}  // namespace axc
