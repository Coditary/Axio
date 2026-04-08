#include "../Internal/LLVMEmitterInternal.h"

#include <llvm/IR/Argument.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

#include "axc/Support/Diagnostic.h"

namespace axc {

bool ModuleEmitter::stmtAlwaysReturns(const Stmt& stmt) const {
    switch (stmt.kind) {
        case StmtKind::Return:
            return true;
        case StmtKind::Compound: {
            const auto& block = static_cast<const CompoundStmt&>(stmt);
            for (const auto& child : block.statements) {
                if (stmtAlwaysReturns(*child)) {
                    return true;
                }
            }
            return false;
        }
        case StmtKind::If: {
            const auto& ifStmt = static_cast<const IfStmt&>(stmt);
            return ifStmt.elseBranch != nullptr && stmtAlwaysReturns(*ifStmt.thenBlock) && stmtAlwaysReturns(*ifStmt.elseBranch);
        }
        default:
            return false;
    }
}

void ModuleEmitter::declareStruct(const StructDecl& declaration) {
    auto* structType = llvm::StructType::create(context_, declaration.name);
    structTypes_[declaration.name] = structType;

    std::vector<llvm::Type*> fields;
    fields.reserve(declaration.fields.size());
    AggregateLayout layout;
    for (const StructField& field : declaration.fields) {
        layout.fieldIndices[field.name] = layout.fieldTypes.size();
        layout.fieldTypes.push_back(field.type);
        fields.push_back(lowerType(field.type));
    }

    structType->setBody(fields, false);
    aggregateLayouts_[declaration.name] = std::move(layout);
}

void ModuleEmitter::declareClass(const ClassDecl& declaration) {
    auto* classType = llvm::StructType::create(context_, declaration.name);
    structTypes_[declaration.name] = classType;

    std::vector<llvm::Type*> fields;
    AggregateLayout layout;
    for (const auto& member : declaration.members) {
        layout.fieldIndices[member.name] = layout.fieldTypes.size();
        layout.fieldTypes.push_back(member.type);
        fields.push_back(lowerType(member.type));
    }

    if (fields.empty()) {
        fields.push_back(llvm::Type::getInt8Ty(context_));
        Type paddingType;
        paddingType.name = "char";
        layout.fieldIndices["__padding"] = 0;
        layout.fieldTypes.push_back(paddingType);
    }

    classType->setBody(fields, false);
    aggregateLayouts_[declaration.name] = std::move(layout);
}

void ModuleEmitter::declareGlobal(const GlobalVarDecl& declaration) {
    Type storageType = globalStorageType(declaration);
    llvm::Type* llvmType = lowerType(storageType);
    llvm::Constant* initializer = nullptr;
    if (declaration.initializer) {
        initializer = lowerConstantExpr(*declaration.initializer, storageType);
        if (initializer == nullptr) {
            diagnostics_.error(declaration.range, "global initializers must be compile-time constants in this prototype");
            initializer = llvm::Constant::getNullValue(llvmType);
        }
    } else {
        initializer = llvm::Constant::getNullValue(llvmType);
    }

    auto* global = new llvm::GlobalVariable(*module_,
                                            llvmType,
                                            !declaration.mutableStorage,
                                            llvm::GlobalValue::ExternalLinkage,
                                            initializer,
                                            declaration.name);
    globals_[declaration.name] = Symbol {global, storageType};
}

void ModuleEmitter::declareFunction(const FunctionDecl& declaration) {
    if (!isLowerableFunction(declaration)) {
        return;
    }

    const std::string loweredName = declaration.receiverType.empty() ? declaration.name : declaration.receiverType + "." + declaration.name;

    if (functions_.contains(loweredName)) {
        diagnostics_.error(declaration.range, "duplicate function declaration for '" + loweredName + "'");
        return;
    }

    if (llvm::Function* existing = module_->getFunction(loweredName); existing != nullptr) {
        functions_[loweredName] = existing;
        functionDecls_[loweredName] = &declaration;
        return;
    }

    std::vector<llvm::Type*> params;
    params.reserve(declaration.parameters.size());
    for (const Parameter& param : declaration.parameters) {
        params.push_back(lowerType(param.type));
    }

    llvm::FunctionType* functionType = llvm::FunctionType::get(lowerFunctionReturnType(declaration), params, false);
    llvm::Function* function = llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, loweredName, module_.get());

    std::size_t index = 0;
    for (llvm::Argument& arg : function->args()) {
        arg.setName(declaration.parameters[index].name);
        ++index;
    }

    functions_[loweredName] = function;
    functionDecls_[loweredName] = &declaration;
}

llvm::AllocaInst* ModuleEmitter::createEntryAlloca(llvm::Function* function, llvm::Type* type, llvm::StringRef name) {
    llvm::IRBuilder<> entryBuilder(&function->getEntryBlock(), function->getEntryBlock().begin());
    return entryBuilder.CreateAlloca(type, nullptr, name);
}

std::optional<Symbol> ModuleEmitter::lookupSymbol(const std::string& name) const {
    auto it = locals_.find(name);
    if (it != locals_.end()) {
        return it->second;
    }
    auto globalIt = globals_.find(name);
    if (globalIt != globals_.end()) {
        return globalIt->second;
    }
    return std::nullopt;
}

void ModuleEmitter::defineFunction(const FunctionDecl& declaration) {
    const std::string loweredName = declaration.receiverType.empty() ? declaration.name : declaration.receiverType + "." + declaration.name;
    auto it = functions_.find(loweredName);
    if (it == functions_.end()) {
        return;
    }

    llvm::Function* function = it->second;
    if (!function->empty()) {
        diagnostics_.error(declaration.range, "duplicate function definition for '" + loweredName + "'");
        return;
    }

    if (declaration.body == nullptr) {
        return;
    }

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(context_, "entry", function);
    builder_.SetInsertPoint(entry);
    currentFunction_ = function;
    locals_.clear();
    deferScopes_.clear();

    std::size_t index = 0;
    for (llvm::Argument& arg : function->args()) {
        const Parameter& parameter = declaration.parameters[index];
        llvm::AllocaInst* alloca = createEntryAlloca(function, arg.getType(), arg.getName());
        builder_.CreateStore(&arg, alloca);
        locals_[parameter.name] = Symbol {alloca, parameter.type};
        ++index;
    }

    if (!emitStmt(*declaration.body, declaration)) {
        function->eraseFromParent();
        functions_.erase(loweredName);
        return;
    }

    if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
        if (declaration.returnsVoid()) {
            builder_.CreateRetVoid();
        } else if (stmtAlwaysReturns(*declaration.body)) {
            llvm::BasicBlock* deadBlock = builder_.GetInsertBlock();
            builder_.ClearInsertionPoint();
            deadBlock->eraseFromParent();
        } else {
            diagnostics_.error(declaration.range, "control reaches end of non-void function '" + declaration.name + "'");
            function->eraseFromParent();
            functions_.erase(loweredName);
            return;
        }
    }

    if (llvm::verifyFunction(*function, &llvm::errs())) {
        diagnostics_.error(declaration.range, "LLVM verification failed for function '" + declaration.name + "'");
    }
}

}  // namespace axc
