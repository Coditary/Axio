/// @file
/// @brief LLVM lowering for expressions, constants, calls, ownership operations, and conversions.

#include "../Internal/LLVMEmitterInternal.h"

#include "axc/Support/Diagnostic.h"
#include "axc/Support/SourceManager.h"

namespace axc {

namespace {

std::optional<std::uint64_t> lookupEnumValueBySuffix(const std::unordered_map<std::string, EnumValueInfo>& enumValues,
                                                    std::string_view qualifiedName) {
    for (const auto& [enumName, info] : enumValues) {
        auto exactIt = info.values.find(std::string(qualifiedName));
        if (exactIt != info.values.end()) {
            return exactIt->second;
        }
        const std::string suffix = "." + std::string(qualifiedName);
        for (const auto& [name, value] : info.values) {
            if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                return value;
            }
        }
    }
    return std::nullopt;
}

llvm::Constant* nullConstantForType(llvm::LLVMContext& context, llvm::Type* type) {
    if (type->isVoidTy()) {
        return nullptr;
    }
    if (type->isPointerTy()) {
        return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(type));
    }
    if (type->isIntegerTy()) {
        return llvm::ConstantInt::get(type, 0);
    }
    if (type->isFloatingPointTy()) {
        return llvm::ConstantFP::get(type, 0.0);
    }
    if (type->isStructTy() || type->isArrayTy()) {
        return llvm::ConstantAggregateZero::get(type);
    }
    return llvm::Constant::getNullValue(type);
}

llvm::Value* coerceArgumentValue(llvm::IRBuilder<>& builder,
                                llvm::LLVMContext& context,
                                llvm::Value* value,
                                llvm::Type* targetType) {
    if (value == nullptr) {
        return nullptr;
    }
    if (value->getType() == targetType) {
        return value;
    }
    if (targetType->isStructTy() && value->getType()->isPointerTy()) {
        return builder.CreateLoad(targetType, value, "arg.load");
    }
    if (targetType->isPointerTy() && value->getType()->isPointerTy()) {
        return builder.CreatePointerCast(value, targetType, "arg.ptrcast");
    }
    if (targetType->isIntegerTy(1) && value->getType()->isPointerTy()) {
        return builder.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())), "arg.ptrbool");
    }
    return value;
}

bool hasModifier(const Type& type, TypeModifier modifier) {
    return std::find(type.modifiers.begin(), type.modifiers.end(), modifier) != type.modifiers.end();
}

}  // namespace

llvm::Value* ModuleEmitter::emitStringConstant(const std::string& value, const std::string& nameHint) {
    auto* constant = llvm::ConstantDataArray::getString(context_, value, true);
    auto* global = new llvm::GlobalVariable(*module_, constant->getType(), true, llvm::GlobalValue::PrivateLinkage, constant, nameHint);
    global->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
    global->setAlignment(llvm::Align(1));

    llvm::Value* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0);
    return builder_.CreateInBoundsGEP(global->getValueType(), global, {zero, zero}, nameHint + ".ptr");
}

