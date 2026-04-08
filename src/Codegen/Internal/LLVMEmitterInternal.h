#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/IR/Attributes.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/ValueMapper.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include "axc/AST/AST.h"
#include "axc/Support/InlineLlvm.h"
#include "axc/Support/QualifiedName.h"

namespace axc {

namespace detail {
class ModuleEmissionWorkflow;
}

class DiagnosticEngine;
class SourceManager;

/// @brief Addressable storage slot tracked during code generation.
struct Symbol {
    llvm::Value* address = nullptr;
    Type type {};
    bool nullable = false;
};

/// @brief Runtime helper functions used by ARC and weak-reference lowering.
struct RuntimeFunctions {
    llvm::Function* arcAlloc = nullptr;
    llvm::Function* arcRetain = nullptr;
    llvm::Function* arcRelease = nullptr;
    llvm::Function* weakInit = nullptr;
    llvm::Function* weakRelease = nullptr;
    llvm::Function* weakLoad = nullptr;
    llvm::Function* arcStrongCount = nullptr;
};

/// @brief Container for lowered multi-return expression values.
struct MultiValue {
    std::vector<llvm::Value*> values {};
};

/// @brief Lowered enum metadata used during LLVM emission and constant folding.
struct EnumValueInfo {
    bool isFlags = false;
    std::uint64_t maxOrdinal = 0;
    std::unordered_map<std::string, std::uint64_t> values {};
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint64_t>> paramValues {};
};

/// @brief Struct/class field layout information used during member lowering.
struct AggregateLayout {
    std::vector<Type> fieldTypes {};
    std::unordered_map<std::string, std::size_t> fieldIndices {};
};

/// @brief Deferred call captured for execution when leaving a scope.
struct DeferredCall {
    const CallExpr* call = nullptr;
    SourceRange range {};
};

/// @brief Break/continue targets active for the current control-flow nesting.
struct BranchTarget {
    llvm::BasicBlock* breakBlock = nullptr;
    llvm::BasicBlock* continueBlock = nullptr;
    std::size_t deferDepth = 0;
};

/// @brief Internal LLVM lowering engine backing the public `LLVMEmitter` facade.
class ModuleEmitter {
  public:
    /// @brief Create a lowering engine bound to one source manager and diagnostics sink.
    ModuleEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    /// @brief Lower a translation unit into a fresh LLVM module.
    std::unique_ptr<llvm::Module> emit(const TranslationUnit& translationUnit);

  private:
    friend class detail::ModuleEmissionWorkflow;

    /// @brief Cache enum constants for later code generation.
    void collectEnum(const EnumDecl& declaration);

    /// @brief Return whether a type lowers as an unsigned integer.
    bool isUnsignedType(const Type& type) const;
    /// @brief Return whether storage for a type may contain `null`.
    bool isNullableStorageType(const Type& type) const;
    /// @brief Cast a lowered LLVM value into the requested target type.
    llvm::Value* castValueToType(llvm::Value* value, const Type& targetType);
    /// @brief Lower a parsed Axio type into an LLVM type.
    llvm::Type* lowerType(const Type& type);
    /// @brief Compute the stored representation type for a global variable.
    Type globalStorageType(const GlobalVarDecl& declaration) const;
    /// @brief Lower a compile-time constant expression.
    llvm::Constant* lowerConstantExpr(const Expr& expr, const Type& targetType);
    /// @brief Return whether a function can be lowered by the current backend.
    bool isLowerableFunction(const FunctionDecl& declaration);
    /// @brief Compute the semantic return type used for single-value lowering.
    Type functionReturnType(const FunctionDecl& declaration);
    /// @brief Lower a function signature's return type to LLVM.
    llvm::Type* lowerFunctionReturnType(const FunctionDecl& declaration);
    /// @brief Infer the Axio type of an expression during lowering.
    Type inferExprType(const Expr& expr, std::size_t valueIndex = 0) const;
    /// @brief Infer the class type referenced by an expression, when possible.
    std::optional<std::string> inferClassTypeName(const Expr& expr) const;
    /// @brief Resolve an expression to a module-qualified symbol name.
    std::optional<std::string> moduleQualifiedName(const Expr& expr) const;

    /// @brief Declare an LLVM struct for one Axio struct.
    void declareStruct(const StructDecl& declaration);
    /// @brief Declare an LLVM struct for one Axio class.
    void declareClass(const ClassDecl& declaration);
    /// @brief Declare one global variable.
    void declareGlobal(const GlobalVarDecl& declaration);
    /// @brief Declare one function prototype.
    void declareFunction(const FunctionDecl& declaration);
    /// @brief Allocate storage in the function entry block.
    llvm::AllocaInst* createEntryAlloca(llvm::Function* function, llvm::Type* type, llvm::StringRef name);
    /// @brief Look up a symbol in local or global lowering scope.
    std::optional<Symbol> lookupSymbol(const std::string& name) const;

