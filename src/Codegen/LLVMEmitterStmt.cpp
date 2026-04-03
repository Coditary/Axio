#include "LLVMEmitterInternal.h"

#include "axc/Support/Diagnostic.h"

namespace axc {

bool ModuleEmitter::emitStmt(const Stmt& stmt, const FunctionDecl& functionDecl) {
    switch (stmt.kind) {
        case StmtKind::Compound: {
            const auto& block = static_cast<const CompoundStmt&>(stmt);
            for (const auto& child : block.statements) {
                if (!emitStmt(*child, functionDecl)) {
                    return false;
                }
                if (builder_.GetInsertBlock()->getTerminator() != nullptr) {
                    break;
                }
            }
            return true;
        }
        case StmtKind::Return: {
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            llvm::Type* loweredReturnType = lowerFunctionReturnType(functionDecl);
            if (loweredReturnType->isVoidTy()) {
                builder_.CreateRetVoid();
                return true;
            }

            if (ret.values.empty()) {
                diagnostics_.error(ret.range, "non-void function must return a value");
                return false;
            }

            std::vector<llvm::Value*> returnedValues;
            returnedValues.reserve(functionDecl.returnTypes.size());
            for (const auto& expr : ret.values) {
                MultiValue values = emitExprValues(*expr);
                if (values.values.empty()) {
                    return false;
                }
                for (llvm::Value* value : values.values) {
                    returnedValues.push_back(value);
                }
            }

            if (functionDecl.returnTypes.size() <= 1) {
                llvm::Value* value = castValueToType(returnedValues.front(), functionReturnType(functionDecl));
                if (value == nullptr) {
                    return false;
                }
                if (value->getType()->isPointerTy()) {
                    value = retainForStorage(value, functionReturnType(functionDecl));
                } else if (isArcOwnedType(functionReturnType(functionDecl))) {
                    diagnostics_.error(ret.range, "ARC-backed class returns must lower to pointer values");
                    return false;
                }
                releaseLocals();
                builder_.CreateRet(value);
                return true;
            }

            llvm::Value* aggregate = llvm::UndefValue::get(loweredReturnType);
            for (std::size_t i = 0; i < functionDecl.returnTypes.size(); ++i) {
                llvm::Value* value = castValueToType(returnedValues[i], functionDecl.returnTypes[i]);
                if (value != nullptr && value->getType()->isPointerTy()) {
                    value = retainForStorage(value, functionDecl.returnTypes[i]);
                }
                aggregate = builder_.CreateInsertValue(aggregate, value, {static_cast<unsigned>(i)}, "ret.insert");
            }
            releaseLocals();
            builder_.CreateRet(aggregate);
            return true;
        }
        case StmtKind::Expr: {
            const auto& exprStmt = static_cast<const ExprStmt&>(stmt);
            return emitExpr(*exprStmt.expression) != nullptr;
        }
        case StmtKind::Let: {
            const auto& letStmt = static_cast<const LetStmt&>(stmt);
            for (const auto& binding : letStmt.bindings) {
                if (locals_.contains(binding.name)) {
                    diagnostics_.error(binding.range, "duplicate local variable '" + binding.name + "'");
                    return false;
                }
            }

            std::vector<llvm::Value*> initializerValues;
            if (letStmt.initializer) {
                MultiValue values = emitExprValues(*letStmt.initializer);
                if (values.values.empty()) {
                    return false;
                }
                initializerValues = std::move(values.values);
            }

            for (std::size_t i = 0; i < letStmt.bindings.size(); ++i) {
                const auto& binding = letStmt.bindings[i];
                Type storageType = !binding.explicitType.name.empty() ? binding.explicitType : inferExprType(*letStmt.initializer, i);
                storageType.range = binding.range;

                if (isArcOwnedType(storageType) && storageType.pointerDepth == 0) {
                    ++storageType.pointerDepth;
                }

                llvm::Type* llvmType = lowerType(storageType);
                llvm::AllocaInst* alloca = createEntryAlloca(currentFunction_, llvmType, binding.name);
                const bool nullable = letStmt.initializer != nullptr && (letStmt.initializer->kind == ExprKind::NullLiteral ||
                    (letStmt.initializer->kind == ExprKind::Initializer && static_cast<const InitializerExpr&>(*letStmt.initializer).initKind != InitKind::Value));
                locals_[binding.name] = Symbol {alloca, storageType, nullable};

                if (i < initializerValues.size()) {
                    llvm::Value* stored = castValueToType(initializerValues[i], storageType);
                    if (letStmt.initializer != nullptr && shouldRetainForStorage(*letStmt.initializer, storageType)) {
                        stored = retainForStorage(stored, storageType);
                    }
                    builder_.CreateStore(stored, alloca);
                }
            }
            return true;
        }
        case StmtKind::If: {
            const auto& ifStmt = static_cast<const IfStmt&>(stmt);
            llvm::Value* condition = emitExpr(*ifStmt.condition);
            if (condition == nullptr) {
                return false;
            }
            if (!condition->getType()->isIntegerTy(1)) {
                condition = builder_.CreateICmpNE(condition, llvm::ConstantInt::get(condition->getType(), 0), "ifcond");
            }

            llvm::Function* function = builder_.GetInsertBlock()->getParent();
            llvm::BasicBlock* thenBlock = llvm::BasicBlock::Create(context_, "if.then", function);
            llvm::BasicBlock* elseBlock = llvm::BasicBlock::Create(context_, "if.else");
            llvm::BasicBlock* mergeBlock = llvm::BasicBlock::Create(context_, "if.end");

            builder_.CreateCondBr(condition, thenBlock, ifStmt.elseBranch ? elseBlock : mergeBlock);

            builder_.SetInsertPoint(thenBlock);
            if (!emitStmt(*ifStmt.thenBlock, functionDecl)) {
                return false;
            }
            const bool thenTerminated = builder_.GetInsertBlock()->getTerminator() != nullptr;
            if (!thenTerminated) {
                builder_.CreateBr(mergeBlock);
            }

            bool elseTerminated = false;

            if (ifStmt.elseBranch) {
                function->insert(function->end(), elseBlock);
                builder_.SetInsertPoint(elseBlock);
                if (!emitStmt(*ifStmt.elseBranch, functionDecl)) {
                    return false;
                }
                elseTerminated = builder_.GetInsertBlock()->getTerminator() != nullptr;
                if (!elseTerminated) {
                    builder_.CreateBr(mergeBlock);
                }
            }

            function->insert(function->end(), mergeBlock);
            builder_.SetInsertPoint(mergeBlock);
            if (ifStmt.elseBranch && thenTerminated && elseTerminated) {
                builder_.CreateUnreachable();
            }
            return true;
        }
    }
    return false;
}

}  // namespace axc