MultiValue ModuleEmitter::emitExprValues(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::Call: {
            const auto& call = static_cast<const CallExpr&>(expr);
            std::vector<const Expr*> suppliedArguments;
            suppliedArguments.reserve(call.compileArguments.size() + call.runtimeArguments.size());
            for (const auto& argument : call.compileArguments) {
                suppliedArguments.push_back(argument.get());
            }
            for (const auto& argument : call.runtimeArguments) {
                suppliedArguments.push_back(argument.get());
            }
            if (call.callee->kind == ExprKind::DeclRef) {
                const auto* ref = static_cast<const DeclRefExpr*>(call.callee.get());
                if (ref->name == "len") {
                    if (call.runtimeArguments.size() != 1 || !call.compileArguments.empty()) {
                        diagnostics_.error(call.range, "wrong number of arguments for call to 'len'");
                        return {};
                    }
                    Type arrayType = inferExprType(*call.runtimeArguments[0]);
                    if (arrayType.arrayExtents.empty() || !arrayType.arrayExtents.front().has_value()) {
                        diagnostics_.error(call.range, "len expects an array argument");
                        return {};
                    }
                    return MultiValue {{llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), *arrayType.arrayExtents.front(), false)}};
                }
            }
            if (auto qualifiedCallee = moduleQualifiedName(*call.callee); qualifiedCallee.has_value()) {
                auto enumIt = enumValues_.find(*qualifiedCallee);
                if (enumIt != enumValues_.end() && call.runtimeArguments.size() == 1) {
                    llvm::Value* value = emitExpr(*call.runtimeArguments[0]);
                    return value == nullptr ? MultiValue {} : MultiValue {{value}};
                }

                auto it = functions_.find(*qualifiedCallee);
                if (it != functions_.end()) {
                    llvm::Function* function = it->second;
                    if (function->arg_size() != suppliedArguments.size()) {
                        diagnostics_.error(call.range, "wrong number of arguments for call to '" + *qualifiedCallee + "'");
                        return {};
                    }

                    std::vector<llvm::Value*> args;
                    args.reserve(suppliedArguments.size());
                    const FunctionDecl* functionDecl = functionDecls_[*qualifiedCallee];
                    std::size_t suppliedIndex = 0;
                    for (std::size_t compileIndex = 0; compileIndex < functionDecl->compileParameters.size(); ++compileIndex, ++suppliedIndex) {
                        llvm::Value* value = emitExpr(*suppliedArguments[suppliedIndex]);
                        if (value == nullptr) {
                            return {};
                        }
                        llvm::Type* targetType = lowerType(functionDecl->compileParameters[compileIndex].type);
                        value = coerceArgumentValue(builder_, context_, value, targetType);
                        args.push_back(castValueToType(value, functionDecl->compileParameters[compileIndex].type));
                    }
                    for (std::size_t runtimeIndex = 0; runtimeIndex < functionDecl->runtimeParameters.size(); ++runtimeIndex, ++suppliedIndex) {
                        const Expr& argumentExpr = *suppliedArguments[suppliedIndex];
                        llvm::Value* value = nullptr;
                        const Type& parameterType = functionDecl->runtimeParameters[runtimeIndex].type;
                        if (hasModifier(parameterType, TypeModifier::Ref) && argumentExpr.kind == ExprKind::Unary) {
                            const auto& unary = static_cast<const UnaryExpr&>(argumentExpr);
                            if (unary.op == UnaryOp::AddressOf) {
                                Type operandType = inferExprType(*unary.operand);
                                if (isArcOwnedType(operandType) || isWeakType(operandType) || isUniqueType(operandType)) {
                                    value = emitExpr(*unary.operand);
                                }
                            }
                        }
                        if (value == nullptr) {
                            value = emitExpr(argumentExpr);
                        }
                        if (value == nullptr) {
                            return {};
                        }
                        llvm::Type* targetType = lowerType(parameterType);
                        value = coerceArgumentValue(builder_, context_, value, targetType);
                        args.push_back(castValueToType(value, parameterType));
                    }

                    llvm::Value* callValue = builder_.CreateCall(function, args, function->getReturnType()->isVoidTy() ? "" : "calltmp");
                    if (functionDecl->returnValueCount() <= 1) {
                        return callValue == nullptr ? MultiValue {} : MultiValue {{callValue}};
                    }

                    MultiValue result;
                    result.values.reserve(functionDecl->returnValueCount());
                    for (std::size_t i = 0; i < functionDecl->returnValueCount(); ++i) {
                        result.values.push_back(builder_.CreateExtractValue(callValue, {static_cast<unsigned>(i)}, "ret." + std::to_string(i)));
                    }
                    return result;
                }

            }

            if (auto qualifiedCallee = qualifiedNameFromExpr(*call.callee); qualifiedCallee.has_value()) {
                auto it = functions_.find(*qualifiedCallee);
                if (it != functions_.end()) {
                    llvm::Function* function = it->second;
                    const FunctionDecl* functionDecl = functionDecls_[*qualifiedCallee];
                    std::vector<llvm::Value*> args;
                    args.reserve(suppliedArguments.size());
                    std::size_t suppliedIndex = 0;
                    for (std::size_t compileIndex = 0; compileIndex < functionDecl->compileParameters.size(); ++compileIndex, ++suppliedIndex) {
                        llvm::Value* value = emitExpr(*suppliedArguments[suppliedIndex]);
                        if (value == nullptr) {
                            return {};
                        }
                        llvm::Type* targetType = lowerType(functionDecl->compileParameters[compileIndex].type);
                        value = coerceArgumentValue(builder_, context_, value, targetType);
                        args.push_back(castValueToType(value, functionDecl->compileParameters[compileIndex].type));
                    }
                    for (std::size_t runtimeIndex = 0; runtimeIndex < functionDecl->runtimeParameters.size(); ++runtimeIndex, ++suppliedIndex) {
                        const Expr& argumentExpr = *suppliedArguments[suppliedIndex];
                        llvm::Value* value = nullptr;
                        const Type& parameterType = functionDecl->runtimeParameters[runtimeIndex].type;
                        if (hasModifier(parameterType, TypeModifier::Ref) && argumentExpr.kind == ExprKind::Unary) {
                            const auto& unary = static_cast<const UnaryExpr&>(argumentExpr);
                            if (unary.op == UnaryOp::AddressOf) {
                                Type operandType = inferExprType(*unary.operand);
                                if (isArcOwnedType(operandType) || isWeakType(operandType) || isUniqueType(operandType)) {
                                    value = emitExpr(*unary.operand);
                                }
                            }
                        }
                        if (value == nullptr) {
                            value = emitExpr(argumentExpr);
                        }
                        if (value == nullptr) {
                            return {};
                        }
                        llvm::Type* targetType = lowerType(parameterType);
                        value = coerceArgumentValue(builder_, context_, value, targetType);
                        args.push_back(castValueToType(value, parameterType));
                    }
                    llvm::Value* callValue = builder_.CreateCall(function, args, function->getReturnType()->isVoidTy() ? "" : "calltmp");
                    if (functionDecl->returnValueCount() <= 1) {
                        return callValue == nullptr ? MultiValue {} : MultiValue {{callValue}};
                    }
                    MultiValue result;
                    result.values.reserve(functionDecl->returnValueCount());
                    for (std::size_t i = 0; i < functionDecl->returnValueCount(); ++i) {
                        result.values.push_back(builder_.CreateExtractValue(callValue, {static_cast<unsigned>(i)}, "ret." + std::to_string(i)));
                    }
                    return result;
                }
            }

            if (call.callee->kind == ExprKind::Member) {
                const auto& member = static_cast<const MemberExpr&>(*call.callee);
                if (auto className = inferClassTypeName(*member.base); className.has_value()) {
                    const std::string loweredName = *className + "." + member.member;
                    auto it = functions_.find(loweredName);
                    if (it != functions_.end()) {
                        llvm::Function* function = it->second;
                        const FunctionDecl* functionDecl = functionDecls_[loweredName];

                        llvm::Value* selfValue = emitExpr(*member.base);
                        if (selfValue == nullptr) {
                            return {};
                        }

                        llvm::BasicBlock* insertBlock = builder_.GetInsertBlock();
                        llvm::Function* parentFunction = insertBlock->getParent();
                        llvm::BasicBlock* continueBlock = nullptr;
                        llvm::BasicBlock* callBlock = insertBlock;
                        llvm::PHINode* singleResultPhi = nullptr;
                        std::vector<llvm::PHINode*> multiResultPhis;

                        if (member.nullSafe) {
                            if (!selfValue->getType()->isPointerTy()) {
                                diagnostics_.error(call.range, "null-safe call requires a pointer-like receiver");
                                return {};
                            }
                            continueBlock = llvm::BasicBlock::Create(context_, "nullsafe.call.cont", parentFunction);
                            callBlock = llvm::BasicBlock::Create(context_, "nullsafe.call.invoke", parentFunction);
                            llvm::Value* isNonNull = builder_.CreateICmpNE(
                                selfValue,
                                llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(selfValue->getType())),
                                "nullsafe.call.test");
                            builder_.CreateCondBr(isNonNull, callBlock, continueBlock);
                            builder_.SetInsertPoint(callBlock);
                        }

                        std::vector<llvm::Value*> args;
                        args.reserve(suppliedArguments.size() + 1);
                        std::size_t suppliedIndex = 0;
                        for (std::size_t compileIndex = 0; compileIndex < functionDecl->compileParameters.size(); ++compileIndex, ++suppliedIndex) {
                            llvm::Value* value = emitExpr(*suppliedArguments[suppliedIndex]);
                            if (value == nullptr) {
                                return {};
                            }
                            args.push_back(castValueToType(value, functionDecl->compileParameters[compileIndex].type));
                        }
                        llvm::Value* loweredSelf = selfValue;
                        llvm::Type* selfTargetType = lowerType(functionDecl->runtimeParameters[0].type);
                        if (selfTargetType->isStructTy() && selfValue->getType()->isPointerTy()) {
                            loweredSelf = builder_.CreateLoad(selfTargetType, selfValue, "self.load");
                        } else {
                            loweredSelf = castValueToType(selfValue, functionDecl->runtimeParameters[0].type);
                        }
                        args.push_back(loweredSelf);
                        for (std::size_t runtimeIndex = 1; runtimeIndex < functionDecl->runtimeParameters.size(); ++runtimeIndex, ++suppliedIndex) {
                            const Expr& argumentExpr = *suppliedArguments[suppliedIndex];
                            llvm::Value* value = nullptr;
                            const Type& parameterType = functionDecl->runtimeParameters[runtimeIndex].type;
                            if (hasModifier(parameterType, TypeModifier::Ref) && argumentExpr.kind == ExprKind::Unary) {
                                const auto& unary = static_cast<const UnaryExpr&>(argumentExpr);
                                if (unary.op == UnaryOp::AddressOf) {
                                    Type operandType = inferExprType(*unary.operand);
                                    if (isArcOwnedType(operandType) || isWeakType(operandType) || isUniqueType(operandType)) {
                                        value = emitExpr(*unary.operand);
                                    }
                                }
                            }
                            if (value == nullptr) {
                                value = emitExpr(argumentExpr);
                            }
                            if (value == nullptr) {
                                return {};
                            }
                            args.push_back(castValueToType(value, parameterType));
                        }
                        llvm::Value* callValue = builder_.CreateCall(function, args, function->getReturnType()->isVoidTy() ? "" : "calltmp");
                        llvm::BasicBlock* actualCallBlock = builder_.GetInsertBlock();

                        if (member.nullSafe && continueBlock != nullptr) {
                            builder_.CreateBr(continueBlock);
                            builder_.SetInsertPoint(continueBlock);
                            if (functionDecl->returnsVoid()) {
                                return {};
                            }
                            if (functionDecl->returnValueCount() == 1) {
                                llvm::Type* returnType = lowerType(functionDecl->returnTypes.front());
                                singleResultPhi = builder_.CreatePHI(returnType, 2, "nullsafe.call.result");
                                singleResultPhi->addIncoming(castValueToType(callValue, functionDecl->returnTypes.front()), actualCallBlock);
                                singleResultPhi->addIncoming(nullConstantForType(context_, returnType), insertBlock);
                                return MultiValue {{singleResultPhi}};
                            }
                            multiResultPhis.reserve(functionDecl->returnValueCount());
                            for (std::size_t i = 0; i < functionDecl->returnValueCount(); ++i) {
                                llvm::Type* returnType = lowerType(functionDecl->returnTypes[i]);
                                llvm::Value* extracted = builder_.CreateExtractValue(callValue, {static_cast<unsigned>(i)}, "ret." + std::to_string(i));
                                auto* phi = builder_.CreatePHI(returnType, 2, "nullsafe.call.result." + std::to_string(i));
                                phi->addIncoming(castValueToType(extracted, functionDecl->returnTypes[i]), actualCallBlock);
                                phi->addIncoming(nullConstantForType(context_, returnType), insertBlock);
                                multiResultPhis.push_back(phi);
                            }
                            MultiValue result;
                            result.values.reserve(multiResultPhis.size());
                            for (llvm::PHINode* phi : multiResultPhis) {
                                result.values.push_back(phi);
                            }
                            return result;
                        }

                        if (functionDecl->returnValueCount() <= 1) {
                            return callValue == nullptr ? MultiValue {} : MultiValue {{callValue}};
                        }
                        MultiValue result;
                        result.values.reserve(functionDecl->returnValueCount());
                        for (std::size_t i = 0; i < functionDecl->returnValueCount(); ++i) {
                            result.values.push_back(builder_.CreateExtractValue(callValue, {static_cast<unsigned>(i)}, "ret." + std::to_string(i)));
                        }
                        return result;
                    }
                }
            }

            if (call.callee->kind == ExprKind::DeclRef) {
                const auto* ref = static_cast<const DeclRefExpr*>(call.callee.get());
                auto enumIt = enumValues_.find(ref->name);
                if (enumIt != enumValues_.end() && call.runtimeArguments.size() == 1) {
                    llvm::Value* value = emitExpr(*call.runtimeArguments[0]);
                    return value == nullptr ? MultiValue {} : MultiValue {{value}};
                }
            }

            if (call.callee->kind == ExprKind::DeclRef) {
                const auto* ref = static_cast<const DeclRefExpr*>(call.callee.get());
                auto it = functions_.find(ref->name);
                if (it == functions_.end()) {
                    diagnostics_.error(call.range, "unknown function '" + ref->name + "'");
                    return {};
                }

                llvm::Function* function = it->second;
                if (function->arg_size() != suppliedArguments.size()) {
                    diagnostics_.error(call.range, "wrong number of arguments for call to '" + ref->name + "'");
                    return {};
                }

                std::vector<llvm::Value*> args;
                args.reserve(suppliedArguments.size());
                const FunctionDecl* functionDecl = functionDecls_[ref->name];
                std::size_t suppliedIndex = 0;
                for (std::size_t compileIndex = 0; compileIndex < functionDecl->compileParameters.size(); ++compileIndex, ++suppliedIndex) {
                    llvm::Value* value = emitExpr(*suppliedArguments[suppliedIndex]);
                    if (value == nullptr) {
                        return {};
                    }
                    llvm::Type* targetType = lowerType(functionDecl->compileParameters[compileIndex].type);
                    value = coerceArgumentValue(builder_, context_, value, targetType);
                    args.push_back(castValueToType(value, functionDecl->compileParameters[compileIndex].type));
                }
                for (std::size_t runtimeIndex = 0; runtimeIndex < functionDecl->runtimeParameters.size(); ++runtimeIndex, ++suppliedIndex) {
                    const Expr& argumentExpr = *suppliedArguments[suppliedIndex];
                    llvm::Value* value = nullptr;
                    const Type& parameterType = functionDecl->runtimeParameters[runtimeIndex].type;
                    if (hasModifier(parameterType, TypeModifier::Ref) && argumentExpr.kind == ExprKind::Unary) {
                        const auto& unary = static_cast<const UnaryExpr&>(argumentExpr);
                        if (unary.op == UnaryOp::AddressOf) {
                            Type operandType = inferExprType(*unary.operand);
                            if (isArcOwnedType(operandType) || isWeakType(operandType) || isUniqueType(operandType)) {
                                value = emitExpr(*unary.operand);
                            }
                        }
                    }
                    if (value == nullptr) {
                        value = emitExpr(argumentExpr);
                    }
                    if (value == nullptr) {
                        return {};
                    }
                    llvm::Type* targetType = lowerType(parameterType);
                    value = coerceArgumentValue(builder_, context_, value, targetType);
                    args.push_back(castValueToType(value, parameterType));
                }

                llvm::Value* callValue = builder_.CreateCall(function, args, function->getReturnType()->isVoidTy() ? "" : "calltmp");
                if (functionDecl->returnValueCount() <= 1) {
                    return callValue == nullptr ? MultiValue {} : MultiValue {{callValue}};
                }

                MultiValue result;
                result.values.reserve(functionDecl->returnValueCount());
                for (std::size_t i = 0; i < functionDecl->returnValueCount(); ++i) {
                    result.values.push_back(builder_.CreateExtractValue(callValue, {static_cast<unsigned>(i)}, "ret." + std::to_string(i)));
                }
                return result;
            }

            diagnostics_.error(call.range, "call target must resolve to a declared function or method");
            return {};
        }
        default: {
            llvm::Value* value = emitExpr(expr);
            return value == nullptr ? MultiValue {} : MultiValue {{value}};
        }
    }
}

