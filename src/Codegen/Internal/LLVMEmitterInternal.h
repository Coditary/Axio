#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>

#include "axc/AST/AST.h"
#include "axc/Support/QualifiedName.h"

namespace axc {

namespace detail {
class ModuleEmissionWorkflow;
}

class DiagnosticEngine;
class SourceManager;

struct Symbol {
    llvm::Value* address = nullptr;
    Type type {};
};

struct EnumValueInfo {
    std::uint64_t maxOrdinal = 0;
    std::unordered_map<std::string, std::uint64_t> values {};
};

struct AggregateLayout {
    std::vector<Type> fieldTypes {};
    std::unordered_map<std::string, std::size_t> fieldIndices {};
};

struct DeferredCall {
    const CallExpr* call = nullptr;
    SourceRange range {};
};

struct BranchTarget {
    llvm::BasicBlock* breakBlock = nullptr;
    llvm::BasicBlock* continueBlock = nullptr;
    std::size_t deferDepth = 0;
};

class ModuleEmitter {
  public:
    ModuleEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    std::unique_ptr<llvm::Module> emit(const TranslationUnit& translationUnit);

  private:
    friend class detail::ModuleEmissionWorkflow;

    void collectEnum(const EnumDecl& declaration);

    bool isUnsignedType(const Type& type) const;
    llvm::Value* castValueToType(llvm::Value* value, const Type& targetType);
    llvm::Type* lowerType(const Type& type);
    Type globalStorageType(const GlobalVarDecl& declaration) const;
    llvm::Constant* lowerConstantExpr(const Expr& expr, const Type& targetType);
    bool isLowerableFunction(const FunctionDecl& declaration);
    llvm::Type* lowerFunctionReturnType(const FunctionDecl& declaration);
    Type inferExprType(const Expr& expr) const;
    std::optional<std::string> inferClassTypeName(const Expr& expr) const;
    std::optional<std::string> moduleQualifiedName(const Expr& expr) const;

    void declareStruct(const StructDecl& declaration);
    void declareClass(const ClassDecl& declaration);
    void declareGlobal(const GlobalVarDecl& declaration);
    void declareFunction(const FunctionDecl& declaration);
    void defineFunction(const FunctionDecl& declaration);
    bool stmtAlwaysReturns(const Stmt& stmt) const;
    llvm::AllocaInst* createEntryAlloca(llvm::Function* function, llvm::Type* type, llvm::StringRef name);
    std::optional<Symbol> lookupSymbol(const std::string& name) const;

    llvm::Value* emitStringConstant(const std::string& value, const std::string& nameHint);
    llvm::Value* emitLValue(const Expr& expr);
    llvm::Value* emitExpr(const Expr& expr);
    bool emitStmt(const Stmt& stmt, const FunctionDecl& functionDecl);
    bool emitLoopConditionBranch(const Expr* condition, llvm::BasicBlock* trueBlock, llvm::BasicBlock* falseBlock, llvm::StringRef nameHint);
    bool emitDeferredCallsFromDepth(std::size_t scopeDepth);
    bool emitDeferredCallsForCurrentScope();
    bool emitAllDeferredCalls();
    bool emitDeferredCall(const DeferredCall& deferred, const FunctionDecl& functionDecl);

    const SourceManager& sourceManager_;
    DiagnosticEngine& diagnostics_;
    llvm::LLVMContext context_ {};
    std::unique_ptr<llvm::Module> module_;
    llvm::IRBuilder<> builder_;
    llvm::Function* currentFunction_ = nullptr;
    std::unordered_map<std::string, llvm::StructType*> structTypes_ {};
    std::unordered_map<std::string, AggregateLayout> aggregateLayouts_ {};
    std::unordered_map<std::string, llvm::Function*> functions_ {};
    std::unordered_map<std::string, const FunctionDecl*> functionDecls_ {};
    std::unordered_map<std::string, Symbol> globals_ {};
    std::unordered_map<std::string, EnumValueInfo> enumValues_ {};
    std::unordered_map<std::string, std::unordered_set<std::string>> classFieldNames_ {};
    std::unordered_map<std::string, std::unordered_set<std::string>> classMethodNames_ {};
    std::unordered_map<std::string, Symbol> locals_ {};
    std::vector<std::vector<DeferredCall>> deferScopes_ {};
    std::vector<BranchTarget> branchTargets_ {};
};

std::string shellQuote(const std::filesystem::path& path);
bool emitObjectFile(llvm::Module& module, const std::filesystem::path& objectPath, DiagnosticEngine& diagnostics, SourceRange errorRange);

}  // namespace axc