    /// @brief Materialize a string constant in the LLVM module.
    llvm::Value* emitStringConstant(const std::string& value, const std::string& nameHint);
    /// @brief Lower an expression that may yield multiple runtime values.
    MultiValue emitExprValues(const Expr& expr);
    /// @brief Lower an expression as an addressable l-value.
    llvm::Value* emitLValue(const Expr& expr);
    /// @brief Lower an expression as a single runtime LLVM value.
    llvm::Value* emitExpr(const Expr& expr);
    /// @brief Lower one statement in the context of the current function.
    bool emitStmt(const Stmt& stmt, const FunctionDecl& functionDecl);
    /// @brief Emit the body for one declared function.
    void defineFunction(const FunctionDecl& declaration);
    /// @brief Emit a conditional branch for loop-style control flow.
    bool emitLoopConditionBranch(const Expr* condition, llvm::BasicBlock* trueBlock, llvm::BasicBlock* falseBlock, llvm::StringRef nameHint);
    /// @brief Emit deferred calls for all scopes deeper than `scopeDepth`.
    bool emitDeferredCallsFromDepth(std::size_t scopeDepth);
    /// @brief Emit deferred calls for the current lexical scope only.
    bool emitDeferredCallsForCurrentScope();
    /// @brief Emit deferred calls for all active scopes.
    bool emitAllDeferredCalls();
    /// @brief Emit one deferred call expression.
    bool emitDeferredCall(const DeferredCall& deferred, const FunctionDecl& functionDecl);
    /// @brief Import verified inline LLVM text into the destination function.
    bool importInlineLlvmBody(llvm::Function& destination, const FunctionDecl& declaration, const std::string& loweredName);
    /// @brief Declare runtime helper functions used by lowered ARC/weak operations.
    void declareRuntimeFunctions();
    /// @brief Return whether a type uses ARC heap ownership.
    bool isArcOwnedType(const Type& type) const;
    /// @brief Return whether a type uses weak ownership.
    bool isWeakType(const Type& type) const;
    /// @brief Return whether a type uses unique ownership.
    bool isUniqueType(const Type& type) const;
    /// @brief Return whether a type names a class.
    bool isClassType(const Type& type) const;
    /// @brief Retain a value when storing into ARC-managed storage.
    llvm::Value* retainForStorage(llvm::Value* value, const Type& type);
    /// @brief Decide whether storing an expression requires an ARC retain.
    bool shouldRetainForStorage(const Expr& expr, const Type& type) const;
    /// @brief Release a previously stored ARC/weak value.
    void releaseStoredValue(llvm::Value* address, const Type& type);
    /// @brief Release currently tracked local values at scope exit.
    void releaseLocals();

    /// Source mapping used for diagnostics and inline-LLVM helpers.
    const SourceManager& sourceManager_;
    /// Shared diagnostic sink.
    DiagnosticEngine& diagnostics_;
    /// LLVM context for the current emission session.
    llvm::LLVMContext context_ {};
    /// Output module under construction.
    std::unique_ptr<llvm::Module> module_;
    /// Instruction builder positioned at the current insertion point.
    llvm::IRBuilder<> builder_;
    /// Function currently being lowered.
    llvm::Function* currentFunction_ = nullptr;
    /// Declared LLVM struct/class types by Axio name.
    std::unordered_map<std::string, llvm::StructType*> structTypes_ {};
    /// Cached aggregate field layouts.
    std::unordered_map<std::string, AggregateLayout> aggregateLayouts_ {};
    /// Declared LLVM functions by lowered name.
    std::unordered_map<std::string, llvm::Function*> functions_ {};
    /// Original AST function declarations by lowered name.
    std::unordered_map<std::string, const FunctionDecl*> functionDecls_ {};
    /// Lowered global symbols.
    std::unordered_map<std::string, Symbol> globals_ {};
    /// Enum metadata for code generation and constant folding.
    std::unordered_map<std::string, EnumValueInfo> enumValues_ {};
    /// Known class field names by class.
    std::unordered_map<std::string, std::unordered_set<std::string>> classFieldNames_ {};
    /// Known class method names by class.
    std::unordered_map<std::string, std::unordered_set<std::string>> classMethodNames_ {};
    /// Currently visible local symbols.
    std::unordered_map<std::string, Symbol> locals_ {};
    /// Deferred calls grouped by lexical scope depth.
    std::vector<std::vector<DeferredCall>> deferScopes_ {};
    /// Active break/continue targets for nested control flow.
    std::vector<BranchTarget> branchTargets_ {};
    /// Cached runtime helper function declarations.
    RuntimeFunctions runtime_ {};
};

/// @brief Safely quote a filesystem path for shell command construction.
std::string shellQuote(const std::filesystem::path& path);
/// @brief Emit a native object file from an LLVM module.
bool emitObjectFile(llvm::Module& module, const std::filesystem::path& objectPath, DiagnosticEngine& diagnostics, SourceRange errorRange);

}  // namespace axc