llvm::Value* ModuleEmitter::emitLValue(const Expr& expr) {
    if (expr.kind == ExprKind::DeclRef) {
        const auto& ref = static_cast<const DeclRefExpr&>(expr);
        const auto symbol = lookupSymbol(ref.name);
        if (!symbol.has_value()) {
            diagnostics_.error(ref.range, "unknown variable '" + ref.name + "'");
            return nullptr;
        }
        return symbol->address;
    }

    if (expr.kind == ExprKind::Member) {
        const auto& member = static_cast<const MemberExpr&>(expr);
        auto className = inferClassTypeName(*member.base);
        const std::string aggregateName = className.has_value() ? *className : inferExprType(*member.base).name;
        auto layoutIt = aggregateLayouts_.find(aggregateName);
        if (layoutIt == aggregateLayouts_.end()) {
            diagnostics_.error(expr.range, "member access base is not a lowered aggregate type");
            return nullptr;
        }
        auto fieldIt = layoutIt->second.fieldIndices.find(member.member);
        if (fieldIt == layoutIt->second.fieldIndices.end()) {
            diagnostics_.error(expr.range, "unknown aggregate field '" + member.member + "'");
            return nullptr;
        }
        llvm::Value* baseAddress = emitLValue(*member.base);
        if (baseAddress == nullptr) {
            return nullptr;
        }
        if (className.has_value()) {
            llvm::Type* baseValueType = lowerType(inferExprType(*member.base));
            if (baseValueType->isPointerTy()) {
                llvm::Value* objectValue = builder_.CreateLoad(baseValueType, baseAddress, member.member + ".base");
                if (!objectValue->getType()->isPointerTy()) {
                    diagnostics_.error(expr.range, "class member access requires a pointer-like base value");
                    return nullptr;
                }
                return builder_.CreateStructGEP(structTypes_.at(*className), objectValue, static_cast<unsigned>(fieldIt->second), member.member + ".addr");
            }
            return builder_.CreateStructGEP(structTypes_.at(*className), baseAddress, static_cast<unsigned>(fieldIt->second), member.member + ".addr");
        }
        llvm::StructType* aggregateType = structTypes_.at(aggregateName);
        return builder_.CreateStructGEP(aggregateType, baseAddress, static_cast<unsigned>(fieldIt->second), member.member + ".addr");
    }

    if (expr.kind == ExprKind::Unary) {
        const auto& unary = static_cast<const UnaryExpr&>(expr);
        if (unary.op == UnaryOp::Dereference) {
            return emitExpr(*unary.operand);
        }
        if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement || unary.op == UnaryOp::PostIncrement ||
            unary.op == UnaryOp::PostDecrement) {
            return emitLValue(*unary.operand);
        }
    }

    diagnostics_.error(expr.range, "expression is not assignable");
    return nullptr;
}

