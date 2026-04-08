/// @file
/// @brief LLVM lowering for statements, structured control flow, switch dispatch, and defer handling.

#include "../Internal/LLVMEmitterInternal.h"

#include "axc/Support/Diagnostic.h"

namespace axc {

namespace {

bool containsBreakForCurrentSwitch(const Stmt& stmt, std::size_t loopDepth = 0, std::size_t switchDepth = 0) {
    switch (stmt.kind) {
        case StmtKind::Compound: {
            const auto& block = static_cast<const CompoundStmt&>(stmt);
            for (const auto& child : block.statements) {
                if (containsBreakForCurrentSwitch(*child, loopDepth, switchDepth)) {
                    return true;
                }
            }
            return false;
        }
        case StmtKind::If: {
            const auto& ifStmt = static_cast<const IfStmt&>(stmt);
            if (containsBreakForCurrentSwitch(*ifStmt.thenBlock, loopDepth, switchDepth)) {
                return true;
            }
            return ifStmt.elseBranch && containsBreakForCurrentSwitch(*ifStmt.elseBranch, loopDepth, switchDepth);
        }
        case StmtKind::While:
            return containsBreakForCurrentSwitch(*static_cast<const WhileStmt&>(stmt).body, loopDepth + 1, switchDepth);
        case StmtKind::For:
            return containsBreakForCurrentSwitch(*static_cast<const ForStmt&>(stmt).body, loopDepth + 1, switchDepth);
        case StmtKind::DoWhile:
            return containsBreakForCurrentSwitch(*static_cast<const DoWhileStmt&>(stmt).body, loopDepth + 1, switchDepth);
        case StmtKind::Switch: {
            const auto& switchStmt = static_cast<const SwitchStmt&>(stmt);
            for (const auto& switchCase : switchStmt.cases) {
                if (switchCase.body && containsBreakForCurrentSwitch(*switchCase.body, loopDepth, switchDepth + 1)) {
                    return true;
                }
            }
            return false;
        }
        case StmtKind::Break:
            return loopDepth == 0 && switchDepth == 0;
        case StmtKind::Return:
        case StmtKind::Defer:
        case StmtKind::Expr:
        case StmtKind::Let:
        case StmtKind::Continue:
            return false;
    }
    return false;
}

std::optional<std::uint64_t> enumNameMaxOrdinal(const std::unordered_map<std::string, EnumValueInfo>& enumValues, const Expr& expr) {
    auto qualified = qualifiedNameFromExpr(expr);
    if (!qualified.has_value()) {
        return std::nullopt;
    }
    const std::size_t split = qualified->find('.');
    if (split == std::string::npos) {
        return std::nullopt;
    }
    const std::string root = qualified->substr(0, split);
    auto it = enumValues.find(root);
    if (it == enumValues.end() || it->second.isFlags) {
        return std::nullopt;
    }
    return it->second.maxOrdinal;
}

}  // namespace

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
            if (!deferScopes_.empty()) {
                deferScopes_.pop_back();
            }
            return true;
        }
        case StmtKind::Return: {
            const auto& ret = static_cast<const ReturnStmt&>(stmt);
            llvm::Type* loweredReturnType = lowerFunctionReturnType(functionDecl);
            if (loweredReturnType->isVoidTy()) {
                if (!emitDeferredCallsFromDepth(0)) {
                    return false;
                }
                releaseLocals();
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
                if (!emitDeferredCallsFromDepth(0)) {
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
            if (!emitDeferredCallsFromDepth(0)) {
                return false;
            }
            releaseLocals();
            builder_.CreateRet(aggregate);
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
                if (!binding.explicitType.name.empty() && letStmt.initializer != nullptr && letStmt.initializer->kind == ExprKind::Initializer) {
                    const auto& init = static_cast<const InitializerExpr&>(*letStmt.initializer);
                    if (init.initKind == InitKind::ArrayLiteral && !storageType.arrayExtents.empty() && !storageType.arrayExtents.front().has_value()) {
                        storageType.arrayExtents.front() = init.values.size();
                    }
                }

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
            releaseLocals();
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
            llvm::BasicBlock* unmatchedBlock = defaultBlock;
            bool hasExplicitDefault = false;

            for (std::size_t i = 0; i < switchStmt.cases.size(); ++i) {
                const auto& switchCase = switchStmt.cases[i];
                if (switchCase.isDefault) {
                    defaultBlock = caseBlocks[i];
                    unmatchedBlock = defaultBlock;
                    hasExplicitDefault = true;
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

            llvm::SwitchInst* switchInst = builder_.CreateSwitch(matchedValue, unmatchedBlock, exactCases.size());
            for (const auto& [caseValue, block] : exactCases) {
                switchInst->addCase(caseValue, block);
            }

            const bool exhaustiveWithoutDefault = !hasExplicitDefault && unmatchedBlock == endBlock;
            bool allCasesTerminated = !switchStmt.cases.empty() && (hasExplicitDefault || exhaustiveWithoutDefault);
            for (std::size_t i = 0; i < switchStmt.cases.size(); ++i) {
                builder_.SetInsertPoint(caseBlocks[i]);
                if (switchStmt.cases[i].body && !emitStmt(*switchStmt.cases[i].body, functionDecl)) {
                    branchTargets_.pop_back();
                    return false;
                }
                const bool caseTerminated = builder_.GetInsertBlock()->getTerminator() != nullptr;
                const bool breaksCurrentSwitch = switchStmt.cases[i].body && containsBreakForCurrentSwitch(*switchStmt.cases[i].body);
                allCasesTerminated = allCasesTerminated && caseTerminated && !breaksCurrentSwitch;
                if (!caseTerminated) {
                    builder_.CreateBr(endBlock);
                }
            }

            branchTargets_.pop_back();
            builder_.SetInsertPoint(endBlock);
            if (allCasesTerminated) {
                builder_.CreateUnreachable();
            }
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
    emitExprValues(*deferred.call);
    return !diagnostics_.hasErrors();
}

}  // namespace axc
