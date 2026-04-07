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
        case StmtKind::Foreach:
            return containsBreakForCurrentSwitch(*static_cast<const ForeachStmt&>(stmt).body, loopDepth + 1, switchDepth);
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

std::optional<std::pair<std::int64_t, std::int64_t>> constantRangeBounds(const Expr& expr, const std::unordered_map<std::string, EnumValueInfo>& enumValues) {
    const auto* range = dynamic_cast<const RangeExpr*>(&expr);
    if (range == nullptr || range->start == nullptr || range->end == nullptr) {
        return std::nullopt;
    }

    auto evalConst = [&](const Expr& valueExpr) -> std::optional<std::int64_t> {
        switch (valueExpr.kind) {
            case ExprKind::IntegerLiteral:
                return static_cast<const IntegerLiteralExpr&>(valueExpr).value;
            case ExprKind::Member: {
                auto qualified = qualifiedNameFromExpr(valueExpr);
                if (!qualified.has_value()) {
                    return std::nullopt;
                }
                for (const auto& [enumName, info] : enumValues) {
                    auto it = info.values.find(*qualified);
                    if (it != info.values.end()) {
                        return static_cast<std::int64_t>(it->second);
                    }
                    const std::string prefix = enumName + ".";
                    if (qualified->rfind(prefix, 0) == 0) {
                        auto nestedIt = info.values.find(qualified->substr(prefix.size()));
                        if (nestedIt != info.values.end()) {
                            return static_cast<std::int64_t>(nestedIt->second);
                        }
                    }
                }
                return std::nullopt;
            }
            case ExprKind::DeclRef: {
                const auto& ref = static_cast<const DeclRefExpr&>(valueExpr);
                for (const auto& [_, info] : enumValues) {
                    auto it = info.values.find(ref.name);
                    if (it != info.values.end()) {
                        return static_cast<std::int64_t>(it->second);
                    }
                }
                return std::nullopt;
            }
            default:
                return std::nullopt;
        }
    };

    auto start = evalConst(*range->start);
    auto end = evalConst(*range->end);
    if (!start.has_value() || !end.has_value()) {
        return std::nullopt;
    }

    auto enumMax = enumNameMaxOrdinal(enumValues, *range->start);
    if (enumMax.has_value() && *start > *end) {
        return std::nullopt;
    }

    std::int64_t upper = *end - (range->inclusive ? 0 : 1);
    if (upper < *start) {
        return std::pair<std::int64_t, std::int64_t> {*start, *start - 1};
    }
    return std::pair<std::int64_t, std::int64_t> {*start, upper};
}