llvm::Value* ModuleEmitter::emitExpr(const Expr& expr) {
    switch (expr.kind) {
        case ExprKind::IntegerLiteral: {
            const auto& literal = static_cast<const IntegerLiteralExpr&>(expr);
            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), literal.value, true);
        }
        case ExprKind::FloatLiteral: {
            const auto& literal = static_cast<const FloatLiteralExpr&>(expr);
            return llvm::ConstantFP::get(llvm::Type::getDoubleTy(context_), literal.value);
        }
        case ExprKind::BoolLiteral: {
            const auto& literal = static_cast<const BoolLiteralExpr&>(expr);
            return llvm::ConstantInt::getBool(context_, literal.value);
        }
        case ExprKind::CharLiteral: {
            const auto& literal = static_cast<const CharLiteralExpr&>(expr);
            return llvm::ConstantInt::get(llvm::Type::getInt8Ty(context_), static_cast<unsigned char>(literal.value), false);
        }
        case ExprKind::StringLiteral: {
            const auto& literal = static_cast<const StringLiteralExpr&>(expr);
            return emitStringConstant(literal.value, "str");
        }
        case ExprKind::NullLiteral:
            return llvm::ConstantPointerNull::get(llvm::PointerType::get(context_, 0));
        case ExprKind::DeclRef: {
            const auto& ref = static_cast<const DeclRefExpr&>(expr);
            auto enumIt = enumValues_.find(ref.name);
            if (enumIt != enumValues_.end()) {
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false);
            }
            for (const auto& [enumName, info] : enumValues_) {
                auto valueIt = info.values.find(ref.name);
                if (valueIt != info.values.end()) {
                    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), valueIt->second, false);
                }
            }
            const auto symbol = lookupSymbol(ref.name);
            if (!symbol.has_value()) {
                diagnostics_.error(ref.range, "unknown variable '" + ref.name + "'");
                return nullptr;
            }
            return builder_.CreateLoad(lowerType(symbol->type), symbol->address, ref.name + ".load");
        }
        case ExprKind::Unary: {
            const auto& unary = static_cast<const UnaryExpr&>(expr);
            switch (unary.op) {
                case UnaryOp::Negate: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    return value ? builder_.CreateNeg(value, "negtmp") : nullptr;
                }
                case UnaryOp::AddressOf:
                    return emitLValue(*unary.operand);
                case UnaryOp::Dereference: {
                    llvm::Value* pointer = emitExpr(*unary.operand);
                    if (pointer == nullptr || !pointer->getType()->isPointerTy()) {
                        diagnostics_.error(unary.range, "cannot dereference a non-pointer value");
                        return nullptr;
                    }
                    Type pointeeType = inferExprType(expr);
                    return builder_.CreateLoad(lowerType(pointeeType), pointer, "deref");
                }
                case UnaryOp::LogicalNot: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    if (value == nullptr) {
                        return nullptr;
                    }
                    if (!value->getType()->isIntegerTy(1)) {
                        if (value->getType()->isPointerTy()) {
                            value = builder_.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())), "not.ptrbool");
                        } else if (value->getType()->isFloatingPointTy()) {
                            value = builder_.CreateFCmpONE(value, llvm::ConstantFP::get(value->getType(), 0.0), "not.fpbool");
                        } else if (value->getType()->isIntegerTy()) {
                            value = builder_.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0), "not.intbool");
                        }
                    }
                    return builder_.CreateNot(value, "nottmp");
                }
                case UnaryOp::BitwiseNot: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    return value ? builder_.CreateNot(value, "bnot") : nullptr;
                }
                case UnaryOp::PreIncrement:
                case UnaryOp::PreDecrement:
                case UnaryOp::PostIncrement:
                case UnaryOp::PostDecrement: {
                    llvm::Value* address = emitLValue(*unary.operand);
                    llvm::Value* current = emitExpr(*unary.operand);
                    if (address == nullptr || current == nullptr) {
                        return nullptr;
                    }
                    llvm::Value* delta = current->getType()->isFloatingPointTy()
                        ? static_cast<llvm::Value*>(llvm::ConstantFP::get(current->getType(), 1.0))
                        : static_cast<llvm::Value*>(llvm::ConstantInt::get(current->getType(), 1));
                    llvm::Value* updated = nullptr;
                    if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PostIncrement) {
                        updated = current->getType()->isFloatingPointTy() ? builder_.CreateFAdd(current, delta, "inc") : builder_.CreateAdd(current, delta, "inc");
                    } else {
                        updated = current->getType()->isFloatingPointTy() ? builder_.CreateFSub(current, delta, "dec") : builder_.CreateSub(current, delta, "dec");
                    }
                    builder_.CreateStore(updated, address);
                    if (unary.op == UnaryOp::PreIncrement || unary.op == UnaryOp::PreDecrement) {
                        return updated;
                    }
                    return current;
                }
                case UnaryOp::IsNonNull: {
                    llvm::Value* value = emitExpr(*unary.operand);
                    if (value == nullptr) {
                        return nullptr;
                    }
                    if (value->getType()->isPointerTy()) {
                        return builder_.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())), "nonnull");
                    }
                    Type operandType = inferExprType(*unary.operand);
                    if (!operandType.pointerDepth) {
                        diagnostics_.error(unary.range, "postfix '?' requires a nullable pointer value");
                        return nullptr;
                    }
                    diagnostics_.error(unary.range, "postfix '?' requires a nullable pointer value");
                    return nullptr;
                }
            }
            return nullptr;
        }
        case ExprKind::Cast: {
            const auto& cast = static_cast<const CastExpr&>(expr);
            llvm::Value* value = emitExpr(*cast.value);
            if (value == nullptr) {
                return nullptr;
            }
            return castValueToType(value, cast.targetType);
        }
        case ExprKind::Binary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            if (binary.op == BinaryOp::Assign) {
                llvm::Value* address = emitLValue(*binary.lhs);
                llvm::Value* value = emitExpr(*binary.rhs);
                if (address == nullptr || value == nullptr) {
                    return nullptr;
                }
                if (binary.lhs->kind == ExprKind::DeclRef) {
                    const auto& lhsRef = static_cast<const DeclRefExpr&>(*binary.lhs);
                    auto symbol = lookupSymbol(lhsRef.name);
                    if (symbol.has_value()) {
                        releaseStoredValue(address, symbol->type);
                        if (shouldRetainForStorage(*binary.rhs, symbol->type)) {
                            value = retainForStorage(value, symbol->type);
                        }
                    }
                }
                builder_.CreateStore(value, address);
                return value;
            }
            llvm::Value* lhs = emitExpr(*binary.lhs);
            llvm::Value* rhs = emitExpr(*binary.rhs);
            if (lhs == nullptr || rhs == nullptr) {
                return nullptr;
            }

            auto toBool = [&](llvm::Value* value, llvm::StringRef name) -> llvm::Value* {
                if (value->getType()->isIntegerTy(1)) {
                    return value;
                }
                if (value->getType()->isPointerTy()) {
                    return builder_.CreateICmpNE(value, llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(value->getType())), name);
                }
                if (value->getType()->isFloatingPointTy()) {
                    return builder_.CreateFCmpONE(value, llvm::ConstantFP::get(value->getType(), 0.0), name);
                }
                return builder_.CreateICmpNE(value, llvm::ConstantInt::get(value->getType(), 0), name);
            };

            auto emitStructuralEquality = [&](const auto& self, llvm::Value* left, llvm::Value* right) -> llvm::Value* {
                if (left == nullptr || right == nullptr || left->getType() != right->getType()) {
                    diagnostics_.error(binary.range, "cannot compare values of incompatible types");
                    return nullptr;
                }
                llvm::Type* type = left->getType();
                if (type->isIntegerTy() || type->isPointerTy()) {
                    return builder_.CreateICmpEQ(left, right, "eq");
                }
                if (type->isFloatingPointTy()) {
                    return builder_.CreateFCmpOEQ(left, right, "feq");
                }
                if (type->isStructTy()) {
                    llvm::Value* result = llvm::ConstantInt::getBool(context_, true);
                    for (unsigned i = 0; i < type->getStructNumElements(); ++i) {
                        llvm::Value* leftElement = builder_.CreateExtractValue(left, {i}, "eq.lhs." + std::to_string(i));
                        llvm::Value* rightElement = builder_.CreateExtractValue(right, {i}, "eq.rhs." + std::to_string(i));
                        llvm::Value* fieldEqual = self(self, leftElement, rightElement);
                        if (fieldEqual == nullptr) {
                            return nullptr;
                        }
                        result = builder_.CreateAnd(result, fieldEqual, "eq.struct");
                    }
                    return result;
                }
                if (type->isArrayTy()) {
                    llvm::Value* result = llvm::ConstantInt::getBool(context_, true);
                    const auto elementCount = llvm::cast<llvm::ArrayType>(type)->getNumElements();
                    for (unsigned i = 0; i < elementCount; ++i) {
                        llvm::Value* leftElement = builder_.CreateExtractValue(left, {i}, "eq.lhs.arr." + std::to_string(i));
                        llvm::Value* rightElement = builder_.CreateExtractValue(right, {i}, "eq.rhs.arr." + std::to_string(i));
                        llvm::Value* itemEqual = self(self, leftElement, rightElement);
                        if (itemEqual == nullptr) {
                            return nullptr;
                        }
                        result = builder_.CreateAnd(result, itemEqual, "eq.array");
                    }
                    return result;
                }
                diagnostics_.error(binary.range, "equality is not supported for this value kind");
                return nullptr;
            };

            const bool isFloat = lhs->getType()->isFloatingPointTy() || rhs->getType()->isFloatingPointTy();

            switch (binary.op) {
                case BinaryOp::Add:
                    return isFloat ? builder_.CreateFAdd(lhs, rhs, "faddtmp") : builder_.CreateAdd(lhs, rhs, "addtmp");
                case BinaryOp::Sub:
                    return isFloat ? builder_.CreateFSub(lhs, rhs, "fsubtmp") : builder_.CreateSub(lhs, rhs, "subtmp");
                case BinaryOp::Mul:
                    return isFloat ? builder_.CreateFMul(lhs, rhs, "fmultmp") : builder_.CreateMul(lhs, rhs, "multmp");
                case BinaryOp::Div:
                    return isFloat ? builder_.CreateFDiv(lhs, rhs, "fdivtmp") : builder_.CreateSDiv(lhs, rhs, "divtmp");
                case BinaryOp::Mod:
                    return builder_.CreateSRem(lhs, rhs, "modtmp");
                case BinaryOp::BitAnd:
                    return builder_.CreateAnd(lhs, rhs, "band");
                case BinaryOp::BitOr:
                    return builder_.CreateOr(lhs, rhs, "bor");
                case BinaryOp::BitXor:
                    return builder_.CreateXor(lhs, rhs, "bxor");
                case BinaryOp::ShiftLeft:
                    return builder_.CreateShl(lhs, rhs, "shl");
                case BinaryOp::ShiftRight:
                    return builder_.CreateAShr(lhs, rhs, "shr");
                case BinaryOp::Equal:
                    if (lhs->getType()->isStructTy() || lhs->getType()->isArrayTy()) {
                        return emitStructuralEquality(emitStructuralEquality, lhs, rhs);
                    }
                    return isFloat ? builder_.CreateFCmpOEQ(lhs, rhs, "feq") : builder_.CreateICmpEQ(lhs, rhs, "eq");
                case BinaryOp::NotEqual:
                    if (lhs->getType()->isStructTy() || lhs->getType()->isArrayTy()) {
                        llvm::Value* equal = emitStructuralEquality(emitStructuralEquality, lhs, rhs);
                        return equal == nullptr ? nullptr : builder_.CreateNot(equal, "ne");
                    }
                    return isFloat ? builder_.CreateFCmpONE(lhs, rhs, "fne") : builder_.CreateICmpNE(lhs, rhs, "ne");
                case BinaryOp::Less:
                    return isFloat ? builder_.CreateFCmpOLT(lhs, rhs, "flt") : builder_.CreateICmpSLT(lhs, rhs, "lt");
                case BinaryOp::LessEqual:
                    return isFloat ? builder_.CreateFCmpOLE(lhs, rhs, "fle") : builder_.CreateICmpSLE(lhs, rhs, "le");
                case BinaryOp::Greater:
                    return isFloat ? builder_.CreateFCmpOGT(lhs, rhs, "fgt") : builder_.CreateICmpSGT(lhs, rhs, "gt");
                case BinaryOp::GreaterEqual:
                    return isFloat ? builder_.CreateFCmpOGE(lhs, rhs, "fge") : builder_.CreateICmpSGE(lhs, rhs, "ge");
                case BinaryOp::LogicalAnd:
                    return builder_.CreateAnd(toBool(lhs, "land.lhs"), toBool(rhs, "land.rhs"), "land");
                case BinaryOp::LogicalOr:
                    return builder_.CreateOr(toBool(lhs, "lor.lhs"), toBool(rhs, "lor.rhs"), "lor");
                case BinaryOp::Set:
                    return builder_.CreateOr(lhs, rhs, "enum.set");
                case BinaryOp::Unset: {
                    llvm::Value* inverted = builder_.CreateNot(rhs, "enum.unset.mask");
                    return builder_.CreateAnd(lhs, inverted, "enum.unset");
                }
                case BinaryOp::Toggle:
                    return builder_.CreateXor(lhs, rhs, "enum.toggle");
                case BinaryOp::Is: {
                    llvm::Value* masked = builder_.CreateAnd(lhs, rhs, "enum.is.masked");
                    return builder_.CreateICmpEQ(masked, rhs, "enum.is");
                }
                case BinaryOp::IsNot: {
                    llvm::Value* masked = builder_.CreateAnd(lhs, rhs, "enum.isnot.masked");
                    return builder_.CreateICmpNE(masked, rhs, "enum.isnot");
                }
                case BinaryOp::Assign:
                    break;
            }
            return nullptr;
        }
        case ExprKind::Call: {
            MultiValue values = emitExprValues(expr);
            if (values.values.empty()) {
                return nullptr;
            }
            if (values.values.size() != 1) {
                diagnostics_.error(expr.range, "multi-value expressions require destructuring or multi-return forwarding");
                return nullptr;
            }
            return values.values.front();
        }
        case ExprKind::Member:
            if (const auto* member = dynamic_cast<const MemberExpr*>(&expr)) {
                if (auto qualified = qualifiedNameFromExpr(expr); qualified.has_value()) {
                    if (auto value = lookupEnumValueBySuffix(enumValues_, *qualified); value.has_value()) {
                        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), *value, false);
                    }
                }
                if (member->base->kind == ExprKind::Member) {
                    const auto& nestedBase = static_cast<const MemberExpr&>(*member->base);
                    if (auto qualifiedBase = moduleQualifiedName(*member->base); qualifiedBase.has_value()) {
                        for (const auto& [enumName, info] : enumValues_) {
                            const std::string prefix = enumName + ".";
                            if (qualifiedBase->rfind(prefix, 0) == 0) {
                                const std::string variant = qualifiedBase->substr(prefix.size());
                                auto paramsIt = info.paramValues.find(variant);
                                if (paramsIt != info.paramValues.end()) {
                                    auto paramIt = paramsIt->second.find(member->member);
                                    if (paramIt != paramsIt->second.end()) {
                                        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), paramIt->second, false);
                                    }
                                }
                            }
                        }
                    }
                    if (nestedBase.base->kind == ExprKind::Call) {
                        const auto& call = static_cast<const CallExpr&>(*nestedBase.base);
                        if (call.callee->kind == ExprKind::DeclRef && !call.runtimeArguments.empty()) {
                            const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                            auto enumIt = enumValues_.find(callee.name);
                            if (enumIt != enumValues_.end()) {
                                llvm::Value* ordinalValue = emitExpr(*call.runtimeArguments[0]);
                                if (auto* ordinalConst = llvm::dyn_cast_or_null<llvm::ConstantInt>(ordinalValue)) {
                                    const std::uint64_t ordinal = ordinalConst->getZExtValue();
                                    for (const auto& [variant, value] : enumIt->second.values) {
                                        if (value == ordinal) {
                                            auto paramsIt = enumIt->second.paramValues.find(variant);
                                            if (paramsIt != enumIt->second.paramValues.end()) {
                                                auto paramIt = paramsIt->second.find(member->member);
                                                if (paramIt != paramsIt->second.end()) {
                                                    return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), paramIt->second, false);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (auto qualified = moduleQualifiedName(expr); qualified.has_value()) {
                    for (const auto& [enumName, info] : enumValues_) {
                        const std::string prefix = enumName + ".";
                        if (qualified->rfind(prefix, 0) == 0) {
                            const std::string suffix = qualified->substr(prefix.size());
                            auto valueIt = info.values.find(suffix);
                            if (valueIt != info.values.end()) {
                                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), valueIt->second, false);
                            }
                            for (const auto& [variant, params] : info.paramValues) {
                                if (variant == suffix) {
                                    continue;
                                }
                            }
                        }
                    }
                }
                if (auto className = inferClassTypeName(*member->base); className.has_value()) {
                    auto layoutIt = aggregateLayouts_.find(*className);
                    if (layoutIt != aggregateLayouts_.end()) {
                        auto fieldIt = layoutIt->second.fieldIndices.find(member->member);
                        if (fieldIt != layoutIt->second.fieldIndices.end()) {
                            if (member->nullSafe) {
                                llvm::Value* baseValue = emitExpr(*member->base);
                                if (baseValue == nullptr) {
                                    return nullptr;
                                }
                                if (!baseValue->getType()->isPointerTy()) {
                                    diagnostics_.error(expr.range, "null-safe member access requires a pointer-like base value");
                                    return nullptr;
                                }
                                llvm::BasicBlock* startBlock = builder_.GetInsertBlock();
                                llvm::Function* parentFunction = startBlock->getParent();
                                llvm::BasicBlock* loadBlock = llvm::BasicBlock::Create(context_, "nullsafe.member.load", parentFunction);
                                llvm::BasicBlock* continueBlock = llvm::BasicBlock::Create(context_, "nullsafe.member.cont", parentFunction);
                                llvm::Value* isNonNull = builder_.CreateICmpNE(
                                    baseValue,
                                    llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(baseValue->getType())),
                                    "nullsafe.member.test");
                                builder_.CreateCondBr(isNonNull, loadBlock, continueBlock);

                                builder_.SetInsertPoint(loadBlock);
                                llvm::Value* fieldAddress = builder_.CreateStructGEP(structTypes_.at(*className), baseValue, static_cast<unsigned>(fieldIt->second), member->member + ".addr");
                                llvm::Type* fieldType = lowerType(layoutIt->second.fieldTypes[fieldIt->second]);
                                llvm::Value* loadedValue = builder_.CreateLoad(fieldType, fieldAddress, member->member + ".load");
                                llvm::BasicBlock* loadedFromBlock = builder_.GetInsertBlock();
                                builder_.CreateBr(continueBlock);

                                builder_.SetInsertPoint(continueBlock);
                                auto* phi = builder_.CreatePHI(fieldType, 2, "nullsafe.member.result");
                                phi->addIncoming(loadedValue, loadedFromBlock);
                                phi->addIncoming(nullConstantForType(context_, fieldType), startBlock);
                                return phi;
                            }
                            llvm::Value* fieldAddress = emitLValue(expr);
                            if (fieldAddress == nullptr) {
                                return nullptr;
                            }
                            return builder_.CreateLoad(lowerType(layoutIt->second.fieldTypes[fieldIt->second]), fieldAddress, member->member + ".load");
                        }
                    }
                }
                Type baseType = inferExprType(*member->base);
                auto aggregateLayout = aggregateLayouts_.find(baseType.name);
                if (aggregateLayout != aggregateLayouts_.end()) {
                    auto fieldIt = aggregateLayout->second.fieldIndices.find(member->member);
                    if (fieldIt != aggregateLayout->second.fieldIndices.end()) {
                        llvm::Value* fieldAddress = emitLValue(expr);
                        if (fieldAddress == nullptr) {
                            return nullptr;
                        }
                        return builder_.CreateLoad(lowerType(aggregateLayout->second.fieldTypes[fieldIt->second]), fieldAddress, member->member + ".load");
                    }
                }
                if (auto className = inferClassTypeName(*member->base); className.has_value()) {
                    if (classFieldNames_.contains(*className) && classFieldNames_.at(*className).contains(member->member)) {
                        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false);
                    }
                    if (classMethodNames_.contains(*className) && classMethodNames_.at(*className).contains(member->member)) {
                        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false);
                    }
                }
                if (member->base->kind == ExprKind::DeclRef) {
                    const auto& base = static_cast<const DeclRefExpr&>(*member->base);
                    const auto symbol = lookupSymbol(base.name);
                    if (symbol.has_value() && symbol->type.name == "Obj") {
                        return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), 0, false);
                    }
                }
                if (member->base->kind == ExprKind::DeclRef) {
                    const auto& base = static_cast<const DeclRefExpr&>(*member->base);
                    auto enumIt = enumValues_.find(base.name);
                    if (enumIt != enumValues_.end()) {
                        auto valueIt = enumIt->second.values.find(member->member);
                        if (valueIt != enumIt->second.values.end()) {
                            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), valueIt->second, false);
                        }
                        for (const auto& [variant, params] : enumIt->second.paramValues) {
                            auto paramIt = params.find(member->member);
                            if (paramIt != params.end()) {
                                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), paramIt->second, false);
                            }
                        }
                    }
                }
                if (member->base->kind == ExprKind::Member) {
                    if (auto nestedName = qualifiedNameFromExpr(expr); nestedName.has_value()) {
                        for (const auto& [enumName, info] : enumValues_) {
                            auto valueIt = info.values.find(*nestedName);
                            if (valueIt != info.values.end()) {
                                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), valueIt->second, false);
                            }
                        }
                    }
                }
                if (member->base->kind == ExprKind::Call) {
                    const auto& call = static_cast<const CallExpr&>(*member->base);
                    if (call.callee->kind == ExprKind::DeclRef && !call.runtimeArguments.empty()) {
                        const auto& callee = static_cast<const DeclRefExpr&>(*call.callee);
                        auto enumIt = enumValues_.find(callee.name);
                        if (enumIt != enumValues_.end()) {
                            llvm::Value* ordinalValue = emitExpr(*call.runtimeArguments[0]);
                            if (auto* ordinalConst = llvm::dyn_cast_or_null<llvm::ConstantInt>(ordinalValue)) {
                                const std::uint64_t ordinal = ordinalConst->getZExtValue();
                                for (const auto& [variant, value] : enumIt->second.values) {
                                    if (value == ordinal) {
                                        auto paramsIt = enumIt->second.paramValues.find(variant);
                                        if (paramsIt != enumIt->second.paramValues.end()) {
                                            auto paramIt = paramsIt->second.find(member->member);
                                            if (paramIt != paramsIt->second.end()) {
                                                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), paramIt->second, false);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                if (member->base->kind == ExprKind::Initializer) {
                    const auto& init = static_cast<const InitializerExpr&>(*member->base);
                    auto enumIt = enumValues_.find(init.typeName);
                    if (enumIt != enumValues_.end() && init.values.size() == 1) {
                        llvm::Value* ordinalValue = emitExpr(*init.values[0]);
                        if (auto* ordinalConst = llvm::dyn_cast_or_null<llvm::ConstantInt>(ordinalValue)) {
                            const std::uint64_t ordinal = ordinalConst->getZExtValue();
                            for (const auto& [variant, value] : enumIt->second.values) {
                                if (value == ordinal) {
                                    auto paramsIt = enumIt->second.paramValues.find(variant);
                                    if (paramsIt != enumIt->second.paramValues.end()) {
                                        auto paramIt = paramsIt->second.find(member->member);
                                        if (paramIt != paramsIt->second.end()) {
                                            return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), paramIt->second, false);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            diagnostics_.error(expr.range, "member access codegen is not implemented yet");
            return nullptr;
        case ExprKind::Initializer: {
            const auto& init = static_cast<const InitializerExpr&>(expr);
            if (init.initKind == InitKind::ArrayLiteral) {
                Type arrayType = inferExprType(expr);
                llvm::Type* loweredArrayType = lowerType(arrayType);
                llvm::Value* aggregate = llvm::UndefValue::get(loweredArrayType);
                for (std::size_t i = 0; i < init.values.size(); ++i) {
                    llvm::Value* value = emitExpr(*init.values[i]);
                    if (value == nullptr) {
                        return nullptr;
                    }
                    Type elementType = arrayType;
                    if (!elementType.arrayExtents.empty()) {
                        elementType.arrayExtents.erase(elementType.arrayExtents.begin());
                    }
                    aggregate = builder_.CreateInsertValue(aggregate,
                                                           castValueToType(value, elementType),
                                                           {static_cast<unsigned>(i)},
                                                           "array.init");
                }
                return aggregate;
            }
            auto enumIt = enumValues_.find(init.typeName);
            if (enumIt != enumValues_.end() && enumIt->second.isFlags) {
                std::uint64_t value = 0;
                for (const auto& item : init.values) {
                    llvm::Value* lowered = emitExpr(*item);
                    if (lowered == nullptr && item->kind == ExprKind::Member) {
                        if (auto nestedName = qualifiedNameFromExpr(*item); nestedName.has_value()) {
                            if (auto nestedValue = lookupEnumValueBySuffix(enumValues_, *nestedName); nestedValue.has_value()) {
                                lowered = llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), *nestedValue, false);
                            }
                        }
                    }
                    if (auto* constant = llvm::dyn_cast_or_null<llvm::ConstantInt>(lowered)) {
                        value |= constant->getZExtValue();
                    } else {
                        diagnostics_.error(item->range, "flag enum initializer requires constant enum members");
                        return nullptr;
                    }
                }
                return llvm::ConstantInt::get(llvm::Type::getInt32Ty(context_), value, false);
            }
            auto it = structTypes_.find(init.typeName);
            if (it == structTypes_.end()) {
                diagnostics_.error(expr.range, "initializer targets unknown type '" + init.typeName + "'");
                return nullptr;
            }
            auto layoutIt = aggregateLayouts_.find(init.typeName);
            if (layoutIt == aggregateLayouts_.end()) {
                diagnostics_.error(expr.range, "initializer layout is unknown for type '" + init.typeName + "'");
                return nullptr;
            }
            llvm::Value* aggregate = llvm::UndefValue::get(it->second);
            const std::size_t count = std::min(init.values.size(), layoutIt->second.fieldTypes.size());
            for (std::size_t i = 0; i < count; ++i) {
                llvm::Value* value = emitExpr(*init.values[i]);
                if (value == nullptr) {
                    return nullptr;
                }
                aggregate = builder_.CreateInsertValue(aggregate, castValueToType(value, layoutIt->second.fieldTypes[i]), {static_cast<unsigned>(i)}, init.typeName + ".init");
            }
            const bool heapBackedClass = isClassType(Type {.name = init.typeName});
            if (heapBackedClass || init.initKind != InitKind::Value) {
                llvm::Value* rawHeap = builder_.CreateCall(
                    runtime_.arcAlloc,
                    {llvm::ConstantInt::get(llvm::Type::getInt64Ty(context_), module_->getDataLayout().getTypeAllocSize(it->second))},
                    init.typeName + ".heap.raw");
                llvm::Value* typedHeap = builder_.CreatePointerCast(rawHeap, llvm::PointerType::get(it->second, 0), init.typeName + ".heap");
                builder_.CreateStore(aggregate, typedHeap);
                if (init.initKind == InitKind::Weak) {
                    llvm::Value* weakObject = builder_.CreateCall(runtime_.weakInit, {rawHeap}, init.typeName + ".weak");
                    return builder_.CreatePointerCast(weakObject, llvm::PointerType::get(it->second, 0), init.typeName + ".weak.cast");
                }
                return typedHeap;
            }
            return aggregate;
        }
    }

    return nullptr;
}

}  // namespace axc
