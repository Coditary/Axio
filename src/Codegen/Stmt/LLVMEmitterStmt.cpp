/// @file
/// @brief LLVM lowering for MVP statements and control flow.

#include "../Internal/LLVMEmitterInternal.h"

#include "axc/Support/Diagnostic.h"

namespace axc {

bool ModuleEmitter::emitLoopConditionBranch(const Expr* condition,
                                            llvm::BasicBlock* trueBlock,
                                            llvm::BasicBlock* falseBlock,
                                            llvm::StringRef nameHint) {
    llvm::Value* loweredCondition = nullptr;
    if (condition != nullptr) {
        loweredCondition = emitExpr(*condition);
        if (loweredCondition == nullptr) {
            return false;
        }
        if (!loweredCondition->getType()->isIntegerTy(1)) {
            loweredCondition = builder_.CreateICmpNE(loweredCondition,
                                                     llvm::ConstantInt::get(loweredCondition->getType(), 0),
                                                     nameHint);
        }
    } else {
        loweredCondition = llvm::ConstantInt::getTrue(context_);
    }
    builder_.CreateCondBr(loweredCondition, trueBlock, falseBlock);
    return true;
}

bool ModuleEmitter::emitDeferredCallsFromDepth(std::size_t scopeDepth) {
    const FunctionDecl* functionDecl = nullptr;
    if (currentFunction_ != nullptr) {
        auto it = functionDecls_.find(currentFunction_->getName().str());
        if (it != functionDecls_.end()) {
            functionDecl = it->second;
        }
    }
    if (functionDecl == nullptr) {
        return true;
    }
    if (scopeDepth > deferScopes_.size()) {
        scopeDepth = deferScopes_.size();
    }
    for (std::size_t i = deferScopes_.size(); i > scopeDepth; --i) {
        auto& scope = deferScopes_[i - 1];
        for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
            if (!emitDeferredCall(*it, *functionDecl)) {
                return false;
            }
        }
    }
    return true;
}

