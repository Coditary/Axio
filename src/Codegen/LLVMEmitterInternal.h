#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

class DiagnosticEngine;
class SourceManager;

struct Symbol {
    llvm::Value* address = nullptr;
    Type type {};
    bool nullable = false;
};

struct MultiValue {
    std::vector<llvm::Value*> values {};
};

struct EnumValueInfo {
    bool isFlags = false;
    std::uint64_t maxOrdinal = 0;
    std::unordered_map<std::string, std::uint64_t> values {};
    std::unordered_map<std::string, std::unordered_map<std::string, std::uint64_t>> paramValues {};
};

struct AggregateLayout {
    std::vector<Type> fieldTypes {};
    std::unordered_map<std::string, std::size_t> fieldIndices {};
};

class ModuleEmitter {
  public:
    ModuleEmitter(const SourceManager& sourceManager, DiagnosticEngine& diagnostics);

    std::unique_ptr<llvm::Module> emit(const TranslationUnit& translationUnit);

  private:
    void collectEnum(const EnumDecl& declaration);

    bool isUnsignedType(const Type& type) const;
    llvm::Value* castValueToType(llvm::Value* value, const Type& targetType);
    llvm::Type* lowerType(const Type& type);
    bool isLowerableFunction(const FunctionDecl& declaration);
    Type functionReturnType(const FunctionDecl& declaration);
    llvm::Type* lowerFunctionReturnType(const FunctionDecl& declaration);
    Type inferExprType(const Expr& expr, std::size_t valueIndex = 0) const;
    std::optional<std::string> inferClassTypeName(const Expr& expr) const;
    std::optional<std::string> moduleQualifiedName(const Expr& expr) const;

    void declareStruct(const StructDecl& declaration);
    void declareClass(const ClassDecl& declaration);
    void declareFunction(const FunctionDecl& declaration);
    llvm::AllocaInst* createEntryAlloca(llvm::Function* function, llvm::Type* type, llvm::StringRef name);
    std::optional<Symbol> lookupSymbol(const std::string& name) const;

    llvm::Value* emitStringConstant(const std::string& value, const std::string& nameHint);
    MultiValue emitExprValues(const Expr& expr);
    llvm::Value* emitCompileCall(const CompileCallExpr& call);
    llvm::Value* emitLValue(const Expr& expr);
    llvm::Value* emitExpr(const Expr& expr);
    bool emitStmt(const Stmt& stmt, const FunctionDecl& functionDecl);
    void defineFunction(const FunctionDecl& declaration);

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
    std::unordered_map<std::string, EnumValueInfo> enumValues_ {};
    std::unordered_map<std::string, std::unordered_set<std::string>> classFieldNames_ {};
    std::unordered_map<std::string, std::unordered_set<std::string>> classMethodNames_ {};
    std::unordered_map<std::string, Symbol> locals_ {};
};

std::string shellQuote(const std::filesystem::path& path);
bool emitObjectFile(llvm::Module& module, const std::filesystem::path& objectPath, DiagnosticEngine& diagnostics, SourceRange errorRange);

}  // namespace axc
