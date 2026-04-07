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

/// @brief Semantic enum metadata built during analysis.
struct EnumInfo {
    bool isFlags = false;
    std::uint64_t maxOrdinal = 0;
    std::unordered_map<std::string, std::uint64_t> values {};
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint64_t>> paramValues {};
};

/// @brief Semantic value properties tracked for symbols.
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

/// @brief Semantic class metadata used for member and method validation.
struct ClassInfo {
    std::unordered_set<std::string> fields {};
    std::unordered_set<std::string> methods {};
    std::unordered_map<std::string, Type> fieldTypes {};
    std::unordered_map<std::string, std::size_t> methodArgumentCounts {};
    std::unordered_map<std::string, std::vector<ValueInfo::Ownership>> methodParamOwnerships {};
};

/// @brief Internal semantic-analysis implementation.
///
/// `Sema` is the public facade; `SemaImpl` owns the mutable analysis state and
/// the helper routines needed for symbol tables, enum metadata, ownership
/// tracking, nullability checks, and switch exhaustiveness analysis.
class SemaImpl {
  public:
    /// @brief Create the semantic analyzer implementation.
    explicit SemaImpl(DiagnosticEngine& diagnostics);

    /// @brief Analyze one translation unit.
    bool analyze(TranslationUnit& translationUnit);

  private:
    /// @brief Derive ownership behavior from a parsed type.
    ValueInfo::Ownership ownershipFromType(const Type& type) const;
    /// @brief Derive ownership behavior from an initializer form.
    ValueInfo::Ownership ownershipFromInitKind(InitKind kind) const;
    /// @brief Return whether a type is represented through a pointer-like value.
    bool isPointerLike(const Type& type) const;
    /// @brief Return whether a type may legally carry `null`.
    bool typeSupportsNullability(const Type& type) const;
    /// @brief Compute the semantic type of an expression value.
    std::optional<Type> exprType(const Expr& expr, std::size_t valueIndex = 0) const;
    /// @brief Resolve an expression to a module-qualified name when possible.
    std::optional<std::string> moduleQualifiedName(const Expr& expr) const;
    /// @brief Return whether a name is known in the current symbol environment.
    bool isKnownDeclRefName(const std::string& name) const;
    /// @brief Record function signatures before validating bodies.
    void recordFunctionSignatures(TranslationUnit& translationUnit);
    /// @brief Build class metadata tables used by semantic validation.
    void buildClassTables(TranslationUnit& translationUnit);
    /// @brief Infer semantic value properties from an expression.
    ValueInfo inferExpr(const Expr& expr) const;
    /// @brief Count the number of values produced by one expression.
    std::size_t exprValueCount(const Expr& expr) const;
    /// @brief Count values produced by a list of expressions after flattening tuples.
    std::size_t flattenedValueCount(const std::vector<std::unique_ptr<Expr>>& values) const;
    /// @brief Return whether an expression subtree references a given declaration name.
    bool containsDeclRef(const Expr& expr, const std::string& name) const;
    /// @brief Detect repeated use of a unique value within one expression tree.
    void collectRepeatedUniqueUses(const Expr& expr, std::unordered_set<std::string>& seen, const std::string& message);
    /// @brief Require that an expression produces exactly one runtime value.
    void requireSingleValue(const Expr& expr, const std::string& message);
    /// @brief Build enum metadata tables used by constant evaluation and switch checks.
    void buildEnumTables(TranslationUnit& translationUnit);
    /// @brief Infer the element type yielded by a `foreach` statement.
    Type foreachElementType(const ForeachStmt& stmt) const;
    /// @brief Compute missing enum cases for exhaustiveness diagnostics.
    std::vector<std::string> missingEnumSwitchCases(const SwitchStmt& stmt, const Type& conditionType) const;
    /// @brief Expand a switch pattern into concrete constant values for overlap/exhaustiveness checks.
    std::vector<std::pair<std::uint64_t, std::string>> expandSwitchPatternValues(const SwitchCasePattern& pattern,
                                                                                  const Type& conditionType) const;

    /// @brief Validate one declaration subtree.
    void validateDecl(const Decl& decl);
    /// @brief Validate one global variable declaration.
    void validateGlobal(const GlobalVarDecl& decl);
    /// @brief Validate a class declaration and its members.
    void validateClass(const ClassDecl& decl);
    /// @brief Validate a function signature and body.
    void validateFunction(const FunctionDecl& fn);
    /// @brief Validate a statement with default control-flow nesting state.
    void validateStmt(const Stmt& stmt, const FunctionDecl& fn);
    /// @brief Validate a statement with explicit loop/switch nesting state.
    void validateStmt(const Stmt& stmt, const FunctionDecl& fn, std::size_t loopDepth, std::size_t switchDepth);

    /// @brief Try to constant-evaluate an expression to an unsigned integer.
    std::optional<std::uint64_t> evalExpr(const Expr& expr) const;
    /// @brief Resolve the enum type referenced by an expression, when applicable.
    std::optional<std::string> enumTypeName(const Expr& expr) const;
    /// @brief Validate one expression subtree.
    void validateExpr(const Expr& expr);

    /// Shared diagnostic sink.
    DiagnosticEngine& diagnostics_;
    /// Enum metadata indexed by enum type name.
    std::unordered_map<std::string, EnumInfo> enumInfos_ {};
    /// Class metadata indexed by class name.
    std::unordered_map<std::string, ClassInfo> classInfos_ {};
    /// Global symbol properties.
    std::unordered_map<std::string, ValueInfo> globalSymbols_ {};
    /// Global symbol types.
    std::unordered_map<std::string, Type> globalSymbolTypes_ {};
    /// Current lexical scope symbol properties.
    std::unordered_map<std::string, ValueInfo> symbols_ {};
    /// Current lexical scope symbol types.
    std::unordered_map<std::string, Type> symbolTypes_ {};
    /// Known struct fields by struct name.
    std::unordered_map<std::string, std::unordered_set<std::string>> structFields_ {};
    /// Known struct field types by struct name.
    std::unordered_map<std::string, std::unordered_map<std::string, Type>> structFieldTypes_ {};
    /// Unique values already consumed in the current validation context.
    std::unordered_set<std::string> consumedUnique_ {};
    /// Function parameter ownership metadata.
    std::unordered_map<std::string, std::vector<ValueInfo::Ownership>> functionParamOwnership_ {};
    /// Function arity metadata.
    std::unordered_map<std::string, std::size_t> functionArgumentCount_ {};
    /// Function return-count metadata.
    std::unordered_map<std::string, std::size_t> functionReturnCount_ {};
    /// Function return-type metadata.
    std::unordered_map<std::string, std::vector<Type>> functionReturnTypes_ {};
};

}  // namespace axc