std::optional<std::vector<std::int64_t>> constantRangeMembers(const Expr& expr, const std::unordered_map<std::string, EnumValueInfo>& enumValues) {
    auto bounds = constantRangeBounds(expr, enumValues);
    if (!bounds.has_value()) {
        return std::nullopt;
    }
    const auto* range = dynamic_cast<const RangeExpr*>(&expr);
    if (range == nullptr) {
        return std::nullopt;
    }
    auto enumMax = enumNameMaxOrdinal(enumValues, *range->start);
    if (!enumMax.has_value()) {
        return std::nullopt;
    }
    std::vector<std::int64_t> members;
    for (std::int64_t value = bounds->first; value <= bounds->second; ++value) {
        members.push_back(value);
    }
    return members;
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
        case StmtKind::Foreach: {
            const auto& foreachStmt = static_cast<const ForeachStmt&>(stmt);
            Type iterableType = inferExprType(*foreachStmt.iterable);
            if (iterableType.arrayExtents.empty() || !iterableType.arrayExtents.front().has_value()) {
                diagnostics_.error(foreachStmt.range, "foreach currently supports only arrays with known length");
                return false;
            }

            Type elementType = !foreachStmt.bindingType.name.empty() ? foreachStmt.bindingType : iterableType;
            if (!elementType.arrayExtents.empty()) {
                elementType.arrayExtents.erase(elementType.arrayExtents.begin());
            }
            elementType.range = foreachStmt.bindingRange;

            llvm::Value* iterableAddress = emitLValue(*foreachStmt.iterable);
            if (iterableAddress == nullptr) {
                diagnostics_.error(foreachStmt.range, "foreach iterable must be a named array value");
                return false;
            }

            llvm::Function* function = builder_.GetInsertBlock()->getParent();
            const auto savedLocals = locals_;
            llvm::AllocaInst* indexAlloca = createEntryAlloca(function, llvm::Type::getInt32Ty(context_), foreachStmt.bindingName + ".index");
            builder_.CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0), indexAlloca);

            llvm::AllocaInst* itemAlloca = createEntryAlloca(function, lowerType(elementType), foreachStmt.bindingName);
            locals_[foreachStmt.bindingName] = Symbol {itemAlloca, elementType, false};

            llvm::BasicBlock* condBlock = llvm::BasicBlock::Create(context_, "foreach.cond", function);
            llvm::BasicBlock* bodyBlock = llvm::BasicBlock::Create(context_, "foreach.body", function);
            llvm::BasicBlock* stepBlock = llvm::BasicBlock::Create(context_, "foreach.step", function);
            llvm::BasicBlock* endBlock = llvm::BasicBlock::Create(context_, "foreach.end", function);
            branchTargets_.push_back(BranchTarget {endBlock, stepBlock, deferScopes_.size()});

            builder_.CreateBr(condBlock);
            builder_.SetInsertPoint(condBlock);
            llvm::Value* indexValue = builder_.CreateLoad(llvm::Type::getInt32Ty(context_), indexAlloca, "foreach.idx");
            llvm::Value* limitValue = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), *iterableType.arrayExtents.front(), false);
            llvm::Value* condition = builder_.CreateICmpULT(indexValue, limitValue, "foreachcond");
            builder_.CreateCondBr(condition, bodyBlock, endBlock);

            builder_.SetInsertPoint(bodyBlock);
            llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
            llvm::Value* elementAddress = builder_.CreateInBoundsGEP(lowerType(iterableType), iterableAddress, {zero, indexValue}, foreachStmt.bindingName + ".addr");
            llvm::Value* elementValue = builder_.CreateLoad(lowerType(elementType), elementAddress, foreachStmt.bindingName + ".value");
            builder_.CreateStore(elementValue, itemAlloca);
            if (!emitStmt(*foreachStmt.body, functionDecl)) {
                branchTargets_.pop_back();
                locals_ = savedLocals;
                return false;
            }
            if (builder_.GetInsertBlock()->getTerminator() == nullptr) {
                builder_.CreateBr(stepBlock);
            }

            builder_.SetInsertPoint(stepBlock);
            llvm::Value* nextIndex = builder_.CreateAdd(builder_.CreateLoad(llvm::Type::getInt32Ty(context_), indexAlloca, "foreach.idx.step"),
                                                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 1),
                                                        "foreach.next");
            builder_.CreateStore(nextIndex, indexAlloca);
            builder_.CreateBr(condBlock);

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

            struct RangePatternTarget {
                std::int64_t start = 0;
                std::int64_t end = -1;
                llvm::BasicBlock* block = nullptr;
                SourceRange range {};
            };

            std::vector<std::pair<llvm::ConstantInt*, llvm::BasicBlock*>> exactCases;
            std::vector<RangePatternTarget> rangeCases;
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
                    if (pattern.isRange) {
                        if (auto enumMembers = constantRangeMembers(*pattern.value, enumValues_); enumMembers.has_value()) {
                            for (std::int64_t member : *enumMembers) {
                                exactCases.emplace_back(llvm::cast<llvm::ConstantInt>(llvm::ConstantInt::get(matchedValue->getType(), member, true)),
                                                        caseBlocks[i]);
                            }
                            continue;
                        }
                        auto bounds = constantRangeBounds(*pattern.value, enumValues_);
                        if (!bounds.has_value()) {
                            branchTargets_.pop_back();
                            diagnostics_.error(pattern.range, "switch range cases must use constant ascending bounds");
                            return false;
                        }
                        rangeCases.push_back(RangePatternTarget {bounds->first, bounds->second, caseBlocks[i], pattern.range});
                        continue;
                    }
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

            if (rangeCases.empty()) {
                llvm::SwitchInst* switchInst = builder_.CreateSwitch(matchedValue, unmatchedBlock, exactCases.size());
                for (const auto& [caseValue, block] : exactCases) {
                    switchInst->addCase(caseValue, block);
                }
            } else {
                llvm::BasicBlock* dispatchBlock = llvm::BasicBlock::Create(context_, "switch.dispatch", function);
                builder_.CreateBr(dispatchBlock);
                builder_.SetInsertPoint(dispatchBlock);

                llvm::BasicBlock* nextDispatch = nullptr;
                for (std::size_t i = 0; i < rangeCases.size(); ++i) {
                    llvm::Value* lower = builder_.CreateICmpSGE(matchedValue,
                                                                llvm::ConstantInt::get(matchedValue->getType(), rangeCases[i].start, true),
                                                                "switch.range.lower");
                    llvm::Value* upper = builder_.CreateICmpSLE(matchedValue,
                                                                llvm::ConstantInt::get(matchedValue->getType(), rangeCases[i].end, true),
                                                                "switch.range.upper");
                    llvm::Value* inRange = builder_.CreateAnd(lower, upper, "switch.range.match");
                    llvm::BasicBlock* fallthrough = nullptr;
                    if (i + 1 == rangeCases.size()) {
                        fallthrough = llvm::BasicBlock::Create(context_, "switch.exact", function);
                    } else {
                        nextDispatch = llvm::BasicBlock::Create(context_, "switch.range", function);
                        fallthrough = nextDispatch;
                    }
                    builder_.CreateCondBr(inRange, rangeCases[i].block, fallthrough);
                    if (i + 1 != rangeCases.size()) {
                        builder_.SetInsertPoint(nextDispatch);
                    } else {
                        builder_.SetInsertPoint(fallthrough);
                    }
                }

                llvm::SwitchInst* switchInst = builder_.CreateSwitch(matchedValue, unmatchedBlock, exactCases.size());
                for (const auto& [caseValue, block] : exactCases) {
                    switchInst->addCase(caseValue, block);
                }
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
