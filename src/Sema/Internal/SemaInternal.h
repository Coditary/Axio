#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "axc/AST/AST.h"

namespace axc {

class DiagnosticEngine;

struct EnumInfo {
    bool isFlags = false;
    std::uint64_t maxOrdinal = 0;
    std::unordered_map<std::string, std::uint64_t> values {};
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint64_t>> paramValues {};
};

struct ValueInfo {
    enum class Ownership {
        Unknown,
        Value,
        Arc,
        Weak,
        Unique,
        Ref,
    } ownership = Ownership::Unknown;

    std::string typeName {};
    bool nullable = false;
    bool mutableStorage = true;
};

struct ClassInfo {
    std::unordered_set<std::string> fields {};
    std::unordered_set<std::string> methods {};
    std::unordered_map<std::string, Type> fieldTypes {};
    std::unordered_map<std::string, std::size_t> methodArgumentCounts {};
    std::unordered_map<std::string, std::vector<ValueInfo::Ownership>> methodParamOwnerships {};
};

class SemaImpl {
  public:
    explicit SemaImpl(DiagnosticEngine& diagnostics);

    bool analyze(TranslationUnit& translationUnit);

  private:
    ValueInfo::Ownership ownershipFromType(const Type& type) const;
    ValueInfo::Ownership ownershipFromInitKind(InitKind kind) const;
    bool isPointerLike(const Type& type) const;
    bool typeSupportsNullability(const Type& type) const;
    std::optional<Type> exprType(const Expr& expr, std::size_t valueIndex = 0) const;
    std::optional<std::string> moduleQualifiedName(const Expr& expr) const;
    bool isKnownDeclRefName(const std::string& name) const;
    void recordFunctionSignatures(TranslationUnit& translationUnit);
    void buildClassTables(TranslationUnit& translationUnit);
    ValueInfo inferExpr(const Expr& expr) const;
    std::size_t exprValueCount(const Expr& expr) const;
    std::size_t flattenedValueCount(const std::vector<std::unique_ptr<Expr>>& values) const;
    bool containsDeclRef(const Expr& expr, const std::string& name) const;
    void collectRepeatedUniqueUses(const Expr& expr, std::unordered_set<std::string>& seen, const std::string& message);
    void requireSingleValue(const Expr& expr, const std::string& message);
    void buildEnumTables(TranslationUnit& translationUnit);
    Type foreachElementType(const ForeachStmt& stmt) const;
    std::vector<std::string> missingEnumSwitchCases(const SwitchStmt& stmt, const Type& conditionType) const;
    std::vector<std::pair<std::uint64_t, std::string>> expandSwitchPatternValues(const SwitchCasePattern& pattern,
                                                                                  const Type& conditionType) const;

    void validateDecl(const Decl& decl);
    void validateGlobal(const GlobalVarDecl& decl);
    void validateClass(const ClassDecl& decl);
    void validateFunction(const FunctionDecl& fn);
    void validateStmt(const Stmt& stmt, const FunctionDecl& fn);
    void validateStmt(const Stmt& stmt, const FunctionDecl& fn, std::size_t loopDepth, std::size_t switchDepth);

    std::optional<std::uint64_t> evalExpr(const Expr& expr) const;
    std::optional<std::string> enumTypeName(const Expr& expr) const;
    void validateExpr(const Expr& expr);

    DiagnosticEngine& diagnostics_;
    std::unordered_map<std::string, EnumInfo> enumInfos_ {};
    std::unordered_map<std::string, ClassInfo> classInfos_ {};
    std::unordered_map<std::string, ValueInfo> globalSymbols_ {};
    std::unordered_map<std::string, Type> globalSymbolTypes_ {};
    std::unordered_map<std::string, ValueInfo> symbols_ {};
    std::unordered_map<std::string, Type> symbolTypes_ {};
    std::unordered_map<std::string, std::unordered_set<std::string>> structFields_ {};
    std::unordered_map<std::string, std::unordered_map<std::string, Type>> structFieldTypes_ {};
    std::unordered_set<std::string> consumedUnique_ {};
    std::unordered_map<std::string, std::vector<ValueInfo::Ownership>> functionParamOwnership_ {};
    std::unordered_map<std::string, std::size_t> functionArgumentCount_ {};
    std::unordered_map<std::string, std::size_t> functionReturnCount_ {};
    std::unordered_map<std::string, std::vector<Type>> functionReturnTypes_ {};
};

}  // namespace axc
