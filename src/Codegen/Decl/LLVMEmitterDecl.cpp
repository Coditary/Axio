#include "../Internal/LLVMEmitterInternal.h"

#include <llvm/AsmParser/Parser.h>
#include <llvm/IR/Argument.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/SourceMgr.h>

#include "axc/Support/Diagnostic.h"

namespace axc {

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
    for (const auto& includedStruct : declaration.includedStructs) {
        auto it = aggregateLayouts_.find(includedStruct);
        if (it == aggregateLayouts_.end()) {
            continue;
        }
        for (const auto& [fieldName, index] : it->second.fieldIndices) {
            layout.fieldIndices[fieldName] = layout.fieldTypes.size() + index;
        }
        for (const auto& type : it->second.fieldTypes) {
            layout.fieldTypes.push_back(type);
            fields.push_back(lowerType(type));
        }
    }

    for (const auto& member : declaration.members) {
        if (member.dynamicValue) {
            continue;
        }
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
    globals_[declaration.name] = Symbol {global, storageType, isNullableStorageType(storageType)};
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
    params.reserve(declaration.compileParameters.size() + declaration.runtimeParameters.size());
    for (const Parameter& param : declaration.compileParameters) {
        params.push_back(lowerType(param.type));
    }
    for (const Parameter& param : declaration.runtimeParameters) {
        params.push_back(lowerType(param.type));
    }

    llvm::FunctionType* functionType = llvm::FunctionType::get(lowerFunctionReturnType(declaration), params, false);
    llvm::Function* function = llvm::Function::Create(functionType, llvm::Function::ExternalLinkage, loweredName, module_.get());

    std::size_t index = 0;
    for (llvm::Argument& arg : function->args()) {
        if (index < declaration.compileParameters.size()) {
            arg.setName(declaration.compileParameters[index].name);
        } else {
            arg.setName(declaration.runtimeParameters[index - declaration.compileParameters.size()].name);
        }
        ++index;
    }

    for (const Annotation& annotation : declaration.annotations) {
        if (annotation.name == "inline") {
            function->addFnAttr(llvm::Attribute::AlwaysInline);
        }
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

    if (declaration.isLlvm) {
        if (!importInlineLlvmBody(*function, declaration, loweredName)) {
            function->eraseFromParent();
            functions_.erase(loweredName);
            functionDecls_.erase(loweredName);
        }
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
        const Parameter& parameter = index < declaration.compileParameters.size()
            ? declaration.compileParameters[index]
            : declaration.runtimeParameters[index - declaration.compileParameters.size()];
        llvm::AllocaInst* alloca = createEntryAlloca(function, arg.getType(), arg.getName());
        llvm::Value* storedArg = &arg;
        if (storedArg->getType()->isPointerTy()) {
            storedArg = retainForStorage(storedArg, parameter.type);
        }
        builder_.CreateStore(storedArg, alloca);
        locals_[parameter.name] = Symbol {alloca, parameter.type, isNullableStorageType(parameter.type)};
        ++index;
    }

    if (!emitStmt(*declaration.body, declaration)) {
        function->eraseFromParent();
        functions_.erase(loweredName);
        return;
    }

    if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
        if (declaration.returnsVoid()) {
            releaseLocals();
            builder_.CreateRetVoid();
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

bool ModuleEmitter::importInlineLlvmBody(llvm::Function& destination, const FunctionDecl& declaration, const std::string& loweredName) {
    destination.eraseFromParent();

    std::string moduleText;
    std::string errorMessage;
    if (!buildInlineLlvmModuleText(declaration, loweredName, moduleText, errorMessage)) {
        diagnostics_.error(declaration.llvmBodyRange.begin.offset == 0 && declaration.llvmBodyRange.end.offset == 0 ? declaration.range : declaration.llvmBodyRange,
                           errorMessage);
        return false;
    }

    llvm::SMDiagnostic parseError;
    std::unique_ptr<llvm::Module> parsedModule = llvm::parseAssemblyString(moduleText, parseError, context_);
    if (!parsedModule) {
        diagnostics_.error(declaration.llvmBodyRange.begin.offset == 0 && declaration.llvmBodyRange.end.offset == 0 ? declaration.range : declaration.llvmBodyRange,
                           "invalid llvm function body: " + parseError.getMessage().str());
        return false;
    }

    llvm::Function* parsedFunction = parsedModule->getFunction(loweredName);
    if (parsedFunction == nullptr) {
        diagnostics_.error(declaration.range, "inline llvm body did not define the expected function");
        return false;
    }

    if (llvm::Linker::linkModules(*module_, std::move(parsedModule))) {
        diagnostics_.error(declaration.range, "failed to link inline llvm function '" + declaration.name + "'");
        return false;
    }

    llvm::Function* linkedFunction = module_->getFunction(loweredName);
    if (linkedFunction == nullptr) {
        diagnostics_.error(declaration.range, "inline llvm body did not define the expected function");
        return false;
    }
    functions_[loweredName] = linkedFunction;

    if (llvm::verifyFunction(*linkedFunction, &llvm::errs())) {
        diagnostics_.error(declaration.range, "LLVM verification failed for function '" + declaration.name + "'");
        return false;
    }

    return true;
}

}  // namespace axc