bool ModuleEmitter::emitStmt(const Stmt& stmt, const FunctionDecl& functionDecl) {
    switch (stmt.kind) {
        case StmtKind::Compound: {
            const auto& block = static_cast<const CompoundStmt&>(stmt);
            deferScopes_.emplace_back();
            const auto savedLocals = locals_;
            for (const auto& child : block.statements) {
                if (!emitStmt(*child, functionDecl)) {
                    if (!deferScopes_.empty()) {
                        deferScopes_.pop_back();
                    }
                    return false;
                }
                if (builder_.GetInsertBlock()->getTerminator() != nullptr) {
                    break;
                }
            }
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                if (!emitDeferredCallsForCurrentScope()) {
                    if (!deferScopes_.empty()) {
                        deferScopes_.pop_back();
                    }
                    return false;
                }
            }
            locals_ = savedLocals;
            if (!deferScopes_.empty()) {
                deferScopes_.pop_back();
            }
            return true;
        }
        case StmtKind::Return: {
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            if (functionDecl.returnsVoid()) {
                if (!emitDeferredCallsFromDepth(0)) {
                    return false;
                }
                builder_.CreateRetVoid();
                return true;
            }
            if (!ret.value) {
                diagnostics_.error(ret.range, "non-void function must return a value");
                return false;
            }
            llvm::Value* value = emitExpr(*ret.value);
            if (value == nullptr) {
                return false;
            }
            value = castValueToType(value, *functionDecl.returnType);
            if (!emitDeferredCallsFromDepth(0)) {
                return false;
            }
            builder_.CreateRet(value);
            return true;
        }
        case StmtKind::Defer: {
            const auto& deferStmt = static_cast<const DeferStmt&>(stmt);
            if (deferStmt.call == nullptr || deferStmt.call->kind != ExprKind::Call) {
                diagnostics_.error(deferStmt.range, "defer expects a function or method call");
                return false;
            }
            if (deferScopes_.empty()) {
                deferScopes_.emplace_back();
            }
            deferScopes_.back().push_back(DeferredCall {static_cast<const CallExpr*>(deferStmt.call.get()), deferStmt.range});
            return true;
        }
        case StmtKind::Expr: {
            const auto& exprStmt = static_cast<const ExprStmt&>(stmt);
            if (exprStmt.expression->kind == ExprKind::Call) {
                const auto& call = static_cast<const CallExpr&>(*exprStmt.expression);
                llvm::Value* callValue = emitExpr(call);
                return callValue != nullptr || (call.callee && call.callee->kind == ExprKind::DeclRef && functionDecls_.contains(static_cast<const DeclRefExpr&>(*call.callee).name) && functionDecls_.at(static_cast<const DeclRefExpr&>(*call.callee).name)->returnsVoid());
            }
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

            const auto& binding = letStmt.bindings.front();
            Type storageType = !binding.explicitType.name.empty() ? binding.explicitType : inferExprType(*letStmt.initializer);
            storageType.range = binding.range;
            if (!binding.explicitType.name.empty() && letStmt.initializer != nullptr && letStmt.initializer->kind == ExprKind::Initializer) {
                const auto& init = static_cast<const InitializerExpr&>(*letStmt.initializer);
                if (init.initKind == InitKind::ArrayLiteral && !storageType.arrayExtents.empty() && !storageType.arrayExtents.front().has_value()) {
                    storageType.arrayExtents.front() = init.values.size();
                }
            }

            llvm::Type* llvmType = lowerType(storageType);
            llvm::AllocaInst* alloca = createEntryAlloca(currentFunction_, llvmType, binding.name);
            locals_[binding.name] = Symbol {alloca, storageType};

            if (letStmt.initializer) {
                llvm::Value* stored = emitExpr(*letStmt.initializer);
                if (stored == nullptr) {
                    return false;
                }
                stored = castValueToType(stored, storageType);
                builder_.CreateStore(stored, alloca);
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
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateBr(mergeBlock);
            }

            if (ifStmt.elseBranch) {
                function->insert(function->end(), elseBlock);
                builder_.SetInsertPoint(elseBlock);
                if (!emitStmt(*ifStmt.elseBranch, functionDecl)) {
                    return false;
                }
                if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                    builder_.CreateBr(mergeBlock);
                }
            }

            function->insert(function->end(), mergeBlock);
            builder_.SetInsertPoint(mergeBlock);
            return true;
        }
        case StmtKind::While: {
            const auto& whileStmt = static_cast<const WhileStmt&>(stmt);
            llvm::Function* function = builder_.GetInsertBlock()->getParent();
            llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(context_, "while.cond", function);
            llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(context_, "while.body", function);
            llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context_, "while.end", function);
            branchTargets_.push_back(BranchTarget {endBlock, condBlock, deferScopes_.size()});

            builder_.CreateBr(condBlock);
            builder_.SetInsertPoint(condBlock);
            if (!emitLoopConditionBranch(whileStmt.condition.get(), bodyBlock, endBlock, "whilecond")) {
                branchTargets_.pop_back();
                return false;
            }

            builder_.SetInsertPoint(bodyBlock);
            if (!emitStmt(*whileStmt.body, functionDecl)) {
                branchTargets_.pop_back();
                return false;
            }
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateBr(condBlock);
            }

            branchTargets_.pop_back();
            builder_.SetInsertPoint(endBlock);
            return true;
        }
        case StmtKind::For: {
            const auto& forStmt = static_cast<const ForStmt&>(stmt);
            llvm::Function* function = builder_.GetInsertBlock()->getParent();
            const auto savedLocals = locals_;
            if (forStmt.initializer && !emitStmt(*forStmt.initializer, functionDecl)) {
                return false;
            }

            llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(context_, "for.cond", function);
            llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(context_, "for.body", function);
            llvm::BasicBlock* stepBlock = llvm::BasicBlock::Create(context_, "for.step", function);
            llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context_, "for.end", function);
            branchTargets_.push_back(BranchTarget {endBlock, stepBlock, deferScopes_.size()});

            builder_.CreateBr(condBlock);
            builder_.SetInsertPoint(condBlock);
            if (!emitLoopConditionBranch(forStmt.condition.get(), bodyBlock, endBlock, "forcond")) {
                branchTargets_.pop_back();
                locals_ = savedLocals;
                return false;
            }

            builder_.SetInsertPoint(bodyBlock);
            if (!emitStmt(*forStmt.body, functionDecl)) {
                branchTargets_.pop_back();
                locals_ = savedLocals;
                return false;
            }
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateBr(stepBlock);
            }

            builder_.SetInsertPoint(stepBlock);
            if (forStmt.step && emitExpr(*forStmt.step) == nullptr) {
                locals_ = savedLocals;
                return false;
            }
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateBr(condBlock);
            }

            branchTargets_.pop_back();
            builder_.SetInsertPoint(endBlock);
            locals_ = savedLocals;
            return true;
        }
        case StmtKind::DoWhile: {
            const auto& doWhileStmt = static_cast<const DoWhileStmt&>(stmt);
            llvm::Function* function = builder_.GetInsertBlock()->getParent();
            llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(context_, "do.body", function);
            llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(context_, "do.cond", function);
            llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context_, "do.end", function);
            branchTargets_.push_back(BranchTarget {endBlock, condBlock, deferScopes_.size()});

            builder_.CreateBr(bodyBlock);
            builder_.SetInsertPoint(bodyBlock);
            if (!emitStmt(*doWhileStmt.body, functionDecl)) {
                branchTargets_.pop_back();
                return false;
            }
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateBr(condBlock);
            }

            builder_.SetInsertPoint(condBlock);
            if (!emitLoopConditionBranch(doWhileStmt.condition.get(), bodyBlock, endBlock, "docond")) {
                branchTargets_.pop_back();
                return false;
            }

            branchTargets_.pop_back();
            builder_.SetInsertPoint(endBlock);
            return true;
        }
        case StmtKind::Switch: {
            const auto& switchStmt = static_cast<const SwitchStmt&>(stmt);
            llvm::Value* matchedValue = emitExpr(*switchStmt.condition);
            if (matchedValue == nullptr) {
                return false;
            }
            llvm::Function* function = builder_.GetInsertBlock()->getParent();
            llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context_, "switch.end", function);
            llvm::BasicBlock* defaultBlock = endBlock;
            branchTargets_.push_back(BranchTarget {endBlock, nullptr, deferScopes_.size()});
            std::vector<llvm::BasicBlock*> caseBlocks;
            caseBlocks.reserve(switchStmt.cases.size());
            for (std::size_t i = 0; i < switchStmt.cases.size(); ++i) {
                caseBlocks.push_back(llvm::BasicBlock::Create(context_, "switch.case", function));
            }

            std::vector<std::pair<llvm::ConstantInt*, llvm::BasicBlock*>> exactCases;
            for (std::size_t i = 0; i < switchStmt.cases.size(); ++i) {
                const auto& switchCase = switchStmt.cases[i];
                if (switchCase.isDefault) {
                    defaultBlock = caseBlocks[i];
                    continue;
                }
                for (const auto& pattern : switchCase.patterns) {
                    llvm::Value* caseValue = emitExpr(*pattern.value);
                    auto* constantInt = llvm::dyn_cast_or_null<llvm::ConstantInt>(caseValue);
                    if (constantInt == nullptr) {
                        branchTargets_.pop_back();
                        diagnostics_.error(pattern.range, "switch case values must lower to integer constants");
                        return false;
                    }
                    if (constantInt->getType() != matchedValue->getType()) {
                        caseValue = llvm::ConstantInt::get(matchedValue->getType(), constantInt->getSExtValue(), true);
                        constantInt = llvm::cast<llvm::ConstantInt>(caseValue);
                    }
                    exactCases.emplace_back(constantInt, caseBlocks[i]);
                }
            }

            llvm::SwitchInst* switchInst = builder_.CreateSwitch(matchedValue, defaultBlock, exactCases.size());
            for (const auto& [caseValue, block] : exactCases) {
                switchInst->addCase(caseValue, block);
            }

            for (std::size_t i = 0; i < switchStmt.cases.size(); ++i) {
                builder_.SetInsertPoint(caseBlocks[i]);
                if (switchStmt.cases[i].body && !emitStmt(*switchStmt.cases[i].body, functionDecl)) {
                    branchTargets_.pop_back();
                    return false;
                }
                if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                    builder_.CreateBr(endBlock);
                }
            }

            branchTargets_.pop_back();
            builder_.SetInsertPoint(endBlock);
            return true;
        }
        case StmtKind::Break: {
            if (branchTargets_.empty() || branchTargets_.back().breakBlock == nullptr) {
                diagnostics_.error(stmt.range, "break can only appear inside loops or switch cases");
                return false;
            }
            if (!emitDeferredCallsFromDepth(branchTargets_.back().deferDepth)) {
                return false;
            }
            builder_.CreateBr(branchTargets_.back().breakBlock);
            return true;
        }
        case StmtKind::Continue: {
            if (branchTargets_.empty() || branchTargets_.back().continueBlock == nullptr) {
                diagnostics_.error(stmt.range, "continue can only appear inside loops");
                return false;
            }
            if (!emitDeferredCallsFromDepth(branchTargets_.back().deferDepth)) {
                return false;
            }
            builder_.CreateBr(branchTargets_.back().continueBlock);
            return true;
        }
    }
    return false;
}

bool ModuleEmitter::emitDeferredCallsForCurrentScope() {
    if (deferScopes_.empty()) {
        return true;
    }
    auto& scope = deferScopes_.back();
    const FunctionDecl* functionDecl = nullptr;
    if (currentFunction_ != nullptr) {
        auto it = functionDecls_.find(currentFunction_->getName().str());
        if (it != functionDecls_.end()) {
            functionDecl = it->second;
        }
    }
    if (functionDecl == nullptr) {
        return true;
    }
    for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
        if (!emitDeferredCall(*it, *functionDecl)) {
            return false;
        }
    }
    scope.clear();
    return true;
}

bool ModuleEmitter::emitAllDeferredCalls() {
    while (!deferScopes_.empty()) {
        if (!emitDeferredCallsForCurrentScope()) {
            return false;
        }
        deferScopes_.pop_back();
    }
    return true;
}

bool ModuleEmitter::emitDeferredCall(const DeferredCall& deferred, const FunctionDecl& functionDecl) {
    (void)functionDecl;
    if (deferred.call == nullptr) {
        return true;
    }
    emitExpr(*deferred.call);
    return !diagnostics_.hasErrors();
}

}  // namespace axc
